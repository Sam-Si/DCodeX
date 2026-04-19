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

#include "src/engine/dynamic_worker_coordinator.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "absl/log/log.h"
#include "absl/synchronization/notification.h"

namespace dcodex {

LanguageId ParseLanguageId(const std::string& lang) {
  std::string lower_lang = lang;
  std::transform(lower_lang.begin(), lower_lang.end(), lower_lang.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (lower_lang == "cpp" || lower_lang == "c++" || lower_lang == "c")
    return LanguageId::kCpp;
  if (lower_lang == "python" || lower_lang == "py") return LanguageId::kPython;
  return LanguageId::kUnknown;
}

DynamicWorkerCoordinator::DynamicWorkerCoordinator(Options options)
    : options_(options) {}

DynamicWorkerCoordinator::~DynamicWorkerCoordinator() {
  Shutdown();
}

void DynamicWorkerCoordinator::Start() {
  absl::MutexLock lock(&mutex_);
  // FIX(Issue #3): Guard against Start() being called after Shutdown().
  if (shutting_down_.load(std::memory_order_relaxed)) return;
  if (!workers_.empty()) return;  // Already started.

  for (int i = 0; i < options_.min_workers; ++i) {
    // Initial workers alternate between C++ and Python for balanced pre-boot.
    LanguageId lang = (i % 2 == 0) ? LanguageId::kCpp : LanguageId::kPython;
    workers_.push_back(std::make_unique<Worker>(this, lang));
  }

  balancer_thread_ = std::thread(&DynamicWorkerCoordinator::PoolBalancerLoop, this);
  LOG(INFO) << "DynamicWorkerCoordinator started with " << options_.min_workers
            << " workers.";
}

void DynamicWorkerCoordinator::Shutdown() {
  if (shutting_down_.exchange(true)) return;

  LOG(INFO) << "DynamicWorkerCoordinator shutting down...";

  // Drain pending requests outside the lock to avoid holding mutex
  // while calling Notify() (which can cause contention with callers).
  std::vector<std::shared_ptr<PendingRequest>> to_notify;
  {
    absl::MutexLock lock(&mutex_);
    cv_.SignalAll();  // Wake up balancer so it can exit.
    while (!request_queue_.empty()) {
      to_notify.push_back(request_queue_.front());
      request_queue_.pop_front();
    }
  }

  for (auto& req : to_notify) {
    req->result = absl::CancelledError("Coordinator shutting down");
    req->done_notification->Notify();
  }

  // Join balancer first — it may attempt to add/remove workers.
  if (balancer_thread_.joinable()) {
    balancer_thread_.join();
  }

  // Collect workers then signal and join outside the pool mutex to prevent
  // deadlock: workers lock pool->mutex_ during their post-task queue scan.
  std::vector<Worker*> workers_to_join;
  {
    absl::MutexLock lock(&mutex_);
    for (const auto& worker : workers_) {
      workers_to_join.push_back(worker.get());
    }
  }

  for (auto* worker : workers_to_join) {
    worker->NotifyShutdown();
  }
  for (auto* worker : workers_to_join) {
    worker->Join();
  }

  absl::MutexLock lock(&mutex_);
  workers_.clear();
  active_leases_.clear();
  LOG(INFO) << "DynamicWorkerCoordinator shutdown complete.";
}

absl::StatusOr<WorkerTask*> DynamicWorkerCoordinator::LeaseWorker(
    LanguageId lang, std::shared_ptr<WorkerTask> task) {
  if (shutting_down_.load(std::memory_order_relaxed)) {
    return absl::FailedPreconditionError("Coordinator is shutting down");
  }

  absl::Time start_time = absl::Now();
  absl::Notification done;
  auto req = std::make_shared<PendingRequest>();
  req->lang = lang;
  req->task = std::move(task);
  req->done_notification = &done;
  req->request_time = start_time;

  {
    absl::MutexLock lock(&mutex_);

    // FIX(Issue #4): Re-check shutting_down_ inside the pool lock before
    // queuing to avoid the race where Shutdown() drained the queue between
    // the early-exit check above and this lock acquisition.
    if (shutting_down_.load(std::memory_order_relaxed)) {
      return absl::CancelledError("Coordinator shut down");
    }

    // Two-pass affinity scan:
    // Pass 1: prefer a language-matching idle worker.
    // Pass 2: fall back to any idle worker.
    Worker* best_worker = nullptr;
    for (const auto& w : workers_) {
      if (w->state() == WorkerState::kIdle && w->language() == lang) {
        best_worker = w.get();
        break;
      }
    }
    if (!best_worker) {
      for (const auto& w : workers_) {
        if (w->state() == WorkerState::kIdle) {
          best_worker = w.get();
          break;
        }
      }
    }

    // FIX(Issue #1): TryAssignLocked is called under pool->mutex_. The worker
    // mutex is acquired inside, but we guarantee pool->mutex_ is always taken
    // first (see Worker::Run's post-task dequeue), so lock order is consistent.
    if (best_worker && best_worker->TryAssignLocked(req->task)) {
      active_leases_[req->task.get()] = best_worker;
      return req->task.get();
    }

    // No worker available — enqueue for fair delivery when one is free.
    request_queue_.push_back(req);
  }

  // Block until a worker picks up this request, or optional timeout elapses.
  if (options_.lease_timeout > absl::ZeroDuration()) {
    absl::Time deadline = start_time + options_.lease_timeout;
    if (!done.WaitForNotificationWithDeadline(deadline)) {
      // Timed out. Attempt to remove from queue.
      absl::MutexLock lock(&mutex_);
      auto it = std::find(request_queue_.begin(), request_queue_.end(), req);
      if (it != request_queue_.end()) {
        request_queue_.erase(it);
      }
      return absl::DeadlineExceededError("LeaseWorker timed out");
    }
  } else {
    done.WaitForNotification();
  }

  // Track wait latency for the PoolBalancer's scaling heuristic and system metrics.
  absl::Duration wait_time = absl::Now() - start_time;
  double wait_time_ms = absl::ToDoubleMilliseconds(wait_time);

  total_wait_time_us_.fetch_add(absl::ToInt64Microseconds(wait_time),
                                std::memory_order_relaxed);
  completed_requests_.fetch_add(1, std::memory_order_relaxed);

  {
    absl::MutexLock lock(&stats_mutex_);
    latency_history_ms_.push_back(wait_time_ms);
    // Keep only the last 10,000 requests for percentile calculation.
    if (latency_history_ms_.size() > 10000) {
      latency_history_ms_.erase(latency_history_ms_.begin());
    }
  }

  if (!req->result.ok()) return req->result;
  return req->task.get();
}

void DynamicWorkerCoordinator::ReleaseWorker(WorkerTask* task) {
  absl::MutexLock lock(&mutex_);
  active_leases_.erase(task);
}

DynamicWorkerCoordinator::Metrics DynamicWorkerCoordinator::GetMetrics() {
  Metrics m = {};
  {
    absl::MutexLock lock(&mutex_);
    m.current_pool_size = static_cast<int>(workers_.size());
    for (const auto& w : workers_) {
      switch (w->state()) {
        case WorkerState::kIdle: m.idle_workers++; break;
        case WorkerState::kBusy: m.active_workers++; break;
        case WorkerState::kRecycling: m.recycling_workers++; break;
        default: break;
      }
    }
  }

  {
    absl::MutexLock lock(&stats_mutex_);
    m.total_requests_served = static_cast<int64_t>(latency_history_ms_.size());
    if (!latency_history_ms_.empty()) {
      std::vector<double> sorted = latency_history_ms_;
      std::sort(sorted.begin(), sorted.end());
      m.p50_latency_ms = sorted[sorted.size() / 2];
      m.p99_latency_ms = sorted[static_cast<size_t>(static_cast<double>(sorted.size()) * 0.99)];
    }
  }
  return m;
}

void DynamicWorkerCoordinator::PoolBalancerLoop() {
  while (true) {
    {
      absl::MutexLock lock(&mutex_);
      cv_.WaitWithTimeout(&mutex_, options_.balance_period);
      if (shutting_down_.load(std::memory_order_relaxed)) break;
    }
    AdjustPoolSize();
  }
}

void DynamicWorkerCoordinator::AdjustPoolSize() {
  int64_t completed = completed_requests_.exchange(0);
  int64_t total_wait = total_wait_time_us_.exchange(0);

  absl::Duration avg_latency = absl::ZeroDuration();
  if (completed > 0) {
    avg_latency = absl::Microseconds(total_wait / completed);
  }

  // --- Scale-Up Phase ---
  // Collect pending requests to service with new workers before locking,
  // then batch-notify outside the lock to prevent notification under mutex.
  std::vector<std::shared_ptr<PendingRequest>> to_assign;
  std::vector<std::unique_ptr<Worker>> new_workers;
  Worker* worker_to_remove = nullptr;

  {
    absl::MutexLock lock(&mutex_);
    if (shutting_down_.load()) return;

    int current_workers = static_cast<int>(workers_.size());

    if ((avg_latency > options_.scale_up_latency_threshold ||
         !request_queue_.empty()) &&
        current_workers < options_.max_workers) {
      int to_add = std::min(2, options_.max_workers - current_workers);
      for (int i = 0; i < to_add; ++i) {
        LanguageId lang = LanguageId::kCpp;
        std::shared_ptr<PendingRequest> req = nullptr;
        if (!request_queue_.empty()) {
          req = request_queue_.front();
          request_queue_.pop_front();
          lang = req->lang;
        }

        auto worker = std::make_unique<Worker>(this, lang);
        if (req) {
          // FIX(Issue #2): TryAssignLocked is called here under pool->mutex_.
          // New workers are always idle, so this always succeeds.
          if (worker->TryAssignLocked(req->task)) {
            active_leases_[req->task.get()] = worker.get();
            to_assign.push_back(req);
          } else {
            request_queue_.push_front(req);
          }
        }
        // Collect all new workers; push to pool after notifications.
        new_workers.push_back(std::move(worker));
      }
      // FIX(Issue #2): Add workers to the pool atomically, after the loop.
      for (auto& w : new_workers) {
        workers_.push_back(std::move(w));
      }
      LOG(INFO) << "Scaled up pool to " << workers_.size()
                << " workers. Avg latency: " << avg_latency;

    } else if (request_queue_.empty() &&
               current_workers > options_.min_workers) {
      // --- Scale-Down Phase ---
      // Find and remove one idle worker. Join outside the lock.
      for (auto it = workers_.begin(); it != workers_.end(); ++it) {
        if ((*it)->state() == WorkerState::kIdle) {
          worker_to_remove = it->release();
          workers_.erase(it);
          LOG(INFO) << "Scaling down: removing idle worker.";
          worker_to_remove->NotifyShutdown();
          break;
        }
      }
    }
  }  // pool mutex released here.

  // Notify waiting callers outside the lock.
  for (auto& req : to_assign) {
    req->done_notification->Notify();
  }

  // Clean up removed worker outside the pool lock to avoid deadlocks
  // with workers that acquire pool->mutex_ during their post-task queue scan.
  if (worker_to_remove) {
    worker_to_remove->Join();
    delete worker_to_remove;
  }
}

// --- Worker Implementation ---

DynamicWorkerCoordinator::Worker::Worker(DynamicWorkerCoordinator* pool,
                                          LanguageId lang)
    : pool_(pool), lang_(lang), thread_(&Worker::Run, this) {}

DynamicWorkerCoordinator::Worker::~Worker() {
  Join();
}

// NOTE: Callers MUST hold pool_->mutex_ when calling this.
// Lock order: pool_->mutex_ → worker->mutex_.
bool DynamicWorkerCoordinator::Worker::TryAssignLocked(
    std::shared_ptr<WorkerTask> task) {
  if (state_.load(std::memory_order_acquire) != WorkerState::kIdle) return false;
  // Acquire worker mutex to safely write task_ and signal the cv.
  absl::MutexLock lock(&mutex_);
  // Re-check state inside worker lock for safety.
  if (state_.load(std::memory_order_acquire) != WorkerState::kIdle) return false;
  task_ = std::move(task);
  state_.store(WorkerState::kBusy, std::memory_order_release);
  cv_.Signal();
  return true;
}

void DynamicWorkerCoordinator::Worker::NotifyShutdown() {
  state_.store(WorkerState::kShutdown, std::memory_order_release);
  absl::MutexLock lock(&mutex_);
  cv_.Signal();
}

void DynamicWorkerCoordinator::Worker::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void DynamicWorkerCoordinator::Worker::Run() {
  while (true) {
    std::shared_ptr<WorkerTask> current_task;
    {
      absl::MutexLock lock(&mutex_);
      while (task_ == nullptr &&
             state_.load(std::memory_order_acquire) != WorkerState::kShutdown) {
        cv_.Wait(&mutex_);
      }
      if (state_.load(std::memory_order_acquire) == WorkerState::kShutdown &&
          task_ == nullptr) {
        break;
      }
      current_task = std::move(task_);
    }

    if (current_task) {
      current_task->StartExecution();
      current_task->PumpWrites();

      // Transition to RECYCLING. compare_exchange prevents overwriting kShutdown
      // if NotifyShutdown() was called during task execution.
      state_.store(WorkerState::kRecycling, std::memory_order_release);
      DoRecycle();

      WorkerState expected = WorkerState::kRecycling;
      state_.compare_exchange_strong(expected, WorkerState::kIdle,
                                     std::memory_order_acq_rel);

      // After recycling, scan the pool queue for a waiting request.
      // Lock order: pool->mutex_ is taken here, worker->mutex_ is taken
      // inside TryAssignLocked — consistent with LeaseWorker path.
      absl::Notification* pending_notify = nullptr;
      {
        absl::MutexLock pool_lock(&pool_->mutex_);
        if (!pool_->request_queue_.empty()) {
          auto req = pool_->request_queue_.front();
          pool_->request_queue_.pop_front();
          if (TryAssignLocked(req->task)) {
            pool_->active_leases_[req->task.get()] = this;
            pending_notify = req->done_notification;
          } else {
            // Should not happen — worker just became idle.
            pool_->request_queue_.push_front(req);
          }
        }
      }  // pool_lock released here.

      if (pending_notify) {
        pending_notify->Notify();
        continue;  // Loop back to execute the newly assigned task.
      }
    }
  }
}

void DynamicWorkerCoordinator::Worker::DoRecycle() {
  // Production: wipe temp files, reset namespaces, purge memory arenas.
  // Duration is configurable via Options::recycle_duration.
  absl::SleepFor(pool_->options_.recycle_duration);
}

}  // namespace dcodex
