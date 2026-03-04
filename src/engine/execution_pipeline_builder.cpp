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

#include "src/engine/execution_pipeline_builder.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "src/common/execution_cache.h"
#include "src/engine/execution_pipeline.h"
#include "src/engine/execution_step.h"

namespace dcodex {

ExecutionPipelineBuilder& ExecutionPipelineBuilder::WithCache(
    std::shared_ptr<CacheInterface> cache) {
  cache_ = std::move(cache);
  return *this;
}

ExecutionPipelineBuilder& ExecutionPipelineBuilder::AddCreateSourceFileStep(
    absl::string_view extension) {
  steps_.push_back(std::make_unique<CreateSourceFileStep>(extension));
  return *this;
}

ExecutionPipelineBuilder& ExecutionPipelineBuilder::AddCompileStep(
    absl::string_view compiler,
    std::vector<std::string> compiler_flags) {
  steps_.push_back(std::make_unique<CompileStep>(compiler, std::move(compiler_flags)));
  return *this;
}

ExecutionPipelineBuilder& ExecutionPipelineBuilder::AddRunProcessStep(bool sandboxed) {
  steps_.push_back(std::make_unique<RunProcessStep>(sandboxed));
  return *this;
}

ExecutionPipelineBuilder& ExecutionPipelineBuilder::AddFinalizeResultStep(
    absl::string_view context_name) {
  steps_.push_back(std::make_unique<FinalizeResultStep>(context_name));
  return *this;
}

ExecutionPipelineBuilder& ExecutionPipelineBuilder::AddStep(
    std::unique_ptr<ExecutionStep> step) {
  steps_.push_back(std::move(step));
  return *this;
}

std::unique_ptr<ExecutionPipeline> ExecutionPipelineBuilder::Build() {
  auto pipeline = std::make_unique<ExecutionPipeline>(std::move(cache_));
  for (auto& step : steps_) {
    pipeline->AddStep(std::move(step));
  }
  steps_.clear();
  return pipeline;
}

void ExecutionPipelineBuilder::Reset() {
  steps_.clear();
  cache_.reset();
}

}  // namespace dcodex