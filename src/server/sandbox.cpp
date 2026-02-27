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

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>

#include "absl/cleanup/cleanup.h"
#include "absl/container/inlined_vector.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace dcodex {

namespace {

// Global cache instance.
class CacheHolder {
 public:
  static ExecutionCache& Get() {
    static ExecutionCache cache;
    return cache;
  }
};

// Buffer to capture output for caching.
class OutputBuffer {
 public:
  void AppendStdout(absl::string_view data) { stdout_.append(data); }
  void AppendStderr(absl::string_view data) { stderr_.append(data); }

  [[nodiscard]] const std::string& GetStdout() const { return stdout_; }
  [[nodiscard]] const std::string& GetStderr() const { return stderr_; }

 private:
  std::string stdout_;
  std::string stderr_;
};

// RAII wrapper for file descriptors.
class FileDescriptor {
 public:
  explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}

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

  ~FileDescriptor() { Reset(); }

  [[nodiscard]] int Get() const noexcept { return fd_; }
  [[nodiscard]] bool IsValid() const noexcept { return fd_ != -1; }

  int Release() noexcept {
    int fd = fd_;
    fd_ = -1;
    return fd;
  }

  void Reset(int fd = -1) noexcept {
    if (fd_ != -1) {
      close(fd_);
    }
    fd_ = fd;
  }

 private:
  int fd_;
};

// RAII wrapper for pipe file descriptor pairs.
class PipePair {
 public:
  PipePair() = default;

  PipePair(PipePair&& other) noexcept
      : read_end_(std::move(other.read_end_)),
        write_end_(std::move(other.write_end_)) {}

  PipePair& operator=(PipePair&& other) noexcept {
    if (this != &other) {
      read_end_ = std::move(other.read_end_);
      write_end_ = std::move(other.write_end_);
    }
    return *this;
  }

  [[nodiscard]] bool Create() {
    int fds[2];
    if (pipe(fds) == -1) {
      return false;
    }
    read_end_.Reset(fds[0]);
    write_end_.Reset(fds[1]);
    return true;
  }

  [[nodiscard]] int ReadFd() const noexcept { return read_end_.Get(); }
  [[nodiscard]] int WriteFd() const noexcept { return write_end_.Get(); }

  FileDescriptor& ReadEnd() noexcept { return read_end_; }
  FileDescriptor& WriteEnd() noexcept { return write_end_; }

  void CloseRead() noexcept { read_end_.Reset(); }
  void CloseWrite() noexcept { write_end_.Reset(); }

  [[nodiscard]] bool IsValid() const noexcept {
    return read_end_.IsValid() && write_end_.IsValid();
  }

 private:
  FileDescriptor read_end_;
  FileDescriptor write_end_;
};

// RAII wrapper for temporary files.
class TempFile {
 public:
  explicit TempFile(std::filesystem::path path) noexcept
      : path_(std::move(path)) {}

  TempFile(TempFile&& other) noexcept : path_(std::move(other.path_)) {
    other.path_.clear();
  }

  TempFile& operator=(TempFile&& other) noexcept {
    if (this != &other) {
      Remove();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }

  ~TempFile() { Remove(); }

  [[nodiscard]] const std::filesystem::path& Path() const noexcept {
    return path_;
  }

  void Release() noexcept { path_.clear(); }

 private:
  void Remove() noexcept {
    if (!path_.empty()) {
      std::error_code ec;
      std::filesystem::remove(path_, ec);
    }
  }

  std::filesystem::path path_;
};

// Wall-clock timeout in seconds for sandboxed execution.
constexpr int kWallClockTimeoutSeconds = 5;

// Helper to set resource limits for sandboxed execution.
void ApplyResourceLimits() {
  struct rlimit cpu_limit;
  cpu_limit.rlim_cur = 2;
  cpu_limit.rlim_max = 2;
  setrlimit(RLIMIT_CPU, &cpu_limit);

  struct rlimit mem_limit;
  mem_limit.rlim_cur = 50 * 1024 * 1024;
  mem_limit.rlim_max = 50 * 1024 * 1024;
  setrlimit(RLIMIT_AS, &mem_limit);
}

// Helper to redirect file descriptors to pipes in child process.
void RedirectFileDescriptors(int stdout_write_fd, int stderr_write_fd) {
  dup2(stdout_write_fd, STDOUT_FILENO);
  dup2(stderr_write_fd, STDERR_FILENO);
}

// Helper to prepare argv for execvp call.
[[nodiscard]] absl::InlinedVector<char*, 8> PrepareArgv(
    absl::Span<const std::string> argv) {
  absl::InlinedVector<char*, 8> c_argv;
  c_argv.reserve(argv.size() + 1);
  for (const auto& arg : argv) {
    c_argv.push_back(const_cast<char*>(arg.c_str()));
  }
  c_argv.push_back(nullptr);
  return c_argv;
}

// Helper to read from a pipe and invoke callback.
// Returns the number of bytes read (> 0), 0 on EOF, or -1 on error.
[[nodiscard]] ssize_t ReadFromPipe(
    int fd, std::array<char, 4096>& buffer,
    const SandboxedProcess::OutputCallback& callback, bool is_stdout) {
  ssize_t bytes = read(fd, buffer.data(), buffer.size());
  if (bytes > 0) {
    if (is_stdout) {
      callback(absl::string_view(buffer.data(), bytes), "");
    } else {
      callback("", absl::string_view(buffer.data(), bytes));
    }
  }
  return bytes;
}

// Helper to compute resource statistics from rusage.
[[nodiscard]] SandboxedProcess::ResourceStats ComputeResourceStats(
    const struct rusage& usage, absl::Time start_time, absl::Time end_time) {
  SandboxedProcess::ResourceStats stats;
#ifdef __APPLE__
  stats.peak_memory_bytes = usage.ru_maxrss;
#else
  stats.peak_memory_bytes = usage.ru_maxrss * 1024;
#endif
  stats.user_time_ms =
      usage.ru_utime.tv_sec * 1000 + usage.ru_utime.tv_usec / 1000;
  stats.system_time_ms =
      usage.ru_stime.tv_sec * 1000 + usage.ru_stime.tv_usec / 1000;
  stats.elapsed_time_ms = absl::ToInt64Milliseconds(end_time - start_time);
  return stats;
}

}  // namespace

ExecutionCache& SandboxedProcess::GetCache() { return CacheHolder::Get(); }

void SandboxedProcess::ClearCache() { CacheHolder::Get().Clear(); }

// Replays cached output and creates result from cached data.
[[nodiscard]] SandboxedProcess::Result CreateCacheHitResult(
    const std::shared_ptr<const CachedResult>& cached,
    const SandboxedProcess::OutputCallback& callback) {
  if (!cached->stdout_output.empty()) {
    callback(cached->stdout_output, "");
  }
  if (!cached->stderr_output.empty()) {
    callback("", cached->stderr_output);
  }

  SandboxedProcess::Result result;
  result.success = cached->success;
  result.error_message = cached->error_message;
  result.cache_hit = true;
  result.cached_stdout = cached->stdout_output;
  result.cached_stderr = cached->stderr_output;
  result.stats.peak_memory_bytes = cached->peak_memory_bytes;
  result.stats.elapsed_time_ms =
      static_cast<long>(cached->execution_time_ms);
  result.stats.user_time_ms = 0;
  result.stats.system_time_ms = 0;
  return result;
}

// Stores execution result in cache.
void StoreCacheResult(const std::string& code_hash,
                      const SandboxedProcess::Result& result,
                      const OutputBuffer& buffer) {
  CachedResult cached_result;
  cached_result.stdout_output = buffer.GetStdout();
  cached_result.stderr_output = buffer.GetStderr();
  cached_result.peak_memory_bytes = result.stats.peak_memory_bytes;
  cached_result.execution_time_ms =
      static_cast<float>(result.stats.elapsed_time_ms);
  cached_result.success = result.success;
  cached_result.error_message = result.error_message;

  CacheHolder::Get().Put(code_hash, cached_result);
}

SandboxedProcess::Result SandboxedProcess::CompileAndRunStreaming(
    absl::string_view code, absl::string_view stdin_data,
    OutputCallback callback) {
  // The cache key must incorporate both the source code and the stdin data,
  // because the same code with different stdin produces different output.
  // We concatenate them with a NUL byte separator (NUL cannot appear in valid
  // UTF-8 source code) before hashing.
  std::string cache_input = absl::StrCat(code, absl::string_view("\0", 1),
                                         stdin_data);
  auto hash_result = ExecutionCache::ComputeHash(cache_input);
  if (!hash_result.ok()) {
    // Hash computation failed, executes without caching.
    return ExecuteWithoutCache(code, stdin_data, callback);
  }
  const std::string& code_hash = hash_result.value();

  // Checks cache first.
  auto cached = GetCache().Get(code_hash);
  if (cached != nullptr) {
    // Cache hit.
    return CreateCacheHitResult(cached, callback);
  }

  // Cache miss - executes and stores result.
  OutputBuffer buffer;

  auto wrapping_callback = [&callback, &buffer](
                               absl::string_view stdout_chunk,
                               absl::string_view stderr_chunk) {
    if (!stdout_chunk.empty()) {
      buffer.AppendStdout(stdout_chunk);
      callback(stdout_chunk, "");
    }
    if (!stderr_chunk.empty()) {
      buffer.AppendStderr(stderr_chunk);
      callback("", stderr_chunk);
    }
  };

  SandboxedProcess::Result result =
      ExecuteWithoutCache(code, stdin_data, wrapping_callback);

  // Stores in cache if execution was successful.
  if (result.success) {
    StoreCacheResult(code_hash, result, buffer);
  }

  return result;
}

SandboxedProcess::Result SandboxedProcess::ExecuteWithoutCache(
    absl::string_view code, absl::string_view stdin_data,
    OutputCallback callback) {
  std::string source_file = WriteTempFile(".cpp", code);
  if (source_file.empty()) {
    SandboxedProcess::Result r;
    r.success = false;
    r.error_message = "Failed to create source file";
    return r;
  }

  TempFile source_guard(source_file);
  std::filesystem::path binary_path = source_file + ".bin";
  absl::Cleanup binary_cleanup = [&binary_path] {
    std::error_code ec;
    std::filesystem::remove(binary_path, ec);
  };

  // Compilation step (not sandboxed, no stdin needed for the compiler).
  absl::InlinedVector<std::string, 4> compile_args = {
      "g++", "-std=c++17", source_file, "-o", binary_path.string()};
  Result compile_result = ExecuteCommandStreaming(
      absl::MakeSpan(compile_args), /*stdin_data=*/"", callback, false);
  if (!compile_result.success) {
    return compile_result;
  }

  // Execution step (sandboxed) — forward the caller-supplied stdin_data.
  absl::InlinedVector<std::string, 2> run_args = {binary_path.string()};
  Result run_result = ExecuteCommandStreaming(
      absl::MakeSpan(run_args), stdin_data, callback, true);

  return run_result;
}

std::string SandboxedProcess::WriteTempFile(absl::string_view extension,
                                            absl::string_view content) {
  char template_str[] = "/tmp/dcodex_XXXXXX";
  FileDescriptor fd(mkstemp(template_str));
  if (!fd.IsValid()) return "";
  // File descriptor closed by RAII, we just need the filename

  std::string path = absl::StrCat(template_str, extension);
  std::ofstream out(path);
  out << content;
  return path;
}

// Handles the child process execution setup.
// stdin_read_fd is the read end of the stdin pipe; it is dup2'd onto
// STDIN_FILENO so the child reads from it instead of the terminal.
void ExecuteChildProcess(absl::Span<const std::string> argv,
                         int stdin_read_fd, int stdout_write_fd,
                         int stderr_write_fd, bool sandboxed) {
  if (sandboxed) {
    ApplyResourceLimits();
  }

  // Redirect stdin from the pipe.
  dup2(stdin_read_fd, STDIN_FILENO);

  RedirectFileDescriptors(stdout_write_fd, stderr_write_fd);

  auto c_argv = PrepareArgv(argv);
  execvp(c_argv[0], c_argv.data());
  exit(1);
}

// Handles reading output from child process pipes.
//
// Uses a short select() timeout so that a sleeping or hung process that has
// closed its own output (or never produces output) does not block the parent
// indefinitely.  The loop exits as soon as both pipe read-ends reach EOF,
// which happens when the child process exits (or is killed).
//
// Output size limiting: the combined byte count of stdout and stderr is
// tracked across every read.  When the total exceeds kMaxOutputBytes the
// child is killed immediately with SIGKILL, a human-readable truncation
// notice is emitted as a stderr chunk, and the loop exits.
//
// Returns true if the output was truncated, false otherwise.
[[nodiscard]] bool ReadProcessOutput(
    int stdout_read_fd, int stderr_read_fd, pid_t child_pid,
    const SandboxedProcess::OutputCallback& callback) {
  std::array<char, 4096> buffer{};
  bool stdout_open = true;
  bool stderr_open = true;
  size_t total_output_bytes = 0;
  bool truncated = false;

  while (stdout_open || stderr_open) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;

    if (stdout_open) {
      FD_SET(stdout_read_fd, &read_fds);
      max_fd = std::max(max_fd, stdout_read_fd);
    }
    if (stderr_open) {
      FD_SET(stderr_read_fd, &read_fds);
      max_fd = std::max(max_fd, stderr_read_fd);
    }

    if (max_fd == -1) break;

    // Use a 100 ms timeout so we re-check whether both pipes are still open
    // even when the child is sleeping or produces no output.  This prevents
    // an indefinite block when a hung process holds the write-ends open but
    // never writes.  The watcher will eventually kill the child, causing the
    // write-ends to close and select() to return with EOF on the next poll.
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100 ms

    int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
    if (activity < 0) break;
    if (activity == 0) continue;  // timeout — loop again to re-arm select

    if (stdout_open && FD_ISSET(stdout_read_fd, &read_fds)) {
      ssize_t n = ReadFromPipe(stdout_read_fd, buffer, callback, true);
      if (n <= 0) {
        stdout_open = false;
      } else {
        total_output_bytes += static_cast<size_t>(n);
      }
    }
    if (stderr_open && FD_ISSET(stderr_read_fd, &read_fds)) {
      ssize_t n = ReadFromPipe(stderr_read_fd, buffer, callback, false);
      if (n <= 0) {
        stderr_open = false;
      } else {
        total_output_bytes += static_cast<size_t>(n);
      }
    }

    // Enforce the output size cap.  We check after every successful read so
    // the limit is applied as soon as possible without an extra syscall per
    // byte.  When the limit is exceeded we:
    //   1. Kill the child immediately so it cannot produce more output.
    //   2. Stream a clear truncation notice as a stderr chunk so the caller
    //      (and ultimately the end user) knows the output was cut short.
    //   3. Break out of the loop; the watcher / WaitWithTimeout will reap
    //      the child normally.
    if (total_output_bytes >= SandboxedProcess::kMaxOutputBytes) {
      kill(child_pid, SIGKILL);
      const std::string notice = absl::StrFormat(
          "\n[Output truncated: combined stdout+stderr exceeded %zu KB limit]\n",
          SandboxedProcess::kMaxOutputBytes / 1024);
      callback("", notice);
      truncated = true;
      break;
    }
  }

  return truncated;
}

// Waits for child process and collects resource statistics.
[[nodiscard]] SandboxedProcess::Result WaitForChildProcess(
    pid_t pid, absl::Time start_time) {
  int status;
  struct rusage usage;
  pid_t result = wait4(pid, &status, 0, &usage);

  if (result == -1) {
    return {false, "Failed to wait for child process", {}};
  }

  absl::Time end_time = absl::Now();

  bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  SandboxedProcess::ResourceStats stats =
      ComputeResourceStats(usage, start_time, end_time);

  return {success, success ? "" : "Process exited with non-zero status",
          stats};
}

// Watcher process that monitors wall-clock time and kills child if timeout exceeded.
// IMPORTANT: Must close all inherited file descriptors before sleeping so that
// the parent's ReadProcessOutput loop sees EOF as soon as the sandboxed child
// exits, rather than waiting for the watcher to finish its sleep.  If the
// watcher kept the pipe write-ends open, the parent would always block for the
// full timeout duration even for programs that finish instantly.
void RunTimeoutWatcher(pid_t child_pid, int timeout_seconds) {
  // Close all file descriptors inherited from the parent.  We only need the
  // ability to send a signal, which requires no open fds.  Closing everything
  // ensures the parent's pipe read-ends see EOF the moment the child exits,
  // not when the watcher's sleep finishes.
  //
  // We iterate up to a reasonable limit (1024) rather than using
  // sysconf(_SC_OPEN_MAX) which can return a very large value on some systems.
  for (int fd = 0; fd < 1024; ++fd) {
    close(fd);
  }

  // Sleep for the timeout duration.
  sleep(timeout_seconds);

  // Check if child is still running and kill it if so.
  if (kill(child_pid, 0) == 0) {
    kill(child_pid, SIGKILL);
  }

  _exit(0);
}

// Waits for child process with a watcher process enforcing wall-clock timeout.
// Collects rusage for the child via wait4.
// Returns true if the child was killed by the watcher (timeout), false if the
// child finished on its own.
[[nodiscard]] bool WaitWithTimeout(pid_t child_pid, pid_t watcher_pid,
                                    int& child_status,
                                    struct rusage& child_usage) {
  // Wait for the sandboxed child process.
  if (wait4(child_pid, &child_status, 0, &child_usage) == -1) {
    return false;
  }

  // The child has finished. Check if it was killed by a signal.
  bool killed_by_sigkill = WIFSIGNALED(child_status) && WTERMSIG(child_status) == SIGKILL;

  // Now we need to determine if it was the watcher that killed it.
  // We'll wait a very short time for the watcher to finish its exit if it's the one that killed us.
  int watcher_status = 0;
  pid_t reaped_watcher = 0;
  
  // If it was killed by SIGKILL, it's very likely the watcher (or output truncation, 
  // which is handled by the caller checking output_truncated).
  // We'll give the watcher a moment to exit.
  for (int i = 0; i < 10; ++i) {
    reaped_watcher = waitpid(watcher_pid, &watcher_status, WNOHANG);
    if (reaped_watcher == watcher_pid) break;
    usleep(1000); // 1ms
  }

  if (reaped_watcher == watcher_pid) {
    // Watcher finished. If it finished on its own (not killed by us), it means it timed out.
    // In RunTimeoutWatcher, it exits with _exit(0) after sending SIGKILL.
    if (WIFEXITED(watcher_status) && WEXITSTATUS(watcher_status) == 0 && killed_by_sigkill) {
      return true;
    }
  }

  // If we're here, either the child finished on its own or it was killed by something else
  // (like output truncation). Clean up the watcher if it's still running.
  if (reaped_watcher == 0) {
    kill(watcher_pid, SIGKILL);
    waitpid(watcher_pid, nullptr, 0);
  }

  return false;
}

SandboxedProcess::Result SandboxedProcess::ExecuteCommandStreaming(
    absl::Span<const std::string> argv, absl::string_view stdin_data,
    OutputCallback callback, bool sandboxed) {
  PipePair stdin_pipe;
  PipePair stdout_pipe;
  PipePair stderr_pipe;

  if (!stdin_pipe.Create() || !stdout_pipe.Create() || !stderr_pipe.Create()) {
    SandboxedProcess::Result r;
    r.success = false;
    r.error_message = "Failed to create pipes";
    return r;
  }

  absl::Time start_time = absl::Now();

  pid_t pid = fork();
  if (pid == -1) {
    SandboxedProcess::Result r;
    r.success = false;
    r.error_message = "Failed to fork";
    return r;
  }

  if (pid == 0) {
    // Child process — set up and exec.  Never returns.
    // The child reads from the read end of the stdin pipe.
    ExecuteChildProcess(argv, stdin_pipe.ReadFd(), stdout_pipe.WriteFd(),
                        stderr_pipe.WriteFd(), sandboxed);
    // ExecuteChildProcess calls _exit / exec; this line is unreachable.
  }

  // ---- Parent process ----

  // Close the child-side ends in the parent:
  //   • stdin  read end  — the parent only writes to stdin.
  //   • stdout write end — the parent only reads from stdout.
  //   • stderr write end — the parent only reads from stderr.
  // Closing the output write-ends here (before forking the watcher) ensures
  // the watcher never inherits them, so EOF is seen as soon as the child exits.
  stdin_pipe.CloseRead();
  stdout_pipe.CloseWrite();
  stderr_pipe.CloseWrite();

  // Write all stdin data to the child and close the write end so the child
  // receives EOF after reading all provided input.  We write the entire buffer
  // in a loop to handle partial writes (write() may write fewer bytes than
  // requested, especially for large inputs).
  if (!stdin_data.empty()) {
    const char* buf = stdin_data.data();
    size_t remaining = stdin_data.size();
    while (remaining > 0) {
      ssize_t written = write(stdin_pipe.WriteFd(), buf, remaining);
      if (written <= 0) break;  // pipe full or error; child will see EOF
      buf += written;
      remaining -= written;
    }
  }
  // Close the write end of stdin so the child sees EOF after consuming all
  // input.  This must happen before we start reading output, otherwise the
  // child may block waiting for more input and we deadlock.
  stdin_pipe.CloseWrite();

  pid_t watcher_pid = -1;

  // Fork a watcher process that enforces the wall-clock timeout.
  // Only for sandboxed (untrusted) execution — compilation runs unsandboxed.
  if (sandboxed) {
    watcher_pid = fork();
    if (watcher_pid == 0) {
      // Watcher process — never returns to parent code.
      RunTimeoutWatcher(pid, kWallClockTimeoutSeconds);
    }
    if (watcher_pid == -1) {
      // Failed to fork watcher — kill the child and bail out.
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
      SandboxedProcess::Result r;
      r.success = false;
      r.error_message = "Failed to fork timeout watcher";
      return r;
    }
  }

  // Drain output from the child.  ReadProcessOutput uses a short select()
  // timeout internally, so it will unblock promptly after the child is killed
  // by the watcher (pipe write-ends close when child exits).  It also enforces
  // the output size cap and returns true if the output was truncated.
  bool output_truncated =
      ReadProcessOutput(stdout_pipe.ReadFd(), stderr_pipe.ReadFd(), pid, callback);

  // Wait for the child (and watcher) and collect resource usage.
  int status = 0;
  struct rusage usage{};
  bool timeout_occurred = false;

  if (sandboxed && watcher_pid > 0) {
    timeout_occurred = WaitWithTimeout(pid, watcher_pid, status, usage);
  } else {
    // Non-sandboxed execution (e.g. compilation) — wait normally.
    wait4(pid, &status, 0, &usage);
  }

  absl::Time end_time = absl::Now();
  SandboxedProcess::ResourceStats stats =
      ComputeResourceStats(usage, start_time, end_time);

  if (timeout_occurred) {
    SandboxedProcess::Result r;
    r.success = false;
    r.error_message = absl::StrCat("Wall-clock timeout exceeded (",
                                   kWallClockTimeoutSeconds, " seconds)");
    r.stats = stats;
    r.wall_clock_timeout = true;
    r.output_truncated = output_truncated;
    return r;
  }

  // If the output was truncated the child was killed by us (SIGKILL), so it
  // will have exited with a signal rather than exit code 0.  We treat this as
  // a non-success but with a dedicated flag so callers can distinguish it from
  // a genuine runtime error.
  bool success = !output_truncated &&
                 WIFEXITED(status) && WEXITSTATUS(status) == 0;
  SandboxedProcess::Result r;
  r.success = success;
  if (output_truncated) {
    r.error_message = absl::StrFormat(
        "Output truncated: combined stdout+stderr exceeded %zu KB limit",
        SandboxedProcess::kMaxOutputBytes / 1024);
  } else {
    r.error_message = success ? "" : "Process exited with non-zero status";
  }
  r.stats = stats;
  r.output_truncated = output_truncated;
  return r;
}

}  // namespace dcodex
