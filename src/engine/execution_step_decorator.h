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

#ifndef SRC_ENGINE_EXECUTION_STEP_DECORATOR_H_
#define SRC_ENGINE_EXECUTION_STEP_DECORATOR_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "src/engine/execution_step.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// Decorator Pattern (GoF): ExecutionStepDecorator
// Base class for all step decorators. Wraps an ExecutionStep and adds behavior.
// Decorators can be stacked to add multiple cross-cutting concerns.
// -----------------------------------------------------------------------------
class ExecutionStepDecorator : public ExecutionStep {
 public:
  // Constructs a decorator wrapping the given step.
  explicit ExecutionStepDecorator(std::unique_ptr<ExecutionStep> wrapped);

  ~ExecutionStepDecorator() override = default;

  // Disallow copy operations.
  ExecutionStepDecorator(const ExecutionStepDecorator&) = delete;
  ExecutionStepDecorator& operator=(const ExecutionStepDecorator&) = delete;

  // Allow move operations.
  ExecutionStepDecorator(ExecutionStepDecorator&&) = default;
  ExecutionStepDecorator& operator=(ExecutionStepDecorator&&) = default;

  // Delegates to the wrapped step's name by default.
  [[nodiscard]] absl::string_view Name() const override;

 protected:
  // Provides access to the wrapped step for derived decorators.
  [[nodiscard]] ExecutionStep* Wrapped() const;

 private:
  std::unique_ptr<ExecutionStep> wrapped_;
};

// -----------------------------------------------------------------------------
// TimingStepDecorator: Measures and records execution time of a step.
// Adds timing information to the execution context trace.
// -----------------------------------------------------------------------------
class TimingStepDecorator final : public ExecutionStepDecorator {
 public:
  explicit TimingStepDecorator(std::unique_ptr<ExecutionStep> wrapped);

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override;

 private:
  // Records timing information in the context trace.
  void RecordTiming(ExecutionContext& context, absl::Duration duration) const;
};

// -----------------------------------------------------------------------------
// LoggingStepDecorator: Logs step execution start/end to VLOG.
// Useful for debugging and monitoring step execution flow.
// -----------------------------------------------------------------------------
class LoggingStepDecorator final : public ExecutionStepDecorator {
 public:
  explicit LoggingStepDecorator(std::unique_ptr<ExecutionStep> wrapped);

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override;

 private:
  void LogStart(absl::string_view step_name) const;
  void LogEnd(absl::string_view step_name, const absl::Status& status) const;
};

// -----------------------------------------------------------------------------
// TracingStepDecorator: Adds detailed trace entries for step execution.
// Writes step start/end markers and error details to context trace.
// -----------------------------------------------------------------------------
class TracingStepDecorator final : public ExecutionStepDecorator {
 public:
  explicit TracingStepDecorator(std::unique_ptr<ExecutionStep> wrapped);

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override;

 private:
  void TraceStart(ExecutionContext& context, absl::string_view step_name) const;
  void TraceEnd(ExecutionContext& context, absl::string_view step_name) const;
  void TraceError(ExecutionContext& context, absl::string_view step_name,
                  const absl::Status& status) const;
};

// -----------------------------------------------------------------------------
// CompositeStepDecorator: Combines timing, logging, and tracing.
// Convenience decorator that applies all three behaviors in one wrapper.
// Order: Timing -> Logging -> Tracing -> Wrapped Step
// -----------------------------------------------------------------------------
class CompositeStepDecorator final : public ExecutionStepDecorator {
 public:
  explicit CompositeStepDecorator(std::unique_ptr<ExecutionStep> wrapped);

  absl::Status ExecuteStep(ExecutionContext& context) override;
  [[nodiscard]] absl::string_view Name() const override;

 private:
  // Internal state for timing measurement.
  absl::Time start_time_;
};

// -----------------------------------------------------------------------------
// Decorator Factory Functions
// Convenience functions for creating decorated steps.
// -----------------------------------------------------------------------------

// Wraps a step with timing decoration.
[[nodiscard]] std::unique_ptr<ExecutionStep> WithTiming(
    std::unique_ptr<ExecutionStep> step);

// Wraps a step with logging decoration.
[[nodiscard]] std::unique_ptr<ExecutionStep> WithLogging(
    std::unique_ptr<ExecutionStep> step);

// Wraps a step with tracing decoration.
[[nodiscard]] std::unique_ptr<ExecutionStep> WithTracing(
    std::unique_ptr<ExecutionStep> step);

// Wraps a step with all standard decorations (timing, logging, tracing).
[[nodiscard]] std::unique_ptr<ExecutionStep> WithAllDecorators(
    std::unique_ptr<ExecutionStep> step);

}  // namespace dcodex

#endif  // SRC_ENGINE_EXECUTION_STEP_DECORATOR_H_