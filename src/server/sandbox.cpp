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

// --- SRP: Output Filtering ---
class OutputFilterStrategy {
 public:
  virtual ~OutputFilterStrategy() = default;
  virtual bool ShouldSuppress(absl::string_view chunk) const = 0;
};

class DefaultOutputFilterStrategy final : public OutputFilterStrategy {
 public:
  bool ShouldSuppress(absl::string_view chunk) const override {
    if (chunk.find("rosetta error:") != absl::string_view::npos) {
      return true;
    }
    // Handle cases where the message might be slightly different or fragmented
    if (chunk.find("mmap_anonymous_rw mmap failed") != absl::string_view::npos) {
      return true;
    }
    return false;
  }
};

const OutputFilterStrategy& GetOutputFilter() {
  static DefaultOutputFilterStrategy filter;
  return filter;
}

// --- SRP: Resource Monitoring Constants ---
constexpr int kWallClockTimeoutSeconds = SandboxLimits::kWallClockTimeoutSeconds;

// --- SRP: File Management ---
class TempFileManager {
 public:
  static std::string WriteTempFile(absl::string_view extension,
                                   absl::string_view content) {
    char template_str[] = "/tmp/dcodex_XXXXXX";
    int fd_val = mkstemp(template_str);
    if (fd_val == -1) return "";
    close(fd_val);

    std::string path = absl::StrCat(template_str, extension);
    std::ofstream out(path);
    out << content;
    return path;
  }

  class Guard {
   public:
    explicit Guard(std::filesystem::path path) : path_(std::move(path)) {}
    ~Guard() {
      if (!path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
      }
    }
    void Release() { path_.clear(); }
   private:
    std::filesystem::path path_;
  };
};

// --- SRP: Process Management (RAII) ---
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

// --- SRP: Process Execution Logic ---
class ProcessRunner {
 public:
  static void ApplyResourceLimits() {
    struct rlimit cpu_limit{SandboxLimits::kCpuTimeLimitSeconds,
                            SandboxLimits::kCpuTimeLimitSeconds};
    setrlimit(RLIMIT_CPU, &cpu_limit);
    struct rlimit mem_limit{SandboxLimits::kMemoryLimitBytes,
                            SandboxLimits::kMemoryLimitBytes};
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

      struct timeval tv{0, 100000};
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
              if (GetOutputFilter().ShouldSuppress(chunk)) {
                return;
              }
              callback("", chunk);
            }
          }
        }
      };

      read_from(stdout_fd, true, stdout_open);
      read_from(stderr_fd, false, stderr_open);

      if (total_bytes >= SandboxLimits::kMaxOutputBytes) {
        kill(child_pid, SIGKILL);
        truncated = true;
        callback("", absl::StrFormat("\n[Output truncated: exceeded %zu KB limit]\n",
                                    SandboxLimits::kMaxOutputBytes / 1024));
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
};

// --- SRP: Global Cache Management ---
class CacheHolder {
 public:
  static ExecutionCache& Get() {
    static ExecutionCache cache;
    return cache;
  }
};

ResourceStats ComputeResourceStats(const struct rusage& usage, absl::Time start, absl::Time end) {
  ResourceStats stats;
#ifdef __APPLE__
  stats.peak_memory_bytes = usage.ru_maxrss;
#else
  stats.peak_memory_bytes = usage.ru_maxrss * 1024;
#endif
  stats.user_time_ms = usage.ru_utime.tv_sec * 1000 + usage.ru_utime.tv_usec / 1000;
  stats.system_time_ms = usage.ru_stime.tv_sec * 1000 + usage.ru_stime.tv_usec / 1000;
  stats.elapsed_time_ms = absl::ToInt64Milliseconds(end - start);
  return stats;
}

}  // namespace

// --- CppExecutionStrategy Implementation ---
ExecutionResult CppExecutionStrategy::Execute(absl::string_view code,
                                              absl::string_view stdin_data,
                                              OutputCallback callback) {
  std::string source_file = TempFileManager::WriteTempFile(".cpp", code);
  if (source_file.empty()) return {false, "Failed to create source file"};
  TempFileManager::Guard source_guard(source_file);

  std::string binary_path = source_file + ".bin";
  TempFileManager::Guard binary_guard(binary_path);

  // Use SandboxedProcess helper for command execution
  auto run_cmd = [&](const std::vector<std::string>& argv, absl::string_view input, bool sandboxed,
                     const OutputCallback& cmd_callback) {
    PipePair stdin_p, stdout_p, stderr_p;
    if (!stdin_p.Create() || !stdout_p.Create() || !stderr_p.Create()) return ExecutionResult{false, "Pipe creation failed"};

    absl::Time start = absl::Now();
    pid_t pid = fork();
    if (pid == 0) {
      ProcessRunner::RedirectAndExec(absl::MakeSpan(argv), stdin_p.ReadFd(), stdout_p.WriteFd(), stderr_p.WriteFd(), sandboxed);
    }

    stdin_p.CloseRead(); stdout_p.CloseWrite(); stderr_p.CloseWrite();
    if (!input.empty()) {
      write(stdin_p.WriteFd(), input.data(), input.size());
    }
    stdin_p.CloseWrite();

    pid_t watcher_pid = -1;
    if (sandboxed) {
      watcher_pid = fork();
      if (watcher_pid == 0) {
        for (int fd = 0 ; fd < 1024; ++fd) close(fd);
        sleep(kWallClockTimeoutSeconds);
        if (kill(pid, 0) == 0) kill(pid, SIGKILL);
        _exit(0);
      }
    }

    bool truncated = false;
    ProcessRunner::ReadOutput(stdout_p.ReadFd(), stderr_p.ReadFd(), pid, cmd_callback, truncated);

    int status = 0; struct rusage usage{};
    bool timed_out = false;
    if (sandboxed && watcher_pid > 0) timed_out = ProcessRunner::WaitWithTimeout(pid, watcher_pid, status, usage);
    else wait4(pid, &status, 0, &usage);

    ExecutionResult res;
    res.stats = ComputeResourceStats(usage, start, absl::Now());
    res.wall_clock_timeout = timed_out;
    res.output_truncated = truncated;
    res.success = !truncated && !timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (timed_out) res.error_message = "Wall-clock timeout exceeded";
    else if (truncated) res.error_message = "Output truncated";
    else if (!res.success) res.error_message = "Process exited with non-zero status";
    return res;
  };

  // Compile
  auto null_cb = [](absl::string_view, absl::string_view) {};
  ExecutionResult comp_res = run_cmd({"g++", "-std=c++17", source_file, "-o", binary_path}, "", false, null_cb);
  if (!comp_res.success) {
    // If compilation fails, we might want to see the errors, so we run it again with the real callback
    // or we could have just passed the real callback to begin with.
    // Given the user's request to fix the Rosetta error appearing in ALL examples,
    // and since compilation also runs via run_cmd, let's make sure run_cmd itself
    // handles the callback correctly.
    return run_cmd({"g++", "-std=c++17", source_file, "-o", binary_path}, "", false, callback);
  }

  // Run
  return run_cmd({binary_path}, stdin_data, true, callback);
}

// --- SandboxedProcess Implementation ---
ExecutionCache& SandboxedProcess::GetCache() { return CacheHolder::Get(); }
void SandboxedProcess::ClearCache() { CacheHolder::Get().Clear(); }

ExecutionResult SandboxedProcess::CompileAndRunStreaming(
    absl::string_view code, absl::string_view stdin_data,
    OutputCallback callback) {
  CppExecutionStrategy strategy;
  std::string cache_input = absl::StrCat(strategy.GetStrategyId(), ":", code, "\0", stdin_data);
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

  ExecutionResult result = strategy.Execute(code, stdin_data, wrapped_cb);

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
