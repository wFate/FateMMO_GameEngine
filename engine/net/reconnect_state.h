#pragma once
#include <cstdint>
#include <algorithm>

namespace fate {

enum class ReconnectPhase : uint8_t { Idle, Reconnecting };

class ReconnectState {
public:
    ReconnectPhase state() const { return phase_; }

    void beginReconnect(double now) {
        phase_ = ReconnectPhase::Reconnecting;
        startTime_ = now;
        nextAttemptTime_ = now + 1.0;
        backoff_ = 1.0;
        attempts_ = 0;
    }

    bool shouldAttemptNow(double now) const {
        return phase_ == ReconnectPhase::Reconnecting && now >= nextAttemptTime_;
    }

    void onAttemptFailed(double now) {
        ++attempts_;
        backoff_ = std::min(backoff_ * 2.0, 30.0);
        nextAttemptTime_ = now + backoff_;
    }

    void onSuccess() {
        phase_ = ReconnectPhase::Idle;
        attempts_ = 0;
        backoff_ = 1.0;
    }

    bool isTimedOut(double now) const {
        return phase_ == ReconnectPhase::Reconnecting
            && (now - startTime_) > 60.0;
    }

    int attempts() const { return attempts_; }

private:
    ReconnectPhase phase_ = ReconnectPhase::Idle;
    double startTime_ = 0.0;
    double nextAttemptTime_ = 0.0;
    double backoff_ = 1.0;
    int attempts_ = 0;
};

} // namespace fate
