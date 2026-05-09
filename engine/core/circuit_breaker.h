/**************************************************************************/
/*  circuit_breaker.h                                                     */
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
#include <cstdint>
#include <chrono>

namespace fate {

enum class CircuitState : uint8_t { Closed, Open, HalfOpen };

class CircuitBreaker {
public:
    CircuitBreaker(uint32_t failureThreshold = 5, float cooldownSeconds = 30.0f)
        : failureThreshold_(failureThreshold), cooldownSeconds_(cooldownSeconds) {}

    bool allowRequest() {
        if (state_ == CircuitState::Closed) return true;
        if (state_ == CircuitState::Open) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - openedAt_).count();
            if (elapsed >= cooldownSeconds_) {
                state_ = CircuitState::HalfOpen;
                return true;
            }
            return false;
        }
        // HalfOpen: allow only one probe request
        if (!halfOpenProbeUsed_) {
            halfOpenProbeUsed_ = true;
            return true;
        }
        return false;
    }

    void recordSuccess() {
        consecutiveFailures_ = 0;
        halfOpenProbeUsed_ = false;
        state_ = CircuitState::Closed;
    }

    void recordFailure() {
        ++consecutiveFailures_;
        if (consecutiveFailures_ >= failureThreshold_ || state_ == CircuitState::HalfOpen) {
            halfOpenProbeUsed_ = false;
            state_ = CircuitState::Open;
            openedAt_ = std::chrono::steady_clock::now();
        }
    }

    CircuitState state() const { return state_; }
    uint32_t consecutiveFailures() const { return consecutiveFailures_; }

private:
    uint32_t failureThreshold_;
    float cooldownSeconds_;
    CircuitState state_ = CircuitState::Closed;
    uint32_t consecutiveFailures_ = 0;
    bool halfOpenProbeUsed_ = false;
    std::chrono::steady_clock::time_point openedAt_;
};

} // namespace fate
