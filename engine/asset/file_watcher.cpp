#include "engine/asset/file_watcher.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <filesystem>

namespace fate {

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::start(const std::string& directory, Callback onFileChanged) {
    if (running_.load()) return;

    watchDir_ = directory;
    callback_ = std::move(onFileChanged);

#ifdef _WIN32
    // Convert UTF-8 to UTF-16 for Windows API
    int wlen = MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), -1, nullptr, 0);
    std::wstring wdir(wlen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), -1, wdir.data(), wlen);
    dirHandle_ = CreateFileW(
        wdir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (dirHandle_ == INVALID_HANDLE_VALUE) {
        LOG_ERROR("FileWatcher", "Failed to open directory: %s", directory.c_str());
        return;
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    running_.store(true);

    watchThread_ = std::jthread([this](std::stop_token) {
        while (running_.load()) {
            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            BOOL result = ReadDirectoryChangesW(
                dirHandle_,
                ioBuffer_,
                sizeof(ioBuffer_),
                TRUE, // recursive
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                nullptr,
                &overlapped,
                nullptr
            );

            if (!result) {
                CloseHandle(overlapped.hEvent);
                break;
            }

            HANDLE handles[] = { overlapped.hEvent, stopEvent_ };
            DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            if (waitResult != WAIT_OBJECT_0) {
                // Stop requested — cancel the pending I/O, then close
                // the directory handle while overlapped is still on the
                // stack so the kernel's cancellation write lands safely.
                CancelIo(dirHandle_);
                CloseHandle(dirHandle_);
                dirHandle_ = INVALID_HANDLE_VALUE;
                CloseHandle(overlapped.hEvent);
                break;
            }

            DWORD bytesReturned = 0;
            GetOverlappedResult(dirHandle_, &overlapped, &bytesReturned, FALSE);
            CloseHandle(overlapped.hEvent);

            if (bytesReturned == 0) continue;

            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ioBuffer_);
            while (true) {
                if (info->Action == FILE_ACTION_MODIFIED ||
                    info->Action == FILE_ACTION_ADDED) {
                    std::wstring wname(info->FileName, info->FileNameLength / sizeof(WCHAR));
                    int utf8len = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), (int)wname.size(), nullptr, 0, nullptr, nullptr);
                    std::string name(utf8len, 0);
                    WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), (int)wname.size(), name.data(), utf8len, nullptr, nullptr);
                    std::replace(name.begin(), name.end(), '\\', '/');

                    if (callback_) callback_(name);
                }

                if (info->NextEntryOffset == 0) break;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<char*>(info) + info->NextEntryOffset);
            }
        }
    });

    LOG_INFO("FileWatcher", "Watching: %s", directory.c_str());
#else
    LOG_WARN("FileWatcher", "File watching not implemented on this platform");
#endif
}

void FileWatcher::stop() {
    if (!running_.load()) return;
    running_.store(false);

#ifdef _WIN32
    LOG_DEBUG("FileWatcher", "stop: signaling event...");
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    LOG_DEBUG("FileWatcher", "stop: joining thread...");
    if (watchThread_.joinable()) {
        watchThread_.join();
    }
    LOG_DEBUG("FileWatcher", "stop: closing handles...");
    if (dirHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(dirHandle_);
        dirHandle_ = INVALID_HANDLE_VALUE;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    LOG_DEBUG("FileWatcher", "stop: handles closed");
#endif

    LOG_INFO("FileWatcher", "Stopped watching: %s", watchDir_.c_str());
}

} // namespace fate
