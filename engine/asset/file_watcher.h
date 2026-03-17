#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fate {

class FileWatcher {
public:
    using Callback = std::function<void(const std::string& path)>;

    FileWatcher() = default;
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    void start(const std::string& directory, Callback onFileChanged);
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    std::jthread watchThread_;
    std::atomic<bool> running_{false};
    Callback callback_;
    std::string watchDir_;

#ifdef _WIN32
    HANDLE dirHandle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
#endif
};

} // namespace fate
