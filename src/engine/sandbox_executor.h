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

#ifndef SRC_ENGINE_SANDBOX_EXECUTOR_H_
#define SRC_ENGINE_SANDBOX_EXECUTOR_H_

#include <memory>
#include <string>
#include <vector>
#include <sstream>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/engine/execution_types.h"
#include "src/engine/resource_policy.h"

namespace dcodex {

// Orchestrates the low-level execution of a sandboxed process.
// Handles process spawning, I/O piping, and timeout management.
class SandboxExecutor {
 public:
  explicit SandboxExecutor(ResourcePolicy policy = ResourcePolicy::FromFlags());
  ~SandboxExecutor() = default;

  // Executes a command with the configured resource policy.
  // Returns the execution result or an error status.
  [[nodiscard]] absl::StatusOr<ExecutionResult> RunCommand(
      absl::string_view context_name, 
      const std::vector<std::string>& argv,
      absl::string_view input_data, 
      bool sandboxed, 
      OutputCallback callback,
      std::stringstream& trace_stream);

  // Gets the current resource policy.
  [[nodiscard]] const ResourcePolicy& GetPolicy() const { return policy_; }

  // Overrides the resource policy.
  void SetPolicy(ResourcePolicy policy) { policy_ = std::move(policy); }

 private:
  ResourcePolicy policy_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_SANDBOX_EXECUTOR_H_
