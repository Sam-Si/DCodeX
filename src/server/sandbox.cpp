#include "src/server/sandbox.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/time.h>


namespace dcodex {

namespace {

// Global cache instance
class CacheHolder {
public:
    static ExecutionCache& Get() {
        static ExecutionCache cache;
        return cache;
    }
};

// Buffer to capture output for caching
class OutputBuffer {
public:
    void AppendStdout(const std::string& data) {
        stdout_ += data;
    }
    
    void AppendStderr(const std::string& data) {
        stderr_ += data;
    }
    
    const std::string& GetStdout() const { return stdout_; }
    const std::string& GetStderr() const { return stderr_; }
    
private:
    std::string stdout_;
    std::string stderr_;
};

}  // namespace

ExecutionCache& SandboxedProcess::GetCache() {
    return CacheHolder::Get();
}

void SandboxedProcess::ClearCache() {
    CacheHolder::Get().Clear();
}

SandboxedProcess::Result SandboxedProcess::CompileAndRunStreaming(
    const std::string& code, OutputCallback callback) {
    
    // Compute hash of the code
    std::string code_hash = ExecutionCache::ComputeHash(code);
    if (code_hash.empty()) {
        // Hash computation failed, execute without caching
        return ExecuteWithoutCache(code, callback);
    }
    
    // Check cache first
    auto cached = GetCache().Get(code_hash);
    if (cached != nullptr) {
        // Cache hit - replay the output
        if (!cached->stdout_output.empty()) {
            callback(cached->stdout_output, "");
        }
        if (!cached->stderr_output.empty()) {
            callback("", cached->stderr_output);
        }
        
        Result result;
        result.success = cached->success;
        result.error_message = cached->error_message;
        result.cache_hit = true;
        result.cached_stdout = cached->stdout_output;
        result.cached_stderr = cached->stderr_output;
        result.stats.peak_memory_bytes = cached->peak_memory_bytes;
        result.stats.elapsed_time_ms = static_cast<long>(cached->execution_time_ms);
        result.stats.user_time_ms = 0;
        result.stats.system_time_ms = 0;
        return result;
    }
    
    // Cache miss - execute and store result
    OutputBuffer buffer;
    
    auto wrapping_callback = [&callback, &buffer](const std::string& stdout_chunk, 
                                                   const std::string& stderr_chunk) {
        if (!stdout_chunk.empty()) {
            buffer.AppendStdout(stdout_chunk);
            callback(stdout_chunk, "");
        }
        if (!stderr_chunk.empty()) {
            buffer.AppendStderr(stderr_chunk);
            callback("", stderr_chunk);
        }
    };
    
    Result result = ExecuteWithoutCache(code, wrapping_callback);
    
    // Store in cache if execution was successful
    if (result.success) {
        CachedResult cached_result;
        cached_result.stdout_output = buffer.GetStdout();
        cached_result.stderr_output = buffer.GetStderr();
        cached_result.peak_memory_bytes = result.stats.peak_memory_bytes;
        cached_result.execution_time_ms = static_cast<float>(result.stats.elapsed_time_ms);
        cached_result.success = result.success;
        cached_result.error_message = result.error_message;
        
        GetCache().Put(code_hash, cached_result);
    }
    
    return result;
}

SandboxedProcess::Result SandboxedProcess::ExecuteWithoutCache(
    const std::string& code, OutputCallback callback) {
    
    std::string source_file = WriteTempFile(".cpp", code);
    std::string binary_file = source_file + ".bin";

    // Compilation step (not sandboxed, captures output via callback)
    Result compile_result = ExecuteCommandStreaming(
        {"g++", "-std=c++17", source_file, "-o", binary_file}, callback, false);
    if (!compile_result.success) {
        unlink(source_file.c_str());
        return compile_result;
    }

    // Execution step (sandboxed)
    Result run_result = ExecuteCommandStreaming({binary_file}, callback, true);

    // Cleanup
    unlink(source_file.c_str());
    unlink(binary_file.c_str());

    return run_result;
}

std::string SandboxedProcess::WriteTempFile(const std::string& extension, const std::string& content) {
    char template_str[] = "/tmp/dcodex_XXXXXX";
    int fd = mkstemp(template_str);
    if (fd == -1) return "";
    close(fd);

    std::string path = std::string(template_str) + extension;
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

SandboxedProcess::Result SandboxedProcess::ExecuteCommandStreaming(const std::vector<std::string>& argv, OutputCallback callback, bool sandboxed) {
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        return {false, "Failed to create pipes", {}};
    }

    // Record start time
    struct timeval start_time;
    gettimeofday(&start_time, nullptr);

    pid_t pid = fork();
    if (pid == -1) {
        return {false, "Failed to fork", {}};
    }

    if (pid == 0) {
        if (sandboxed) {
            struct rlimit cpu_limit;
            cpu_limit.rlim_cur = 2;
            cpu_limit.rlim_max = 2;
            setrlimit(RLIMIT_CPU, &cpu_limit);

            struct rlimit mem_limit;
            mem_limit.rlim_cur = 50 * 1024 * 1024;
            mem_limit.rlim_max = 50 * 1024 * 1024;
            setrlimit(RLIMIT_AS, &mem_limit);
        }

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        std::vector<char*> c_argv;
        for (const auto& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);

        execvp(c_argv[0], c_argv.data());
        exit(1);
    } else {
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        char buffer[4096];
        bool stdout_open = true;
        bool stderr_open = true;

        while (stdout_open || stderr_open) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            int max_fd = -1;

            if (stdout_open) {
                FD_SET(stdout_pipe[0], &read_fds);
                max_fd = std::max(max_fd, stdout_pipe[0]);
            }
            if (stderr_open) {
                FD_SET(stderr_pipe[0], &read_fds);
                max_fd = std::max(max_fd, stderr_pipe[0]);
            }

            if (max_fd == -1) break;

            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
            if (activity < 0) break;

            if (stdout_open && FD_ISSET(stdout_pipe[0], &read_fds)) {
                ssize_t bytes = read(stdout_pipe[0], buffer, sizeof(buffer));
                if (bytes > 0) {
                    callback(std::string(buffer, bytes), "");
                } else {
                    stdout_open = false;
                }
            }
            if (stderr_open && FD_ISSET(stderr_pipe[0], &read_fds)) {
                ssize_t bytes = read(stderr_pipe[0], buffer, sizeof(buffer));
                if (bytes > 0) {
                    callback("", std::string(buffer, bytes));
                } else {
                    stderr_open = false;
                }
            }
        }

        int status;
        struct rusage usage;
        // Use wait4 to get resource usage for this specific child process
        pid_t result = wait4(pid, &status, 0, &usage);
        
        if (result == -1) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return {false, "Failed to wait for child process", {}};
        }

        // Record end time
        struct timeval end_time;
        gettimeofday(&end_time, nullptr);

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        
        // Calculate resource stats
        ResourceStats stats;
        // Peak memory: ru_maxrss is in KB on Linux, bytes on macOS
        #ifdef __APPLE__
            stats.peak_memory_bytes = usage.ru_maxrss;
        #else
            stats.peak_memory_bytes = usage.ru_maxrss * 1024;
        #endif
        // User time in milliseconds
        stats.user_time_ms = usage.ru_utime.tv_sec * 1000 + usage.ru_utime.tv_usec / 1000;
        // System time in milliseconds
        stats.system_time_ms = usage.ru_stime.tv_sec * 1000 + usage.ru_stime.tv_usec / 1000;
        // Elapsed time in milliseconds
        long elapsed_sec = end_time.tv_sec - start_time.tv_sec;
        long elapsed_usec = end_time.tv_usec - start_time.tv_usec;
        stats.elapsed_time_ms = elapsed_sec * 1000 + elapsed_usec / 1000;

        return {success, success ? "" : "Process exited with non-zero status", stats};
    }
}

} // namespace dcodex