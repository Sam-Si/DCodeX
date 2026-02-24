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

#ifndef SRC_ENGINE_PROCESS_RUNNER_H_
#define SRC_ENGINE_PROCESS_RUNNER_H_

#include <array>
#include <vector>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#endif
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/engine/output_filter.h"
#include "src/engine/sandbox.h"

extern char **environ;

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
// MultiplexInstance: RAII Wrapper for epoll (Linux) or kqueue (macOS)
// ==============================================================================

/// RAII wrapper for an I/O multiplexing instance.
class MultiplexInstance {
 public:
#ifdef __linux__
  MultiplexInstance() : fd_(epoll_create1(EPOLL_CLOEXEC)) {}
#elif defined(__APPLE__)
  MultiplexInstance() : fd_(kqueue()) {}
#endif
  ~MultiplexInstance() = default;

  // Non-copyable, movable
  MultiplexInstance(const MultiplexInstance&) = delete;
  MultiplexInstance& operator=(const MultiplexInstance&) = delete;
  MultiplexInstance(MultiplexInstance&&) = default;
  MultiplexInstance& operator=(MultiplexInstance&&) = default;

  /// Checks if the multiplex instance is valid.
  [[nodiscard]] bool IsValid() const noexcept { return fd_.IsValid(); }

  /// Gets the multiplex file descriptor.
  [[nodiscard]] int Get() const noexcept { return fd_.Get(); }

  /// Adds a file descriptor to the multiplex instance.
  /// Returns OK on success, or an error status on failure.
  absl::Status AddFd(int fd) {
#ifdef __linux__
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(fd_.Get(), EPOLL_CTL_ADD, fd, &ev) == -1) {
      return absl::ErrnoToStatus(errno, "epoll_ctl ADD failed");
    }
#elif defined(__APPLE__)
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, (void*)(intptr_t)fd);
    if (kevent(fd_.Get(), &ev, 1, nullptr, 0, nullptr) == -1) {
      return absl::ErrnoToStatus(errno, "kevent ADD failed");
    }
#endif
    return absl::OkStatus();
  }

  /// Removes a file descriptor from the multiplex instance.
  void RemoveFd(int fd) {
#ifdef __linux__
    epoll_ctl(fd_.Get(), EPOLL_CTL_DEL, fd, nullptr);
#elif defined(__APPLE__)
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(fd_.Get(), &ev, 1, nullptr, 0, nullptr);
#endif
  }

 private:
  FileDescriptor fd_;
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
// ScopedProcess: RAII Wrapper for Child Processes
// ==============================================================================

/// RAII wrapper for a child process.
/// Ensures the child is killed and reaped on destruction to prevent zombies.
class ScopedProcess {
 public:
  explicit ScopedProcess(pid_t pid = -1) noexcept : pid_(pid), reaped_(false) {}

  ~ScopedProcess() { KillAndReap(); }

  // Non-copyable
  ScopedProcess(const ScopedProcess&) = delete;
  ScopedProcess& operator=(const ScopedProcess&) = delete;

  // Movable
  ScopedProcess(ScopedProcess&& other) noexcept
      : pid_(other.pid_), reaped_(other.reaped_) {
    other.pid_ = -1;
    other.reaped_ = true;
  }

  ScopedProcess& operator=(ScopedProcess&& other) noexcept {
    if (this != &other) {
      KillAndReap();
      pid_ = other.pid_;
      reaped_ = other.reaped_;
      other.pid_ = -1;
      other.reaped_ = true;
    }
    return *this;
  }

  /// Returns the PID of the child process.
  [[nodiscard]] pid_t Get() const noexcept { return pid_; }

  /// Checks if the process is valid.
  [[nodiscard]] bool IsValid() const noexcept { return pid_ > 0; }

  /// Releases ownership without killing/reaping.
  [[nodiscard]] pid_t Release() noexcept {
    const pid_t pid = pid_;
    pid_ = -1;
    reaped_ = true;
    return pid;
  }

  /// Kills the process (if running) and waits for it to prevent zombies.
  /// Returns the exit status, or -1 if the process was already reaped.
  int KillAndReap() {
    if (pid_ <= 0 || reaped_) {
      return -1;
    }

    // Check if process is still running
    if (kill(pid_, 0) == 0) {
      kill(pid_, SIGKILL);
    }

    // Wait for the process to prevent zombies
    int status = 0;
    waitpid(pid_, &status, 0);
    reaped_ = true;
    pid_ = -1;
    return status;
  }

 private:
  pid_t pid_;
  bool reaped_;
};

// ==============================================================================
// ProcessRunner: Process Execution Utilities
// ==============================================================================
//
// Sandboxing strategy — why two spawn paths:
//
//   sandboxed=false  (CompileStep):  posix_spawnp()
//     Fast path for compilation. No resource limits needed; we just want the
//     compiler to run without restrictions and without the fork overhead.
//
//   sandboxed=true   (RunProcessStep):  fork() + exec()
//     The ONLY portable way to set rlimits on a child before exec is to call
//     setrlimit() inside the child after fork() but before exec(). Alternatives:
//
//       • posix_spawnattr_setrlimit() — Linux-only (glibc ≥ 2.34, kernel ≥ 5.12),
//         absent on macOS. Not portable.
//       • pthread_atfork() — fires for ALL forks in all threads, not targeted.
//       • setrlimit from parent — impossible: rlimits live in the target process's
//         task_struct; there is no syscall to set them for another PID.
//       • cgroups v2 — powerful but requires root or cgroup delegation; overkill.
//
//     So: fork+exec with ApplyResourceLimits() in the child is the correct,
//     portable, and minimal approach.
//
// ==============================================================================

/// Utility class for process execution with sandboxing support.
class ProcessRunner {
 public:
  /// Spawns a new process with the given arguments.
  ///
  /// sandboxed=false → posix_spawnp (fast, no rlimits)
  /// sandboxed=true  → fork+exec with setrlimit in child (real enforcement)
  ///
  /// Returns the PID on success, or an error status on failure.
  static absl::StatusOr<pid_t> SpawnProcess(
      absl::Span<const std::string> argv,
      int stdin_fd, int stdout_fd, int stderr_fd,
      bool sandboxed) {
    if (sandboxed) {
      return ForkAndExecSandboxed(argv, stdin_fd, stdout_fd, stderr_fd);
    }
    return PosixSpawnUnsandboxed(argv, stdin_fd, stdout_fd, stderr_fd);
  }

  /// Applies resource limits (CPU time, address space) for sandboxed execution.
  /// Must be called inside the child process after fork() but before exec().
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

  /// Reads output from stdout and stderr pipes using the best available method.
  /// Returns true if output was truncated.
  static bool ReadOutput(int stdout_fd, int stderr_fd, pid_t child_pid,
                         const OutputCallback& callback, bool& truncated) {
    return ReadOutputMultiplexed(stdout_fd, stderr_fd, child_pid, callback,
                                 truncated);
  }

 private:
  // ---------------------------------------------------------------------------
  // ForkAndExecSandboxed
  //
  // Performs a fork+exec with full child-side setup:
  //   1. setrlimit (CPU + AS) — real enforcement, not best-effort
  //   2. signal mask + disposition reset — don't inherit gRPC handlers
  //   3. dup2 → stdio redirect
  //   4. close all FDs ≥ 3 — don't leak epoll/kqueue/gRPC FDs into child
  //   5. execvp
  //   6. _exit(127) on exec failure — NEVER exit(), avoids flushing parent
  //      stdio buffers and running C++ / atexit destructors
  // ---------------------------------------------------------------------------
  static absl::StatusOr<pid_t> ForkAndExecSandboxed(
      absl::Span<const std::string> argv,
      int stdin_fd, int stdout_fd, int stderr_fd) {
    // Build C-style argv. Strings are caller-owned and persist past exec.
    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
      c_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    c_argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
      return absl::ErrnoToStatus(errno, "fork() failed");
    }

    if (pid == 0) {
      // -----------------------------------------------------------------------
      // CHILD PROCESS — only async-signal-safe operations allowed here.
      // -----------------------------------------------------------------------

      // Step 1: Apply resource limits BEFORE exec so they are enforced.
      //   RLIMIT_CPU  → kernel sends SIGXCPU (then SIGKILL) on CPU exhaustion.
      //   RLIMIT_AS   → malloc/mmap returns ENOMEM when address space is full.
      ApplyResourceLimits();

      // Step 2: Reset signal mask — gRPC blocks several signals; clear them.
      sigset_t empty_mask;
      sigemptyset(&empty_mask);
      sigprocmask(SIG_SETMASK, &empty_mask, nullptr);

      // Step 3: Reset all signal dispositions to SIG_DFL.
      //   Inherited handlers (e.g. absl/gRPC SIGSEGV, SIGTERM handlers) must
      //   not run in the child. We iterate instead of using POSIX_SPAWN_SETSIGDEF
      //   because we need this to work with fork+exec.
      struct sigaction sa{};
      sa.sa_handler = SIG_DFL;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      for (int sig = 1; sig < NSIG; ++sig) {
        if (sig == SIGKILL || sig == SIGSTOP) continue;  // uncatchable
        sigaction(sig, &sa, nullptr);
      }

      // Step 4: Redirect stdio.
      if (dup2(stdin_fd,  STDIN_FILENO)  == -1) _exit(127);
      if (dup2(stdout_fd, STDOUT_FILENO) == -1) _exit(127);
      if (dup2(stderr_fd, STDERR_FILENO) == -1) _exit(127);

      // Step 5: Close all file descriptors above 2.
      //   This prevents the child from inheriting the parent's pipe read/write
      //   ends, epoll fd, kqueue fd, gRPC channel sockets, etc.
      CloseExtraFds(3);

      // Step 6: Execute. On success this never returns.
      execvp(c_argv[0], c_argv.data());

      // Step 7: exec failed — use _exit, not exit.
      //   exit() would: flush parent's stdio buffers, run C++ destructors for
      //   globals, call atexit() handlers, close FILE* streams. All wrong.
      //   _exit(127) matches the POSIX convention for "command not found".
      _exit(127);
    }

    // Parent: return the child PID. Caller wraps it in ScopedProcess.
    return pid;
  }

  // ---------------------------------------------------------------------------
  // PosixSpawnUnsandboxed
  //
  // Fast path for the CompileStep. No rlimits, no fork overhead.
  // posix_spawnp does an internal fork+exec but is optimized (vfork on some
  // platforms, or clone() on Linux) and avoids COW page-table duplication.
  // ---------------------------------------------------------------------------
  static absl::StatusOr<pid_t> PosixSpawnUnsandboxed(
      absl::Span<const std::string> argv,
      int stdin_fd, int stdout_fd, int stderr_fd) {
    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
      c_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    c_argv.push_back(nullptr);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    // Reset signal mask and dispositions for the child.
    const short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    posix_spawnattr_setflags(&attr, flags);

    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    posix_spawnattr_setsigmask(&attr, &empty_mask);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    posix_spawn_file_actions_adddup2(&actions, stdin_fd,  STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdout_fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_fd, STDERR_FILENO);
    // Close the originals in the child after dup2 (they're now at 0/1/2).
    posix_spawn_file_actions_addclose(&actions, stdin_fd);
    posix_spawn_file_actions_addclose(&actions, stdout_fd);
    posix_spawn_file_actions_addclose(&actions, stderr_fd);

    pid_t pid = 0;
    const int result = posix_spawnp(&pid, c_argv[0], &actions, &attr,
                                    c_argv.data(), environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    if (result != 0) {
      return absl::ErrnoToStatus(
          result,
          absl::StrFormat("posix_spawnp failed for '%s'", c_argv[0]));
    }
    return pid;
  }

  // ---------------------------------------------------------------------------
  // CloseExtraFds
  //
  // Called in the child after fork, closes all FDs >= lowfd.
  // Tries the fastest available method, falls back progressively:
  //
  //   Linux ≥ 5.9:  close_range() syscall           — O(1), one syscall
  //   Linux fallback: /proc/self/fd enumeration      — O(open_fds)
  //   Portable fallback: iterate to sysconf OPEN_MAX — O(OPEN_MAX), ~1-4K
  // ---------------------------------------------------------------------------
  static void CloseExtraFds(int lowfd) noexcept {
#if defined(__linux__)
    // close_range(2) was added in Linux 5.9 (syscall 436 on x86-64).
    // We call via syscall() to avoid a hard glibc >= 2.34 dependency.
#  ifndef SYS_close_range
#    define SYS_close_range 436
#  endif
    if (syscall(SYS_close_range, static_cast<unsigned>(lowfd),
                ~0U, 0U) == 0) {
      return;  // Done in one syscall.
    }

    // Fallback: enumerate /proc/self/fd (Linux ≥ 2.6.22).
    // This avoids iterating thousands of potentially-unused FD slots.
    DIR* dir = opendir("/proc/self/fd");
    if (dir != nullptr) {
      const int dirfd_val = dirfd(dir);
      struct dirent* ent;
      while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        const int fd = static_cast<int>(strtol(ent->d_name, nullptr, 10));
        if (fd >= lowfd && fd != dirfd_val) {
          close(fd);
        }
      }
      closedir(dir);
      return;
    }
#endif  // __linux__

    // Portable fallback: iterate up to sysconf(_SC_OPEN_MAX).
    // EBADF is silently ignored for already-closed slots.
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd <= 0) maxfd = 1024;
    for (int fd = lowfd; fd < static_cast<int>(maxfd); ++fd) {
      close(fd);
    }
  }

  // ---------------------------------------------------------------------------
  // ReadOutputMultiplexed (unchanged — epoll on Linux, kqueue on macOS)
  // ---------------------------------------------------------------------------

  static bool ReadOutputMultiplexed(int stdout_fd, int stderr_fd,
                                    pid_t child_pid,
                                    const OutputCallback& callback,
                                    bool& truncated) {
    MultiplexInstance multiplex;
    if (!multiplex.IsValid()) {
      truncated = false;
      return false;
    }

    SetNonBlocking(stdout_fd);
    SetNonBlocking(stderr_fd);

    if (!multiplex.AddFd(stdout_fd).ok()) {
      truncated = false;
      return false;
    }
    if (!multiplex.AddFd(stderr_fd).ok()) {
      multiplex.RemoveFd(stdout_fd);
      truncated = false;
      return false;
    }

    bool stdout_open = true, stderr_open = true;
    size_t total_bytes = 0;
    truncated = false;

    constexpr int kMaxEvents = 2;
#ifdef __linux__
    struct epoll_event events[kMaxEvents];
#elif defined(__APPLE__)
    struct kevent events[kMaxEvents];
    struct timespec timeout = {0, 10000000};  // 10 ms
#endif

    while (stdout_open || stderr_open) {
      int nfds = 0;
#ifdef __linux__
      nfds = epoll_wait(multiplex.Get(), events, kMaxEvents, 10);
#elif defined(__APPLE__)
      nfds = kevent(multiplex.Get(), nullptr, 0, events, kMaxEvents, &timeout);
#endif
      if (nfds < 0) {
        if (errno == EINTR) continue;
        break;
      }

      if (nfds == 0) {
        int status;
        pid_t result = waitpid(child_pid, &status, WNOHANG);
        if (result == child_pid) {
          DrainFd(stdout_fd, callback, true,  stdout_open, total_bytes);
          DrainFd(stderr_fd, callback, false, stderr_open, total_bytes);
          break;
        }
        continue;
      }

      for (int i = 0; i < nfds; ++i) {
        int fd = -1;
#ifdef __linux__
        fd = events[i].data.fd;
#elif defined(__APPLE__)
        fd = static_cast<int>(reinterpret_cast<intptr_t>(events[i].udata));
#endif
        if (fd == stdout_fd && stdout_open) {
          ReadFromFd(fd, callback, true,  stdout_open, total_bytes);
        } else if (fd == stderr_fd && stderr_open) {
          ReadFromFd(fd, callback, false, stderr_open, total_bytes);
        }
      }

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

  static void SetNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  }

  static void ReadFromFd(int fd, const OutputCallback& callback,
                         bool is_stdout, bool& open_flag,
                         size_t& total_bytes) {
    std::array<char, 4096> buffer{};
    while (true) {
      const ssize_t n = read(fd, buffer.data(), buffer.size());
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        open_flag = false;
        break;
      }
      if (n == 0) {
        open_flag = false;
        break;
      }

      total_bytes += static_cast<size_t>(n);
      const absl::string_view chunk(buffer.data(), static_cast<size_t>(n));
      if (is_stdout) {
        callback(chunk, "");
      } else {
        if (!GetOutputFilter().ShouldSuppress(chunk)) {
          callback("", chunk);
        }
      }
    }
  }

  static void DrainFd(int fd, const OutputCallback& callback, bool is_stdout,
                      bool& open_flag, size_t& total_bytes) {
    if (!open_flag) return;
    SetNonBlocking(fd);
    ReadFromFd(fd, callback, is_stdout, open_flag, total_bytes);
  }
};

}  // namespace dcodex::internal

#endif  // SRC_ENGINE_PROCESS_RUNNER_H_
