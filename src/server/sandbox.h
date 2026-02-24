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

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
  };

  using OutputCallback =
      std::function<void(const std::string& stdout_chunk,
                         const std::string& stderr_chunk)>;

  // Compiles and runs code with caching support.
  // Returns cached result if available, otherwise executes and caches.
  [[nodiscard]] static Result CompileAndRunStreaming(const std::string& code,
                                                      OutputCallback callback);

  // Accesses the global execution cache.
  [[nodiscard]] static ExecutionCache& GetCache();

  // Clears the execution cache.
  static void ClearCache();

 private:
  [[nodiscard]] static std::string WriteTempFile(std::string_view extension,
                                                  std::string_view content);
  [[nodiscard]] static Result ExecuteCommandStreaming(
      const std::vector<std::string>& argv, OutputCallback callback,
      bool sandboxed = false);

  // Internal execution without caching.
  [[nodiscard]] static Result ExecuteWithoutCache(const std::string& code,
                                                   OutputCallback callback);
};

}  // namespace dcodex

#endif  // SRC_SERVER_SANDBOX_H_