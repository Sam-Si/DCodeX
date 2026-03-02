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

#ifndef SRC_ENGINE_RESOURCE_POLICY_H_
#define SRC_ENGINE_RESOURCE_POLICY_H_

#include <cstdint>
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/time/time.h"

// Forward declare flags to avoid circular dependencies.
ABSL_DECLARE_FLAG(int, sandbox_cpu_time_limit_seconds);
ABSL_DECLARE_FLAG(int, sandbox_wall_clock_timeout_seconds);
ABSL_DECLARE_FLAG(uint64_t, sandbox_memory_limit_bytes);
ABSL_DECLARE_FLAG(uint64_t, sandbox_max_output_bytes);

namespace dcodex {

// Encapsulates resource limits for sandboxed execution.
// This allows for easy swapping of policies (e.g., Strict vs. Lax).
class ResourcePolicy {
 public:
  ResourcePolicy()
      : cpu_time_limit_seconds(absl::GetFlag(FLAGS_sandbox_cpu_time_limit_seconds)),
        wall_clock_timeout_seconds(absl::GetFlag(FLAGS_sandbox_wall_clock_timeout_seconds)),
        memory_limit_bytes(absl::GetFlag(FLAGS_sandbox_memory_limit_bytes)),
        max_output_bytes(absl::GetFlag(FLAGS_sandbox_max_output_bytes)) {}

  ResourcePolicy(int cpu, int wall, uint64_t mem, uint64_t out)
      : cpu_time_limit_seconds(cpu),
        wall_clock_timeout_seconds(wall),
        memory_limit_bytes(mem),
        max_output_bytes(out) {}

  // Returns a "Strict" policy for untrusted code.
  static ResourcePolicy Strict() {
    return ResourcePolicy(1, 2, 64 * 1024 * 1024, 10 * 1024);
  }

  // Returns a "Lax" policy for trusted or heavy code.
  static ResourcePolicy Lax() {
    return ResourcePolicy(10, 20, 1024LL * 1024 * 1024, 1024 * 1024);
  }

  // Returns a policy based on current Abseil flags.
  static ResourcePolicy FromFlags() {
    return ResourcePolicy();
  }

  int cpu_time_limit_seconds;
  int wall_clock_timeout_seconds;
  uint64_t memory_limit_bytes;
  uint64_t max_output_bytes;

  [[nodiscard]] absl::Duration GetWallClockTimeout() const {
    return absl::Seconds(wall_clock_timeout_seconds);
  }
};

}  // namespace dcodex

#endif  // SRC_ENGINE_RESOURCE_POLICY_H_
