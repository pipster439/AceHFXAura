#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <cstdio>
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
            log_file_path_ = log_file_path;
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
            RotateLogIfNeeded();
            file_stream_ << out;
            file_stream_.flush();
        }
    }

    // 必须在启动工作线程前调用（单线程初始化阶段）
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

    // 日志轮转：close -> 删最旧 -> .2->.3, .1->.2, log->.1 -> 重开
    void RotateLogIfNeeded() {
        if (!file_stream_.is_open() || log_file_path_.empty()) return;
        auto pos = file_stream_.tellp();
        if (pos < 0 || static_cast<size_t>(pos) < max_bytes_) return;

        file_stream_.close();

        std::string log3 = log_file_path_ + ".3";
        std::string log2 = log_file_path_ + ".2";
        std::string log1 = log_file_path_ + ".1";

        std::remove(log3.c_str());
        std::rename(log2.c_str(), log3.c_str());
        std::rename(log1.c_str(), log2.c_str());
        std::rename(log_file_path_.c_str(), log1.c_str());

        file_stream_.open(log_file_path_, std::ios::app);
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
    std::string log_file_path_;
    size_t max_bytes_{8 * 1024 * 1024}; // 默认 8MB
    size_t rotate_keep_{3};             // 默认保留 3 份历史日志
};

#define LOG_DEBUG(msg) do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Debug)) { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Debug, _oss.str()); } } while(0)
#define LOG_INFO(msg)  do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Info))  { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Info,  _oss.str()); } } while(0)
#define LOG_WARN(msg)  do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Warn))  { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Warn,  _oss.str()); } } while(0)
#define LOG_ERROR(msg) do { if (aura::Logger::Instance().ShouldLog(aura::LogLevel::Error)) { std::ostringstream _oss; _oss << msg; aura::Logger::Instance().Log(aura::LogLevel::Error, _oss.str()); } } while(0)

} // namespace aura
