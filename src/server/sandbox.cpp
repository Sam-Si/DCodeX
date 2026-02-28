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

#include "src/server/sandbox.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <sstream>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/server/process_runner.h"
#include "src/server/temp_file_manager.h"

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
ExecutionResult RunCommandWithSandbox(absl::string_view context,
                                      const std::vector<std::string>& argv,
                                      absl::string_view input, bool sandboxed,
                                      OutputCallback callback,
                                      std::stringstream& trace) {
  std::string cmd_str = absl::StrJoin(argv, " ");
  // Using ANSI colors for backend trace
  const char* color_tag =
      sandboxed ? "\033[94m[SANDBOX]\033[0m " : "\033[92m[LOCAL]\033[0m ";
  trace << color_tag << context << ": " << cmd_str << "\n";
  LOG(INFO) << (sandboxed ? "Sandboxed" : "Local") << " exec: " << cmd_str;

  PipePair stdin_p, stdout_p, stderr_p;
  if (!stdin_p.Create() || !stdout_p.Create() || !stderr_p.Create()) {
    trace << "[FAIL] Pipe creation failed\n";
    return ExecutionResult{false, "Pipe creation failed"};
  }

  absl::Time start = absl::Now();
  pid_t pid = fork();
  if (pid == 0) {
    ProcessRunner::RedirectAndExec(absl::MakeSpan(argv), stdin_p.ReadFd(),
                                   stdout_p.WriteFd(), stderr_p.WriteFd(),
                                   sandboxed);
  }

  stdin_p.CloseRead();
  stdout_p.CloseWrite();
  stderr_p.CloseWrite();
  if (!input.empty()) {
    trace << "[INFO] Feeding " << input.size() << " bytes to stdin\n";
    write(stdin_p.WriteFd(), input.data(), input.size());
  }
  stdin_p.CloseWrite();

  // Use gRPC Alarm-based timeout manager instead of fork-based watcher
  std::atomic<bool> timed_out_flag{false};
  std::unique_ptr<ProcessTimeoutManager> timeout_manager;
  
  if (sandboxed) {
    absl::Duration timeout = absl::Seconds(
        absl::GetFlag(FLAGS_sandbox_wall_clock_timeout_seconds));
    timeout_manager = std::make_unique<ProcessTimeoutManager>(
        pid, timeout, [&timed_out_flag, pid]() {
          timed_out_flag.store(true);
          // Kill the process if still running
          if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
          }
        });
    timeout_manager->Start();
    trace << "[INFO] gRPC Alarm timeout armed for " 
          << absl::ToInt64Seconds(timeout) << " seconds\n";
  }

  bool truncated = false;
  ProcessRunner::ReadOutput(stdout_p.ReadFd(), stderr_p.ReadFd(), pid, callback,
                            truncated);

  // Cancel timeout if process finished before alarm
  if (timeout_manager) {
    timeout_manager->Cancel();
  }

  int status = 0;
  struct rusage usage {};
  wait4(pid, &status, 0, &usage);

  bool timed_out = timed_out_flag.load();

  ExecutionResult res;
  res.stats = ComputeResourceStats(usage, start, absl::Now());
  res.wall_clock_timeout = timed_out;
  res.output_truncated = truncated;
  res.success =
      !truncated && !timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0;

  if (timed_out) {
    res.error_message = "Wall-clock timeout exceeded";
  } else if (truncated) {
    res.error_message = "Output truncated";
  } else if (!res.success) {
    if (WIFEXITED(status)) {
      res.error_message = absl::StrCat(
          "Process exited with non-zero status: ", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      res.error_message =
          absl::StrCat("Process killed by signal: ", WTERMSIG(status));
    } else {
      res.error_message = "Process failed for unknown reasons";
    }
  }
  return res;
}

ExecutionResult HandleExecutionResult(absl::string_view context,
                                      ExecutionResult res,
                                      std::stringstream& trace) {
  if (res.wall_clock_timeout) {
    res.error_message = "Wall-clock timeout exceeded";
    trace << "\033[91m[TIMEOUT]\033[0m " << context << ": " << res.error_message << "\n";
  } else if (res.output_truncated) {
    res.error_message = "Output truncated";
    trace << "\033[93m[LIMIT]\033[0m " << context << ": " << res.error_message << "\n";
  } else if (!res.success) {
    if (res.error_message.empty()) {
      res.error_message = "Process failed";
    }
    trace << "\033[91m[ERROR]\033[0m " << context << ": " << res.error_message << "\n";
  } else {
    trace << "\033[92m[OK]\033[0m " << context << " finished successfully\n";
  }
  trace << "    Resources: \033[1mCPU time\033[0m=\033[92m"
        << res.stats.user_time_ms + res.stats.system_time_ms
        << "ms\033[0m, \033[1mMemory\033[0m=\033[95m" << res.stats.peak_memory_bytes / 1024 << "KB\033[0m\n";
  res.backend_trace = trace.str();
  return res;
}

}  // namespace

// -----------------------------------------------------------------------------
// ProcessTimeoutManager Implementation
// -----------------------------------------------------------------------------

ProcessTimeoutManager::ProcessTimeoutManager(pid_t pid, absl::Duration timeout,
                                             TimeoutCallback callback)
    : pid_(pid), timeout_(timeout), callback_(std::move(callback)) {}

ProcessTimeoutManager::~ProcessTimeoutManager() { Cancel(); }

void ProcessTimeoutManager::Start() {
  absl::MutexLock lock(&mutex_);
  if (started_ || cancelled_) {
    return;
  }
  started_ = true;

  // Calculate deadline from current time
  auto deadline = absl::ToChronoTime(absl::Now() + timeout_);
  
  // Set the gRPC Alarm with a callback
  alarm_.Set(deadline, [this](bool ok) { OnAlarmTriggered(ok); });
}

void ProcessTimeoutManager::Cancel() {
  absl::MutexLock lock(&mutex_);
  if (cancelled_ || triggered_) {
    return;
  }
  cancelled_ = true;
  alarm_.Cancel();
}

bool ProcessTimeoutManager::IsTriggered() const {
  absl::MutexLock lock(&mutex_);
  return triggered_;
}

bool ProcessTimeoutManager::IsCancelled() const {
  absl::MutexLock lock(&mutex_);
  return cancelled_;
}

void ProcessTimeoutManager::OnAlarmTriggered(bool ok) {
  absl::MutexLock lock(&mutex_);
  if (cancelled_) {
    return;
  }
  
  if (ok) {
    // Alarm fired (timeout occurred)
    triggered_ = true;
    if (callback_) {
      callback_();
    }
  }
  // If !ok, the alarm was cancelled
}

// -----------------------------------------------------------------------------
// Concrete Execution Steps Implementation
// -----------------------------------------------------------------------------

bool CreateSourceFileStep::Execute(ExecutionContext& context) {
  context.source_file_path = TempFileManager::WriteTempFile(extension_, context.code);
  if (context.source_file_path.empty()) {
    context.trace << "[FAIL] Failed to create source file\n";
    context.Fail("Failed to create source file");
    return false;
  }
  context.trace << "[OK] Created source file: " << context.source_file_path << "\n";
  context.AddCleanupPath(context.source_file_path);
  return true;
}

bool CompileStep::Execute(ExecutionContext& context) {
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
  ExecutionResult comp_res = RunCommandWithSandbox(
      "Compile", argv, "", false, null_cb, context.trace);
  comp_res = HandleExecutionResult("Compile", comp_res, context.trace);

  if (!comp_res.success) {
    context.trace << "[FAIL] Compilation failed - capturing detailed errors\n";
    // Re-run with actual callback to capture error output
    ExecutionResult err_res = RunCommandWithSandbox(
        "Compile", argv, "", false, context.callback, context.trace);
    err_res = HandleExecutionResult("Compile", err_res, context.trace);
    if (err_res.error_message.empty()) {
      err_res.error_message = "Compilation failed";
    }
    context.result = err_res;
    return false;
  }

  context.result = comp_res;
  return true;
}

bool RunProcessStep::Execute(ExecutionContext& context) {
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

  ExecutionResult run_res = RunCommandWithSandbox(
      "Run", argv, context.stdin_data, sandboxed_, context.callback, context.trace);
  context.result = HandleExecutionResult("Run", run_res, context.trace);
  return context.result.success;
}

bool FinalizeResultStep::Execute(ExecutionContext& context) {
  // Ensure trace is captured in result
  context.result.backend_trace = context.trace.str();
  return context.result.success;
}

// -----------------------------------------------------------------------------
// CompiledLanguageStrategy Implementation
// Supports both C and C++ compilation with appropriate compiler selection.
// -----------------------------------------------------------------------------
ExecutionResult CompiledLanguageStrategy::Execute(absl::string_view code,
                                                  absl::string_view stdin_data,
                                                  OutputCallback callback) {
  ExecutionContext context(code, stdin_data, std::move(callback));
  
  auto pipeline = CreatePipeline();
  return pipeline->Run(context);
}

std::unique_ptr<ExecutionPipeline> CompiledLanguageStrategy::CreatePipeline() {
  auto pipeline = std::make_unique<ExecutionPipeline>();
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
ExecutionResult PythonExecutionStrategy::Execute(absl::string_view code,
                                                 absl::string_view stdin_data,
                                                 OutputCallback callback) {
  ExecutionContext context(code, stdin_data, std::move(callback));
  
  auto pipeline = CreatePipeline();
  return pipeline->Run(context);
}

std::unique_ptr<ExecutionPipeline> PythonExecutionStrategy::CreatePipeline() {
  auto pipeline = std::make_unique<ExecutionPipeline>();
  pipeline->AddStep(std::make_unique<CreateSourceFileStep>(".py"))
           .AddStep(std::make_unique<RunProcessStep>(true))
           .AddStep(std::make_unique<FinalizeResultStep>("PythonExecution"));
  return pipeline;
}

// --- ExecutionStrategy Factory ---
std::unique_ptr<ExecutionStrategy> ExecutionStrategy::Create(
    absl::string_view filename_or_extension) {
  // Check for Python
  if (absl::EndsWith(filename_or_extension, ".py") ||
      filename_or_extension == "python") {
    return std::make_unique<PythonExecutionStrategy>();
  }
  
  // Check for C
  if (absl::EndsWith(filename_or_extension, ".c") ||
      filename_or_extension == "c") {
    return std::make_unique<CompiledLanguageStrategy>(
        CompiledLanguageStrategy::Language::kC);
  }
  
  // Default to C++ for .cpp, .cc, .cxx, or unknown
  return std::make_unique<CompiledLanguageStrategy>(
      CompiledLanguageStrategy::Language::kCpp);
}

// --- SandboxedProcess Implementation ---
ExecutionCache& SandboxedProcess::GetCache() { return CacheHolder::Get(); }
void SandboxedProcess::ClearCache() { CacheHolder::Get().Clear(); }

ExecutionResult SandboxedProcess::CompileAndRunStreaming(
    absl::string_view filename_or_extension, absl::string_view code,
    absl::string_view stdin_data, OutputCallback callback) {
  auto strategy = ExecutionStrategy::Create(filename_or_extension);

  std::string cache_input = absl::StrCat(strategy->GetStrategyId(), ":", code,
                                        "\0", stdin_data);
  auto hash_res = ExecutionCache::ComputeHash(cache_input);

  if (hash_res.ok()) {
    auto cached = GetCache().Get(hash_res.value());
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

  ExecutionResult result = strategy->Execute(code, stdin_data, wrapped_cb);

  if (result.success && hash_res.ok()) {
    CachedResult cr;
    cr.stdout_output = buffer.out;
    cr.stderr_output = buffer.err;
    cr.peak_memory_bytes = result.stats.peak_memory_bytes;
    cr.execution_time_ms = static_cast<float>(result.stats.elapsed_time_ms);
    cr.success = result.success;
    cr.error_message = result.error_message;
    GetCache().Put(hash_res.value(), cr);
  }

  return result;
}

}  // namespace dcodex
