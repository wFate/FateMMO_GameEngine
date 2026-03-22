#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"
#include "engine/ecs/reflect.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace fate {

// Animation definition for a single animation state
struct AnimationDef {
    std::string name;
    int startFrame = 0;
    int frameCount = 1;
    float frameRate = 8.0f; // frames per second
    bool loop = true;
    int hitFrame = -1;      // frame index (0-based) that triggers the hit event (-1 = none)
};

// Animator component - handles sprite animation state
//
// Supports animation events for combat windup:
//   - hitFrame: fires onHitFrame callback when the animation reaches that frame
//   - onComplete: fires when a non-looping animation finishes
//   - returnAnimation: auto-transitions to this animation after a non-looping one completes
struct Animator {
    FATE_COMPONENT(Animator)

    std::unordered_map<std::string, AnimationDef> animations;
    std::string currentAnimation;
    float timer = 0.0f;
    bool playing = true;

    // Animation event callbacks (set per use, cleared on play())
    std::function<void()> onHitFrame;
    std::function<void()> onComplete;

    // Animation to auto-transition to after a non-looping animation finishes
    std::string returnAnimation;

    void addAnimation(const std::string& name, int startFrame, int frameCount,
                      float fps = 8.0f, bool loop = true, int hitFrame = -1) {
        animations[name] = {name, startFrame, frameCount, fps, loop, hitFrame};
    }

    void play(const std::string& name) {
        if (currentAnimation == name && playing) return;
        currentAnimation = name;
        timer = 0.0f;
        playing = true;
        hitFrameFired_ = false;
    }

    void stop() { playing = false; }

    // Returns the absolute sprite frame (startFrame + local index)
    int getCurrentFrame() const {
        auto it = animations.find(currentAnimation);
        if (it == animations.end()) return 0;

        const auto& anim = it->second;
        int idx = getFrameIndex(anim);
        return anim.startFrame + idx;
    }

    // Returns the 0-based frame index within the current animation
    int getLocalFrameIndex() const {
        auto it = animations.find(currentAnimation);
        if (it == animations.end()) return 0;
        return getFrameIndex(it->second);
    }

    // True if a non-looping animation has reached its last frame
    bool isFinished() const {
        auto it = animations.find(currentAnimation);
        if (it == animations.end()) return true;
        const auto& anim = it->second;
        if (anim.loop) return false;
        return (int)(timer * anim.frameRate) >= anim.frameCount;
    }

private:
    friend class AnimationSystem;
    bool hitFrameFired_ = false;

    int getFrameIndex(const AnimationDef& anim) const {
        int idx = (int)(timer * anim.frameRate);
        if (anim.loop) {
            idx = idx % anim.frameCount;
        } else {
            if (idx > anim.frameCount - 1) idx = anim.frameCount - 1;
        }
        return idx;
    }
};

} // namespace fate

// Animator has complex inner state (unordered_map of AnimationDefs) — needs custom serializer
FATE_REFLECT(fate::Animator,
    FATE_FIELD(currentAnimation, String),
    FATE_FIELD(timer, Float),
    FATE_FIELD(playing, Bool)
)
