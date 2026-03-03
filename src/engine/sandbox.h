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

#ifndef SRC_ENGINE_SANDBOX_H_
#define SRC_ENGINE_SANDBOX_H_

#include <memory>

#include "absl/flags/declare.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/common/execution_cache.h"
#include "src/engine/execution_types.h"

// Abseil Flags for sandboxed resource limits (must be in global namespace).
ABSL_DECLARE_FLAG(int, sandbox_cpu_time_limit_seconds);
ABSL_DECLARE_FLAG(int, sandbox_wall_clock_timeout_seconds);
ABSL_DECLARE_FLAG(uint64_t, sandbox_memory_limit_bytes);
ABSL_DECLARE_FLAG(uint64_t, sandbox_max_output_bytes);

namespace dcodex {

// Orchestrator class that manages sandboxed execution and caching.
class SandboxedProcess {
 public:
  // Constructs with required dependencies.
  explicit SandboxedProcess(std::shared_ptr<CacheInterface> cache);

  // Compiles and runs code with caching support.
  [[nodiscard]] absl::StatusOr<ExecutionResult> CompileAndRunStreaming(
      absl::string_view filename_or_extension, absl::string_view code,
      absl::string_view stdin_data, OutputCallback callback);

 private:
  std::shared_ptr<CacheInterface> cache_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_SANDBOX_H_
