#pragma once
#include <cstdint>

namespace fate {

// DbCircuitBreaker: a circuit breaker for the DB pool that uses explicit
// simulation/game time (double seconds) rather than wall-clock time, so
// the server tick can control time advancement via updateBreakerTime().
//
// States: Closed (normal) -> Open (after N failures, reject for cooldown)
//         -> HalfOpen (probe one query) -> Closed (on success)
//
// Unlike engine/core/circuit_breaker.h (which uses std::chrono wall time),
// this variant is injected with time so it can be deterministically tested.

class DbCircuitBreaker {
public:
    DbCircuitBreaker(uint32_t failureThreshold = 5, double cooldownSeconds = 30.0)
        : threshold_(failureThreshold), cooldown_(cooldownSeconds) {}

    enum class State : uint8_t { Closed, Open, HalfOpen };

    State state() const { return state_; }

    bool allowRequest() {
        if (state_ == State::Closed) return true;
        if (state_ == State::Open) {
            if (currentTime_ >= openedAt_ + cooldown_) {
                state_ = State::HalfOpen;
                return true;
            }
            return false;
        }
        return true; // HalfOpen: allow one probe
    }

    void recordSuccess() {
        consecutiveFailures_ = 0;
        state_ = State::Closed;
    }

    void recordFailure(double now) {
        currentTime_ = now;
        ++consecutiveFailures_;
        if (state_ == State::HalfOpen || consecutiveFailures_ >= threshold_) {
            state_ = State::Open;
            openedAt_ = now;
        }
    }

    void updateTime(double now) { currentTime_ = now; }
    uint32_t consecutiveFailures() const { return consecutiveFailures_; }

private:
    State state_ = State::Closed;
    uint32_t threshold_;
    double cooldown_;
    uint32_t consecutiveFailures_ = 0;
    double openedAt_ = 0.0;
    double currentTime_ = 0.0;
};

} // namespace fate
