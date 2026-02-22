#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>

#include "src/server/execution_cache.h"

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
        // Indicates if result was served from cache
        bool cache_hit = false;
        // Cached stdout/stderr for cache hits
        std::string cached_stdout;
        std::string cached_stderr;
    };

    using OutputCallback = std::function<void(const std::string& stdout_chunk, const std::string& stderr_chunk)>;

    // Compile and run code with caching support
    // Returns cached result if available, otherwise executes and caches
    static Result CompileAndRunStreaming(const std::string& code, OutputCallback callback);

    // Access the global execution cache
    static ExecutionCache& GetCache();

    // Clear the execution cache
    static void ClearCache();

private:
    static std::string WriteTempFile(const std::string& extension, const std::string& content);
    static Result ExecuteCommandStreaming(const std::vector<std::string>& argv, OutputCallback callback, bool sandboxed = false);
    
    // Internal execution without caching
    static Result ExecuteWithoutCache(const std::string& code, OutputCallback callback);
};

} // namespace dcodex