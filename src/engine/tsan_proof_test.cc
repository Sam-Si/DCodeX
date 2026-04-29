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

// =====================================================================
// TSan PROOF-OF-DETECTION Test
// =====================================================================
//
// PURPOSE:
//   This file contains a DELIBERATELY BUGGY implementation of a pattern
//   that is common in high-performance systems like DCodeX: a metrics
//   collector where a developer removed mutex protection for "performance",
//   introducing a subtle data race.
//
// WHY IT'S SUBTLE:
//   On x86-64, aligned 64-bit reads and writes are naturally atomic at
//   the hardware level. This means that in normal testing — even with
//   hundreds of threads — the race almost never manifests as a wrong
//   answer. The program "works fine" in dev, in staging, and even in
//   production for weeks. Then, one day, the compiler reorders a store
//   past a load, or the CPU's store buffer batches two writes, and you
//   get a corrupted metrics snapshot served to a monitoring dashboard.
//
// WHAT THIS PROVES:
//   ThreadSanitizer instruments every memory access at compile time and
//   detects the happens-before violation DETERMINISTICALLY, regardless
//   of whether the hardware would actually reorder. This test is run in
//   CI with --config=tsan and is EXPECTED TO FAIL with exit code 66.
//   If TSan does NOT detect the race, the CI step fails — proving the
//   sanitizer pipeline itself is broken.
//
// DO NOT FIX THE BUGS IN THIS FILE. THEY ARE INTENTIONAL.
// =====================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace {

// =====================================================================
// DELIBERATELY BUGGY: Unprotected Execution Metrics Collector
//
// This mirrors the real DCodeX pattern:
//   DynamicWorkerCoordinator::Metrics  → MetricsSnapshot
//   GetMetrics()                       → GetSnapshot()
//   completed_requests_               → total_executions_
//
// The real code uses absl::Mutex + ABSL_GUARDED_BY correctly.
// This "optimized" version removes the lock. The race is on the
// compound read-modify-write of the shared struct fields.
// =====================================================================

struct MetricsSnapshot {
  int64_t total_executions;
  int64_t cache_hits;
  int64_t cache_misses;
  double cumulative_latency_ms;
  double avg_latency_ms;
};

class BuggyMetricsCollector {
 public:
  BuggyMetricsCollector()
      : total_executions_(0),
        cache_hits_(0),
        cache_misses_(0),
        cumulative_latency_ms_(0.0) {}

  // BUG: Multiple threads call this concurrently without synchronization.
  // The individual increments LOOK atomic on x86, but:
  //   1) They are not atomic in the C++ memory model.
  //   2) The compound operation (read + modify + write) on
  //      cumulative_latency_ms_ is NEVER atomic, even on x86.
  //   3) The cache_hit branch means different threads take different
  //      code paths, increasing the window for interleaving.
  void RecordExecution(double latency_ms, bool cache_hit) {
    total_executions_++;                       // DATA RACE
    cumulative_latency_ms_ += latency_ms;      // DATA RACE (compound RMW)

    if (cache_hit) {
      cache_hits_++;                           // DATA RACE
    } else {
      cache_misses_++;                         // DATA RACE
    }
  }

  // BUG: TOCTOU race — reads of total_executions_ and cumulative_latency_ms_
  // are not atomic with respect to each other. A writer can increment
  // total_executions_ between our read of cumulative_latency_ms_ and
  // our read of total_executions_, producing a snapshot where
  // avg_latency = cumulative / (N+1) instead of cumulative / N.
  MetricsSnapshot GetSnapshot() const {
    MetricsSnapshot snap;
    snap.total_executions = total_executions_;          // DATA RACE (read)
    snap.cache_hits = cache_hits_;                      // DATA RACE (read)
    snap.cache_misses = cache_misses_;                  // DATA RACE (read)
    snap.cumulative_latency_ms = cumulative_latency_ms_;// DATA RACE (read)

    // TOCTOU: total_executions_ may have changed between the reads above.
    if (snap.total_executions > 0) {
      snap.avg_latency_ms =
          snap.cumulative_latency_ms / static_cast<double>(snap.total_executions);
    } else {
      snap.avg_latency_ms = 0.0;
    }
    return snap;
  }

  double GetCacheHitRate() const {
    int64_t total = total_executions_;   // DATA RACE (read)
    if (total == 0) return 0.0;
    int64_t hits = cache_hits_;          // DATA RACE (read, TOCTOU with above)
    return static_cast<double>(hits) / static_cast<double>(total);
  }

 private:
  // Shared mutable state with NO synchronization.
  // In the real DCodeX code, these would be protected by absl::Mutex
  // or std::atomic. Here, they are deliberately unprotected.
  int64_t total_executions_;
  int64_t cache_hits_;
  int64_t cache_misses_;
  double cumulative_latency_ms_;
};

}  // namespace

// =====================================================================
// TEST: Exercises the race with realistic DCodeX-like traffic patterns.
//
// Without TSan: Passes on x86 (hardware hides the race).
// With TSan:    Fails with exit code 66 on the FIRST racy access.
// =====================================================================
TEST(TsanProof, CatchesMetricsCollectorRace) {
  BuggyMetricsCollector collector;

  constexpr int kWriterThreads = 6;
  constexpr int kReaderThreads = 2;
  constexpr int kOpsPerWriter = 5000;
  constexpr int kReadsPerReader = 2000;

  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  threads.reserve(kWriterThreads + kReaderThreads);

  // --- Writer threads (simulate concurrent gRPC Execute requests) ---
  for (int t = 0; t < kWriterThreads; ++t) {
    threads.emplace_back([&collector, &start, t]() {
      while (!start.load(std::memory_order_acquire)) {
        // spin-wait for synchronized start
      }
      for (int i = 0; i < kOpsPerWriter; ++i) {
        double latency = 1.0 + static_cast<double>(i % 50) * 0.1;
        bool cache_hit = ((i + t) % 3) != 0;  // ~67% hit rate
        collector.RecordExecution(latency, cache_hit);
      }
    });
  }

  // --- Reader threads (simulate monitoring/GetSystemMetrics RPCs) ---
  for (int r = 0; r < kReaderThreads; ++r) {
    threads.emplace_back([&collector, &start]() {
      while (!start.load(std::memory_order_acquire)) {
        // spin-wait for synchronized start
      }
      volatile double sink = 0.0;
      for (int i = 0; i < kReadsPerReader; ++i) {
        MetricsSnapshot snap = collector.GetSnapshot();
        double hit_rate = collector.GetCacheHitRate();

        // Consume values to prevent compiler optimization.
        // We do NOT assert here — the values may be torn/inconsistent
        // due to the race, and that's the whole point. Only TSan
        // should report the error.
        sink += snap.avg_latency_ms + hit_rate;
      }
      (void)sink;
    });
  }

  // Release all threads simultaneously to maximize contention.
  start.store(true, std::memory_order_release);

  for (auto& t : threads) {
    t.join();
  }

  // Without TSan, this test passes — the values may be slightly off on
  // some architectures, but no assertion checks them during the race.
  // With TSan, the process was killed long before reaching this line.
  MetricsSnapshot final_snap = collector.GetSnapshot();
  EXPECT_GT(final_snap.total_executions, 0);
}
