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
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/engine/execution_pipeline.h"
#include "src/engine/execution_step.h"
#include "src/engine/execution_strategy.h"
#include "src/engine/execution_types.h"
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

// --- SRP: Global Cache Management ---
class CacheHolder {
 public:
  static ExecutionCache& Get() {
    static ExecutionCache cache;
    return cache;
  }
};

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
// Now uses gRPC Alarm for timeout instead of fork-based watcher.
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
  absl::Status pipe_status = CreatePipes(stdin_p, stdout_p, stderr_p, trace);
  if (!pipe_status.ok()) {
    return pipe_status;
  }

  const absl::Time start = absl::Now();
  
  // Use posix_spawn for efficient process creation
  absl::StatusOr<pid_t> spawn_result = ProcessRunner::SpawnProcess(
      absl::MakeSpan(argv),
      stdin_p.ReadFd(),
      stdout_p.WriteFd(),
      stderr_p.WriteFd(),
      sandboxed);
  
  if (!spawn_result.ok()) {
    trace << "[FAIL] Failed to spawn process: " << spawn_result.status().message() << "\n";
    return spawn_result.status();
  }
  
  const pid_t pid = *spawn_result;

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
        pid, timeout, [&timed_out_flag, pid]() {
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
  ProcessRunner::ReadOutput(stdout_p.ReadFd(), stderr_p.ReadFd(), pid, callback,
                            truncated);

  if (timeout_manager) {
    timeout_manager->Cancel();
  }

  int status = 0;
  struct rusage usage {};
  wait4(pid, &status, 0, &usage);

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

absl::StatusOr<ExecutionResult> ExecutionPipeline::Run(ExecutionContext& context) {
  for (const auto& step : steps_) {
    context.trace << absl::StrFormat("[STEP] %s\n", step->Name());
    absl::Status status = step->Execute(context);
    if (!status.ok()) {
      context.trace << absl::StrFormat("[FAIL] Step '%s' failed: %s\n",
                                       step->Name(), status.message());
      if (context.result.error_message.empty()) {
        context.result.error_message =
            absl::StrFormat("Step '%s' failed: %s", step->Name(),
                            status.message());
      }
      context.result.backend_trace = context.trace.str();
      return status;
    }
    context.trace << absl::StrFormat("[OK] Step '%s' completed\n",
                                     step->Name());
  }
  context.result.backend_trace = context.trace.str();
  return context.result;
}

// -----------------------------------------------------------------------------
// Concrete Execution Steps Implementation
// -----------------------------------------------------------------------------

absl::Status CreateSourceFileStep::Execute(ExecutionContext& context) {
  absl::StatusOr<std::string> path_result =
      TempFileManager::WriteTempFile(extension_, context.code);
  if (!path_result.ok()) {
    context.trace << "[FAIL] Failed to create source file: "
                  << path_result.status().message() << "\n";
    return context.Fail(absl::StrCat("Failed to create source file: ",
                                     path_result.status().message()));
  }
  context.source_file_path = std::move(*path_result);
  context.trace << "[OK] Created source file: " << context.source_file_path
                << "\n";
  context.AddCleanupPath(context.source_file_path);
  return absl::OkStatus();
}

absl::Status CompileStep::Execute(ExecutionContext& context) {
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
  absl::StatusOr<ExecutionResult> comp_res = RunCommandWithSandbox(
      "Compile", argv, "", false, null_cb, context.trace);
  if (!comp_res.ok()) {
    return comp_res.status();
  }
  
  absl::StatusOr<ExecutionResult> handled_res =
      HandleExecutionResult("Compile", *comp_res, context.trace);
  if (!handled_res.ok()) {
    return handled_res.status();
  }

  if (!handled_res->success) {
    context.trace << "[FAIL] Compilation failed - capturing detailed errors\n";
    // Re-run with actual callback to capture error output
    absl::StatusOr<ExecutionResult> err_res = RunCommandWithSandbox(
        "Compile", argv, "", false, context.callback, context.trace);
    if (!err_res.ok()) {
      return err_res.status();
    }
    
    absl::StatusOr<ExecutionResult> handled_err =
        HandleExecutionResult("Compile", *err_res, context.trace);
    if (!handled_err.ok()) {
      return handled_err.status();
    }
    
    if (handled_err->error_message.empty()) {
      context.SetError("Compilation failed");
    } else {
      context.SetError(handled_err->error_message);
    }
    context.result = *handled_err;
    return absl::InternalError(context.result.error_message);
  }

  context.result = *handled_res;
  return absl::OkStatus();
}

absl::Status RunProcessStep::Execute(ExecutionContext& context) {
  std::vector<std::string> argv;
  
  // Determine what to run based on binary_path
  if (!context.binary_path.empty()) {
    // Compiled language: run the binary
    argv = {context.binary_path};
  } else {
    // Interpreted language: run with interpreter
    // Detect language from source file extension
    if (context.source_file_path.ends_with(".py")) {
      argv = {"python", "-u", context.source_file_path};
    } else {
      // Default: try to execute directly
      argv = {context.source_file_path};
    }
  }

  absl::StatusOr<ExecutionResult> run_res = RunCommandWithSandbox(
      "Run", argv, context.stdin_data, sandboxed_, context.callback,
      context.trace);
  if (!run_res.ok()) {
    return run_res.status();
  }
  
  absl::StatusOr<ExecutionResult> handled_res =
      HandleExecutionResult("Run", *run_res, context.trace);
  if (!handled_res.ok()) {
    return handled_res.status();
  }
  
  context.result = *handled_res;
  if (!context.result.success) {
    return absl::InternalError(context.result.error_message);
  }
  return absl::OkStatus();
}

absl::Status FinalizeResultStep::Execute(ExecutionContext& context) {
  // Ensure trace is captured in result
  context.result.backend_trace = context.trace.str();
  return absl::OkStatus();
}

// -----------------------------------------------------------------------------
// CompiledLanguageStrategy Implementation
// Supports both C and C++ compilation with appropriate compiler selection.
// -----------------------------------------------------------------------------
CompiledLanguageStrategy::CompiledLanguageStrategy(
    Language language, std::shared_ptr<CacheInterface> cache)
    : language_(language), cache_(std::move(cache)) {}

absl::StatusOr<ExecutionResult> CompiledLanguageStrategy::Execute(
    absl::string_view code, absl::string_view stdin_data,
    OutputCallback callback) {
  ExecutionContext context(code, stdin_data, std::move(callback));
  
  auto pipeline = CreatePipeline(cache_);
  return pipeline->Run(context);
}

std::unique_ptr<ExecutionPipeline> CompiledLanguageStrategy::CreatePipeline(
    std::shared_ptr<CacheInterface> cache) {
  auto pipeline = std::make_unique<ExecutionPipeline>(std::move(cache));
  pipeline->AddStep(std::make_unique<CreateSourceFileStep>(GetExtension()))
           .AddStep(std::make_unique<CompileStep>(GetCompiler(), GetStandardFlags()))
           .AddStep(std::make_unique<RunProcessStep>(true))
           .AddStep(std::make_unique<FinalizeResultStep>(
               language_ == Language::kC ? "CExecution" : "CppExecution"));
  return pipeline;
}

// -----------------------------------------------------------------------------
// PythonExecutionStrategy Implementation
// -----------------------------------------------------------------------------
PythonExecutionStrategy::PythonExecutionStrategy(
    std::shared_ptr<CacheInterface> cache)
    : cache_(std::move(cache)) {}

absl::StatusOr<ExecutionResult> PythonExecutionStrategy::Execute(
    absl::string_view code, absl::string_view stdin_data,
    OutputCallback callback) {
  ExecutionContext context(code, stdin_data, std::move(callback));
  
  auto pipeline = CreatePipeline(cache_);
  return pipeline->Run(context);
}

std::unique_ptr<ExecutionPipeline> PythonExecutionStrategy::CreatePipeline(
    std::shared_ptr<CacheInterface> cache) {
  auto pipeline = std::make_unique<ExecutionPipeline>(std::move(cache));
  pipeline->AddStep(std::make_unique<CreateSourceFileStep>(".py"))
           .AddStep(std::make_unique<RunProcessStep>(true))
           .AddStep(std::make_unique<FinalizeResultStep>("PythonExecution"));
  return pipeline;
}

// --- ExecutionStrategy Factory ---
absl::StatusOr<std::unique_ptr<ExecutionStrategy>> ExecutionStrategy::Create(
    absl::string_view filename_or_extension,
    std::shared_ptr<CacheInterface> cache) {
  // Check for Python
  if (absl::EndsWith(filename_or_extension, ".py") ||
      filename_or_extension == "python") {
    return std::make_unique<PythonExecutionStrategy>(std::move(cache));
  }
  
  // Check for C
  if (absl::EndsWith(filename_or_extension, ".c") ||
      filename_or_extension == "c") {
    return std::make_unique<CExecutionStrategy>(std::move(cache));
  }
  
  // Default to C++ for .cpp, .cc, .cxx, or unknown
  return std::make_unique<CppExecutionStrategy>(std::move(cache));
}

// --- SandboxedProcess Implementation ---
ExecutionCache& SandboxedProcess::GetCache() { return CacheHolder::Get(); }
void SandboxedProcess::ClearCache() { CacheHolder::Get().Clear(); }

absl::StatusOr<ExecutionResult> SandboxedProcess::CompileAndRunStreaming(
    absl::string_view filename_or_extension, absl::string_view code,
    absl::string_view stdin_data, OutputCallback callback,
    std::shared_ptr<CacheInterface> cache) {
  // Use global cache if no cache provided
  std::shared_ptr<CacheInterface> effective_cache = cache ? cache : std::shared_ptr<CacheInterface>(&GetCache(), [](CacheInterface*){});
  
  absl::StatusOr<std::unique_ptr<ExecutionStrategy>> strategy_result =
      ExecutionStrategy::Create(filename_or_extension, effective_cache);
  if (!strategy_result.ok()) {
    return strategy_result.status();
  }
  
  std::unique_ptr<ExecutionStrategy>& strategy = *strategy_result;

  std::string cache_input = absl::StrCat(strategy->GetStrategyId(), ":", code,
                                        "\0", stdin_data);
  absl::StatusOr<std::string> hash_res = CacheInterface::ComputeHash(cache_input);

  if (hash_res.ok()) {
    auto cached = effective_cache->Get(hash_res.value());
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

  absl::StatusOr<ExecutionResult> result =
      strategy->Execute(code, stdin_data, wrapped_cb);

  if (!result.ok()) {
    return result;
  }

  if (result->success && hash_res.ok()) {
    CachedResult cr;
    cr.stdout_output = buffer.out;
    cr.stderr_output = buffer.err;
    cr.peak_memory_bytes = result->stats.peak_memory_bytes;
    cr.execution_time_ms = static_cast<float>(result->stats.elapsed_time_ms);
    cr.success = result->success;
    cr.error_message = result->error_message;
    effective_cache->Put(hash_res.value(), cr);
  }

  return result;
}

}  // namespace dcodex
