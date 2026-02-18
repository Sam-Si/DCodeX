#pragma once

#include <string>
#include <vector>
#include <functional>

namespace dcodex {

class SandboxedProcess {
public:
    struct Result {
        bool success;
        std::string error_message;
    };

    using OutputCallback = std::function<void(const std::string& stdout_chunk, const std::string& stderr_chunk)>;

    static Result CompileAndRunStreaming(const std::string& code, OutputCallback callback);

private:
    static std::string WriteTempFile(const std::string& extension, const std::string& content);
    static Result ExecuteCommandStreaming(const std::vector<std::string>& argv, OutputCallback callback, bool sandboxed = false);
};

} // namespace dcodex