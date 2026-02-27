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

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/server/execution_cache.h"

namespace dcodex {

class SandboxedProcess {
 public:
  struct ResourceStats {
    // Peak memory usage in bytes.
    long peak_memory_bytes;
    // User CPU time in milliseconds.
    long user_time_ms;
    // System CPU time in milliseconds.
    long system_time_ms;
    // Total elapsed time in milliseconds.
    long elapsed_time_ms;
  };

  struct Result {
    bool success;
    std::string error_message;
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

  // Maximum combined stdout+stderr output in bytes before the process is
  // killed and the output is truncated.  10 KB is small enough for demos
  // while still protecting against runaway printers.
  static constexpr size_t kMaxOutputBytes = 10 * 1024;  // 10 KB

  using OutputCallback =
      std::function<void(absl::string_view stdout_chunk,
                         absl::string_view stderr_chunk)>;

  // Compiles and runs code with caching support.
  // Returns cached result if available, otherwise executes and caches.
  // stdin_data is fed to the program's stdin; empty string means EOF at start.
  [[nodiscard]] static Result CompileAndRunStreaming(
      absl::string_view code, absl::string_view stdin_data,
      OutputCallback callback);

  // Accesses the global execution cache.
  [[nodiscard]] static ExecutionCache& GetCache();

  // Clears the execution cache.
  static void ClearCache();

 private:
  [[nodiscard]] static std::string WriteTempFile(absl::string_view extension,
                                                  absl::string_view content);
  // stdin_data is written to the child's stdin pipe.
  // For compilation (sandboxed=false) stdin_data is always empty.
  [[nodiscard]] static Result ExecuteCommandStreaming(
      absl::Span<const std::string> argv, absl::string_view stdin_data,
      OutputCallback callback, bool sandboxed = false);

  // Internal execution without caching.
  [[nodiscard]] static Result ExecuteWithoutCache(absl::string_view code,
                                                   absl::string_view stdin_data,
                                                   OutputCallback callback);
};

}  // namespace dcodex

#endif  // SRC_SERVER_SANDBOX_H_