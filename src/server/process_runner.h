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

#ifndef SRC_SERVER_PROCESS_RUNNER_H_
#define SRC_SERVER_PROCESS_RUNNER_H_

#include <array>
#include <vector>

#include <signal.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/server/output_filter.h"
#include "src/server/sandbox.h"

namespace dcodex::internal {

class FileDescriptor {
 public:
  explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
  ~FileDescriptor() { Reset(); }
  FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) { Reset(); fd_ = other.fd_; other.fd_ = -1; }
    return *this;
  }
  int Get() const noexcept { return fd_; }
  bool IsValid() const noexcept { return fd_ != -1; }
  void Reset(int fd = -1) noexcept {
    if (fd_ != -1) close(fd_);
    fd_ = fd;
  }
 private:
  int fd_;
};

class PipePair {
 public:
  bool Create() {
    int fds[2];
    if (pipe(fds) == -1) return false;
    read_end_.Reset(fds[0]);
    write_end_.Reset(fds[1]);
    return true;
  }
  int ReadFd() const noexcept { return read_end_.Get(); }
  int WriteFd() const noexcept { return write_end_.Get(); }
  void CloseRead() noexcept { read_end_.Reset(); }
  void CloseWrite() noexcept { write_end_.Reset(); }
 private:
  FileDescriptor read_end_;
  FileDescriptor write_end_;
};

class ProcessRunner {
 public:
  static void ApplyResourceLimits() {
    int cpu_limit_secs = absl::GetFlag(FLAGS_sandbox_cpu_time_limit_seconds);
    struct rlimit cpu_limit{static_cast<rlim_t>(cpu_limit_secs),
                            static_cast<rlim_t>(cpu_limit_secs)};
    setrlimit(RLIMIT_CPU, &cpu_limit);
    uint64_t mem_limit_bytes = absl::GetFlag(FLAGS_sandbox_memory_limit_bytes);
    struct rlimit mem_limit{static_cast<rlim_t>(mem_limit_bytes),
                            static_cast<rlim_t>(mem_limit_bytes)};
    setrlimit(RLIMIT_AS, &mem_limit);
  }

  static void RedirectAndExec(absl::Span<const std::string> argv,
                              int stdin_fd, int stdout_fd, int stderr_fd,
                              bool sandboxed) {
    if (sandboxed) ApplyResourceLimits();
    dup2(stdin_fd, STDIN_FILENO);
    dup2(stdout_fd, STDOUT_FILENO);
    dup2(stderr_fd, STDERR_FILENO);

    std::vector<char*> c_argv;
    for (const auto& arg : argv) c_argv.push_back(const_cast<char*>(arg.c_str()));
    c_argv.push_back(nullptr);
    execvp(c_argv[0], c_argv.data());
    exit(1);
  }

  static bool ReadOutput(int stdout_fd, int stderr_fd, pid_t child_pid,
                         const OutputCallback& callback, bool& truncated) {
    std::array<char, 4096> buffer{};
    bool stdout_open = true, stderr_open = true;
    size_t total_bytes = 0;
    truncated = false;

    while (stdout_open || stderr_open) {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      int max_fd = -1;
      if (stdout_open) { FD_SET(stdout_fd, &read_fds); max_fd = std::max(max_fd, stdout_fd); }
      if (stderr_open) { FD_SET(stderr_fd, &read_fds); max_fd = std::max(max_fd, stderr_fd); }
      if (max_fd == -1) break;

      struct timeval tv{0, 10000};
      int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
      if (activity < 0) break;
      if (activity == 0) continue;

      auto read_from = [&](int fd, bool is_stdout, bool& open_flag) {
        if (open_flag && FD_ISSET(fd, &read_fds)) {
          ssize_t n = read(fd, buffer.data(), buffer.size());
          if (n <= 0) {
            open_flag = false;
          } else {
            total_bytes += n;
            absl::string_view chunk(buffer.data(), static_cast<size_t>(n));
            if (is_stdout) {
              callback(chunk, "");
            } else {
              if (!internal::GetOutputFilter().ShouldSuppress(chunk)) {
                callback("", chunk);
              }
            }
          }
        }
      };

      read_from(stdout_fd, true, stdout_open);
      read_from(stderr_fd, false, stderr_open);

      uint64_t max_output_bytes = absl::GetFlag(FLAGS_sandbox_max_output_bytes);
      if (total_bytes >= max_output_bytes) {
        kill(child_pid, SIGKILL);
        truncated = true;
        callback("", absl::StrFormat("\n[Output truncated: exceeded %zu KB limit]\n",
                                    max_output_bytes / 1024));
        break;
      }
    }
    return truncated;
  }

  static bool WaitWithTimeout(pid_t child_pid, pid_t watcher_pid, int& status, struct rusage& usage) {
    if (wait4(child_pid, &status, 0, &usage) == -1) return false;
    bool killed_by_sig = WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;

    int watcher_status = 0;
    pid_t reaped_watcher = 0;
    for (int i = 0; i < 10; ++i) {
      reaped_watcher = waitpid(watcher_pid, &watcher_status, WNOHANG);
      if (reaped_watcher == watcher_pid) break;
      usleep(1000);
    }

    if (reaped_watcher == watcher_pid) {
      if (WIFEXITED(watcher_status) && WEXITSTATUS(watcher_status) == 0 && killed_by_sig) return true;
    }

    if (reaped_watcher == 0) {
      kill(watcher_pid, SIGKILL);
      waitpid(watcher_pid, nullptr, 0);
    }
    return false;
  }

  // NOTE: WaitWithTimeout is DEPRECATED in favor of ProcessTimeoutManager
  // (see sandbox.h) which uses gRPC Alarm for more efficient timeout handling.
  // The fork-based watcher approach has the following issues:
  // - Creates an extra process for each timeout (process table pollution)
  // - Duplicates parent's address space (memory overhead)
  // - Requires complex cleanup (zombie process handling)
  // - Not integrated with gRPC server lifecycle
};

}  // namespace dcodex::internal

#endif  // SRC_SERVER_PROCESS_RUNNER_H_
