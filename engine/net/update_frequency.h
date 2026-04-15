#pragma once
#include <cstdint>

namespace fate {

enum class UpdateTier : uint8_t {
    Near = 0, // <=40 tiles (1280px) — every tick (20Hz), covers full viewport
    Mid  = 1, // 40-60 tiles         — every 3rd tick (~7Hz)
    Far  = 2, // 60-80 tiles         — every 5th tick (4Hz)
    Edge = 3  // 80+ tiles           — every 10th tick (2Hz)
};

inline UpdateTier getUpdateTier(float distancePixels) {
    constexpr float TILE = 32.0f;
    if (distancePixels <= 40.0f * TILE) return UpdateTier::Near;
    if (distancePixels <= 60.0f * TILE) return UpdateTier::Mid;
    if (distancePixels <= 80.0f * TILE) return UpdateTier::Far;
    return UpdateTier::Edge;
}

inline uint32_t getTickInterval(UpdateTier tier) {
    constexpr uint32_t intervals[] = {1, 3, 5, 10};
    return intervals[static_cast<uint8_t>(tier)];
}

inline bool shouldSendUpdate(UpdateTier tier, uint32_t currentTick) {
    return (currentTick % getTickInterval(tier)) == 0;
}

} // namespace fate
