#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>

namespace dcodex {

struct ResourceLimits {
    unsigned long cpu_time_seconds = 2; // Default 2 seconds
    unsigned long memory_bytes = 50 * 1024 * 1024; // Default 50MB
    unsigned long max_processes = 1; // Prevent fork bombs
};

struct ExecutionResult {
    bool success;
    int exit_code;
    std::string error_message;
};

class Process {
public:
    using OutputCallback = std::function<void(const std::string& stdout_chunk, const std::string& stderr_chunk)>;

    static std::string CreateTempDirectory();
    static bool RemoveDirectory(const std::string& path);
    static bool WriteFile(const std::string& path, const std::string& content);

    // Runs a command in a subprocess, streaming output via callback.
    // If sandboxed is true, resource limits are applied.
    static ExecutionResult Run(const std::vector<std::string>& argv, 
                               OutputCallback callback, 
                               bool sandboxed = false, 
                               std::optional<ResourceLimits> limits = std::nullopt);
};

} // namespace dcodex
