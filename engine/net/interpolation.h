#pragma once
#include "engine/core/types.h"
#include <unordered_map>
#include <cstdint>

namespace fate {

struct InterpolationState {
    Vec2 previousPosition;
    Vec2 targetPosition;
    float elapsed = 0.0f;         // time since last server update
    float updateInterval = 0.05f; // 50ms (20 tick/sec)
    bool hasData = false;

    // Call when a new server position arrives
    void onServerUpdate(const Vec2& newPosition) {
        previousPosition = hasData ? targetPosition : newPosition;
        targetPosition = newPosition;
        elapsed = 0.0f;
        hasData = true;
    }

    // Call every frame with deltaTime, returns interpolated position
    Vec2 interpolate(float dt) {
        if (!hasData) return targetPosition;
        elapsed += dt;
        float t = (updateInterval > 0.0f) ? (elapsed / updateInterval) : 1.0f;
        if (t > 1.0f) t = 1.0f;
        return {
            previousPosition.x + (targetPosition.x - previousPosition.x) * t,
            previousPosition.y + (targetPosition.y - previousPosition.y) * t
        };
    }
};

// Manages interpolation state for all ghost entities (keyed by PersistentId value)
class InterpolationManager {
public:
    void onEntityUpdate(uint64_t persistentId, const Vec2& position) {
        states_[persistentId].onServerUpdate(position);
    }

    // Pre-seed position for a new entity (e.g. from SvEntityEnter) so the first
    // interpolation sample never returns a zero vector / flash-to-origin.
    void initEntity(uint64_t persistentId, const Vec2& spawnPosition) {
        auto& s = states_[persistentId];
        if (!s.hasData) {
            s.previousPosition = spawnPosition;
            s.targetPosition   = spawnPosition;
            s.hasData = true;
        }
    }

    // WARNING: Returns Vec2{0,0} when entity has no interpolation state.
    // Callers MUST check the `valid` out-param before writing the result to a
    // Transform — blindly writing (0,0) causes entities to flash to world origin.
    Vec2 getInterpolatedPosition(uint64_t persistentId, float dt, bool* valid = nullptr) {
        auto it = states_.find(persistentId);
        if (it == states_.end()) {
            if (valid) *valid = false;
            return {};
        }
        if (valid) *valid = it->second.hasData;
        return it->second.interpolate(dt);
    }

    void removeEntity(uint64_t persistentId) {
        states_.erase(persistentId);
    }

    void clear() {
        states_.clear();
    }

private:
    std::unordered_map<uint64_t, InterpolationState> states_;
};

} // namespace fate
