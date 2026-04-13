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

#include "src/engine/sandbox.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <sstream>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/common/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/engine/execution_pipeline.h"
#include "src/engine/execution_pipeline_builder.h"
#include "src/engine/execution_step.h"
#include "src/engine/execution_strategy.h"
#include "src/engine/execution_types.h"
#include "src/engine/language_toolchain.h"
#include "src/engine/process_runner.h"
#include "src/engine/process_timeout_manager.h"
#include "src/engine/temp_file_manager.h"

ABSL_FLAG(int, sandbox_cpu_time_limit_seconds, 1,
          "CPU time limit in seconds for sandboxed execution");
ABSL_FLAG(int, sandbox_wall_clock_timeout_seconds, 2,
          "Wall-clock timeout in seconds for sandboxed execution");
ABSL_FLAG(uint64_t, sandbox_memory_limit_bytes, 4ULL * 1024 * 1024 * 1024,
          "Memory limit in bytes for sandboxed execution");
ABSL_FLAG(uint64_t, sandbox_max_output_bytes, 10 * 1024,
          "Maximum combined stdout+stderr output in bytes");

namespace dcodex {

namespace {

using internal::ProcessRunner;
using internal::TempFileManager;
using internal::PipePair;
using internal::ScopedProcess;

// --- SRP: Global Cache Management ---
// REMOVED: CacheHolder singleton in favor of DI.

ResourceStats ComputeResourceStats(const struct rusage& usage,
                                   absl::Time start, absl::Time end) {
  ResourceStats stats;
#ifdef __APPLE__
  stats.peak_memory_bytes = usage.ru_maxrss;
#else
  stats.peak_memory_bytes = usage.ru_maxrss * 1024;
#endif
  stats.user_time_ms =
      usage.ru_utime.tv_sec * 1000 + usage.ru_utime.tv_usec / 1000;
  stats.system_time_ms =
      usage.ru_stime.tv_sec * 1000 + usage.ru_stime.tv_usec / 1000;
  stats.elapsed_time_ms = absl::ToInt64Milliseconds(end - start);
  return stats;
}

// -----------------------------------------------------------------------------
// SRP: Process Execution Helper
// Handles low-level process execution with sandboxing support.
// Sandboxed path: fork+exec with setrlimit() in child (real enforcement).
// Non-sandboxed path: posix_spawnp for fast compilation without limits.
// Wall-clock timeout enforced via gRPC Alarm.
// -----------------------------------------------------------------------------

// Formats the command trace with appropriate coloring.
void FormatCommandTrace(std::stringstream& trace, bool sandboxed,
                        absl::string_view context,
                        absl::string_view cmd_str) {
  trace << (sandboxed ? "\033[94m[SANDBOX]\033[0m " : "\033[92m[LOCAL]\033[0m ")
        << context << ": " << cmd_str << "\n";
}

// Creates pipe pairs for stdin, stdout, and stderr.
absl::Status CreatePipes(PipePair& stdin_p, PipePair& stdout_p,
                         PipePair& stderr_p, std::stringstream& trace) {
  if (!stdin_p.Create() || !stdout_p.Create() || !stderr_p.Create()) {
    trace << "[FAIL] Pipe creation failed\n";
    return absl::InternalError("Pipe creation failed");
  }
  return absl::OkStatus();
}

// Writes input data to the stdin pipe.
void FeedStdin(PipePair& stdin_p, absl::string_view input,
               std::stringstream& trace) {
  if (!input.empty()) {
    trace << absl::StrFormat("[INFO] Feeding %zu bytes to stdin\n", input.size());
    write(stdin_p.WriteFd(), input.data(), input.size());
  }
  stdin_p.CloseWrite();
}

// Formats the process exit error message.
std::string FormatProcessError(int status) {
  if (WIFEXITED(status)) {
    return absl::StrFormat("Process exited with non-zero status: %d",
                           WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return absl::StrFormat("Process killed by signal: %d", WTERMSIG(status));
  }
  return "Process failed for unknown reasons";
}

// Builds the execution result from process status and resource usage.
ExecutionResult BuildExecutionResult(int status, const struct rusage& usage,
                                     absl::Time start, bool timed_out,
                                     bool truncated) {
  ExecutionResult res;
  res.stats = ComputeResourceStats(usage, start, absl::Now());
  res.wall_clock_timeout = timed_out;
  res.output_truncated = truncated;
  res.success = !truncated && !timed_out && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0;

  if (timed_out) {
    res.error_message = "Wall-clock timeout exceeded";
  } else if (truncated) {
    res.error_message = "Output truncated";
  } else if (!res.success) {
    res.error_message = FormatProcessError(status);
  }
  return res;
}

absl::StatusOr<ExecutionResult> RunCommandWithSandbox(
    absl::string_view context, const std::vector<std::string>& argv,
    absl::string_view input, bool sandboxed, OutputCallback callback,
    std::stringstream& trace) {
  const std::string cmd_str = absl::StrJoin(argv, " ");
  FormatCommandTrace(trace, sandboxed, context, cmd_str);
  LOG(INFO) << (sandboxed ? "Sandboxed" : "Local") << " exec: " << cmd_str;

  PipePair stdin_p, stdout_p, stderr_p;
  ABSL_RETURN_IF_ERROR(CreatePipes(stdin_p, stdout_p, stderr_p, trace));

  const absl::Time start = absl::Now();
  
  // Spawn the child process.
  //   sandboxed=false → posix_spawnp (fast, no rlimits — used for compilation)
  //   sandboxed=true  → fork+exec with setrlimit in child (real enforcement)
  ABSL_ASSIGN_OR_RETURN(const pid_t raw_pid, ProcessRunner::SpawnProcess(
      absl::MakeSpan(argv),
      stdin_p.ReadFd(),
      stdout_p.WriteFd(),
      stderr_p.WriteFd(),
      sandboxed));
  
  // Wrap the process in RAII to ensure cleanup on any exit path
  ScopedProcess process(raw_pid);
  
  stdin_p.CloseRead();
  stdout_p.CloseWrite();
  stderr_p.CloseWrite();
  FeedStdin(stdin_p, input, trace);

  // Use gRPC Alarm-based timeout manager instead of fork-based watcher
  std::atomic<bool> timed_out_flag{false};
  std::unique_ptr<ProcessTimeoutManager> timeout_manager;

  if (sandboxed) {
    const absl::Duration timeout = absl::Seconds(
        absl::GetFlag(FLAGS_sandbox_wall_clock_timeout_seconds));
    timeout_manager = std::make_unique<ProcessTimeoutManager>(
        process.Get(), timeout, [&timed_out_flag, pid = process.Get()]() {
          timed_out_flag.store(true);
          if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
          }
        });
    timeout_manager->Start();
    trace << absl::StrFormat("[INFO] gRPC Alarm timeout armed for %ld seconds\n",
                             absl::ToInt64Seconds(timeout));
  }

  bool truncated = false;
  ProcessRunner::ReadOutput(stdout_p.ReadFd(), stderr_p.ReadFd(), process.Get(), 
                            callback, truncated);

  if (timeout_manager) {
    timeout_manager->Cancel();
  }

  int status = 0;
  struct rusage usage {};
  
  // Wait for the process and collect resource usage
  wait4(process.Get(), &status, 0, &usage);
  
  // Release ownership since we've reaped it
  (void)process.Release();

  return BuildExecutionResult(status, usage, start, timed_out_flag.load(),
                              truncated);
}

// Formats the result trace with appropriate coloring based on status.
void FormatResultTrace(std::stringstream& trace, absl::string_view context,
                       const ExecutionResult& res) {
  if (res.wall_clock_timeout) {
    trace << absl::StrFormat("\033[91m[TIMEOUT]\033[0m %s: %s\n", context,
                             res.error_message);
  } else if (res.output_truncated) {
    trace << absl::StrFormat("\033[93m[LIMIT]\033[0m %s: %s\n", context,
                             res.error_message);
  } else if (!res.success) {
    trace << absl::StrFormat("\033[91m[ERROR]\033[0m %s: %s\n", context,
                             res.error_message);
  } else {
    trace << absl::StrFormat("\033[92m[OK]\033[0m %s finished successfully\n",
                             context);
  }

  const long cpu_time_ms = res.stats.user_time_ms + res.stats.system_time_ms;
  trace << absl::StrFormat(
      "    Resources: \033[1mCPU time\033[0m=\033[92m%ldms\033[0m, "
      "\033[1mMemory\033[0m=\033[95m%ldKB\033[0m\n",
      cpu_time_ms, res.stats.peak_memory_bytes / 1024);
}

absl::StatusOr<ExecutionResult> HandleExecutionResult(
    absl::string_view context, ExecutionResult res,
    std::stringstream& trace) {
  if (res.wall_clock_timeout) {
    res.error_message = "Wall-clock timeout exceeded";
  } else if (res.output_truncated) {
    res.error_message = "Output truncated";
  } else if (!res.success && res.error_message.empty()) {
    res.error_message = "Process failed";
  }

  FormatResultTrace(trace, context, res);
  res.backend_trace = trace.str();
  return res;
}

}  // namespace

// -----------------------------------------------------------------------------
// ProcessTimeoutManager Implementation
// -----------------------------------------------------------------------------

struct ProcessTimeoutState {
  ProcessTimeoutState(absl::Duration timeout, ProcessTimeoutManager::TimeoutCallback callback)
      : timeout(timeout), callback(std::move(callback)) {}

  absl::Duration timeout;
  ProcessTimeoutManager::TimeoutCallback callback;
  grpc::Alarm alarm;
  std::mutex mutex;
  bool started = false;
  bool triggered = false;
  bool cancelled = false;
};

ProcessTimeoutManager::ProcessTimeoutManager(pid_t pid, absl::Duration timeout,
                                             TimeoutCallback callback)
    : timeout_(timeout), callback_(std::move(callback)) {
  (void)pid;
  state_ = std::make_shared<ProcessTimeoutState>(timeout, std::move(callback_));
}

ProcessTimeoutManager::~ProcessTimeoutManager() { Cancel(); }

void ProcessTimeoutManager::Start() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->started || state_->cancelled) {
    return;
  }
  state_->started = true;

  auto deadline = absl::ToChronoTime(absl::Now() + state_->timeout);
  
  state_->alarm.Set(deadline, [state = state_](bool ok) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->cancelled || !ok) {
        return;
      }
      state->triggered = true;
    }
    if (state->callback) {
      state->callback();
    }
  });
}

void ProcessTimeoutManager::Cancel() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->cancelled || state_->triggered) {
    return;
  }
  state_->cancelled = true;
  state_->alarm.Cancel();
}

bool ProcessTimeoutManager::IsTriggered() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->triggered;
}

bool ProcessTimeoutManager::IsCancelled() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->cancelled;
}

void ProcessTimeoutManager::OnAlarmTriggered(bool ok) {
  // This is no longer used as we use a lambda in Start()
  (void)ok;
}

// -----------------------------------------------------------------------------
// ExecutionPipeline Implementation
// -----------------------------------------------------------------------------

ExecutionPipeline::ExecutionPipeline(std::shared_ptr<CacheInterface> cache)
    : cache_(std::move(cache)) {}

ExecutionPipeline& ExecutionPipeline::AddStep(std::unique_ptr<ExecutionStep> step) {
  if (!head_) {
    head_ = std::move(step);
    tail_ = head_.get();
  } else {
    tail_ = tail_->SetNext(std::move(step));
  }
  return *this;
}

CacheInterface* ExecutionPipeline::GetCache() const {
  return cache_.get();
}

absl::StatusOr<ExecutionResult> ExecutionPipeline::Run(ExecutionContext& context) {
  if (!head_) {
    return context.result;
  }
  
  const absl::Status status = head_->Execute(context);
  if (!status.ok()) {
    // Error handling is now done within the steps or decorators, but we ensure
    // the result error message is set if it's empty.
    if (context.result.error_message.empty()) {
      context.result.error_message = status.message();
    }
    context.result.backend_trace = context.trace.str();
    return status;
  }
  
  context.result.backend_trace = context.trace.str();
  return context.result;
}

// -----------------------------------------------------------------------------
// Concrete Execution Steps Implementation
// -----------------------------------------------------------------------------

absl::Status CreateSourceFileStep::ExecuteStep(ExecutionContext& context) {
  ABSL_ASSIGN_OR_RETURN(context.source_file_path,
                        TempFileManager::WriteTempFile(extension_, context.code));
  context.trace << "[OK] Created source file: " << context.source_file_path
                << "\n";
  context.AddCleanupPath(context.source_file_path);
  return absl::OkStatus();
}

absl::Status CompileStep::ExecuteStep(ExecutionContext& context) {
  // Build the compile command
  std::vector<std::string> argv = {compiler_};
  for (const auto& flag : compiler_flags_) {
    argv.push_back(flag);
  }
  argv.push_back(context.source_file_path);
  context.binary_path = context.source_file_path + ".bin";
  argv.push_back("-o");
  argv.push_back(context.binary_path);
  
  context.trace << "[INFO] Compiler: " << compiler_ << "\n";
  context.trace << "[INFO] Binary target: " << context.binary_path << "\n";
  context.AddCleanupPath(context.binary_path);

  // First compile attempt (null callback to suppress output)
  auto null_cb = [](absl::string_view, absl::string_view) {};
  ABSL_ASSIGN_OR_RETURN(const ExecutionResult comp_res,
                        RunCommandWithSandbox("Compile", argv, "", false,
                                              null_cb, context.trace));
  
  ABSL_ASSIGN_OR_RETURN(const ExecutionResult handled_res,
                        HandleExecutionResult("Compile", comp_res, context.trace));

  if (!handled_res.success) {
    context.trace << "[FAIL] Compilation failed - capturing detailed errors\n";
    // Re-run with actual callback to capture error output
    ABSL_ASSIGN_OR_RETURN(const ExecutionResult err_res,
                          RunCommandWithSandbox("Compile", argv, "", false,
                                                context.callback, context.trace));
    
    ABSL_ASSIGN_OR_RETURN(const ExecutionResult handled_err,
                          HandleExecutionResult("Compile", err_res, context.trace));
    
    if (handled_err.error_message.empty()) {
      context.SetError("Compilation failed");
    } else {
      context.SetError(handled_err.error_message);
    }
    context.result = handled_err;
    return absl::InternalError(context.result.error_message);
  }

  context.result = handled_res;
  return absl::OkStatus();
}

absl::Status RunProcessStep::ExecuteStep(ExecutionContext& context) {
  std::vector<std::string> argv;
  
  // Determine what to run based on binary_path
  if (!context.binary_path.empty()) {
    // Compiled language: run the binary
    argv = {context.binary_path};
  } else {
    // Interpreted language: run with interpreter
    // Detect language from source file extension
    if (context.source_file_path.ends_with(".py")) {
      argv = {"python3", "-u", context.source_file_path};
    } else {
      // Default: try to execute directly
      argv = {context.source_file_path};
    }
  }

  ABSL_ASSIGN_OR_RETURN(const ExecutionResult run_res,
                        RunCommandWithSandbox("Run", argv, context.stdin_data,
                                              sandboxed_, context.callback,
                                              context.trace));
  
  ABSL_ASSIGN_OR_RETURN(const ExecutionResult handled_res,
                        HandleExecutionResult("Run", run_res, context.trace));
  
  context.result = handled_res;
  if (!context.result.success) {
    return absl::InternalError(context.result.error_message);
  }
  return absl::OkStatus();
}

absl::Status FinalizeResultStep::ExecuteStep(ExecutionContext& context) {
  // Ensure trace is captured in result
  context.result.backend_trace = context.trace.str();
  return absl::OkStatus();
}

// -----------------------------------------------------------------------------
// CompiledLanguageStrategy Implementation
// Uses LanguageToolchainFactory for compiler/flag configuration.
// -----------------------------------------------------------------------------
CompiledLanguageStrategy::CompiledLanguageStrategy(
    std::unique_ptr<LanguageToolchainFactory> toolchain,
    std::shared_ptr<CacheInterface> cache)
    : toolchain_(std::move(toolchain)), cache_(std::move(cache)) {}

absl::StatusOr<ExecutionResult> CompiledLanguageStrategy::Execute(
    absl::string_view code, absl::string_view stdin_data,
    OutputCallback callback) {
  ExecutionContext context(code, stdin_data, std::move(callback));
  
  auto pipeline = CreatePipeline(cache_);
  return pipeline->Run(context);
}

absl::string_view CompiledLanguageStrategy::GetStrategyId() const {
  return toolchain_->GetLanguageId();
}

std::unique_ptr<ExecutionPipeline> CompiledLanguageStrategy::CreatePipeline(
    std::shared_ptr<CacheInterface> cache) {
  return ExecutionPipelineBuilder()
      .WithCache(std::move(cache))
      .AddCreateSourceFileStep(toolchain_->GetFileExtension())
      .AddCompileStep(toolchain_->GetExecutable(), toolchain_->GetStandardFlags())
      .AddRunProcessStep(true)
      .AddFinalizeResultStep(toolchain_->GetLanguageId())
      .Build();
}

CExecutionStrategy::CExecutionStrategy(std::shared_ptr<CacheInterface> cache)
    : CompiledLanguageStrategy(LanguageToolchainFactory::CreateC(), std::move(cache)) {}

CppExecutionStrategy::CppExecutionStrategy(std::shared_ptr<CacheInterface> cache)
    : CompiledLanguageStrategy(LanguageToolchainFactory::CreateCpp(), std::move(cache)) {}

PythonExecutionStrategy::PythonExecutionStrategy(
    std::shared_ptr<CacheInterface> cache)
    : toolchain_(LanguageToolchainFactory::CreatePython()), cache_(std::move(cache)) {}

absl::StatusOr<ExecutionResult> PythonExecutionStrategy::Execute(
    absl::string_view code, absl::string_view stdin_data,
    OutputCallback callback) {
  ExecutionContext context(code, stdin_data, std::move(callback));
  auto pipeline = CreatePipeline(cache_);
  return pipeline->Run(context);
}

absl::string_view PythonExecutionStrategy::GetStrategyId() const {
  return toolchain_->GetLanguageId();
}

std::unique_ptr<ExecutionPipeline> PythonExecutionStrategy::CreatePipeline(
    std::shared_ptr<CacheInterface> cache) {
  return ExecutionPipelineBuilder()
      .WithCache(std::move(cache))
      .AddCreateSourceFileStep(toolchain_->GetFileExtension())
      .AddRunProcessStep(true)
      .AddFinalizeResultStep(toolchain_->GetLanguageId())
      .Build();
}

// --- ExecutionStrategy Factory ---
absl::StatusOr<std::unique_ptr<ExecutionStrategy>> ExecutionStrategy::Create(
    absl::string_view filename_or_extension,
    std::shared_ptr<CacheInterface> cache) {
  // Use LanguageToolchainFactory to determine language type
  auto toolchain = LanguageToolchainFactory::Create(filename_or_extension);
  
  // Create appropriate strategy based on language
  if (toolchain->GetLanguageId() == "python") {
    return std::make_unique<PythonExecutionStrategy>(std::move(cache));
  }
  
  if (toolchain->GetLanguageId() == "c") {
    return std::make_unique<CExecutionStrategy>(std::move(cache));
  }
  
  // Default to C++
  return std::make_unique<CppExecutionStrategy>(std::move(cache));
}

SandboxedProcess::SandboxedProcess(std::shared_ptr<CacheInterface> cache)
    : cache_(std::move(cache)) {}

absl::StatusOr<ExecutionResult> SandboxedProcess::CompileAndRunStreaming(
    absl::string_view filename_or_extension, absl::string_view code,
    absl::string_view stdin_data, OutputCallback callback) {
  ABSL_ASSIGN_OR_RETURN(
      std::unique_ptr<ExecutionStrategy> strategy,
      ExecutionStrategy::Create(filename_or_extension, cache_));

  const std::string cache_input = absl::StrCat(strategy->GetStrategyId(), ":",
                                               code, "\0", stdin_data);
  const absl::StatusOr<std::string> hash_res =
      CacheInterface::ComputeHash(cache_input);

  if (hash_res.ok()) {
    const auto cached = cache_->Get(*hash_res);
    if (cached) {
      if (!cached->stdout_output.empty()) callback(cached->stdout_output, "");
      if (!cached->stderr_output.empty()) callback("", cached->stderr_output);
      ExecutionResult res;
      res.success = cached->success;
      res.error_message = cached->error_message;
      res.cache_hit = true;
      res.cached_stdout = cached->stdout_output;
      res.cached_stderr = cached->stderr_output;
      res.stats.peak_memory_bytes = cached->peak_memory_bytes;
      res.stats.elapsed_time_ms = static_cast<long>(cached->execution_time_ms);
      return res;
    }
  }

  struct OutputBuffer {
    std::string out, err;
    void Append(absl::string_view o, absl::string_view e) {
      if (!o.empty()) out.append(o);
      if (!e.empty()) err.append(e);
    }
  } buffer;

  auto wrapped_cb = [&](absl::string_view o, absl::string_view e) {
    buffer.Append(o, e);
    callback(o, e);
  };

  ABSL_ASSIGN_OR_RETURN(const ExecutionResult result,
                        strategy->Execute(code, stdin_data, wrapped_cb));

  if (result.success && hash_res.ok()) {
    CachedResult cr;
    cr.stdout_output = buffer.out;
    cr.stderr_output = buffer.err;
    cr.peak_memory_bytes = result.stats.peak_memory_bytes;
    cr.execution_time_ms = static_cast<float>(result.stats.elapsed_time_ms);
    cr.success = result.success;
    cr.error_message = result.error_message;
    cache_->Put(*hash_res, cr);
  }

  return result;
}

}  // namespace dcodex
