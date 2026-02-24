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

#ifndef SRC_ENGINE_WARM_WORKER_POOL_H_
#define SRC_ENGINE_WARM_WORKER_POOL_H_

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

namespace dcodex {

class WorkerTask {
 public:
  virtual ~WorkerTask() = default;
  virtual void StartExecution() = 0;
  virtual void PumpWrites() = 0;
};

class WarmWorkerPool {
 public:
  explicit WarmWorkerPool(int max_workers);
  ~WarmWorkerPool();

  WarmWorkerPool(const WarmWorkerPool&) = delete;
  WarmWorkerPool& operator=(const WarmWorkerPool&) = delete;

  void Start();
  void Shutdown();

  absl::Status AcquireWorker(std::shared_ptr<WorkerTask> task);
  void NotifyWorkerIdle();
  void ReleaseTask(WorkerTask* task);

 private:
  class Worker {
   public:
    explicit Worker(WarmWorkerPool* pool);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    bool TryAssign(std::shared_ptr<WorkerTask> task);
    void Notify();
    void Join();

   private:
    void Run();

    WarmWorkerPool* pool_;
    std::shared_ptr<WorkerTask> task_;
    mutable absl::Mutex mutex_;
    absl::CondVar cv_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
  };

  absl::Mutex mutex_;
  absl::flat_hash_map<WorkerTask*, std::shared_ptr<WorkerTask>> active_tasks_;
  std::vector<std::unique_ptr<Worker>> workers_;
  int max_workers_;
  std::atomic<int> idle_workers_{0};
  std::atomic<bool> shutting_down_{false};
};

}  // namespace dcodex

#endif  // SRC_ENGINE_WARM_WORKER_POOL_H_
