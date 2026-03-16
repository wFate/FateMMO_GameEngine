#pragma once
#include "engine/ecs/component.h"
#include "engine/core/types.h"
#include <string>
#include <unordered_map>

namespace fate {

// Animation definition for a single animation state
struct AnimationDef {
    std::string name;
    int startFrame = 0;
    int frameCount = 1;
    float frameRate = 8.0f; // frames per second
    bool loop = true;
};

// Animator component - handles sprite animation state
struct Animator : public Component {
    FATE_COMPONENT(Animator)

    std::unordered_map<std::string, AnimationDef> animations;
    std::string currentAnimation;
    float timer = 0.0f;
    bool playing = true;

    void addAnimation(const std::string& name, int startFrame, int frameCount,
                      float fps = 8.0f, bool loop = true) {
        animations[name] = {name, startFrame, frameCount, fps, loop};
    }

    void play(const std::string& name) {
        if (currentAnimation == name && playing) return;
        currentAnimation = name;
        timer = 0.0f;
        playing = true;
    }

    void stop() { playing = false; }

    // Returns the current frame index (call from system update)
    int getCurrentFrame() const {
        auto it = animations.find(currentAnimation);
        if (it == animations.end()) return 0;

        const auto& anim = it->second;
        int frameIndex = (int)(timer * anim.frameRate);
        if (anim.loop) {
            frameIndex = frameIndex % anim.frameCount;
        } else {
            if (frameIndex > anim.frameCount - 1) frameIndex = anim.frameCount - 1;
        }
        return anim.startFrame + frameIndex;
    }
};

} // namespace fate
