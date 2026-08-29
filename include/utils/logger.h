#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <windows.h>

namespace aura {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Init(const std::string& log_file_path = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        // Set console to UTF-8
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        
        if (!log_file_path.empty()) {
            file_stream_.open(log_file_path, std::ios::app);
        }
    }

    void Log(LogLevel level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt{};
        localtime_s(&bt, &timer);

        std::ostringstream oss;
        oss << "[" << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
            << LevelToString(level) << " " << msg << "\n";

        std::string out = oss.str();
        std::cout << out;
        std::cout.flush();

        if (file_stream_.is_open()) {
            file_stream_ << out;
            file_stream_.flush();
        }
    }

    void SetLogLevel(LogLevel level) {
        current_level_ = level;
    }

    bool ShouldLog(LogLevel level) const {
        return static_cast<int>(level) >= static_cast<int>(current_level_);
    }

private:
    Logger() : current_level_(LogLevel::Info) {}
    ~Logger() {
        if (file_stream_.is_open()) {
            file_stream_.close();
        }
    }

    const char* LevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug: return "[DEBUG]";
            case LogLevel::Info:  return "[INFO ]";
            case LogLevel::Warn:  return "[WARN ]";
            case LogLevel::Error: return "[ERROR]";
        }
        return "[UNKNOWN]";
    }

    std::mutex mutex_;
    LogLevel current_level_;
    std::ofstream file_stream_;
};

#define LOG_DEBUG(msg) do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Debug)) { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Debug, _oss.str()); } } while(0)
#define LOG_INFO(msg)  do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Info))  { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Info,  _oss.str()); } } while(0)
#define LOG_WARN(msg)  do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Warn))  { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Warn,  _oss.str()); } } while(0)
#define LOG_ERROR(msg) do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Error)) { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Error, _oss.str()); } } while(0)

} // namespace aura
