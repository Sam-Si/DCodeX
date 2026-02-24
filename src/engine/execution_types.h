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

#ifndef SRC_ENGINE_EXECUTION_TYPES_H_
#define SRC_ENGINE_EXECUTION_TYPES_H_

#include <functional>
#include <string>

#include "absl/strings/string_view.h"

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

}  // namespace dcodex

#endif  // SRC_ENGINE_EXECUTION_TYPES_H_
