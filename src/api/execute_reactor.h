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

#ifndef SRC_API_EXECUTE_REACTOR_H_
#define SRC_API_EXECUTE_REACTOR_H_

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <memory>
#include <queue>
#include <string>

#include "absl/synchronization/mutex.h"
#include "proto/sandbox.grpc.pb.h"
#include "src/engine/sandbox.h"
#include "src/engine/dynamic_worker_coordinator.h"

namespace dcodex {

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
  void Push(ExecutionLog log);
  bool Pop(ExecutionLog& log);
  bool Empty() const;

 private:
  mutable absl::Mutex mutex_;
  std::queue<ExecutionLog> queue_;
};

// Internal state of the reactor, shared between threads
struct ReactorInternalState {
  ReactorInternalState(const CodeRequest* req, std::atomic<int>& c, class ExecuteReactor* r);

  const CodeRequest* request;
  std::atomic<int>& counter;
  class ExecuteReactor* reactor;

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

class ExecuteReactor final : public grpc::ServerWriteReactor<ExecutionLog>,
                            public WorkerTask {
 public:
  ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter,
                 DynamicWorkerCoordinator* pool, std::shared_ptr<SandboxedProcess> executor);

  void StartExecution() override;
  void PumpWrites() override;

  void OnWriteDone(bool ok) override;
  void OnDone() override;
  void OnCancel() override;

 private:
  std::shared_ptr<ReactorInternalState> shared_state_;
  DynamicWorkerCoordinator* pool_;
  std::shared_ptr<SandboxedProcess> executor_;
};

}  // namespace dcodex

#endif  // SRC_API_EXECUTE_REACTOR_H_
