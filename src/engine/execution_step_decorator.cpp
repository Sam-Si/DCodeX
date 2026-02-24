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

#include "src/engine/execution_step_decorator.h"

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/engine/execution_step.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// ExecutionStepDecorator Implementation
// -----------------------------------------------------------------------------

ExecutionStepDecorator::ExecutionStepDecorator(std::unique_ptr<ExecutionStep> wrapped)
    : wrapped_(std::move(wrapped)) {}

absl::string_view ExecutionStepDecorator::Name() const {
  return wrapped_ ? wrapped_->Name() : "Unknown";
}

ExecutionStep* ExecutionStepDecorator::Wrapped() const { return wrapped_.get(); }

// -----------------------------------------------------------------------------
// TimingStepDecorator Implementation
// -----------------------------------------------------------------------------

TimingStepDecorator::TimingStepDecorator(std::unique_ptr<ExecutionStep> wrapped)
    : ExecutionStepDecorator(std::move(wrapped)) {}

absl::Status TimingStepDecorator::ExecuteStep(ExecutionContext& context) {
  const absl::Time start = absl::Now();
  const absl::Status status = Wrapped()->Execute(context);
  const absl::Duration duration = absl::Now() - start;
  RecordTiming(context, duration);
  return status;
}

absl::string_view TimingStepDecorator::Name() const { return Wrapped()->Name(); }

void TimingStepDecorator::RecordTiming(ExecutionContext& context,
                                        absl::Duration duration) const {
  context.trace << absl::StrFormat("[TIMING] Step '%s' took %s\n",
                                   Wrapped()->Name(),
                                   absl::FormatDuration(duration));
}

// -----------------------------------------------------------------------------
// LoggingStepDecorator Implementation
// -----------------------------------------------------------------------------

LoggingStepDecorator::LoggingStepDecorator(std::unique_ptr<ExecutionStep> wrapped)
    : ExecutionStepDecorator(std::move(wrapped)) {}

absl::Status LoggingStepDecorator::ExecuteStep(ExecutionContext& context) {
  const absl::string_view step_name = Wrapped()->Name();
  LogStart(step_name);
  const absl::Status status = Wrapped()->Execute(context);
  LogEnd(step_name, status);
  return status;
}

absl::string_view LoggingStepDecorator::Name() const { return Wrapped()->Name(); }

void LoggingStepDecorator::LogStart(absl::string_view step_name) const {
  VLOG(1) << "[STEP_START] " << step_name;
}

void LoggingStepDecorator::LogEnd(absl::string_view step_name,
                                   const absl::Status& status) const {
  if (status.ok()) {
    VLOG(1) << "[STEP_END] " << step_name << " - OK";
  } else {
    VLOG(1) << "[STEP_END] " << step_name << " - FAILED: " << status.message();
  }
}

// -----------------------------------------------------------------------------
// TracingStepDecorator Implementation
// -----------------------------------------------------------------------------

TracingStepDecorator::TracingStepDecorator(std::unique_ptr<ExecutionStep> wrapped)
    : ExecutionStepDecorator(std::move(wrapped)) {}

absl::Status TracingStepDecorator::ExecuteStep(ExecutionContext& context) {
  const absl::string_view step_name = Wrapped()->Name();
  TraceStart(context, step_name);
  const absl::Status status = Wrapped()->Execute(context);
  if (status.ok()) {
    TraceEnd(context, step_name);
  } else {
    TraceError(context, step_name, status);
  }
  return status;
}

absl::string_view TracingStepDecorator::Name() const { return Wrapped()->Name(); }

void TracingStepDecorator::TraceStart(ExecutionContext& context,
                                       absl::string_view step_name) const {
  context.trace << absl::StrFormat("[STEP] %s\n", step_name);
}

void TracingStepDecorator::TraceEnd(ExecutionContext& context,
                                     absl::string_view step_name) const {
  context.trace << absl::StrFormat("[OK] Step '%s' completed\n", step_name);
}

void TracingStepDecorator::TraceError(ExecutionContext& context,
                                       absl::string_view step_name,
                                       const absl::Status& status) const {
  context.trace << absl::StrFormat("[FAIL] Step '%s' failed: %s\n",
                                   step_name, status.message());
}

// -----------------------------------------------------------------------------
// CompositeStepDecorator Implementation
// -----------------------------------------------------------------------------

CompositeStepDecorator::CompositeStepDecorator(std::unique_ptr<ExecutionStep> wrapped)
    : ExecutionStepDecorator(std::move(wrapped)), start_time_(absl::InfinitePast()) {}

absl::Status CompositeStepDecorator::ExecuteStep(ExecutionContext& context) {
  const absl::string_view step_name = Wrapped()->Name();

  // Logging: Start
  VLOG(1) << "[STEP_START] " << step_name;

  // Tracing: Start
  context.trace << absl::StrFormat("[STEP] %s\n", step_name);

  // Timing: Start
  start_time_ = absl::Now();

  // Execute wrapped step
  const absl::Status status = Wrapped()->Execute(context);

  // Timing: End
  const absl::Duration duration = absl::Now() - start_time_;
  context.trace << absl::StrFormat("[TIMING] Step '%s' took %s\n",
                                   step_name, absl::FormatDuration(duration));

  // Tracing & Logging: End
  if (status.ok()) {
    context.trace << absl::StrFormat("[OK] Step '%s' completed\n", step_name);
    VLOG(1) << "[STEP_END] " << step_name << " - OK";
  } else {
    context.trace << absl::StrFormat("[FAIL] Step '%s' failed: %s\n",
                                     step_name, status.message());
    VLOG(1) << "[STEP_END] " << step_name << " - FAILED: " << status.message();
  }

  return status;
}

absl::string_view CompositeStepDecorator::Name() const { return Wrapped()->Name(); }

// -----------------------------------------------------------------------------
// Decorator Factory Functions
// -----------------------------------------------------------------------------

std::unique_ptr<ExecutionStep> WithTiming(std::unique_ptr<ExecutionStep> step) {
  return std::make_unique<TimingStepDecorator>(std::move(step));
}

std::unique_ptr<ExecutionStep> WithLogging(std::unique_ptr<ExecutionStep> step) {
  return std::make_unique<LoggingStepDecorator>(std::move(step));
}

std::unique_ptr<ExecutionStep> WithTracing(std::unique_ptr<ExecutionStep> step) {
  return std::make_unique<TracingStepDecorator>(std::move(step));
}

std::unique_ptr<ExecutionStep> WithAllDecorators(std::unique_ptr<ExecutionStep> step) {
  return std::make_unique<CompositeStepDecorator>(std::move(step));
}

}  // namespace dcodex