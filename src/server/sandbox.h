#pragma once

#include "src/server/process.h"
#include <string>
#include <memory>
#include <map>

namespace dcodex {

class LanguageStrategy {
public:
    virtual ~LanguageStrategy() = default;
    virtual std::string GetExtension() const = 0;
    virtual ExecutionResult Compile(const std::string& source_path, const std::string& binary_path, Process::OutputCallback callback) = 0;
    virtual ExecutionResult Run(const std::string& binary_path, Process::OutputCallback callback, const ResourceLimits& limits) = 0;
};

class CppStrategy : public LanguageStrategy {
public:
    std::string GetExtension() const override { return ".cpp"; }
    ExecutionResult Compile(const std::string& source_path, const std::string& binary_path, Process::OutputCallback callback) override;
    ExecutionResult Run(const std::string& binary_path, Process::OutputCallback callback, const ResourceLimits& limits) override;
};

class PythonStrategy : public LanguageStrategy {
public:
    std::string GetExtension() const override { return ".py"; }
    ExecutionResult Compile(const std::string& source_path, const std::string& binary_path, Process::OutputCallback callback) override;
    ExecutionResult Run(const std::string& binary_path, Process::OutputCallback callback, const ResourceLimits& limits) override;
};

class Sandbox {
public:
    using OutputCallback = Process::OutputCallback;

    static ExecutionResult Execute(const std::string& language, const std::string& code, OutputCallback callback);

private:
    static std::unique_ptr<LanguageStrategy> GetStrategy(const std::string& language);
};

} // namespace dcodex
