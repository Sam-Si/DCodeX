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

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "proto/sandbox.grpc.pb.h"
#include "src/server/sandbox.h"

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(int, max_concurrent_sandboxes, 10,
          "Maximum number of concurrent sandboxes allowed");

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerWriteReactor;
using grpc::Status;
using grpc::StatusCode;

namespace dcodex {

namespace {

// Reactor state enumeration
enum class ReactorState {
  kIdle,
  kWriting,
  kFinishing,
  kFinished,
};

// Thread-safe log queue
class ThreadSafeLogQueue {
 public:
  void Push(const ExecutionLog& log) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(log);
  }

  bool Pop(ExecutionLog& log) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    log = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  bool Empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }

 private:
  mutable std::mutex mutex_;
  std::queue<ExecutionLog> queue_;
};

// Forward declaration
class ExecuteReactor;

// Internal state of the reactor, shared between threads
struct ReactorInternalState {
  ReactorInternalState(const CodeRequest* req, std::atomic<int>& c, ExecuteReactor* r)
      : request(req), counter(c), reactor(r), state(ReactorState::kIdle) {}

  const CodeRequest* request;
  std::atomic<int>& counter;
  ExecuteReactor* reactor; // Raw pointer to reactor, only used by processor thread

  std::atomic<ReactorState> state;
  std::atomic<bool> execution_finished{false};
  std::atomic<bool> stats_sent{false};
  std::atomic<bool> cache_hit{false};
  std::atomic<bool> wall_clock_timeout{false};
  std::atomic<bool> output_truncated{false};
  std::atomic<bool> cancelled{false};
  
  std::mutex notify_mutex;
  std::condition_variable notify_cv;
  bool notification_pending = false;
  bool reactor_done = false;

  ThreadSafeLogQueue log_queue;
  ExecutionLog current_log;
  ResourceStats final_stats;
};

class ExecuteReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter)
      : shared_state_(std::make_shared<ReactorInternalState>(request, counter, this)) {
    shared_state_->counter.fetch_add(1);
    
    // Start processor thread
    std::thread processor([state = shared_state_]() {
      // Start worker thread
      std::thread worker([state]() {
        absl::StatusOr<ExecutionResult> result = SandboxedProcess::CompileAndRunStreaming(
            state->request->language(), state->request->code(), state->request->stdin_data(),
            [state](absl::string_view o, absl::string_view e) {
              if (o.empty() && e.empty()) return;
              ExecutionLog log;
              if (!o.empty()) log.set_stdout_chunk(std::string(o));
              if (!e.empty()) log.set_stderr_chunk(std::string(e));
              state->log_queue.Push(std::move(log));
              {
                std::lock_guard<std::mutex> lock(state->notify_mutex);
                state->notification_pending = true;
              }
              state->notify_cv.notify_one();
            });

        ExecutionResult final_res;
        if (result.ok()) {
          final_res = *result;
        } else {
          final_res.success = false;
          final_res.error_message = std::string(result.status().message());
        }

        // Handle completion
        state->final_stats = final_res.stats;
        state->cache_hit.store(final_res.cache_hit);
        state->wall_clock_timeout.store(final_res.wall_clock_timeout);
        state->output_truncated.store(final_res.output_truncated);

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
            state->log_queue.Push(std::move(error_log));
          }
        }
        state->execution_finished.store(true);
        {
          std::lock_guard<std::mutex> lock(state->notify_mutex);
          state->notification_pending = true;
        }
        state->notify_cv.notify_one();
      });
      worker.detach();

      while (true) {
        {
          std::unique_lock<std::mutex> lock(state->notify_mutex);
          state->notify_cv.wait(lock, [&] {
            return state->notification_pending || state->reactor_done || state->cancelled.load();
          });
          if (state->reactor_done || (state->cancelled.load() && state->state.load() == ReactorState::kIdle)) {
            break;
          }
          state->notification_pending = false;
        }

        // Process state
        ReactorState current = state->state.load();
        if (current == ReactorState::kIdle) {
          ExecutionLog log;
          if (state->log_queue.Pop(log)) {
            state->current_log = std::move(log);
            if (state->state.compare_exchange_strong(current, ReactorState::kWriting)) {
              state->reactor->StartWrite(&state->current_log);
            }
          } else if (state->execution_finished.load()) {
            if (!state->stats_sent.load()) {
              ExecutionLog stats_log;
              stats_log.set_peak_memory_bytes(state->final_stats.peak_memory_bytes);
              stats_log.set_execution_time_ms(static_cast<float>(state->final_stats.elapsed_time_ms));
              stats_log.set_cache_hit(state->cache_hit.load());
              stats_log.set_wall_clock_timeout(state->wall_clock_timeout.load());
              stats_log.set_output_truncated(state->output_truncated.load());
              state->current_log = std::move(stats_log);
              state->stats_sent.store(true);
              if (state->state.compare_exchange_strong(current, ReactorState::kWriting)) {
                state->reactor->StartWrite(&state->current_log);
              }
            } else {
              if (state->state.compare_exchange_strong(current, ReactorState::kFinishing)) {
                state->reactor->Finish(Status::OK);
              }
            }
          }
        }
        if (state->state.load() == ReactorState::kFinishing) break;
      }
    });
    processor.detach();
  }

  void OnWriteDone(bool ok) override {
    (void)ok;
    ReactorState expected = ReactorState::kWriting;
    shared_state_->state.compare_exchange_strong(expected, ReactorState::kIdle);
    {
      std::lock_guard<std::mutex> lock(shared_state_->notify_mutex);
      shared_state_->notification_pending = true;
    }
    shared_state_->notify_cv.notify_one();
  }

  void OnDone() override {
    {
      std::lock_guard<std::mutex> lock(shared_state_->notify_mutex);
      shared_state_->reactor_done = true;
    }
    shared_state_->notify_cv.notify_all();
    shared_state_->counter.fetch_sub(1);
    delete this;
  }

  void OnCancel() override {
    shared_state_->cancelled.store(true);
    {
      std::lock_guard<std::mutex> lock(shared_state_->notify_mutex);
      shared_state_->notification_pending = true;
    }
    shared_state_->notify_cv.notify_one();
  }

 private:
  std::shared_ptr<ReactorInternalState> shared_state_;
};

class RejectReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  RejectReactor() {
    Finish(Status(StatusCode::RESOURCE_EXHAUSTED, "Too many active sandboxes"));
  }
  void OnDone() override { delete this; }
};

class CodeExecutorServiceImpl final : public CodeExecutor::CallbackService {
 public:
  CodeExecutorServiceImpl() : active_sandboxes_(0) {}
  ServerWriteReactor<ExecutionLog>* Execute(CallbackServerContext* context, const CodeRequest* request) override {
    (void)context;
    if (active_sandboxes_.load() >= absl::GetFlag(FLAGS_max_concurrent_sandboxes)) {
      return new RejectReactor();
    }
    return new ExecuteReactor(request, active_sandboxes_);
  }
 private:
  std::atomic<int> active_sandboxes_;
};

}  // namespace

absl::Status RunServer() {
  std::string server_address = absl::Substitute("0.0.0.0:$0", absl::GetFlag(FLAGS_port));
  CodeExecutorServiceImpl service;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (!server) return absl::InternalError("Failed to start gRPC server");
  LOG(INFO) << "Server listening on " << server_address;
  server->Wait();
  return absl::OkStatus();
}

}  // namespace dcodex

int main(int argc, char** argv) {
  absl::InitializeLog();
  absl::ParseCommandLine(argc, argv);
  if (absl::Status status = dcodex::RunServer(); !status.ok()) {
    LOG(ERROR) << "Server failed: " << status.message();
    return 1;
  }
  return 0;
}
