#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace dcodex {

class SandboxedProcess {
public:
    struct ResourceStats {
        // Peak memory usage in bytes
        long peak_memory_bytes;
        // User CPU time in milliseconds
        long user_time_ms;
        // System CPU time in milliseconds
        long system_time_ms;
        // Total elapsed time in milliseconds
        long elapsed_time_ms;
    };

    struct Result {
        bool success;
        std::string error_message;
        ResourceStats stats;
    };

    using OutputCallback = std::function<void(const std::string& stdout_chunk, const std::string& stderr_chunk)>;

    static Result CompileAndRunStreaming(const std::string& code, OutputCallback callback);

private:
    static std::string WriteTempFile(const std::string& extension, const std::string& content);
    static Result ExecuteCommandStreaming(const std::vector<std::string>& argv, OutputCallback callback, bool sandboxed = false);
};

} // namespace dcodex