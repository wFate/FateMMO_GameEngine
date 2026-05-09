/**************************************************************************/
/*  command_buffer.h                                                      */
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
#include <vector>
#include <functional>
#include <cstddef>

namespace fate {

// ==========================================================================
// CommandBuffer — deferred structural changes during iteration
//
// Queue commands (entity add/remove/migrate) while iterating archetypes,
// then execute all at once after iteration completes.
// ==========================================================================
class CommandBuffer {
public:
    void push(std::function<void()> cmd) {
        commands_.push_back(std::move(cmd));
    }

    void execute() {
        for (auto& cmd : commands_) {
            cmd();
        }
        commands_.clear();
    }

    bool empty() const { return commands_.empty(); }
    size_t size() const { return commands_.size(); }

private:
    std::vector<std::function<void()>> commands_;
};

} // namespace fate
