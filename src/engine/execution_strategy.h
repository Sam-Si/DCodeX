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

#ifndef SRC_ENGINE_EXECUTION_STRATEGY_H_
#define SRC_ENGINE_EXECUTION_STRATEGY_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/common/execution_cache.h"
#include "src/engine/execution_pipeline.h"
#include "src/engine/execution_types.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// Strategy Pattern: Interface for different execution strategies (e.g., C, C++, Python).
// Now accepts CacheInterface via constructor for dependency injection.
// -----------------------------------------------------------------------------
class ExecutionStrategy {
 public:
  virtual ~ExecutionStrategy() = default;

  // Executes the given code and returns the result or an error status.
  [[nodiscard]] virtual absl::StatusOr<ExecutionResult> Execute(
      absl::string_view code, absl::string_view stdin_data,
      OutputCallback callback) = 0;

  // Returns a unique identifier for this strategy (used for caching).
  [[nodiscard]] virtual absl::string_view GetStrategyId() const = 0;

  // Factory method to create an execution strategy based on file extension or language.
  // Accepts optional cache for dependency injection.
  [[nodiscard]] static absl::StatusOr<std::unique_ptr<ExecutionStrategy>> Create(
      absl::string_view filename_or_extension,
      std::shared_ptr<CacheInterface> cache = nullptr);

 protected:
  // Helper to create the standard pipeline for a strategy.
  // Accepts optional cache for dependency injection.
  [[nodiscard]] virtual std::unique_ptr<ExecutionPipeline> CreatePipeline(
      std::shared_ptr<CacheInterface> cache = nullptr) = 0;
};

// -----------------------------------------------------------------------------
// CompiledLanguageStrategy: Base class for compiled languages (C, C++)
// Supports both C and C++ compilation with appropriate compiler selection.
// -----------------------------------------------------------------------------
class CompiledLanguageStrategy : public ExecutionStrategy {
 public:
  // Language type for compiled languages
  enum class Language { kC, kCpp };

  explicit CompiledLanguageStrategy(
      Language language, std::shared_ptr<CacheInterface> cache = nullptr);
  ~CompiledLanguageStrategy() override = default;

  [[nodiscard]] absl::StatusOr<ExecutionResult> Execute(
      absl::string_view code, absl::string_view stdin_data,
      OutputCallback callback) override;

  [[nodiscard]] absl::string_view GetStrategyId() const override {
    return language_ == Language::kC ? "c" : "cpp";
  }

 protected:
  [[nodiscard]] std::unique_ptr<ExecutionPipeline> CreatePipeline(
      std::shared_ptr<CacheInterface> cache = nullptr) override;

 private:
  Language language_;
  std::shared_ptr<CacheInterface> cache_;

  // Returns the file extension for this language.
  [[nodiscard]] absl::string_view GetExtension() const {
    return language_ == Language::kC ? ".c" : ".cpp";
  }

  // Returns the compiler name for this language.
  [[nodiscard]] absl::string_view GetCompiler() const {
    return language_ == Language::kC ? "clang" : "clang++";
  }

  // Returns the standard flags for this language.
  [[nodiscard]] std::vector<std::string> GetStandardFlags() const {
    if (language_ == Language::kC) {
      return {"-std=c11", "-Wall"};
    }
    return {"-std=c++17"};
  }
};

// C implementation of the ExecutionStrategy.
// Uses clang for compilation.
class CExecutionStrategy final : public CompiledLanguageStrategy {
 public:
  explicit CExecutionStrategy(std::shared_ptr<CacheInterface> cache = nullptr)
      : CompiledLanguageStrategy(Language::kC, std::move(cache)) {}
};

// C++ implementation of the ExecutionStrategy.
// Uses clang++ for compilation.
class CppExecutionStrategy final : public CompiledLanguageStrategy {
 public:
  explicit CppExecutionStrategy(std::shared_ptr<CacheInterface> cache = nullptr)
      : CompiledLanguageStrategy(Language::kCpp, std::move(cache)) {}
};

// Python implementation of the ExecutionStrategy.
class PythonExecutionStrategy final : public ExecutionStrategy {
 public:
  explicit PythonExecutionStrategy(
      std::shared_ptr<CacheInterface> cache = nullptr);
  ~PythonExecutionStrategy() override = default;

  [[nodiscard]] absl::StatusOr<ExecutionResult> Execute(
      absl::string_view code, absl::string_view stdin_data,
      OutputCallback callback) override;

  [[nodiscard]] absl::string_view GetStrategyId() const override { return "python"; }

 protected:
  [[nodiscard]] std::unique_ptr<ExecutionPipeline> CreatePipeline(
      std::shared_ptr<CacheInterface> cache = nullptr) override;

 private:
  std::shared_ptr<CacheInterface> cache_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_EXECUTION_STRATEGY_H_
