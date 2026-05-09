/**************************************************************************/
/*  engine_error.h                                                        */
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
#include <expected>
#include <cstdint>

namespace fate {

enum class ErrorCategory : uint8_t {
    Transient   = 0, // retry likely succeeds (timeout, busy)
    Recoverable = 1, // can queue and degrade (DB down)
    Degraded    = 2, // subsystem offline
    Fatal       = 3  // unrecoverable
};

struct EngineError {
    ErrorCategory category = ErrorCategory::Transient;
    uint16_t code = 0;
    std::string message;
};

template<typename T>
using Result = std::expected<T, EngineError>;

inline EngineError transientError(uint16_t code, std::string msg) {
    return {ErrorCategory::Transient, code, std::move(msg)};
}
inline EngineError recoverableError(uint16_t code, std::string msg) {
    return {ErrorCategory::Recoverable, code, std::move(msg)};
}
inline EngineError fatalError(uint16_t code, std::string msg) {
    return {ErrorCategory::Fatal, code, std::move(msg)};
}

} // namespace fate
