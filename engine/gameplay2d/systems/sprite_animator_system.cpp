/**************************************************************************/
/*  sprite_animator_system.cpp                                            */
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
#include "engine/gameplay2d/systems/sprite_animator_system.h"
#include "engine/gameplay2d/components/sprite_animator2d.h"
#include "engine/components/sprite_component.h"

namespace fate {

void SpriteAnimator2DSystem::update(float dt) {
    if (!world_) return;

    world_->forEach<SpriteAnimator2D>([&](Entity* e, SpriteAnimator2D* anim) {
        if (!anim->playing) return;

        const SpriteAnimation2D* clip = anim->find(anim->currentAnimation);
        if (!clip || clip->frameCount <= 0 || clip->fps <= 0.0f) return;

        anim->timer += dt;

        int localIdx = static_cast<int>(anim->timer * clip->fps);
        if (clip->loop) {
            localIdx %= clip->frameCount;
        }

        // hitFrame fires once per non-loop play, or once per loop iteration.
        if (clip->hitFrame >= 0 && !anim->hitFrameFired_ && localIdx >= clip->hitFrame) {
            anim->hitFrameFired_ = true;
        }

        // Non-looping: clamp + transition.
        if (!clip->loop && localIdx >= clip->frameCount) {
            if (!anim->returnAnimation.empty() &&
                anim->returnAnimation != anim->currentAnimation) {
                anim->play(anim->returnAnimation);
            } else {
                anim->playing = false;
                anim->timer   = (clip->frameCount - 1) / clip->fps;
            }
            return;
        }

        if (auto* sprite = e->getComponent<SpriteComponent>()) {
            sprite->currentFrame = clip->startFrame + localIdx;
            sprite->updateSourceRect();
        }
    });
}

} // namespace fate
