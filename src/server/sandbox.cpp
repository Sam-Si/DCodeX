#include "src/server/sandbox.h"
#include "src/server/logger.h"
#include "src/server/process.h"

#include <iostream>
#include <filesystem>
#include <map>
#include <memory>

namespace dcodex {

namespace fs = std::filesystem;

// --- C++ Strategy ---
ExecutionResult CppStrategy::Compile(const std::string& source_path, const std::string& binary_path, Process::OutputCallback callback) {
    Logger::Info("Compiling C++: ", source_path, " -> ", binary_path);
    return Process::Run(
        {"g++", "-std=c++17", source_path, "-o", binary_path}, 
        callback, 
        false // Not sandboxed for compilation
    );
}

ExecutionResult CppStrategy::Run(const std::string& binary_path, Process::OutputCallback callback, const ResourceLimits& limits) {
    Logger::Info("Running C++ Binary: ", binary_path);
    return Process::Run(
        {binary_path}, 
        callback, 
        true, // Sandboxed!
        limits
    );
}

// --- Python Strategy ---
ExecutionResult PythonStrategy::Compile(const std::string& source_path, const std::string& binary_path, Process::OutputCallback callback) {
    // Syntax check
    Logger::Info("Checking Python syntax: ", source_path);
    return Process::Run(
        {"python3", "-m", "py_compile", source_path}, 
        callback,
        false
    );
}

ExecutionResult PythonStrategy::Run(const std::string& binary_path, Process::OutputCallback callback, const ResourceLimits& limits) {
    // binary_path here is actually the source path (see Sandbox::Execute logic below)
    Logger::Info("Running Python Script: ", binary_path);
    return Process::Run(
        {"python3", binary_path}, 
        callback, 
        true, 
        limits
    );
}

// --- Sandbox ---

std::unique_ptr<LanguageStrategy> Sandbox::GetStrategy(const std::string& language) {
    if (language == "cpp") {
        return std::make_unique<CppStrategy>();
    } else if (language == "python") {
        return std::make_unique<PythonStrategy>();
    }
    return nullptr;
}

ExecutionResult Sandbox::Execute(const std::string& language, const std::string& code, OutputCallback callback) {
    auto strategy = GetStrategy(language);
    if (!strategy) {
        Logger::Error("Unsupported language: ", language);
        return {false, -1, "Unsupported language: " + language};
    }

    std::string temp_dir = Process::CreateTempDirectory();
    if (temp_dir.empty()) {
        return {false, -1, "Failed to create temp directory"};
    }

    // Source file path
    std::string source_path = temp_dir + "/Main" + strategy->GetExtension();
    if (!Process::WriteFile(source_path, code)) {
        Process::RemoveDirectory(temp_dir);
        return {false, -1, "Failed to write source file"};
    }

    // Determine Binary/Run Path
    std::string binary_path;
    if (language == "cpp") {
        binary_path = temp_dir + "/Main.bin";
    } else {
        // For interpreted languages, the "binary" to run is the source itself (or passed to interpreter)
        binary_path = source_path;
    }

    // Compilation Step
    ExecutionResult compile_result = strategy->Compile(source_path, binary_path, callback);
    if (!compile_result.success) {
        Logger::Warn("Compilation failed for ", language);
        Process::RemoveDirectory(temp_dir);
        return compile_result;
    }

    // Execution Step
    ResourceLimits limits; // Default limits
    // Use slightly tighter limits for untrusted code
    limits.cpu_time_seconds = 5;
    limits.memory_bytes = 100 * 1024 * 1024; // 100MB

    ExecutionResult run_result = strategy->Run(binary_path, callback, limits);

    // Cleanup
    Process::RemoveDirectory(temp_dir);
    return run_result;
}

} // namespace dcodex