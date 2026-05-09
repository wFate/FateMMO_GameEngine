/**************************************************************************/
/*  file_watcher.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
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
    alignas(8) char ioBuffer_[4096] = {};
#endif
};

} // namespace fate
