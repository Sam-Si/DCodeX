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

#ifndef SRC_SERVER_SANDBOX_H_
#define SRC_SERVER_SANDBOX_H_

#include <grpcpp/alarm.h>

#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/server/execution_cache.h"

// Abseil Flags for sandboxed resource limits (must be in global namespace).
ABSL_DECLARE_FLAG(int, sandbox_cpu_time_limit_seconds);
ABSL_DECLARE_FLAG(int, sandbox_wall_clock_timeout_seconds);
ABSL_DECLARE_FLAG(uint64_t, sandbox_memory_limit_bytes);
ABSL_DECLARE_FLAG(uint64_t, sandbox_max_output_bytes);

namespace dcodex {

// Resource statistics for a sandboxed process.
struct ResourceStats {
  // Peak memory usage in bytes.
  long peak_memory_bytes = 0;
  // User CPU time in milliseconds.
  long user_time_ms = 0;
  // System CPU time in milliseconds.
  long system_time_ms = 0;
  // Total elapsed time in milliseconds.
  long elapsed_time_ms = 0;
};

// Result of a sandboxed execution.
struct ExecutionResult {
  bool success = false;
  std::string error_message;
  std::string backend_trace;
  ResourceStats stats;
  // Indicates if result was served from cache.
  bool cache_hit = false;
  // Cached stdout/stderr for cache hits.
  std::string cached_stdout;
  std::string cached_stderr;
  // Indicates if the process was killed due to wall-clock timeout.
  bool wall_clock_timeout = false;
  // Indicates if the process was killed because its combined stdout+stderr
  // output exceeded kMaxOutputBytes.
  bool output_truncated = false;
};

// Callback for streaming output.
using OutputCallback =
    std::function<void(absl::string_view stdout_chunk,
                       absl::string_view stderr_chunk)>;

// Forward declarations
class ExecutionStep;
class ExecutionContext;

// -----------------------------------------------------------------------------
// Command Pattern (GoF): ExecutionStep Interface
// Each step represents a single responsibility in the execution pipeline.
// -----------------------------------------------------------------------------
class ExecutionStep {
 public:
  virtual ~ExecutionStep() = default;

  // Executes this step and returns true on success, false on failure.
  // The context is passed by reference to allow steps to share state.
  virtual bool Execute(ExecutionContext& context) = 0;

  // Returns a descriptive name for this step (used in tracing/logging).
  virtual absl::string_view Name() const = 0;
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

  // Adds a path to be cleaned up when context is destroyed
  void AddCleanupPath(const std::string& path) { cleanup_paths.push_back(path); }

  // Marks the execution as failed with an error message
  void Fail(absl::string_view error) {
    result.success = false;
    result.error_message = std::string(error);
  }
};

// -----------------------------------------------------------------------------
// Template Method Pattern (GoF): ExecutionPipeline
// Orchestrates execution steps in a configurable sequence.
// -----------------------------------------------------------------------------
class ExecutionPipeline {
 public:
  ExecutionPipeline() = default;

  // Adds a step to the pipeline. Steps are executed in order.
  ExecutionPipeline& AddStep(std::unique_ptr<ExecutionStep> step) {
    steps_.push_back(std::move(step));
    return *this;
  }

  // Executes all steps in sequence. Stops on first failure.
  // Returns the final execution result.
  ExecutionResult Run(ExecutionContext& context) {
    for (auto& step : steps_) {
      context.trace << "[STEP] " << step->Name() << "\n";
      if (!step->Execute(context)) {
        context.trace << "[FAIL] Step '" << step->Name() << "' failed\n";
        if (context.result.error_message.empty()) {
          context.result.error_message =
              absl::StrCat("Step '", step->Name(), "' failed");
        }
        break;
      }
      context.trace << "[OK] Step '" << step->Name() << "' completed\n";
    }
    context.result.backend_trace = context.trace.str();
    return context.result;
  }

 private:
  std::vector<std::unique_ptr<ExecutionStep>> steps_;
};

// -----------------------------------------------------------------------------
// Strategy Pattern: Interface for different execution strategies (e.g., C, C++, Python).
// Now delegates to ExecutionPipeline for the actual work.
// -----------------------------------------------------------------------------
class ExecutionStrategy {
 public:
  virtual ~ExecutionStrategy() = default;

  // Executes the given code and returns the result.
  virtual ExecutionResult Execute(absl::string_view code,
                                  absl::string_view stdin_data,
                                  OutputCallback callback) = 0;

  // Returns a unique identifier for this strategy (used for caching).
  virtual absl::string_view GetStrategyId() const = 0;

  // Factory method to create an execution strategy based on file extension or language.
  static std::unique_ptr<ExecutionStrategy> Create(
      absl::string_view filename_or_extension);

 protected:
  // Helper to create the standard pipeline for a strategy.
  virtual std::unique_ptr<ExecutionPipeline> CreatePipeline() = 0;
};

// -----------------------------------------------------------------------------
// CompiledLanguageStrategy: Base class for compiled languages (C, C++)
// Supports both C and C++ compilation with appropriate compiler selection.
// -----------------------------------------------------------------------------
class CompiledLanguageStrategy : public ExecutionStrategy {
 public:
  // Language type for compiled languages
  enum class Language { kC, kCpp };

  explicit CompiledLanguageStrategy(Language language) : language_(language) {}
  ~CompiledLanguageStrategy() override = default;

  ExecutionResult Execute(absl::string_view code, absl::string_view stdin_data,
                          OutputCallback callback) override;

  absl::string_view GetStrategyId() const override {
    return language_ == Language::kC ? "c" : "cpp";
  }

 protected:
  std::unique_ptr<ExecutionPipeline> CreatePipeline() override;

 private:
  Language language_;
  
  // Returns the file extension for this language
  absl::string_view GetExtension() const {
    return language_ == Language::kC ? ".c" : ".cpp";
  }
  
  // Returns the compiler name for this language
  absl::string_view GetCompiler() const {
    return language_ == Language::kC ? "clang" : "clang++";
  }
  
  // Returns the standard flag for this language
  std::vector<std::string> GetStandardFlags() const {
    if (language_ == Language::kC) {
      return {"-std=c11", "-Wall"};
    }
    return {"-std=c++17"};
  }
};

// C implementation of the ExecutionStrategy.
// Uses clang for compilation.
using CExecutionStrategy = CompiledLanguageStrategy;

// C++ implementation of the ExecutionStrategy.
// Uses clang++ for compilation.
using CppExecutionStrategy = CompiledLanguageStrategy;

// Python implementation of the ExecutionStrategy.
class PythonExecutionStrategy : public ExecutionStrategy {
 public:
  PythonExecutionStrategy() = default;
  ~PythonExecutionStrategy() override = default;

  ExecutionResult Execute(absl::string_view code, absl::string_view stdin_data,
                          OutputCallback callback) override;

  absl::string_view GetStrategyId() const override { return "python"; }

 protected:
  std::unique_ptr<ExecutionPipeline> CreatePipeline() override;
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

  bool Execute(ExecutionContext& context) override;
  absl::string_view Name() const override { return "CreateSourceFile"; }

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

  bool Execute(ExecutionContext& context) override;
  absl::string_view Name() const override { return "Compile"; }

 private:
  std::string compiler_;
  std::vector<std::string> compiler_flags_;
};

// Step 3: Executes a binary or script with sandboxing.
// SRP: Process execution and sandboxing.
class RunProcessStep : public ExecutionStep {
 public:
  explicit RunProcessStep(bool sandboxed) : sandboxed_(sandboxed) {}

  bool Execute(ExecutionContext& context) override;
  absl::string_view Name() const override { return "RunProcess"; }

 private:
  bool sandboxed_;
};

// Step 4: Handles execution result and formats trace output.
// SRP: Result formatting and error handling.
class FinalizeResultStep : public ExecutionStep {
 public:
  explicit FinalizeResultStep(absl::string_view context_name)
      : context_name_(context_name) {}

  bool Execute(ExecutionContext& context) override;
  absl::string_view Name() const override { return "FinalizeResult"; }

 private:
  std::string context_name_;
};

// -----------------------------------------------------------------------------
// ProcessTimeoutManager: gRPC Alarm-based process timeout
// 
// Replaces the fork-based watcher process with gRPC Alarm for efficient
// timeout handling. This provides several benefits:
// 
// 1. Resource Efficiency: No extra process forked for each timeout
// 2. Integration: Works seamlessly with gRPC's async completion queue
// 3. Cancellation: Supports graceful cancellation via callback
// 4. Thread Safety: Uses absl::Mutex for thread-safe state access
// 
// Benefits of gRPC Alarm over fork-based approach:
// - Eliminates process table pollution (no zombie processes)
// - Reduces memory overhead (no duplicate address space)
// - Better integration with gRPC server lifecycle
// - Cleaner shutdown semantics
// -----------------------------------------------------------------------------
class ProcessTimeoutManager {
 public:
  // Callback type for timeout notification
  using TimeoutCallback = std::function<void()>;

  // Constructs a timeout manager for the given process.
  // The callback will be invoked when the timeout expires.
  ProcessTimeoutManager(pid_t pid, absl::Duration timeout,
                        TimeoutCallback callback);

  // Destructor ensures alarm is cancelled.
  ~ProcessTimeoutManager();

  // Starts the timeout alarm. Must be called after construction.
  void Start();

  // Cancels the pending timeout (idempotent).
  void Cancel();

  // Checks if the timeout has been triggered.
  bool IsTriggered() const;

  // Checks if the timeout was cancelled before triggering.
  bool IsCancelled() const;

 private:
  void OnAlarmTriggered(bool ok);

  pid_t pid_;
  absl::Duration timeout_;
  TimeoutCallback callback_;
  grpc::Alarm alarm_;
  
  mutable absl::Mutex mutex_;
  bool started_ ABSL_GUARDED_BY(mutex_) = false;
  bool triggered_ ABSL_GUARDED_BY(mutex_) = false;
  bool cancelled_ ABSL_GUARDED_BY(mutex_) = false;
};

// Orchestrator class that manages sandboxed execution and caching.
class SandboxedProcess {
 public:
  // Compiles and runs code with caching support.
  [[nodiscard]] static ExecutionResult CompileAndRunStreaming(
      absl::string_view filename_or_extension, absl::string_view code,
      absl::string_view stdin_data, OutputCallback callback);

  // Accesses the global execution cache.
  [[nodiscard]] static ExecutionCache& GetCache();

  // Clears the execution cache.
  static void ClearCache();
};

}  // namespace dcodex

#endif  // SRC_SERVER_SANDBOX_H_