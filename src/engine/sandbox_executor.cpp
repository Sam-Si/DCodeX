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

#include "src/engine/sandbox_executor.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <sstream>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/engine/process_runner.h"
#include "src/engine/process_timeout_manager.h"

namespace dcodex {

namespace {

using internal::ProcessRunner;
using internal::PipePair;

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

}  // namespace

SandboxExecutor::SandboxExecutor(ResourcePolicy policy)
    : policy_(std::move(policy)) {}

absl::StatusOr<ExecutionResult> SandboxExecutor::RunCommand(
    absl::string_view context_name, const std::vector<std::string>& argv,
    absl::string_view input_data, bool sandboxed, OutputCallback callback,
    std::stringstream& trace_stream) {
  const std::string cmd_str = absl::StrJoin(argv, " ");
  FormatCommandTrace(trace_stream, sandboxed, context_name, cmd_str);
  LOG(INFO) << (sandboxed ? "Sandboxed" : "Local") << " exec: " << cmd_str;

  PipePair stdin_p, stdout_p, stderr_p;
  absl::Status pipe_status = CreatePipes(stdin_p, stdout_p, stderr_p, trace_stream);
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
    trace_stream << "[FAIL] Failed to spawn process: " << spawn_result.status().message() << "\n";
    return spawn_result.status();
  }
  
  const pid_t pid = *spawn_result;

  stdin_p.CloseRead();
  stdout_p.CloseWrite();
  stderr_p.CloseWrite();
  FeedStdin(stdin_p, input_data, trace_stream);

  // Use gRPC Alarm-based timeout manager
  std::atomic<bool> timed_out_flag{false};
  std::unique_ptr<ProcessTimeoutManager> timeout_manager;

  if (sandboxed) {
    const absl::Duration timeout = policy_.GetWallClockTimeout();
    timeout_manager = std::make_unique<ProcessTimeoutManager>(
        pid, timeout, [&timed_out_flag, pid]() {
          timed_out_flag.store(true);
          if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
          }
        });
    timeout_manager->Start();
    trace_stream << absl::StrFormat("[INFO] gRPC Alarm timeout armed for %ld seconds\n",
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

}  // namespace dcodex
