#include "src/server/sandbox.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <fcntl.h>


namespace dcodex {

SandboxedProcess::Result SandboxedProcess::CompileAndRunStreaming(const std::string& code, OutputCallback callback) {
    std::string source_file = WriteTempFile(".cpp", code);
    std::string binary_file = source_file + ".bin";

    // Compilation step (not sandboxed, captures output via callback)
    Result compile_result = ExecuteCommandStreaming({"g++", "-std=c++17", source_file, "-o", binary_file}, callback, false);
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
        return {false, "Failed to create pipes"};
    }

    pid_t pid = fork();
    if (pid == -1) {
        return {false, "Failed to fork"};
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
        waitpid(pid, &status, 0);

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        return {success, success ? "" : "Process exited with non-zero status"};
    }
}

} // namespace dcodex