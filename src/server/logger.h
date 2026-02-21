#pragma once

#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace dcodex {

enum class LogLevel {
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static void Log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::cout << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "] ";
        
        switch (level) {
            case LogLevel::INFO: std::cout << "[INFO] "; break;
            case LogLevel::WARNING: std::cout << "[WARN] "; break;
            case LogLevel::ERROR: std::cerr << "[ERROR] "; break;
        }
        
        if (level == LogLevel::ERROR) {
            std::cerr << message << std::endl;
        } else {
            std::cout << message << std::endl;
        }
    }
    
    template<typename... Args>
    static void Info(Args... args) {
        std::stringstream ss;
        (ss << ... << args);
        Log(LogLevel::INFO, ss.str());
    }

    template<typename... Args>
    static void Warn(Args... args) {
        std::stringstream ss;
        (ss << ... << args);
        Log(LogLevel::WARNING, ss.str());
    }

    template<typename... Args>
    static void Error(Args... args) {
        std::stringstream ss;
        (ss << ... << args);
        Log(LogLevel::ERROR, ss.str());
    }

private:
    static std::mutex mutex_;
};

inline std::mutex Logger::mutex_;

} // namespace dcodex
