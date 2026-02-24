// Copyright 2024 DCodeX Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/api/execute_reactor.h"

#include "absl/log/log.h"
#include "absl/strings/substitute.h"
#include "src/engine/dynamic_worker_coordinator.h"

namespace dcodex {

void ThreadSafeLogQueue::Push(ExecutionLog log) {
  absl::MutexLock lock(&mutex_);
  queue_.push(std::move(log));
}

bool ThreadSafeLogQueue::Pop(ExecutionLog& log) {
  absl::MutexLock lock(&mutex_);
  if (queue_.empty()) return false;
  log = std::move(queue_.front());
  queue_.pop();
  return true;
}

bool ThreadSafeLogQueue::Empty() const {
  absl::MutexLock lock(&mutex_);
  return queue_.empty();
}

ReactorInternalState::ReactorInternalState(const CodeRequest* req, std::atomic<int>& c, ExecuteReactor* r)
    : request(req), counter(c), reactor(r), state(ReactorState::kIdle) {}

ExecuteReactor::ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter,
                               DynamicWorkerCoordinator* pool,
                               std::shared_ptr<SandboxedProcess> executor)
    : shared_state_(std::make_shared<ReactorInternalState>(request, counter, this)),
      pool_(pool),
      executor_(std::move(executor)) {
  shared_state_->counter.fetch_add(1);
}

void ExecuteReactor::StartExecution() {
  absl::StatusOr<ExecutionResult> result = executor_->CompileAndRunStreaming(
      shared_state_->request->language(), shared_state_->request->code(),
      shared_state_->request->stdin_data(), [state = shared_state_](absl::string_view o,
                                                                   absl::string_view e) {
        if (o.empty() && e.empty()) return;
        ExecutionLog log;
        if (!o.empty()) log.set_stdout_chunk(std::string(o));
        if (!e.empty()) log.set_stderr_chunk(std::string(e));
        state->log_queue.Push(std::move(log));
        {
          absl::MutexLock lock(&state->notify_mutex);
          state->notification_pending = true;
        }
        state->notify_cv.Signal();
      });

  ExecutionResult final_res;
  if (result.ok()) {
    final_res = *result;
  } else {
    final_res.success = false;
    final_res.error_message = std::string(result.status().message());
  }

  shared_state_->final_stats = final_res.stats;
  shared_state_->cache_hit.store(final_res.cache_hit);
  shared_state_->wall_clock_timeout.store(final_res.wall_clock_timeout);
  shared_state_->output_truncated.store(final_res.output_truncated);

  if (!final_res.success) {
    std::string error_msg;
    if (!final_res.error_message.empty()) {
      absl::SubstituteAndAppend(&error_msg, "ERROR: $0\n", final_res.error_message);
    }
    if (!final_res.backend_trace.empty()) {
      absl::SubstituteAndAppend(&error_msg, "$0\n", final_res.backend_trace);
    }
    if (!error_msg.empty()) {
      ExecutionLog error_log;
      error_log.set_stderr_chunk(error_msg);
      shared_state_->log_queue.Push(std::move(error_log));
    }
  }
  shared_state_->execution_finished.store(true);
  {
    absl::MutexLock lock(&shared_state_->notify_mutex);
    shared_state_->notification_pending = true;
  }
  shared_state_->notify_cv.Signal();
}

void ExecuteReactor::PumpWrites() {
  while (true) {
    {
      absl::MutexLock lock(&shared_state_->notify_mutex);
      while (!shared_state_->notification_pending &&
             !shared_state_->reactor_done &&
             !shared_state_->cancelled.load()) {
        shared_state_->notify_cv.Wait(&shared_state_->notify_mutex);
      }
      if (shared_state_->reactor_done ||
          (shared_state_->cancelled.load() &&
           shared_state_->state.load() == ReactorState::kIdle)) {
        break;
      }
      shared_state_->notification_pending = false;
    }

    ReactorState current = shared_state_->state.load();
    if (current == ReactorState::kIdle) {
      ExecutionLog log;
      if (shared_state_->log_queue.Pop(log)) {
        shared_state_->current_log = std::move(log);
        if (shared_state_->state.compare_exchange_strong(current,
                                                         ReactorState::kWriting)) {
          StartWrite(&shared_state_->current_log);
        }
      } else if (shared_state_->execution_finished.load()) {
        if (!shared_state_->stats_sent.load()) {
          ExecutionLog stats_log;
          stats_log.set_peak_memory_bytes(shared_state_->final_stats.peak_memory_bytes);
          stats_log.set_execution_time_ms(
              static_cast<float>(shared_state_->final_stats.elapsed_time_ms));
          stats_log.set_cache_hit(shared_state_->cache_hit.load());
          stats_log.set_wall_clock_timeout(shared_state_->wall_clock_timeout.load());
          stats_log.set_output_truncated(shared_state_->output_truncated.load());
          shared_state_->current_log = std::move(stats_log);
          shared_state_->stats_sent.store(true);
          if (shared_state_->state.compare_exchange_strong(current,
                                                           ReactorState::kWriting)) {
            StartWrite(&shared_state_->current_log);
          }
        } else {
          if (shared_state_->state.compare_exchange_strong(current,
                                                           ReactorState::kFinishing)) {
            Finish(grpc::Status::OK);
          }
        }
      }
    }
    if (shared_state_->state.load() == ReactorState::kFinishing) {
      break;
    }
  }
}

void ExecuteReactor::OnWriteDone(bool ok) {
  (void)ok;
  ReactorState expected = ReactorState::kWriting;
  shared_state_->state.compare_exchange_strong(expected, ReactorState::kIdle);
  {
    absl::MutexLock lock(&shared_state_->notify_mutex);
    shared_state_->notification_pending = true;
  }
  shared_state_->notify_cv.Signal();
}

void ExecuteReactor::OnDone() {
  {
    absl::MutexLock lock(&shared_state_->notify_mutex);
    shared_state_->reactor_done = true;
  }
  shared_state_->notify_cv.SignalAll();
  shared_state_->counter.fetch_sub(1);
  if (pool_ != nullptr) {
    pool_->ReleaseWorker(this);
  }
}

void ExecuteReactor::OnCancel() {
  shared_state_->cancelled.store(true);
  {
    absl::MutexLock lock(&shared_state_->notify_mutex);
    shared_state_->notification_pending = true;
  }
  shared_state_->notify_cv.Signal();
}

}  // namespace dcodex
