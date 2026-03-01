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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/server/output_filter.h"
#include "src/server/sandbox.h"

namespace dcodex::internal {

// ==============================================================================
// FileDescriptor: RAII Wrapper for POSIX File Descriptors
// ==============================================================================

/// RAII wrapper for POSIX file descriptors.
/// Ensures proper cleanup when the wrapper goes out of scope.
class FileDescriptor {
 public:
  explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
  ~FileDescriptor() { Reset(); }

  FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
  }

  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      Reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  // Non-copyable
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  /// Returns the raw file descriptor.
  [[nodiscard]] int Get() const noexcept { return fd_; }

  /// Checks if the file descriptor is valid.
  [[nodiscard]] bool IsValid() const noexcept { return fd_ != -1; }

  /// Closes the current descriptor and optionally sets a new one.
  void Reset(int fd = -1) noexcept {
    if (fd_ != -1) close(fd_);
    fd_ = fd;
  }

  /// Releases ownership without closing.
  [[nodiscard]] int Release() noexcept {
    int fd = fd_;
    fd_ = -1;
    return fd;
  }

 private:
  int fd_;
};

// ==============================================================================
// EpollInstance: RAII Wrapper for epoll instance
// ==============================================================================

/// RAII wrapper for an epoll instance.
class EpollInstance {
 public:
  EpollInstance() : epfd_(epoll_create1(EPOLL_CLOEXEC)) {}
  ~EpollInstance() = default;

  // Non-copyable, movable
  EpollInstance(const EpollInstance&) = delete;
  EpollInstance& operator=(const EpollInstance&) = delete;
  EpollInstance(EpollInstance&&) = default;
  EpollInstance& operator=(EpollInstance&&) = default;

  /// Checks if the epoll instance is valid.
  [[nodiscard]] bool IsValid() const noexcept { return epfd_.IsValid(); }

  /// Gets the epoll file descriptor.
  [[nodiscard]] int Get() const noexcept { return epfd_.Get(); }

  /// Adds a file descriptor to the epoll instance.
  /// Returns OK on success, or an error status on failure.
  absl::Status AddFd(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_.Get(), EPOLL_CTL_ADD, fd, &ev) == -1) {
      return absl::ErrnoToStatus(errno, "epoll_ctl ADD failed");
    }
    return absl::OkStatus();
  }

  /// Removes a file descriptor from the epoll instance.
  void RemoveFd(int fd) {
    epoll_ctl(epfd_.Get(), EPOLL_CTL_DEL, fd, nullptr);
  }

 private:
  FileDescriptor epfd_;
};

// ==============================================================================
// PipePair: RAII Wrapper for Unix Pipes
// ==============================================================================

/// RAII wrapper for a pair of file descriptors (pipe).
class PipePair {
 public:
  /// Creates a pipe. Returns true on success.
  [[nodiscard]] bool Create() {
    int fds[2];
    if (pipe(fds) == -1) return false;
    read_end_.Reset(fds[0]);
    write_end_.Reset(fds[1]);
    return true;
  }

  [[nodiscard]] int ReadFd() const noexcept { return read_end_.Get(); }
  [[nodiscard]] int WriteFd() const noexcept { return write_end_.Get(); }
  void CloseRead() noexcept { read_end_.Reset(); }
  void CloseWrite() noexcept { write_end_.Reset(); }

  /// Releases ownership of the write end without closing.
  [[nodiscard]] int ReleaseWrite() noexcept { return write_end_.Release(); }
  
  /// Releases ownership of the read end without closing.
  [[nodiscard]] int ReleaseRead() noexcept { return read_end_.Release(); }

 private:
  FileDescriptor read_end_;
  FileDescriptor write_end_;
};

// ==============================================================================
// ProcessRunner: Process Execution Utilities using posix_spawn and epoll
// ==============================================================================

/// Utility class for process execution with sandboxing support.
/// Uses posix_spawn for efficient process creation and epoll for I/O multiplexing.
class ProcessRunner {
 public:
  /// Spawns a new process with the given arguments.
  /// Uses posix_spawn for efficient process creation.
  /// Returns the PID on success, or an error status on failure.
  static absl::StatusOr<pid_t> SpawnProcess(
      absl::Span<const std::string> argv,
      int stdin_fd, int stdout_fd, int stderr_fd,
      bool sandboxed) {
    // Build C-style argv array
    std::vector<char*> c_argv;
    for (const auto& arg : argv) {
      c_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    c_argv.push_back(nullptr);

    // Initialize spawn attributes
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    // Set flags for process control
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    posix_spawnattr_setflags(&attr, flags);

    // Reset signal mask for the child process
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    posix_spawnattr_setsigmask(&attr, &empty_mask);

    // Initialize file actions
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    // Redirect stdin, stdout, stderr
    posix_spawn_file_actions_adddup2(&actions, stdin_fd, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdout_fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_fd, STDERR_FILENO);

    // Close the original fds in the child (they're dup'd)
    posix_spawn_file_actions_addclose(&actions, stdin_fd);
    posix_spawn_file_actions_addclose(&actions, stdout_fd);
    posix_spawn_file_actions_addclose(&actions, stderr_fd);

    // Apply resource limits in the parent before spawn if sandboxed
    // Note: posix_spawn doesn't directly support rlimit, so we'll need
    // to use a different approach - setrlimit in the child via a helper
    // For now, we'll skip sandbox limits in posix_spawn path
    // This is a known limitation - fork+exec is needed for full sandboxing

    pid_t pid = 0;
    int result = posix_spawnp(&pid, c_argv[0], &actions, &attr,
                              c_argv.data(), environ);

    // Cleanup
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    if (result != 0) {
      return absl::ErrnoToStatus(result, 
          absl::StrFormat("posix_spawnp failed for '%s'", c_argv[0]));
    }

    // If sandboxed, apply resource limits to the spawned process
    // Note: This is a best-effort approach. For proper sandboxing,
    // we'd need to use fork+exec or a more sophisticated mechanism.
    if (sandboxed && pid > 0) {
      ApplyResourceLimitsToProcess(pid);
    }

    return pid;
  }

  /// Applies resource limits to a running process (best-effort).
  static void ApplyResourceLimitsToProcess(pid_t pid) {
    // Note: We cannot directly setrlimit for another process.
    // For proper sandboxing, we'd need to use prctl or cgroups.
    // This is a placeholder for future enhancement.
    (void)pid;  // Suppress unused parameter warning
  }

  /// Applies resource limits (CPU time, memory) for sandboxed execution.
  /// Call this in the child process after fork but before exec.
  static void ApplyResourceLimits() {
    const int cpu_limit_secs =
        absl::GetFlag(FLAGS_sandbox_cpu_time_limit_seconds);
    const struct rlimit cpu_limit{static_cast<rlim_t>(cpu_limit_secs),
                                   static_cast<rlim_t>(cpu_limit_secs)};
    setrlimit(RLIMIT_CPU, &cpu_limit);

    const uint64_t mem_limit_bytes =
        absl::GetFlag(FLAGS_sandbox_memory_limit_bytes);
    const struct rlimit mem_limit{static_cast<rlim_t>(mem_limit_bytes),
                                   static_cast<rlim_t>(mem_limit_bytes)};
    setrlimit(RLIMIT_AS, &mem_limit);
  }

  /// Reads output from stdout and stderr pipes using epoll for efficient I/O.
  /// Returns true if output was truncated.
  static bool ReadOutputEpoll(int stdout_fd, int stderr_fd, pid_t child_pid,
                              const OutputCallback& callback, bool& truncated) {
    // Create epoll instance
    EpollInstance epoll;
    if (!epoll.IsValid()) {
      truncated = false;
      return false;
    }

    // Set fds to non-blocking mode
    SetNonBlocking(stdout_fd);
    SetNonBlocking(stderr_fd);

    // Add fds to epoll
    if (!epoll.AddFd(stdout_fd, EPOLLIN | EPOLLET).ok()) {
      truncated = false;
      return false;
    }
    if (!epoll.AddFd(stderr_fd, EPOLLIN | EPOLLET).ok()) {
      epoll.RemoveFd(stdout_fd);
      truncated = false;
      return false;
    }

    std::array<char, 4096> buffer{};
    bool stdout_open = true, stderr_open = true;
    size_t total_bytes = 0;
    truncated = false;

    constexpr int kMaxEvents = 2;
    struct epoll_event events[kMaxEvents];

    while (stdout_open || stderr_open) {
      // Wait for events with a timeout
      int nfds = epoll_wait(epoll.Get(), events, kMaxEvents, 10);
      if (nfds < 0) {
        if (errno == EINTR) continue;
        break;
      }

      if (nfds == 0) {
        // Timeout - check if process is still running
        int status;
        pid_t result = waitpid(child_pid, &status, WNOHANG);
        if (result == child_pid) {
          // Process has exited
          // Drain remaining data
          DrainFd(stdout_fd, callback, true, stdout_open, total_bytes);
          DrainFd(stderr_fd, callback, false, stderr_open, total_bytes);
          break;
        }
        continue;
      }

      for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        if (fd == stdout_fd && stdout_open) {
          ReadFromFd(fd, callback, true, stdout_open, total_bytes);
        } else if (fd == stderr_fd && stderr_open) {
          ReadFromFd(fd, callback, false, stderr_open, total_bytes);
        }
      }

      // Check output limit
      const uint64_t max_output_bytes =
          absl::GetFlag(FLAGS_sandbox_max_output_bytes);
      if (total_bytes >= max_output_bytes) {
        kill(child_pid, SIGKILL);
        truncated = true;
        callback("", absl::StrFormat("\n[Output truncated: exceeded %zu KB "
                                     "limit]\n",
                                     max_output_bytes / 1024));
        break;
      }
    }

    return truncated;
  }

  /// Reads output from stdout and stderr pipes using select (fallback).
  /// Returns true if output was truncated.
  static bool ReadOutput(int stdout_fd, int stderr_fd, pid_t child_pid,
                         const OutputCallback& callback, bool& truncated) {
    // Use epoll on Linux for better performance
    return ReadOutputEpoll(stdout_fd, stderr_fd, child_pid, callback, truncated);
  }

 private:
  /// Sets a file descriptor to non-blocking mode.
  static void SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  }

  /// Reads all available data from a file descriptor.
  static void ReadFromFd(int fd, const OutputCallback& callback,
                         bool is_stdout, bool& open_flag, size_t& total_bytes) {
    std::array<char, 4096> buffer{};
    while (true) {
      ssize_t n = read(fd, buffer.data(), buffer.size());
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // No more data available right now
          break;
        }
        // Error
        open_flag = false;
        break;
      }
      if (n == 0) {
        // EOF
        open_flag = false;
        break;
      }

      total_bytes += n;
      absl::string_view chunk(buffer.data(), static_cast<size_t>(n));
      if (is_stdout) {
        callback(chunk, "");
      } else {
        if (!GetOutputFilter().ShouldSuppress(chunk)) {
          callback("", chunk);
        }
      }
    }
  }

  /// Drains remaining data from a file descriptor.
  static void DrainFd(int fd, const OutputCallback& callback,
                      bool is_stdout, bool& open_flag, size_t& total_bytes) {
    if (!open_flag) return;
    SetNonBlocking(fd);
    ReadFromFd(fd, callback, is_stdout, open_flag, total_bytes);
  }
};

}  // namespace dcodex::internal

#endif  // SRC_SERVER_PROCESS_RUNNER_H_
