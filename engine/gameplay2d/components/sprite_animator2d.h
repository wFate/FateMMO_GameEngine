/**************************************************************************/
/*  sprite_animator2d.h                                                   */
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
// engine/gameplay2d/components/sprite_animator2d.h
//
// Public 2D sprite animator for the open-source demo. Drives SpriteComponent's
// currentFrame from a small set of named clips. Designed for the cardinal-
// directional walk/idle/attack pattern that 2D MMORPGs lean on; not a generic
// state-machine.
//
// Clip storage is a small std::vector instead of unordered_map so iteration
// over a typical 4–12 clips stays cache-friendly and the deserializer doesn't
// have to allocate hashtable buckets per entity. Look-up is O(N) but N is tiny.
//
// Custom (de)serializer is required because the animations vector is not a
// reflectable scalar — registerGameplay2dComponents wires it up.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <string>
#include <vector>

namespace fate {

struct SpriteAnimation2D {
    std::string name;
    int   startFrame  = 0;
    int   frameCount  = 1;
    float fps         = 8.0f;
    bool  loop        = true;
    int   hitFrame    = -1;          // -1 = no hit event
};

struct SpriteAnimator2D {
    FATE_COMPONENT(SpriteAnimator2D)

    std::vector<SpriteAnimation2D> animations;
    std::string currentAnimation;
    std::string returnAnimation;     // auto-played after a non-loop clip ends
    float       timer    = 0.0f;     // seconds into the active clip
    bool        playing  = true;
    bool        hitFrameFired_ = false;  // runtime, reset on play()

    void addClip(const std::string& name, int startFrame, int frameCount,
                 float fps = 8.0f, bool loop = true, int hitFrame = -1) {
        for (auto& a : animations) {
            if (a.name == name) {
                a.startFrame = startFrame;
                a.frameCount = frameCount;
                a.fps        = fps;
                a.loop       = loop;
                a.hitFrame   = hitFrame;
                return;
            }
        }
        animations.push_back({name, startFrame, frameCount, fps, loop, hitFrame});
    }

    void play(const std::string& name) {
        if (currentAnimation == name && playing) return;
        currentAnimation = name;
        timer = 0.0f;
        playing = true;
        hitFrameFired_ = false;
    }

    void stop() { playing = false; }

    const SpriteAnimation2D* find(const std::string& name) const {
        for (const auto& a : animations) {
            if (a.name == name) return &a;
        }
        return nullptr;
    }

    int currentFrame() const {
        const auto* a = find(currentAnimation);
        if (!a || a->frameCount <= 0) return 0;
        int idx = static_cast<int>(timer * a->fps);
        if (a->loop) idx %= a->frameCount;
        else if (idx >= a->frameCount) idx = a->frameCount - 1;
        return a->startFrame + idx;
    }

    bool isFinished() const {
        const auto* a = find(currentAnimation);
        if (!a || a->loop) return false;
        return static_cast<int>(timer * a->fps) >= a->frameCount;
    }
};

} // namespace fate

FATE_REFLECT(fate::SpriteAnimator2D,
    FATE_FIELD(currentAnimation, String),
    FATE_FIELD(returnAnimation, String),
    FATE_FIELD(timer, Float),
    FATE_FIELD(playing, Bool)
)
