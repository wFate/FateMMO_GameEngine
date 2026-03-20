#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <string>
#include <memory>
#include <functional>
#include <cstdio>
#include <cstdarg>

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
        if (initialized_) return;

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath, 5 * 1024 * 1024, 3);

        callbackSink_ = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [this](const spdlog::details::log_msg& msg) {
                if (logCallback_) {
                    std::string text(msg.payload.data(), msg.payload.size());
                    std::string formatted = fmt::format("[{}] {}\n",
                        spdlog::level::to_string_view(msg.level), text);
                    logCallback_(formatted, static_cast<int>(msg.level));
                }
            });

        spdlog::sinks_init_list sinks = {consoleSink, fileSink, callbackSink_};
        defaultLogger_ = std::make_shared<spdlog::logger>("fate", sinks);
        defaultLogger_->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        defaultLogger_->set_level(spdlog::level::debug);
        defaultLogger_->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(defaultLogger_);

        spdlog::enable_backtrace(64);
        initialized_ = true;
    }

    void shutdown() {
        spdlog::shutdown();
        initialized_ = false;
    }

    void setMinLevel(LogLevel level) {
        spdlog::level::level_enum spdLevel;
        switch (level) {
            case LogLevel::Debug: spdLevel = spdlog::level::debug; break;
            case LogLevel::Info:  spdLevel = spdlog::level::info; break;
            case LogLevel::Warn:  spdLevel = spdlog::level::warn; break;
            case LogLevel::Error: spdLevel = spdlog::level::err; break;
            case LogLevel::Fatal: spdLevel = spdlog::level::critical; break;
            default: spdLevel = spdlog::level::debug; break;
        }
        spdlog::set_level(spdLevel);
    }

    void log(LogLevel level, const char* category, const char* fmt, ...) {
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        auto logger = spdlog::get(category);
        if (!logger) {
            if (defaultLogger_) {
                logger = std::make_shared<spdlog::logger>(
                    category,
                    defaultLogger_->sinks().begin(),
                    defaultLogger_->sinks().end());
                logger->set_level(defaultLogger_->level());
                logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
                spdlog::register_logger(logger);
            } else {
                fprintf(stderr, "[%s] %s\n", category, buffer);
                return;
            }
        }

        switch (level) {
            case LogLevel::Debug: logger->debug("{}", buffer); break;
            case LogLevel::Info:  logger->info("{}", buffer); break;
            case LogLevel::Warn:  logger->warn("{}", buffer); break;
            case LogLevel::Error: logger->error("{}", buffer); break;
            case LogLevel::Fatal: logger->critical("{}", buffer); break;
        }
    }

    using LogCallback = std::function<void(const std::string&, int)>;
    void setLogCallback(LogCallback cb) { logCallback_ = std::move(cb); }

private:
    Logger() = default;
    bool initialized_ = false;
    std::shared_ptr<spdlog::logger> defaultLogger_;
    std::shared_ptr<spdlog::sinks::callback_sink_mt> callbackSink_;
    LogCallback logCallback_;
};

#define LOG_DEBUG(cat, ...) fate::Logger::instance().log(fate::LogLevel::Debug, cat, __VA_ARGS__)
#define LOG_INFO(cat, ...)  fate::Logger::instance().log(fate::LogLevel::Info,  cat, __VA_ARGS__)
#define LOG_WARN(cat, ...)  fate::Logger::instance().log(fate::LogLevel::Warn,  cat, __VA_ARGS__)
#define LOG_ERROR(cat, ...) fate::Logger::instance().log(fate::LogLevel::Error, cat, __VA_ARGS__)
#define LOG_FATAL(cat, ...) fate::Logger::instance().log(fate::LogLevel::Fatal, cat, __VA_ARGS__)

} // namespace fate
