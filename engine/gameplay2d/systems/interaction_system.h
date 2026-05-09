/**************************************************************************/
/*  interaction_system.h                                                  */
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
// engine/gameplay2d/systems/interaction_system.h
//
// Finds the local player (CharacterController2D::isLocalPlayer == true), then
// the nearest enabled Interactable within interactionRadius. When the player
// presses the interact action (or the host calls requestInteract()), the
// system writes triggeredThisFrame_=true on the resolved Interactable for
// exactly one frame and invokes the optional onInteract callback.
//
// Action key is stored as an SDL_Scancode so the demo can rebind without a
// full ActionMap configuration; default = SDL_SCANCODE_F.

#pragma once

#include "engine/ecs/world.h"
#include <SDL_scancode.h>
#include <functional>
#include <string>

namespace fate {

struct Interactable;
class Entity;

class Interaction2DSystem : public System {
public:
    const char* name() const override { return "Interaction2DSystem"; }
    void update(float dt) override;

    void setInteractKey(SDL_Scancode k) { interactKey_ = k; }

    // Programmatic trigger (touch button, gamepad). One-shot — flipped back
    // false after the next update consumes it.
    void requestInteract() { wantsTrigger_ = true; }

    // Fired on a successful interaction. Receives the Interactable entity and
    // the Interactable component pointer (lifetime: until next world tick).
    using InteractCallback = std::function<void(Entity*, Interactable*)>;
    void setOnInteract(InteractCallback cb) { onInteract_ = std::move(cb); }

    // Last-frame inspect helpers (demo HUD reads these).
    Entity* nearestInRange() const { return nearestEntity_; }
    float   nearestDistance() const { return nearestDistance_; }
    const std::string& nearestPrompt() const { return nearestPrompt_; }

private:
    SDL_Scancode  interactKey_   = SDL_SCANCODE_F;
    bool          wantsTrigger_  = false;

    Entity*       nearestEntity_ = nullptr;
    float         nearestDistance_ = 0.0f;
    std::string   nearestPrompt_;

    InteractCallback onInteract_;
};

} // namespace fate
