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

// sandbox_test.cc: Integration tests for the sandboxed execution engine.
//
// These tests verify that resource limits enforced via fork+exec+setrlimit
// actually take effect in the child — i.e., that the fix to ApplyResourceLimits()
// propagates correctly through SpawnProcess → ForkAndExecSandboxed.
//
// Test strategy:
//   - Each test submits real source code through the full SandboxedProcess stack.
//   - We verify the observable outcome (timeout, OOM failure, success) rather
//     than mocking internals. This is an integration test, not a unit test.
//   - Linux-only tests (RLIMIT_CPU, RLIMIT_AS) are gated with #ifdef __linux__
//     because macOS enforces limits differently and the server targets Linux CI.

#include "src/engine/sandbox.h"

#include <sys/resource.h>

#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/common/execution_cache.h"
#include "src/engine/execution_types.h"

ABSL_DECLARE_FLAG(int, sandbox_cpu_time_limit_seconds);
ABSL_DECLARE_FLAG(int, sandbox_wall_clock_timeout_seconds);
ABSL_DECLARE_FLAG(uint64_t, sandbox_memory_limit_bytes);
ABSL_DECLARE_FLAG(uint64_t, sandbox_max_output_bytes);

namespace dcodex {
namespace {

// Helper: creates a SandboxedProcess with a fresh, disconnected cache so tests
// are isolated from each other.
std::shared_ptr<SandboxedProcess> MakeSandbox() {
  auto cache = std::make_shared<ExecutionCache>(absl::Hours(1), 1000);
  return std::make_shared<SandboxedProcess>(std::move(cache));
}

// Helper: collects all streaming output into a single string.
struct OutputCapture {
  std::string combined;

  OutputCallback MakeCallback() {
    return [this](absl::string_view out, absl::string_view err) {
      combined.append(out);
      combined.append(err);
    };
  }
};

// =============================================================================
// Basic sanity: non-sandboxed compilation + sandboxed execution round-trip.
// =============================================================================

TEST(SandboxTest, HelloWorldCpp) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  absl::StatusOr<ExecutionResult> result =
      sandbox->CompileAndRunStreaming("cpp",
                                      R"(
#include <iostream>
int main() {
  std::cout << "DCodeX OK" << std::endl;
  return 0;
}
)",
                                      /*stdin_data=*/"", cap.MakeCallback());

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->success);
  EXPECT_NE(cap.combined.find("DCodeX OK"), std::string::npos)
      << "stdout did not contain 'DCodeX OK'. Got: " << cap.combined;
}

TEST(SandboxTest, HelloWorldPython) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  absl::StatusOr<ExecutionResult> result =
      sandbox->CompileAndRunStreaming("python",
                                      "print('py-ok')\n",
                                      /*stdin_data=*/"", cap.MakeCallback());

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->success);
  EXPECT_NE(cap.combined.find("py-ok"), std::string::npos)
      << "stdout did not contain 'py-ok'. Got: " << cap.combined;
}

TEST(SandboxTest, HelloWorldC) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  absl::StatusOr<ExecutionResult> result =
      sandbox->CompileAndRunStreaming("c",
                                      R"(
#include <stdio.h>
int main() { puts("c-ok"); return 0; }
)",
                                      /*stdin_data=*/"", cap.MakeCallback());

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->success);
  EXPECT_NE(cap.combined.find("c-ok"), std::string::npos)
      << "stdout did not contain 'c-ok'. Got: " << cap.combined;
}

// =============================================================================
// stdin passthrough
// =============================================================================

TEST(SandboxTest, StdinPassthrough) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  absl::StatusOr<ExecutionResult> result =
      sandbox->CompileAndRunStreaming("cpp",
                                      R"(
#include <iostream>
#include <string>
int main() {
  std::string line;
  std::getline(std::cin, line);
  std::cout << "got: " << line << std::endl;
  return 0;
}
)",
                                      /*stdin_data=*/"hello stdin",
                                      cap.MakeCallback());

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->success);
  EXPECT_NE(cap.combined.find("got: hello stdin"), std::string::npos)
      << "stdout missing stdin echo. Got: " << cap.combined;
}

// =============================================================================
// Compilation error surface
// =============================================================================

TEST(SandboxTest, CompilationError) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  // Deliberately broken C++ — missing semicolon.
  absl::StatusOr<ExecutionResult> result =
      sandbox->CompileAndRunStreaming("cpp",
                                      "int main() { return 0 }",
                                      /*stdin_data=*/"", cap.MakeCallback());

  // The pipeline returns an error status because compilation fails.
  EXPECT_FALSE(result.ok() && result->success)
      << "Expected compilation failure but got success";
}

// =============================================================================
// Wall-clock timeout (applies on all platforms via gRPC Alarm)
// =============================================================================

TEST(SandboxTest, WallClockTimeoutTriggered) {
  // Very tight wall-clock limit: 1 second.
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 30);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 1);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  // Python infinite loop — will be killed by the wall-clock timer.
  absl::StatusOr<ExecutionResult> result = sandbox->CompileAndRunStreaming(
      "python",
      "import time\nwhile True:\n    time.sleep(0.1)\n",
      /*stdin_data=*/"", cap.MakeCallback());

  // The result may come back as a Status error or as a failed ExecutionResult.
  // Either way, we must NOT see success.
  const bool timed_out =
      (!result.ok()) ||
      (!result->success && result->wall_clock_timeout);
  EXPECT_TRUE(timed_out) << "Expected wall-clock timeout but got success";
}

// =============================================================================
// Output truncation
// =============================================================================

TEST(SandboxTest, OutputTruncation) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  // Very small output cap: 1 KB.
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  // Print 10 KB → should trigger truncation.
  absl::StatusOr<ExecutionResult> result = sandbox->CompileAndRunStreaming(
      "python",
      "print('x' * 10240)\n",
      /*stdin_data=*/"", cap.MakeCallback());

  // Truncated executions may return as a failed ExecutionResult.
  const bool truncated =
      (result.ok() && result->output_truncated) ||
      (!result.ok());
  EXPECT_TRUE(truncated) << "Expected output truncation";
}

// =============================================================================
// Cache hit: identical code should produce a cache hit on second run.
// =============================================================================

TEST(SandboxTest, CacheHitOnSecondRun) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();

  const std::string code = "print('cached')\n";

  // First run — populates cache.
  {
    OutputCapture cap;
    auto r = sandbox->CompileAndRunStreaming("python", code, "", cap.MakeCallback());
    ASSERT_TRUE(r.ok()) << r.status();
    EXPECT_FALSE(r->cache_hit) << "First run should not be a cache hit";
  }

  // Second run — should be a cache hit.
  {
    OutputCapture cap;
    auto r = sandbox->CompileAndRunStreaming("python", code, "", cap.MakeCallback());
    ASSERT_TRUE(r.ok()) << r.status();
    EXPECT_TRUE(r->cache_hit) << "Second run should be a cache hit";
    EXPECT_NE(cap.combined.find("cached"), std::string::npos)
        << "Cache hit output missing. Got: " << cap.combined;
  }
}

// =============================================================================
// Linux-specific: real RLIMIT_CPU enforcement via fork+exec
//
// These tests verify the core fix: that ApplyResourceLimits() is now actually
// called inside the child process. We confirm by running a CPU-burning program
// with a very tight CPU time limit and verifying it is killed by SIGXCPU/SIGKILL
// rather than running to completion.
// =============================================================================

#ifdef __linux__

TEST(SandboxTest, Linux_CpuTimeLimitEnforced) {
  // 1 second CPU limit. The program below will burn CPU for > 10 seconds.
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 1);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 60);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  // Tight CPU burn with no sleep — should be killed by RLIMIT_CPU (SIGXCPU).
  absl::StatusOr<ExecutionResult> result = sandbox->CompileAndRunStreaming(
      "cpp",
      R"(
#include <cstdint>
int main() {
  volatile uint64_t x = 0;
  // Burns ~10 seconds of CPU on any modern machine.
  for (uint64_t i = 0; i < 3000000000ULL; ++i) x += i;
  return 0;
}
)",
      /*stdin_data=*/"", cap.MakeCallback());

  // Expect failure — killed by CPU limit (SIGXCPU → SIGKILL from kernel).
  // Before the fix, the loop would complete because rlimits were never set.
  EXPECT_FALSE(result.ok() && result->success)
      << "CPU-intensive program should have been killed by RLIMIT_CPU";
}

TEST(SandboxTest, Linux_MemoryLimitEnforced) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 15);
  // 32 MB address-space limit — any large allocation will fail.
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 32ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  // Try to allocate 64 MB — will fail if RLIMIT_AS is actually enforced.
  absl::StatusOr<ExecutionResult> result = sandbox->CompileAndRunStreaming(
      "cpp",
      R"(
#include <cstdlib>
#include <cstring>
int main() {
  const size_t kSize = 64ULL * 1024 * 1024;
  void* p = malloc(kSize);
  if (p == nullptr) return 1;  // malloc failed due to RLIMIT_AS
  memset(p, 0, kSize);         // touch pages to force mapping
  free(p);
  return 0;                    // if we reach here, limit wasn't enforced
}
)",
      /*stdin_data=*/"", cap.MakeCallback());

  // Before the fix: posix_spawn + no-op setrlimit → malloc succeeds → return 0.
  // After the fix:  fork+exec + real RLIMIT_AS → malloc returns nullptr → exit(1).
  EXPECT_FALSE(result.ok() && result->success)
      << "Large allocation should have failed due to RLIMIT_AS";
}

TEST(SandboxTest, Linux_ResourceStatsPopulated) {
  absl::SetFlag(&FLAGS_sandbox_cpu_time_limit_seconds, 5);
  absl::SetFlag(&FLAGS_sandbox_wall_clock_timeout_seconds, 10);
  absl::SetFlag(&FLAGS_sandbox_memory_limit_bytes, 256ULL * 1024 * 1024);
  absl::SetFlag(&FLAGS_sandbox_max_output_bytes, 64 * 1024);

  auto sandbox = MakeSandbox();
  OutputCapture cap;

  absl::StatusOr<ExecutionResult> result = sandbox->CompileAndRunStreaming(
      "cpp",
      R"(
#include <iostream>
int main() { std::cout << "stats" << std::endl; return 0; }
)",
      /*stdin_data=*/"", cap.MakeCallback());

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->success);
  // Resource stats should be non-zero for a real execution.
  EXPECT_GT(result->stats.elapsed_time_ms, 0)
      << "elapsed_time_ms should be populated";
  EXPECT_GT(result->stats.peak_memory_bytes, 0)
      << "peak_memory_bytes should be populated";
}

#endif  // __linux__

}  // namespace
}  // namespace dcodex
