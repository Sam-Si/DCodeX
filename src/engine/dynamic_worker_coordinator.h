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

#ifndef SRC_ENGINE_DYNAMIC_WORKER_COORDINATOR_H_
#define SRC_ENGINE_DYNAMIC_WORKER_COORDINATOR_H_

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

namespace dcodex {

// Supported languages for worker affinity.
enum class LanguageId {
  kUnknown = 0,
  kCpp = 1,
  kPython = 2,
};

// Converts a language string to LanguageId.
LanguageId ParseLanguageId(const std::string& lang);

class WorkerTask {
 public:
  virtual ~WorkerTask() = default;
  virtual void StartExecution() = 0;
  virtual void PumpWrites() = 0;
};

// Configuration options for the coordinator.
struct DynamicWorkerCoordinatorOptions {
  int min_workers = 2;
  int max_workers = 20;
  absl::Duration scale_up_latency_threshold = absl::Milliseconds(500);
  // If a worker has been idle for this long, the balancer may scale down.
  absl::Duration scale_down_idle_timeout = absl::Seconds(30);
  absl::Duration balance_period = absl::Seconds(5);
  // Timeout for LeaseWorker. Zero means no timeout (block forever).
  absl::Duration lease_timeout = absl::ZeroDuration();
  // Duration of async worker recycling phase. Configurable for testing.
  absl::Duration recycle_duration = absl::Milliseconds(10);
};

// Evolution of the WarmWorkerPool. Supports dynamic resizing,
// language affinity, and asynchronous recycling.
//
// Thread-safety:
//   All public methods are thread-safe.
//   Internal lock ordering: pool_->mutex_ MUST be acquired before worker->mutex_
//   to prevent AB-BA deadlocks.
class DynamicWorkerCoordinator {
 public:
  using Options = DynamicWorkerCoordinatorOptions;

  // Internal request tracking.
  struct PendingRequest {
    LanguageId lang;
    std::shared_ptr<WorkerTask> task;
    absl::Notification* done_notification;
    absl::Status result;
    absl::Time request_time;
  };

  explicit DynamicWorkerCoordinator(Options options = Options());
  ~DynamicWorkerCoordinator();

  DynamicWorkerCoordinator(const DynamicWorkerCoordinator&) = delete;
  DynamicWorkerCoordinator& operator=(const DynamicWorkerCoordinator&) = delete;

  // Starts the coordinator and pre-boots min_workers workers.
  // No-op if already started. Returns immediately if shutting down.
  void Start();

  // Gracefully shuts down the coordinator. Drains pending requests with
  // CancelledError. Joins the balancer and all worker threads.
  void Shutdown();

  // Leases a worker with affinity for the specified language.
  // Blocks until a worker is available or lease_timeout (if configured) elapses.
  // Returns FailedPreconditionError if coordinator is shutting down.
  // Returns DeadlineExceededError if lease_timeout is exceeded.
  absl::StatusOr<WorkerTask*> LeaseWorker(LanguageId lang,
                                           std::shared_ptr<WorkerTask> task);

  // Releases the worker associated with the given task back to the pool.
  void ReleaseWorker(WorkerTask* task);

 private:
  enum class WorkerState {
    kIdle,
    kBusy,
    kRecycling,
    kShutdown,
  };

  class Worker {
   public:
    explicit Worker(DynamicWorkerCoordinator* pool, LanguageId lang);
    ~Worker();

    LanguageId language() const { return lang_; }
    WorkerState state() const { return state_.load(std::memory_order_relaxed); }

    // Assigns a task to this worker and wakes it up.
    // REQUIRES: pool->mutex_ held by caller. Worker mutex must NOT be held.
    // Returns false if worker is not idle.
    bool TryAssignLocked(std::shared_ptr<WorkerTask> task);

    void NotifyShutdown();
    void Join();

   private:
    void Run();
    void DoRecycle();

    DynamicWorkerCoordinator* pool_;
    LanguageId lang_;
    std::shared_ptr<WorkerTask> task_ ABSL_GUARDED_BY(mutex_);
    std::atomic<WorkerState> state_{WorkerState::kIdle};

    mutable absl::Mutex mutex_;
    absl::CondVar cv_;
    std::thread thread_;
  };

  void PoolBalancerLoop();
  void AdjustPoolSize() ABSL_LOCKS_EXCLUDED(mutex_);

  Options options_;
  absl::Mutex mutex_;

  absl::flat_hash_map<WorkerTask*, Worker*> active_leases_ ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<Worker>> workers_ ABSL_GUARDED_BY(mutex_);
  std::deque<std::shared_ptr<PendingRequest>> request_queue_ ABSL_GUARDED_BY(mutex_);

  std::atomic<int64_t> total_wait_time_us_{0};
  std::atomic<int64_t> completed_requests_{0};

  std::atomic<bool> shutting_down_{false};
  absl::CondVar cv_; // For interruptible sleep in balancer.
  std::thread balancer_thread_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_DYNAMIC_WORKER_COORDINATOR_H_
