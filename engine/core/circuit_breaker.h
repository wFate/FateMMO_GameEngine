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
        return true; // HalfOpen allows one request
    }

    void recordSuccess() {
        consecutiveFailures_ = 0;
        state_ = CircuitState::Closed;
    }

    void recordFailure() {
        ++consecutiveFailures_;
        if (consecutiveFailures_ >= failureThreshold_) {
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
    std::chrono::steady_clock::time_point openedAt_;
};

} // namespace fate
