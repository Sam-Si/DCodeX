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

#ifndef SRC_ENGINE_EXECUTION_PIPELINE_H_
#define SRC_ENGINE_EXECUTION_PIPELINE_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "src/common/execution_cache.h"
#include "src/engine/execution_step.h"
#include "src/engine/execution_types.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// Template Method Pattern (GoF): ExecutionPipeline
// Orchestrates execution steps in a configurable sequence.
// Now accepts CacheInterface via constructor for dependency injection.
// -----------------------------------------------------------------------------
class ExecutionPipeline {
 public:
  // Constructs pipeline with optional cache interface for dependency injection.
  // If cache is nullptr, caching is disabled.
  explicit ExecutionPipeline(std::shared_ptr<CacheInterface> cache = nullptr);

  // Adds a step to the pipeline. Steps are executed in order.
  ExecutionPipeline& AddStep(std::unique_ptr<ExecutionStep> step) {
    steps_.push_back(std::move(step));
    return *this;
  }

  // Executes all steps in sequence. Stops on first failure.
  // Returns the final execution result or an error status.
  [[nodiscard]] absl::StatusOr<ExecutionResult> Run(ExecutionContext& context);

  // Gets the cache interface (may be nullptr).
  [[nodiscard]] CacheInterface* GetCache() const { return cache_.get(); }

 private:
  std::vector<std::unique_ptr<ExecutionStep>> steps_;
  std::shared_ptr<CacheInterface> cache_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_EXECUTION_PIPELINE_H_
