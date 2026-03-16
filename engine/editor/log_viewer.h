#pragma once
#include <string>
#include <vector>
#include <deque>
#include <mutex>

namespace fate {

struct LogEntry {
    std::string message;
    int level; // 0=debug, 1=info, 2=warn, 3=error, 4=fatal
};

// Captures log messages for display in the editor
class LogViewer {
public:
    static LogViewer& instance() {
        static LogViewer s;
        return s;
    }

    void addMessage(const std::string& msg, int level) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back({msg, level});
        if (entries_.size() > maxEntries_) entries_.pop_front();
        autoScroll_ = true;
    }

    void draw(); // ImGui panel
    void clear() { std::lock_guard<std::mutex> lock(mutex_); entries_.clear(); }

private:
    LogViewer() = default;
    std::deque<LogEntry> entries_;
    std::mutex mutex_;
    size_t maxEntries_ = 500;
    bool autoScroll_ = true;
    bool showDebug_ = false;
    bool showInfo_ = true;
    bool showWarn_ = true;
    bool showError_ = true;
    char filterBuf_[128] = "";
};

} // namespace fate
