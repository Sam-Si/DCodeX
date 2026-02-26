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
[[nodiscard]] bool ReadFromPipe(
    int fd, std::array<char, 4096>& buffer,
    const SandboxedProcess::OutputCallback& callback, bool is_stdout) {
  ssize_t bytes = read(fd, buffer.data(), buffer.size());
  if (bytes > 0) {
    if (is_stdout) {
      callback(absl::string_view(buffer.data(), bytes), "");
    } else {
      callback("", absl::string_view(buffer.data(), bytes));
    }
    return true;
  }
  return false;
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
    absl::string_view code, OutputCallback callback) {
  // Computes hash of the code.
  auto hash_result = ExecutionCache::ComputeHash(code);
  if (!hash_result.ok()) {
    // Hash computation failed, executes without caching.
    return ExecuteWithoutCache(code, callback);
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
      ExecuteWithoutCache(code, wrapping_callback);

  // Stores in cache if execution was successful.
  if (result.success) {
    StoreCacheResult(code_hash, result, buffer);
  }

  return result;
}

SandboxedProcess::Result SandboxedProcess::ExecuteWithoutCache(
    absl::string_view code, OutputCallback callback) {
  std::string source_file = WriteTempFile(".cpp", code);
  if (source_file.empty()) {
    return {false, "Failed to create source file", {}};
  }

  TempFile source_guard(source_file);
  std::filesystem::path binary_path = source_file + ".bin";
  absl::Cleanup binary_cleanup = [&binary_path] {
    std::error_code ec;
    std::filesystem::remove(binary_path, ec);
  };

  // Compilation step (not sandboxed, captures output via callback).
  absl::InlinedVector<std::string, 4> compile_args = {
      "g++", "-std=c++17", source_file, "-o", binary_path.string()};
  Result compile_result = ExecuteCommandStreaming(
      absl::MakeSpan(compile_args), callback, false);
  if (!compile_result.success) {
    return compile_result;
  }

  // Execution step (sandboxed).
  absl::InlinedVector<std::string, 2> run_args = {binary_path.string()};
  Result run_result =
      ExecuteCommandStreaming(absl::MakeSpan(run_args), callback, true);

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
void ExecuteChildProcess(absl::Span<const std::string> argv,
                         int stdout_write_fd, int stderr_write_fd,
                         bool sandboxed) {
  if (sandboxed) {
    ApplyResourceLimits();
  }

  RedirectFileDescriptors(stdout_write_fd, stderr_write_fd);

  auto c_argv = PrepareArgv(argv);
  execvp(c_argv[0], c_argv.data());
  exit(1);
}

// Handles reading output from child process pipes.
void ReadProcessOutput(int stdout_read_fd, int stderr_read_fd,
                       const SandboxedProcess::OutputCallback& callback) {
  std::array<char, 4096> buffer{};
  bool stdout_open = true;
  bool stderr_open = true;

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

    int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
    if (activity < 0) break;

    if (stdout_open && FD_ISSET(stdout_read_fd, &read_fds)) {
      stdout_open = ReadFromPipe(stdout_read_fd, buffer, callback, true);
    }
    if (stderr_open && FD_ISSET(stderr_read_fd, &read_fds)) {
      stderr_open = ReadFromPipe(stderr_read_fd, buffer, callback, false);
    }
  }
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

SandboxedProcess::Result SandboxedProcess::ExecuteCommandStreaming(
    absl::Span<const std::string> argv, OutputCallback callback,
    bool sandboxed) {
  PipePair stdout_pipe;
  PipePair stderr_pipe;

  if (!stdout_pipe.Create() || !stderr_pipe.Create()) {
    return {false, "Failed to create pipes", {}};
  }

  absl::Time start_time = absl::Now();

  pid_t pid = fork();
  if (pid == -1) {
    return {false, "Failed to fork", {}};
  }

  if (pid == 0) {
    // Child process - use raw fds since we're about to exec
    ExecuteChildProcess(argv, stdout_pipe.WriteFd(), stderr_pipe.WriteFd(),
                        sandboxed);
  } else {
    // Parent process - close write ends and read output
    stdout_pipe.CloseWrite();
    stderr_pipe.CloseWrite();

    ReadProcessOutput(stdout_pipe.ReadFd(), stderr_pipe.ReadFd(), callback);
    return WaitForChildProcess(pid, start_time);
  }
}

}  // namespace dcodex