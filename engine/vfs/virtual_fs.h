/**************************************************************************/
/*  virtual_fs.h                                                          */
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
#include <vector>
#include <optional>
#include <cstdint>

class VirtualFS {
public:
    VirtualFS() = default;
    ~VirtualFS();

    // Non-copyable, movable
    VirtualFS(const VirtualFS&) = delete;
    VirtualFS& operator=(const VirtualFS&) = delete;
    VirtualFS(VirtualFS&& other) noexcept;
    VirtualFS& operator=(VirtualFS&& other) noexcept;

    bool init(const char* appName);
    void shutdown();

    // Mount a directory or archive at the given virtual mount point.
    // appendToSearchPath: if true, appended to end of search path (lower priority).
    //                     if false, prepended (higher priority / overlay).
    bool mount(const std::string& path, const std::string& mountPoint,
               bool appendToSearchPath = true);

    [[nodiscard]] std::optional<std::vector<uint8_t>> readFile(const std::string& path) const;
    [[nodiscard]] std::optional<std::string> readText(const std::string& path) const;
    [[nodiscard]] bool exists(const std::string& path) const;
    [[nodiscard]] std::vector<std::string> listDir(const std::string& dir) const;

private:
    bool initialized_ = false;
};
