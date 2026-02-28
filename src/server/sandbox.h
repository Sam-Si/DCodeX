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

#ifndef SRC_SERVER_SANDBOX_H_
#define SRC_SERVER_SANDBOX_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/server/execution_cache.h"

namespace dcodex {

// Resource statistics for a sandboxed process.
struct ResourceStats {
  // Peak memory usage in bytes.
  long peak_memory_bytes = 0;
  // User CPU time in milliseconds.
  long user_time_ms = 0;
  // System CPU time in milliseconds.
  long system_time_ms = 0;
  // Total elapsed time in milliseconds.
  long elapsed_time_ms = 0;
};

// Result of a sandboxed execution.
struct ExecutionResult {
  bool success = false;
  std::string error_message;
  std::string backend_trace;
  ResourceStats stats;
  // Indicates if result was served from cache.
  bool cache_hit = false;
  // Cached stdout/stderr for cache hits.
  std::string cached_stdout;
  std::string cached_stderr;
  // Indicates if the process was killed due to wall-clock timeout.
  bool wall_clock_timeout = false;
  // Indicates if the process was killed because its combined stdout+stderr
  // output exceeded kMaxOutputBytes.
  bool output_truncated = false;
};

// Callback for streaming output.
using OutputCallback =
    std::function<void(absl::string_view stdout_chunk,
                       absl::string_view stderr_chunk)>;

// Strategy Pattern: Interface for different execution strategies (e.g., C++, Python).
class ExecutionStrategy {
 public:
  virtual ~ExecutionStrategy() = default;

  // Executes the given code and returns the result.
  virtual ExecutionResult Execute(absl::string_view code,
                                  absl::string_view stdin_data,
                                  OutputCallback callback) = 0;

  // Returns a unique identifier for this strategy (used for caching).
  virtual absl::string_view GetStrategyId() const = 0;
};

// C++ implementation of the ExecutionStrategy.
class CppExecutionStrategy : public ExecutionStrategy {
 public:
  CppExecutionStrategy() = default;
  ~CppExecutionStrategy() override = default;

  ExecutionResult Execute(absl::string_view code, absl::string_view stdin_data,
                          OutputCallback callback) override;

  absl::string_view GetStrategyId() const override { return "cpp"; }
};

// Centralized configuration for sandboxed resource limits.
struct SandboxLimits {
  static constexpr int kCpuTimeLimitSeconds = 1;
  static constexpr int kWallClockTimeoutSeconds = 2;
  static constexpr size_t kMemoryLimitBytes = 4ULL * 1024 * 1024 * 1024;
  static constexpr size_t kMaxOutputBytes = 10 * 1024;  // 10 KB
};

// Orchestrator class that manages sandboxed execution and caching.
class SandboxedProcess {
 public:
  // Compiles and runs code with caching support.
  [[nodiscard]] static ExecutionResult CompileAndRunStreaming(
      absl::string_view code, absl::string_view stdin_data,
      OutputCallback callback);

  // Accesses the global execution cache.
  [[nodiscard]] static ExecutionCache& GetCache();

  // Clears the execution cache.
  static void ClearCache();

 private:
  // Internal execution without caching.
  [[nodiscard]] static ExecutionResult ExecuteWithStrategy(
      ExecutionStrategy& strategy, absl::string_view code,
      absl::string_view stdin_data, OutputCallback callback);
};

}  // namespace dcodex

#endif  // SRC_SERVER_SANDBOX_H_