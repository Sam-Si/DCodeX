#include "src/server/process.h"
#include "src/server/logger.h"

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <fcntl.h>
#include <filesystem>
#include <cstring>
#include <random>

namespace dcodex {

namespace fs = std::filesystem;

std::string Process::CreateTempDirectory() {
    char template_str[] = "/tmp/dcodex_run_XXXXXX";
    char* path = mkdtemp(template_str);
    if (!path) {
        Logger::Error("Failed to create temporary directory: ", strerror(errno));
        return "";
    }
    Logger::Info("Created temporary directory: ", path);
    return std::string(path);
}

bool Process::RemoveDirectory(const std::string& path) {
    try {
        if (fs::exists(path)) {
            fs::remove_all(path);
            Logger::Info("Removed directory: ", path);
            return true;
        }
    } catch (const fs::filesystem_error& e) {
        Logger::Error("Failed to remove directory: ", path, " - ", e.what());
    }
    return false;
}

bool Process::WriteFile(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    if (!out) {
        Logger::Error("Failed to open file for writing: ", path);
        return false;
    }
    out << content;
    out.close();
    return true;
}

ExecutionResult Process::Run(const std::vector<std::string>& argv, 
                            OutputCallback callback, 
                            bool sandboxed, 
                            std::optional<ResourceLimits> limits) {
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        Logger::Error("Failed to create pipes: ", strerror(errno));
        return {false, -1, "Failed to create pipes"};
    }

    pid_t pid = fork();
    if (pid == -1) {
        Logger::Error("Failed to fork: ", strerror(errno));
        return {false, -1, "Failed to fork"};
    }

    if (pid == 0) {
        // Child process
        if (sandboxed) {
            ResourceLimits res_limits = limits.value_or(ResourceLimits{});
            
            struct rlimit cpu_limit;
            cpu_limit.rlim_cur = res_limits.cpu_time_seconds;
            cpu_limit.rlim_max = res_limits.cpu_time_seconds + 1; // Grace period
            setrlimit(RLIMIT_CPU, &cpu_limit);

            struct rlimit mem_limit;
            mem_limit.rlim_cur = res_limits.memory_bytes;
            mem_limit.rlim_max = res_limits.memory_bytes;
            setrlimit(RLIMIT_AS, &mem_limit);

            // RLIMIT_NPROC: Limit number of processes
            // Note: On macOS, Python shims like 'pyenv' can fail with very low NPROC limits.
            // We enable this strictly for Linux (Debian/Ubuntu) where it's more reliable.
            #ifdef __linux__
            struct rlimit proc_limit;
            proc_limit.rlim_cur = 250;
            proc_limit.rlim_max = 250;
            setrlimit(RLIMIT_NPROC, &proc_limit);
            #endif


            // TODO: In a real environment, chroot or advanced sandboxing (like nsjail) would go here.
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
        std::cerr << "Failed to exec: " << strerror(errno) << std::endl;
        exit(127); // Standard exit code for command not found
    } else {
        // Parent process
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

            struct timeval timeout;
            timeout.tv_sec = 5; // Timeout to prevent hang
            timeout.tv_usec = 0;

            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (activity < 0) {
                if (errno == EINTR) continue;
                Logger::Error("Select error: ", strerror(errno));
                break;
            } else if (activity == 0) {
                // Timeout, process might be hung but giving no output. Check if it's dead.
                int status;
                pid_t result = waitpid(pid, &status, WNOHANG);
                if (result == pid) {
                   break; // Process exited
                }
                continue; 
            }

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
        waitpid(pid, &status, 0);

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        
        return {success, exit_code, success ? "" : "Process exited with non-zero status " + std::to_string(exit_code)};
    }
}

} // namespace dcodex
