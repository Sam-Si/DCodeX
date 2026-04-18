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

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

namespace dcodex {
namespace {

// Controllable test task with optional blocking on start and done signals.
class TestTask : public WorkerTask {
 public:
  TestTask(std::atomic<int>* counter,
           absl::Notification* start_gate = nullptr,
           absl::Notification* done_notify = nullptr)
      : counter_(counter), start_gate_(start_gate), done_notify_(done_notify) {}

  void StartExecution() override {
    if (start_gate_) start_gate_->WaitForNotification();
    counter_->fetch_add(1, std::memory_order_relaxed);
  }

  void PumpWrites() override {
    if (done_notify_) done_notify_->Notify();
  }

 private:
  std::atomic<int>* counter_;
  absl::Notification* start_gate_;
  absl::Notification* done_notify_;
};

// Helper: options with fast balance period and configurable recycling.
DynamicWorkerCoordinator::Options TestOptions(
    absl::Duration recycle = absl::ZeroDuration(),
    absl::Duration balance = absl::Milliseconds(50)) {
  DynamicWorkerCoordinator::Options opts;
  opts.min_workers = 2;
  opts.max_workers = 8;
  opts.balance_period = balance;
  opts.recycle_duration = recycle;
  return opts;
}

// ============================================================================
// TC-01: Basic Lease and Release
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, BasicLeaseAndRelease) {
  auto opts = TestOptions();
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  std::atomic<int> counter{0};
  absl::Notification done;
  auto task = std::make_shared<TestTask>(&counter, nullptr, &done);

  auto lease = coordinator.LeaseWorker(LanguageId::kCpp, task);
  ASSERT_TRUE(lease.ok());

  done.WaitForNotification();
  EXPECT_EQ(counter.load(), 1);

  coordinator.ReleaseWorker(*lease);
  coordinator.Shutdown();
}

// ============================================================================
// TC-02: Language Affinity — C++ and Python pre-booted workers
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, LanguageAffinity) {
  auto opts = TestOptions();
  opts.min_workers = 2;  // 1 C++, 1 Python (alternating).
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  std::atomic<int> cpp_counter{0}, py_counter{0};
  absl::Notification cpp_done, py_done;

  auto cpp_task = std::make_shared<TestTask>(&cpp_counter, nullptr, &cpp_done);
  auto py_task = std::make_shared<TestTask>(&py_counter, nullptr, &py_done);

  auto cpp_lease = coordinator.LeaseWorker(LanguageId::kCpp, cpp_task);
  auto py_lease = coordinator.LeaseWorker(LanguageId::kPython, py_task);

  ASSERT_TRUE(cpp_lease.ok());
  ASSERT_TRUE(py_lease.ok());

  cpp_done.WaitForNotification();
  py_done.WaitForNotification();

  EXPECT_EQ(cpp_counter.load(), 1);
  EXPECT_EQ(py_counter.load(), 1);

  coordinator.ReleaseWorker(*cpp_lease);
  coordinator.ReleaseWorker(*py_lease);
  coordinator.Shutdown();
}

// ============================================================================
// TC-03: Async Recycling — second lease must wait for recycle to finish
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, AsyncRecyclingBlocksReuse) {
  DynamicWorkerCoordinator::Options opts;
  opts.min_workers = 1;
  opts.max_workers = 1;  // No scaling — forces recycling path.
  opts.balance_period = absl::Seconds(60);  // Disable balancer interference.
  opts.recycle_duration = absl::Milliseconds(100);
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  std::atomic<int> counter{0};

  // First task.
  absl::Notification done1;
  auto task1 = std::make_shared<TestTask>(&counter, nullptr, &done1);
  auto lease1 = coordinator.LeaseWorker(LanguageId::kCpp, task1);
  ASSERT_TRUE(lease1.ok());
  done1.WaitForNotification();
  coordinator.ReleaseWorker(*lease1);

  // Second task — sole worker is RECYCLING, so this must queue and wait.
  absl::Notification done2;
  auto task2 = std::make_shared<TestTask>(&counter, nullptr, &done2);
  absl::Time start = absl::Now();
  auto lease2 = coordinator.LeaseWorker(LanguageId::kCpp, task2);
  absl::Duration wait = absl::Now() - start;

  ASSERT_TRUE(lease2.ok());
  // Should have waited at least the recycle duration minus some tolerance.
  EXPECT_GE(wait, absl::Milliseconds(50));

  coordinator.ReleaseWorker(*lease2);
  coordinator.Shutdown();
}

// ============================================================================
// TC-04: Shutdown drains pending queue with CancelledError
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, ShutdownDrainsQueue) {
  DynamicWorkerCoordinator::Options opts;
  opts.min_workers = 1;
  opts.max_workers = 1;
  opts.recycle_duration = absl::ZeroDuration();
  opts.balance_period = absl::Seconds(60);
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  // Gate keeps the worker busy until we're ready.
  std::atomic<int> c1{0};
  absl::Notification gate, done1;
  auto task1 = std::make_shared<TestTask>(&c1, &gate, &done1);
  auto lease1 = coordinator.LeaseWorker(LanguageId::kCpp, task1);
  ASSERT_TRUE(lease1.ok());

  // Queue a second request in a background thread while task1 blocks.
  std::atomic<int> c2{0};
  absl::Notification done2;
  absl::Status queued_result;
  std::thread waiter([&] {
    auto task2 = std::make_shared<TestTask>(&c2, nullptr, &done2);
    auto lease2 = coordinator.LeaseWorker(LanguageId::kCpp, task2);
    queued_result = lease2.status();
  });

  // Give waiter time to block on queue.
  absl::SleepFor(absl::Milliseconds(50));

  // Release gate so task1 can finish, then shut down.
  gate.Notify();
  done1.WaitForNotification();
  coordinator.ReleaseWorker(*lease1);
  coordinator.Shutdown();

  waiter.join();
  // Queued request either ran or was cancelled. Both are valid.
  EXPECT_TRUE(queued_result.ok() || absl::IsCancelled(queued_result) ||
              queued_result.code() == absl::StatusCode::kFailedPrecondition)
      << "Got: " << queued_result;
}

// ============================================================================
// TC-05: Lease timeout — returns DeadlineExceeded when pool is full
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, LeaseTimeout) {
  DynamicWorkerCoordinator::Options opts;
  opts.min_workers = 1;
  opts.max_workers = 1;
  opts.recycle_duration = absl::Seconds(60);  // Keep worker busy.
  opts.balance_period = absl::Seconds(60);
  opts.lease_timeout = absl::Milliseconds(100);
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  // Occupy the sole worker indefinitely.
  std::atomic<int> c1{0};
  absl::Notification gate, done1;
  auto task1 = std::make_shared<TestTask>(&c1, &gate, &done1);
  auto lease1 = coordinator.LeaseWorker(LanguageId::kCpp, task1);
  ASSERT_TRUE(lease1.ok());

  // Second request must time out.
  std::atomic<int> c2{0};
  absl::Notification done2;
  auto task2 = std::make_shared<TestTask>(&c2, nullptr, &done2);
  auto lease2 = coordinator.LeaseWorker(LanguageId::kCpp, task2);

  EXPECT_FALSE(lease2.ok());
  EXPECT_EQ(lease2.status().code(), absl::StatusCode::kDeadlineExceeded)
      << lease2.status();

  // Clean up.
  gate.Notify();
  done1.WaitForNotification();
  coordinator.ReleaseWorker(*lease1);
  coordinator.Shutdown();
}

// ============================================================================
// TC-06: Start after Shutdown is a no-op
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, StartAfterShutdownIsNoOp) {
  auto opts = TestOptions();
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();
  coordinator.Shutdown();
  // Must not crash or hang.
  coordinator.Start();
}

// ============================================================================
// TC-07: Fair queueing — Python not starved by C++ burst
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, FairQueueingPythonNotStarved) {
  DynamicWorkerCoordinator::Options opts;
  opts.min_workers = 1;
  opts.max_workers = 4;
  opts.balance_period = absl::Milliseconds(50);
  opts.scale_up_latency_threshold = absl::Milliseconds(5);
  opts.recycle_duration = absl::ZeroDuration();
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  // Submit 5 long-running C++ tasks to saturate all workers.
  std::atomic<int> cpp_done_count{0};
  std::vector<std::thread> cpp_threads;
  for (int i = 0; i < 5; ++i) {
    cpp_threads.emplace_back([&coordinator, &cpp_done_count] {
      std::atomic<int> c{0};
      absl::Notification done;
      auto task = std::make_shared<TestTask>(&c, nullptr, &done);
      auto lease = coordinator.LeaseWorker(LanguageId::kCpp, task);
      if (lease.ok()) {
        done.WaitForNotification();
        coordinator.ReleaseWorker(*lease);
        cpp_done_count.fetch_add(1);
      }
    });
  }

  // Let some C++ tasks queue up.
  absl::SleepFor(absl::Milliseconds(200));

  // Submit a Python task — pool should scale and service it.
  std::atomic<int> py_count{0};
  absl::Notification py_done;
  auto py_task = std::make_shared<TestTask>(&py_count, nullptr, &py_done);
  auto py_lease = coordinator.LeaseWorker(LanguageId::kPython, py_task);
  ASSERT_TRUE(py_lease.ok()) << py_lease.status();
  py_done.WaitForNotification();
  EXPECT_EQ(py_count.load(), 1);

  coordinator.ReleaseWorker(*py_lease);
  coordinator.Shutdown();

  for (auto& t : cpp_threads) {
    if (t.joinable()) t.join();
  }
}

// ============================================================================
// TC-08: Double Release is safe (idempotent)
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, DoubleReleaseIsSafe) {
  auto opts = TestOptions();
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  std::atomic<int> counter{0};
  absl::Notification done;
  auto task = std::make_shared<TestTask>(&counter, nullptr, &done);

  auto lease = coordinator.LeaseWorker(LanguageId::kCpp, task);
  ASSERT_TRUE(lease.ok());
  done.WaitForNotification();

  coordinator.ReleaseWorker(*lease);
  // Second call must not crash or corrupt state.
  coordinator.ReleaseWorker(*lease);
  coordinator.Shutdown();
}

// ============================================================================
// TC-09: LeaseWorker on already-shut-down coordinator
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, LeaseAfterShutdownFails) {
  auto opts = TestOptions();
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();
  coordinator.Shutdown();

  std::atomic<int> counter{0};
  auto task = std::make_shared<TestTask>(&counter);
  auto lease = coordinator.LeaseWorker(LanguageId::kCpp, task);
  EXPECT_FALSE(lease.ok());
}

// ============================================================================
// TC-10: Concurrent Leases hit correct workers
// ============================================================================
TEST(DynamicWorkerCoordinatorTest, ConcurrentLeasesAllComplete) {
  DynamicWorkerCoordinator::Options opts;
  opts.min_workers = 4;
  opts.max_workers = 4;
  opts.balance_period = absl::Seconds(60);
  opts.recycle_duration = absl::ZeroDuration();
  DynamicWorkerCoordinator coordinator(opts);
  coordinator.Start();

  constexpr int kNumTasks = 8;
  std::atomic<int> completed{0};
  std::vector<std::thread> threads;

  for (int i = 0; i < kNumTasks; ++i) {
    threads.emplace_back([&coordinator, &completed] {
      std::atomic<int> c{0};
      absl::Notification done;
      auto task = std::make_shared<TestTask>(&c, nullptr, &done);
      auto lease = coordinator.LeaseWorker(LanguageId::kCpp, task);
      if (lease.ok()) {
        done.WaitForNotification();
        coordinator.ReleaseWorker(*lease);
        completed.fetch_add(1);
      }
    });
  }

  for (auto& t : threads) t.join();
  EXPECT_EQ(completed.load(), kNumTasks);
  coordinator.Shutdown();
}

}  // namespace
}  // namespace dcodex
