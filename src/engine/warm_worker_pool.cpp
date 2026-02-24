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
    : max_workers_(max_workers) {}

WarmWorkerPool::~WarmWorkerPool() { Shutdown(); }

void WarmWorkerPool::Start() {
  absl::MutexLock lock(&mutex_);
  if (!workers_.empty()) {
    return;
  }
  const size_t worker_count = max_workers_ > 0 ? static_cast<size_t>(max_workers_) : 1;
  workers_.reserve(worker_count);
  for (size_t i = 0; i < worker_count; ++i) {
    workers_.push_back(std::make_unique<Worker>(this));
    idle_workers_++;
  }
}

void WarmWorkerPool::Shutdown() {
  if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  // Collect raw worker pointers under the lock so we don't race with
  // AcquireWorker() which iterates workers_ under the same mutex.
  std::vector<Worker*> workers_to_stop;
  {
    absl::MutexLock lock(&mutex_);
    workers_to_stop.reserve(workers_.size());
    for (const auto& worker : workers_) {
      workers_to_stop.push_back(worker.get());
    }
  }

  // Signal all workers to stop (outside the pool lock to avoid deadlock
  // with workers that might re-acquire the pool mutex during their loop).
  for (auto* worker : workers_to_stop) {
    worker->Notify();
  }
  // Wait for all worker threads to finish.
  for (auto* worker : workers_to_stop) {
    worker->Join();
  }

  // Now safe to clear — no worker threads are running.
  {
    absl::MutexLock lock(&mutex_);
    active_tasks_.clear();
    workers_.clear();
  }
}

absl::Status WarmWorkerPool::AcquireWorker(std::shared_ptr<WorkerTask> task) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return absl::FailedPreconditionError("Worker pool is shutting down");
  }
  
  // Quick check before locking.
  if (idle_workers_.load(std::memory_order_acquire) <= 0) {
    return absl::ResourceExhaustedError("No idle workers available");
  }

  absl::MutexLock lock(&mutex_);
  // Re-check after locking.
  if (shutting_down_.load(std::memory_order_acquire)) {
    return absl::FailedPreconditionError("Worker pool is shutting down");
  }

  WorkerTask* raw_task = task.get();
  for (const auto& worker : workers_) {
    if (worker->TryAssign(task)) {
      active_tasks_.emplace(raw_task, std::move(task));
      idle_workers_.fetch_sub(1, std::memory_order_relaxed);
      return absl::OkStatus();
    }
  }
  return absl::InternalError("Failed to assign worker");
}

void WarmWorkerPool::NotifyWorkerIdle() {
  idle_workers_.fetch_add(1, std::memory_order_relaxed);
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
  stopping_.store(true, std::memory_order_release);
  absl::MutexLock lock(&mutex_);
  cv_.Signal();
}

void WarmWorkerPool::Worker::Join() {
  stopping_.store(true, std::memory_order_release);
  {
    absl::MutexLock lock(&mutex_);
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
      while (task_ == nullptr && !stopping_.load(std::memory_order_acquire)) {
        cv_.Wait(&mutex_);
      }
      if (stopping_.load(std::memory_order_acquire) && task_ == nullptr) {
        break;
      }
      task = std::move(task_);
    }
    
    if (task) {
      task->StartExecution();
      task->PumpWrites();
      pool_->NotifyWorkerIdle();
      task.reset();
    }
  }
}

}  // namespace dcodex
