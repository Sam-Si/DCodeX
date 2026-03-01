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

#include "src/engine/warm_worker_pool.h"

namespace dcodex {

WarmWorkerPool::WarmWorkerPool(int max_workers)
    : max_workers_(max_workers),
      idle_workers_(0),
      shutting_down_(false) {}

WarmWorkerPool::~WarmWorkerPool() { Shutdown(); }

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

absl::Status WarmWorkerPool::AcquireWorker(std::shared_ptr<WorkerTask> task) {
  absl::MutexLock lock(&mutex_);
  if (shutting_down_) {
    return absl::FailedPreconditionError("Worker pool is shutting down");
  }
  if (idle_workers_ == 0) {
    return absl::ResourceExhaustedError("No idle workers available");
  }
  WorkerTask* raw_task = task.get();
  for (const auto& worker : workers_) {
    if (worker->TryAssign(task)) {
      active_tasks_.emplace(raw_task, std::move(task));
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

void WarmWorkerPool::ReleaseTask(WorkerTask* task) {
  absl::MutexLock lock(&mutex_);
  active_tasks_.erase(task);
}

WarmWorkerPool::Worker::Worker(WarmWorkerPool* pool)
    : pool_(pool),
      thread_(&Worker::Run, this) {}

WarmWorkerPool::Worker::~Worker() { Join(); }

bool WarmWorkerPool::Worker::TryAssign(std::shared_ptr<WorkerTask> task) {
  absl::MutexLock lock(&mutex_);
  if (task_ || stopping_) {
    return false;
  }
  task_ = std::move(task);
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
    std::shared_ptr<WorkerTask> task;
    {
      absl::MutexLock lock(&mutex_);
      while (task_ == nullptr && !stopping_) {
        cv_.Wait(&mutex_);
      }
      if (stopping_) {
        break;
      }
      task = std::move(task_);
    }
    task->StartExecution();
    task->PumpWrites();
    pool_->NotifyWorkerIdle();
    task.reset();
  }
}

}  // namespace dcodex
