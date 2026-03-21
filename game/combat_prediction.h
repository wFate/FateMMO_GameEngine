#pragma once
#include <cstdint>
#include <array>

namespace fate {

// Tracks a single pending attack that has been visually predicted but not yet
// confirmed by the server (SvCombatEvent / SvSkillResult).
struct PendingAttack {
    uint64_t targetId = 0;
    float    timestamp = 0.0f;
    bool     resolved  = false;
};

// Simple ring-buffer that tracks pending (optimistically displayed) attacks.
// The client adds a prediction every time it fires an attack animation before
// the server has responded. When the server response arrives the oldest
// unresolved prediction is marked resolved.
class CombatPredictionBuffer {
public:
    static constexpr int MAX_PENDING = 32;

    void addPrediction(uint64_t targetId, float timestamp) {
        predictions_[head_ % MAX_PENDING] = {targetId, timestamp, false};
        ++head_;
    }

    // Mark the oldest unresolved prediction as resolved.
    void resolveOldest() {
        for (int i = 0; i < MAX_PENDING; ++i) {
            int idx = ((head_ - MAX_PENDING + i) % MAX_PENDING + MAX_PENDING) % MAX_PENDING;
            if (!predictions_[idx].resolved && predictions_[idx].targetId != 0) {
                predictions_[idx].resolved = true;
                return;
            }
        }
    }

    int pendingCount() const {
        int count = 0;
        for (const auto& p : predictions_) {
            if (!p.resolved && p.targetId != 0) ++count;
        }
        return count;
    }

    void clear() {
        predictions_ = {};
        head_ = 0;
    }

private:
    std::array<PendingAttack, MAX_PENDING> predictions_{};
    int head_ = 0;
};

} // namespace fate
