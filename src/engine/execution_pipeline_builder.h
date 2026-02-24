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

#ifndef SRC_ENGINE_EXECUTION_PIPELINE_BUILDER_H_
#define SRC_ENGINE_EXECUTION_PIPELINE_BUILDER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "src/common/execution_cache.h"
#include "src/engine/execution_pipeline.h"
#include "src/engine/execution_step.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// Builder Pattern (GoF): ExecutionPipelineBuilder
// Provides a fluent interface for constructing ExecutionPipeline instances.
// Separates pipeline construction from its representation.
// -----------------------------------------------------------------------------
class ExecutionPipelineBuilder {
 public:
  ExecutionPipelineBuilder() = default;
  ~ExecutionPipelineBuilder() = default;

  // Disallow copy operations to ensure unique ownership.
  ExecutionPipelineBuilder(const ExecutionPipelineBuilder&) = delete;
  ExecutionPipelineBuilder& operator=(const ExecutionPipelineBuilder&) = delete;

  // Allow move operations for flexibility.
  ExecutionPipelineBuilder(ExecutionPipelineBuilder&&) = default;
  ExecutionPipelineBuilder& operator=(ExecutionPipelineBuilder&&) = default;

  // Sets the cache interface for the pipeline.
  // Returns reference to this for method chaining.
  ExecutionPipelineBuilder& WithCache(std::shared_ptr<CacheInterface> cache);

  // Adds a source file creation step with the specified extension.
  // Returns reference to this for method chaining.
  ExecutionPipelineBuilder& AddCreateSourceFileStep(absl::string_view extension);

  // Adds a compilation step with the specified compiler and flags.
  // Returns reference to this for method chaining.
  ExecutionPipelineBuilder& AddCompileStep(
      absl::string_view compiler,
      std::vector<std::string> compiler_flags);

  // Adds a process execution step with optional sandboxing.
  // Returns reference to this for method chaining.
  ExecutionPipelineBuilder& AddRunProcessStep(bool sandboxed);

  // Adds a finalize result step with the specified context name.
  // Returns reference to this for method chaining.
  ExecutionPipelineBuilder& AddFinalizeResultStep(absl::string_view context_name);

  // Adds a custom execution step.
  // Returns reference to this for method chaining.
  ExecutionPipelineBuilder& AddStep(std::unique_ptr<ExecutionStep> step);

  // Builds and returns the configured pipeline.
  // After calling this method, the builder is in a valid but unspecified state.
  [[nodiscard]] std::unique_ptr<ExecutionPipeline> Build();

  // Resets the builder to its initial state.
  void Reset();

 private:
  std::vector<std::unique_ptr<ExecutionStep>> steps_;
  std::shared_ptr<CacheInterface> cache_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_EXECUTION_PIPELINE_BUILDER_H_