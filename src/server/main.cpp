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
#include <memory>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
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
  void Push(ExecutionLog log) {
    absl::MutexLock lock(&mutex_);
    queue_.push(std::move(log));
  }

  bool Pop(ExecutionLog& log) {
    absl::MutexLock lock(&mutex_);
    if (queue_.empty()) return false;
    log = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  bool Empty() const {
    absl::MutexLock lock(&mutex_);
    return queue_.empty();
  }

 private:
  mutable absl::Mutex mutex_;
  std::queue<ExecutionLog> queue_;
};

// Forward declaration
class ExecuteReactor;

class WarmWorkerPool {
 public:
  explicit WarmWorkerPool(int max_workers);

  WarmWorkerPool(const WarmWorkerPool&) = delete;
  WarmWorkerPool& operator=(const WarmWorkerPool&) = delete;

  void Start();
  void Shutdown();

  absl::Status AcquireWorker(std::shared_ptr<ExecuteReactor> reactor);
  void NotifyWorkerIdle();
  void ReleaseReactor(ExecuteReactor* reactor);

 private:
  class Worker {
   public:
    explicit Worker(WarmWorkerPool* pool);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    bool TryAssign(std::shared_ptr<ExecuteReactor> reactor);
    void Notify();
    void Join();

   private:
    void Run();

    WarmWorkerPool* pool_;
    std::shared_ptr<ExecuteReactor> reactor_;
    mutable absl::Mutex mutex_;
    absl::CondVar cv_;
    bool stopping_ = false;
    std::thread thread_;
  };

  absl::Mutex mutex_;
  absl::flat_hash_map<ExecuteReactor*, std::shared_ptr<ExecuteReactor>> active_reactors_;
  std::vector<std::unique_ptr<Worker>> workers_;
  int max_workers_;
  int idle_workers_;
  bool shutting_down_;
};

// Internal state of the reactor, shared between threads
struct ReactorInternalState {
  ReactorInternalState(const CodeRequest* req, std::atomic<int>& c, ExecuteReactor* r)
      : request(req), counter(c), reactor(r), state(ReactorState::kIdle) {}

  const CodeRequest* request;
  std::atomic<int>& counter;
  ExecuteReactor* reactor;

  std::atomic<ReactorState> state;
  std::atomic<bool> execution_finished{false};
  std::atomic<bool> stats_sent{false};
  std::atomic<bool> cache_hit{false};
  std::atomic<bool> wall_clock_timeout{false};
  std::atomic<bool> output_truncated{false};
  std::atomic<bool> cancelled{false};

  absl::Mutex notify_mutex;
  absl::CondVar notify_cv;
  bool notification_pending = false;
  bool reactor_done = false;

  ThreadSafeLogQueue log_queue;
  ExecutionLog current_log;
  ResourceStats final_stats;
};

class ExecuteReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter,
                 WarmWorkerPool* pool)
      : shared_state_(std::make_shared<ReactorInternalState>(request, counter, this)),
        pool_(pool) {
    shared_state_->counter.fetch_add(1);
  }

  void StartExecution() {
    absl::StatusOr<ExecutionResult> result = SandboxedProcess::CompileAndRunStreaming(
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

  void PumpWrites() {
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
              Finish(Status::OK);
            }
          }
        }
      }
      if (shared_state_->state.load() == ReactorState::kFinishing) {
        break;
      }
    }
  }

  void OnWriteDone(bool ok) override {
    (void)ok;
    ReactorState expected = ReactorState::kWriting;
    shared_state_->state.compare_exchange_strong(expected, ReactorState::kIdle);
    {
      absl::MutexLock lock(&shared_state_->notify_mutex);
      shared_state_->notification_pending = true;
    }
    shared_state_->notify_cv.Signal();
  }

  void OnDone() override {
    {
      absl::MutexLock lock(&shared_state_->notify_mutex);
      shared_state_->reactor_done = true;
    }
    shared_state_->notify_cv.SignalAll();
    shared_state_->counter.fetch_sub(1);
    if (pool_ != nullptr) {
      pool_->ReleaseReactor(this);
    }
  }

  void OnCancel() override {
    shared_state_->cancelled.store(true);
    {
      absl::MutexLock lock(&shared_state_->notify_mutex);
      shared_state_->notification_pending = true;
    }
    shared_state_->notify_cv.Signal();
  }

 private:
  std::shared_ptr<ReactorInternalState> shared_state_;
  WarmWorkerPool* pool_;
};

WarmWorkerPool::WarmWorkerPool(int max_workers)
    : max_workers_(max_workers),
      idle_workers_(0),
      shutting_down_(false) {}

void WarmWorkerPool::Start() {
  absl::MutexLock lock(&mutex_);
  if (!workers_.empty()) {
    return;
  }
  const int worker_count = max_workers_ > 0 ? max_workers_ : 1;
  workers_.reserve(worker_count);
  for (int i = 0; i < worker_count; ++i) {
    workers_.push_back(std::make_unique<Worker>(this));
    idle_workers_++;
  }
}

void WarmWorkerPool::Shutdown() {
  absl::MutexLock lock(&mutex_);
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  for (const auto& worker : workers_) {
    worker->Notify();
  }
}

absl::Status WarmWorkerPool::AcquireWorker(
    std::shared_ptr<ExecuteReactor> reactor) {
  absl::MutexLock lock(&mutex_);
  if (shutting_down_) {
    return absl::FailedPreconditionError("Worker pool is shutting down");
  }
  if (idle_workers_ == 0) {
    return absl::ResourceExhaustedError("No idle workers available");
  }
  ExecuteReactor* raw_reactor = reactor.get();
  for (const auto& worker : workers_) {
    if (worker->TryAssign(reactor)) {
      active_reactors_.emplace(raw_reactor, std::move(reactor));
      idle_workers_--;
      return absl::OkStatus();
    }
  }
  return absl::InternalError("Failed to assign worker");
}

void WarmWorkerPool::NotifyWorkerIdle() {
  absl::MutexLock lock(&mutex_);
  idle_workers_++;
}

void WarmWorkerPool::ReleaseReactor(ExecuteReactor* reactor) {
  absl::MutexLock lock(&mutex_);
  active_reactors_.erase(reactor);
}

WarmWorkerPool::Worker::Worker(WarmWorkerPool* pool)
    : pool_(pool),
      thread_(&Worker::Run, this) {}

WarmWorkerPool::Worker::~Worker() { Join(); }

bool WarmWorkerPool::Worker::TryAssign(std::shared_ptr<ExecuteReactor> reactor) {
  absl::MutexLock lock(&mutex_);
  if (reactor_ || stopping_) {
    return false;
  }
  reactor_ = std::move(reactor);
  cv_.Signal();
  return true;
}

void WarmWorkerPool::Worker::Notify() {
  absl::MutexLock lock(&mutex_);
  stopping_ = true;
  cv_.Signal();
}

void WarmWorkerPool::Worker::Join() {
  {
    absl::MutexLock lock(&mutex_);
    stopping_ = true;
    cv_.Signal();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void WarmWorkerPool::Worker::Run() {
  while (true) {
    std::shared_ptr<ExecuteReactor> reactor;
    {
      absl::MutexLock lock(&mutex_);
      while (reactor_ == nullptr && !stopping_) {
        cv_.Wait(&mutex_);
      }
      if (stopping_) {
        break;
      }
      reactor = std::move(reactor_);
    }
    reactor->StartExecution();
    reactor->PumpWrites();
    pool_->NotifyWorkerIdle();
    reactor.reset();
  }
}

class RejectReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  RejectReactor() {
    Finish(Status(StatusCode::RESOURCE_EXHAUSTED, "Too many active sandboxes"));
  }
  void OnDone() override { delete this; }
};

class CodeExecutorServiceImpl final : public CodeExecutor::CallbackService {
 public:
  explicit CodeExecutorServiceImpl(int max_sandboxes)
      : active_sandboxes_(0),
        worker_pool_(max_sandboxes) {
    worker_pool_.Start();
  }

  ~CodeExecutorServiceImpl() override { worker_pool_.Shutdown(); }

  ServerWriteReactor<ExecutionLog>* Execute(CallbackServerContext* context,
                                           const CodeRequest* request) override {
    (void)context;
    if (active_sandboxes_.load() >= absl::GetFlag(FLAGS_max_concurrent_sandboxes)) {
      return new RejectReactor();
    }
    auto reactor =
        std::make_shared<ExecuteReactor>(request, active_sandboxes_, &worker_pool_);
    absl::Status assignment = worker_pool_.AcquireWorker(reactor);
    if (!assignment.ok()) {
      LOG(WARNING) << "Worker pool rejected request: " << assignment;
      return new RejectReactor();
    }
    return reactor.get();
  }

 private:
  std::atomic<int> active_sandboxes_;
  WarmWorkerPool worker_pool_;
};

}  // namespace

absl::Status RunServer() {
  std::string server_address = absl::Substitute("0.0.0.0:$0", absl::GetFlag(FLAGS_port));
  CodeExecutorServiceImpl service(absl::GetFlag(FLAGS_max_concurrent_sandboxes));
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
