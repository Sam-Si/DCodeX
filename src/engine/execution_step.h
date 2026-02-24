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

#ifndef SRC_ENGINE_EXECUTION_STEP_H_
#define SRC_ENGINE_EXECUTION_STEP_H_

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/engine/execution_types.h"

namespace dcodex {

// Forward declaration
class ExecutionContext;

// -----------------------------------------------------------------------------
// Chain of Responsibility Pattern (GoF): ExecutionStep Interface
// Each step represents a handler in the execution chain.
// -----------------------------------------------------------------------------
class ExecutionStep {
 public:
  ExecutionStep() = default;
  virtual ~ExecutionStep() = default;

  // Disallow copy.
  ExecutionStep(const ExecutionStep&) = delete;
  ExecutionStep& operator=(const ExecutionStep&) = delete;

  // Allow move.
  ExecutionStep(ExecutionStep&&) = default;
  ExecutionStep& operator=(ExecutionStep&&) = default;

  // Sets the next step in the chain.
  // Returns the next step pointer to allow chaining.
  ExecutionStep* SetNext(std::unique_ptr<ExecutionStep> next) {
    next_ = std::move(next);
    return next_.get();
  }

  // Executes this step and delegates to the next step if successful.
  // Returns OK on success, or an error status on failure.
  absl::Status Execute(ExecutionContext& context) {
    // Execute current step logic
    absl::Status status = ExecuteStep(context);
    if (!status.ok()) {
      return status;
    }

    // Delegate to next step if exists
    if (next_) {
      return next_->Execute(context);
    }

    return absl::OkStatus();
  }

  // Pure virtual method for the specific step logic.
  virtual absl::Status ExecuteStep(ExecutionContext& context) = 0;

  // Returns a descriptive name for this step (used in tracing/logging).
  [[nodiscard]] virtual absl::string_view Name() const = 0;

 private:
  std::unique_ptr<ExecutionStep> next_;
};

// -----------------------------------------------------------------------------
// Execution Context: Shared state passed between execution steps
// This follows the Memento Pattern for capturing and restoring state.
// -----------------------------------------------------------------------------
class ExecutionContext {
 public:
  // Input parameters
  std::string code;
  std::string stdin_data;
  OutputCallback callback;

  // Intermediate state (set by steps)
  std::string source_file_path;
  std::string binary_path;
  std::vector<std::string> cleanup_paths;

  // Execution result (built up by steps)
  ExecutionResult result;
  std::stringstream trace;

  // Configuration
  bool sandboxed = true;

  ExecutionContext(absl::string_view code, absl::string_view stdin_data,
                   OutputCallback callback)
      : code(code),
        stdin_data(stdin_data),
        callback(std::move(callback)) {
    trace << "--- Backend Execution Trace ---\n";
  }

  ~ExecutionContext() {
    for (const auto& path : cleanup_paths) {
      if (!path.empty()) {
        unlink(path.c_str());
      }
    }
  }

  // Adds a path to be cleaned up when context is destroyed
  void AddCleanupPath(const std::string& path) { cleanup_paths.push_back(path); }

  // Marks the execution as failed with an error message and returns an error status.
  absl::Status Fail(absl::string_view error) {
    result.success = false;
    result.error_message = std::string(error);
    return absl::InternalError(error);
  }

  // Sets an error on the result without returning (for non-fatal errors)
  void SetError(absl::string_view error) {
    result.success = false;
    result.error_message = std::string(error);
  }
};

// -----------------------------------------------------------------------------
// Concrete Execution Steps (Command Pattern)
// Each step handles a single responsibility in the execution pipeline.
// -----------------------------------------------------------------------------

// Step 1: Creates a temporary source file with the provided code.
// SRP: File creation and management.
class CreateSourceFileStep : public ExecutionStep {
 public:
  explicit CreateSourceFileStep(absl::string_view extension)
      : extension_(extension) {}

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override { return "CreateSourceFile"; }

 private:
  std::string extension_;
};

// Step 2: Compiles source code to a binary (supports C and C++).
// SRP: Compilation process management.
class CompileStep : public ExecutionStep {
 public:
  CompileStep(absl::string_view compiler,
              std::vector<std::string> compiler_flags)
      : compiler_(compiler),
        compiler_flags_(std::move(compiler_flags)) {}

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override { return "Compile"; }

 private:
  std::string compiler_;
  std::vector<std::string> compiler_flags_;
};

// Step 3: Executes a binary or script with sandboxing.
// SRP: Process execution and sandboxing.
class RunProcessStep : public ExecutionStep {
 public:
  explicit RunProcessStep(bool sandboxed) : sandboxed_(sandboxed) {}

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override { return "RunProcess"; }

 private:
  bool sandboxed_;
};

// Step 4: Handles execution result and formats trace output.
// SRP: Result formatting and error handling.
class FinalizeResultStep : public ExecutionStep {
 public:
  explicit FinalizeResultStep(absl::string_view context_name)
      : context_name_(context_name) {}

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override { return "FinalizeResult"; }

 private:
  std::string context_name_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_EXECUTION_STEP_H_
