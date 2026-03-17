#pragma once
#include "engine/input/action_map.h"
#include <cstddef>
#include <cstdint>

namespace fate {

// Circular buffer of recent combat-action inputs for input buffering.
// Capacity is fixed at 32 entries. The caller records actions via record()
// and combat systems consume them via consume() within a configurable
// frame window (default 6 frames).
class InputBuffer {
public:
    static constexpr size_t kCapacity = 32;
    static constexpr uint32_t kDefaultWindow = 6;

    struct Entry {
        uint32_t frame = 0;
        ActionId action = ActionId::COUNT;
        bool consumed = false;
    };

    // Record an action at the current frame and advance write position.
    void record(ActionId action) {
        entries_[writePos_] = { currentFrame_, action, false };
        writePos_ = (writePos_ + 1) % kCapacity;
        if (count_ < kCapacity) ++count_;
    }

    // Scan backward for an unconsumed entry matching `action` within
    // `windowFrames` of the current frame. If found, mark consumed and
    // return true.
    bool consume(ActionId action, uint32_t windowFrames = kDefaultWindow) {
        if (count_ == 0) return false;

        // Start from the most recent entry and scan backward.
        for (size_t i = 0; i < count_; ++i) {
            size_t idx = (writePos_ + kCapacity - 1 - i) % kCapacity;
            Entry& e = entries_[idx];

            // Stop scanning once entries are too old.
            if (currentFrame_ - e.frame > windowFrames) break;

            if (e.action == action && !e.consumed) {
                e.consumed = true;
                return true;
            }
        }
        return false;
    }

    // Advance the frame counter. Call once per game frame.
    void advanceFrame() { ++currentFrame_; }

    // Accessors for testing / debugging.
    uint32_t currentFrame() const { return currentFrame_; }
    size_t   count()        const { return count_; }

private:
    Entry    entries_[kCapacity] = {};
    size_t   writePos_     = 0;
    size_t   count_        = 0;
    uint32_t currentFrame_ = 0;
};

} // namespace fate
