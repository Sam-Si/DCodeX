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

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"

namespace dcodex {
namespace {

class TestTask : public WorkerTask {
 public:
  TestTask(std::atomic<int>* counter, absl::Notification* start_notify = nullptr)
      : counter_(counter), start_notify_(start_notify) {}

  void StartExecution() override {
    if (start_notify_) start_notify_->WaitForNotification();
    counter_->fetch_add(1);
  }

  void PumpWrites() override {
    // No-op for testing
  }

 private:
  std::atomic<int>* counter_;
  absl::Notification* start_notify_;
};

TEST(WarmWorkerPoolTest, BasicAcquireAndRelease) {
  constexpr int kNumWorkers = 4;
  WarmWorkerPool pool(kNumWorkers);
  pool.Start();

  std::atomic<int> counter{0};
  auto task = std::make_shared<TestTask>(&counter);

  ASSERT_TRUE(pool.AcquireWorker(task).ok());

  // Wait for worker to finish (counter reaches 1)
  auto start_time = std::chrono::steady_clock::now();
  while (counter.load() < 1) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 5) {
      FAIL() << "Timeout waiting for task to start execution";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  pool.ReleaseTask(task.get());
  pool.Shutdown();
}

TEST(WarmWorkerPoolTest, ConcurrencyStressTest) {
  constexpr int kNumWorkers = 16;
  constexpr int kNumTasks = 200;
  WarmWorkerPool pool(kNumWorkers);
  pool.Start();

  std::atomic<int> completed_tasks{0};
  std::vector<std::shared_ptr<TestTask>> tasks;

  for (int i = 0; i < kNumTasks; ++i) {
    auto task = std::make_shared<TestTask>(&completed_tasks);
    tasks.push_back(task);

    // Try to acquire a worker, retry if busy
    while (true) {
      auto status = pool.AcquireWorker(task);
      if (status.ok()) break;
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  // Busy wait for all to complete
  auto start_time = std::chrono::steady_clock::now();
  while (completed_tasks.load() < kNumTasks) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30) {
      FAIL() << "Timeout: Only " << completed_tasks.load() << " tasks completed out of " << kNumTasks;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  for (auto& task : tasks) {
    pool.ReleaseTask(task.get());
  }

  pool.Shutdown();
}

TEST(WarmWorkerPoolTest, ShutdownBehavior) {
  WarmWorkerPool pool(4);
  pool.Start();
  pool.Shutdown();

  std::atomic<int> counter{0};
  auto task = std::make_shared<TestTask>(&counter);
  auto status = pool.AcquireWorker(task);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace dcodex
