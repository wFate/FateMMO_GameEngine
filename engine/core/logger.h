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
#include <mutex>
#include <filesystem>
#include <system_error>

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

    // Initialize sinks. `jsonlFilePath` is optional — when non-empty, every
    // log record is also emitted as a JSON Lines record to that file (one
    // record per line). Useful for SIEM ingestion and structured ops queries.
    // Format: {"time":"...","level":"...","cat":"...","msg":"..."}
    // Rotation matches the text sink (5 MB × 3 files).
    void init(const std::string& logFilePath = "fate_engine.log",
              const std::string& jsonlFilePath = "") {
        if (initialized_) return;

        ensureLogParentDirectory(logFilePath);
        ensureLogParentDirectory(jsonlFilePath);

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

        std::vector<spdlog::sink_ptr> sinks = {consoleSink, fileSink, callbackSink_};

        if (!jsonlFilePath.empty()) {
            auto jsonlSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                jsonlFilePath, 5 * 1024 * 1024, 3);
            // Plain JSON-line: msg payload is escaped via spdlog's %v (not safe for
            // arbitrary embedded quotes/newlines, but adequate for our LOG_INFO format
            // strings which are sprintf-style and don't carry raw user input). For
            // user-string sites (chat, character names), the source-side LOG_INFO
            // should pre-escape via fmt::format — but in practice chat is censored
            // before logging (chat_handler.cpp:73) so this is low risk.
            jsonlSink->set_pattern(
                "{\"time\":\"%Y-%m-%dT%H:%M:%S.%e%z\",\"level\":\"%l\",\"cat\":\"%n\",\"msg\":\"%v\"}");
            sinks.push_back(jsonlSink);
        }

        defaultLogger_ = std::make_shared<spdlog::logger>("fate", sinks.begin(), sinks.end());
        defaultLogger_->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        defaultLogger_->set_level(spdlog::level::debug);
        // Flush on INFO so logs/fate_engine.log is tailable in real time
        // (smoke-pass copy-paste workflow, hot-reload swap traces, etc.).
        // INFO is not high-frequency on the editor side; the cost is a
        // per-line fflush which is dwarfed by spdlog's mutex + format work.
        defaultLogger_->flush_on(spdlog::level::info);
        spdlog::set_default_logger(defaultLogger_);

        spdlog::enable_backtrace(64);
        initialized_ = true;
    }

    void shutdown() {
        // Drop OUR owned loggers from the spdlog registry and reset our
        // shared_ptr handles. Do NOT call spdlog::shutdown() here — that is
        // a process-global destructor for spdlog's registry + thread pool,
        // and is unsafe when other call sites (engine/render/palette.cpp,
        // libpqxx adapters, etc.) still expect to call free spdlog::error()
        // later in the same process. spdlog::shutdown is for atexit-style
        // end-of-process cleanup; tests that re-init the logger MUST NOT
        // use it.
        //
        // drop_all() also clears spdlog's cached default-logger pointer.
        // After that, free `spdlog::error(...)` calls dereference a null
        // default_logger_raw() — SIGSEGV in any caller that didn't re-init.
        // Re-install a minimal stderr fallback default so the global free
        // logging API stays callable.
        spdlog::drop_all();
        try {
            auto fallback = std::make_shared<spdlog::logger>(
                "fate_fallback",
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            fallback->set_level(spdlog::level::warn);
            spdlog::set_default_logger(fallback);
        } catch (...) {
            // Best-effort: if even the fallback fails to construct (allocator
            // failure, etc.) leave spdlog as-is. A subsequent free call may
            // still segfault, but that is a strictly worse outcome we can't
            // prevent without re-init.
        }
        defaultLogger_.reset();
        callbackSink_.reset();
        logCallback_ = nullptr;
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

        std::shared_ptr<spdlog::logger> logger;
        {
            static std::mutex loggerMtx;
            std::lock_guard<std::mutex> lock(loggerMtx);
            logger = spdlog::get(category);
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

    static void ensureLogParentDirectory(const std::string& logFilePath) {
        if (logFilePath.empty()) return;

        const std::filesystem::path parent = std::filesystem::path(logFilePath).parent_path();
        if (parent.empty()) return;

        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

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
