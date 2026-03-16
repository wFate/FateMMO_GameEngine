#pragma once
#include <string>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <mutex>
#include <chrono>

namespace fate {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

class Logger {
public:
    static Logger& instance() {
        static Logger s_instance;
        return s_instance;
    }

    void init(const std::string& logFilePath = "fate_engine.log") {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        logFile_.open(logFilePath, std::ios::out | std::ios::trunc);
        initialized_ = true;
        log(LogLevel::Info, "Logger", "FateEngine logger initialized");
    }

    void shutdown() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (logFile_.is_open()) logFile_.close();
        initialized_ = false;
    }

    void setMinLevel(LogLevel level) { minLevel_ = level; }

    void log(LogLevel level, const char* category, const char* fmt, ...) {
        if (level < minLevel_) return;

        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        const char* levelStr = "";
        switch (level) {
            case LogLevel::Debug: levelStr = "DEBUG"; break;
            case LogLevel::Info:  levelStr = "INFO "; break;
            case LogLevel::Warn:  levelStr = "WARN "; break;
            case LogLevel::Error: levelStr = "ERROR"; break;
            case LogLevel::Fatal: levelStr = "FATAL"; break;
        }

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", std::localtime(&time));

        char fullMsg[2200];
        snprintf(fullMsg, sizeof(fullMsg), "[%s.%03d] [%s] [%s] %s\n",
                 timestamp, (int)ms.count(), levelStr, category, buffer);

        std::lock_guard<std::recursive_mutex> lock(mutex_);
        fprintf(stdout, "%s", fullMsg);
        fflush(stdout);
        if (logFile_.is_open()) {
            logFile_ << fullMsg;
            logFile_.flush();
        }
    }

private:
    Logger() = default;
    std::ofstream logFile_;
    std::recursive_mutex mutex_;
    LogLevel minLevel_ = LogLevel::Debug;
    bool initialized_ = false;
};

// Convenience macros - category is always the first arg
#define LOG_DEBUG(cat, ...) fate::Logger::instance().log(fate::LogLevel::Debug, cat, __VA_ARGS__)
#define LOG_INFO(cat, ...)  fate::Logger::instance().log(fate::LogLevel::Info,  cat, __VA_ARGS__)
#define LOG_WARN(cat, ...)  fate::Logger::instance().log(fate::LogLevel::Warn,  cat, __VA_ARGS__)
#define LOG_ERROR(cat, ...) fate::Logger::instance().log(fate::LogLevel::Error, cat, __VA_ARGS__)
#define LOG_FATAL(cat, ...) fate::Logger::instance().log(fate::LogLevel::Fatal, cat, __VA_ARGS__)

} // namespace fate
