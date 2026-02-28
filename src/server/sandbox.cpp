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

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/server/process_runner.h"
#include "src/server/temp_file_manager.h"

namespace dcodex {

namespace {

// --- SRP: Resource Monitoring Constants ---
constexpr int kWallClockTimeoutSeconds = SandboxLimits::kWallClockTimeoutSeconds;

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

  pid_t watcher_pid = -1;
  if (sandboxed) {
    watcher_pid = fork();
    if (watcher_pid == 0) {
      for (int fd = 0; fd < 1024; ++fd) close(fd);
      sleep(kWallClockTimeoutSeconds);
      if (kill(pid, 0) == 0) kill(pid, SIGKILL);
      _exit(0);
    }
  }

  bool truncated = false;
  ProcessRunner::ReadOutput(stdout_p.ReadFd(), stderr_p.ReadFd(), pid, callback,
                            truncated);

  int status = 0;
  struct rusage usage {};
  bool timed_out = false;
  if (sandboxed && watcher_pid > 0) {
    timed_out = ProcessRunner::WaitWithTimeout(pid, watcher_pid, status, usage);
  } else {
    wait4(pid, &status, 0, &usage);
  }

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

ExecutionResult CppExecutionStrategy::Execute(absl::string_view code,
                                              absl::string_view stdin_data,
                                              OutputCallback callback) {
  std::stringstream trace;
  trace << "--- Backend Execution Trace ---\n";

  std::string source_file = TempFileManager::WriteTempFile(".cpp", code);
  if (source_file.empty()) {
    trace << "[FAIL] Failed to create source file\n";
    ExecutionResult res;
    res.success = false;
    res.error_message = "Failed to create source file";
    res.backend_trace = trace.str();
    return res;
  }
  trace << "[OK] Created source file: " << source_file << "\n";
  TempFileManager::Guard source_guard(source_file);

  std::string binary_path = source_file + ".bin";
  trace << "[INFO] Binary target: " << binary_path << "\n";
  TempFileManager::Guard binary_guard(binary_path);

  // Compile
  auto null_cb = [](absl::string_view, absl::string_view) {};
  ExecutionResult comp_res = RunCommandWithSandbox(
      "Compile", {"clang++", "-std=c++17", source_file, "-o", binary_path},
      "", false, null_cb, trace);
  comp_res = HandleExecutionResult("Compile", comp_res, trace);
  if (!comp_res.success) {
    trace << "[FAIL] Compilation failed - capturing detailed errors\n";
    // If compilation fails, we run it again with the real callback to capture
    // output
    ExecutionResult err_res = RunCommandWithSandbox(
        "Compile", {"clang++", "-std=c++17", source_file, "-o", binary_path},
        "", false, callback, trace);
    err_res = HandleExecutionResult("Compile", err_res, trace);
    if (err_res.error_message.empty()) {
      err_res.error_message = "Compilation failed";
    }
    err_res.backend_trace = trace.str();
    return err_res;
  }

  // Run
  ExecutionResult run_res = RunCommandWithSandbox(
      "Run", {binary_path}, stdin_data, true, callback, trace);
  return HandleExecutionResult("Run", run_res, trace);
}

// --- PythonExecutionStrategy Implementation ---
ExecutionResult PythonExecutionStrategy::Execute(absl::string_view code,
                                                 absl::string_view stdin_data,
                                                 OutputCallback callback) {
  std::stringstream trace;
  trace << "--- Backend Execution Trace ---\n";

  std::string source_file = TempFileManager::WriteTempFile(".py", code);
  if (source_file.empty()) {
    trace << "[FAIL] Failed to create source file\n";
    ExecutionResult res;
    res.success = false;
    res.error_message = "Failed to create source file";
    res.backend_trace = trace.str();
    return res;
  }
  trace << "[OK] Created source file: " << source_file << "\n";
  TempFileManager::Guard source_guard(source_file);

  ExecutionResult run_res = RunCommandWithSandbox(
      "Run", {"python", "-u", source_file}, stdin_data, true, callback, trace);
  return HandleExecutionResult("Run", run_res, trace);
}

// --- ExecutionStrategy Factory ---
std::unique_ptr<ExecutionStrategy> ExecutionStrategy::Create(
    absl::string_view filename_or_extension) {
  if (absl::EndsWith(filename_or_extension, ".py") ||
      filename_or_extension == "python") {
    return std::make_unique<PythonExecutionStrategy>();
  }
  return std::make_unique<CppExecutionStrategy>();
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
