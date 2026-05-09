/**************************************************************************/
/*  register_gameplay2d.h                                                 */
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
// engine/gameplay2d/register_gameplay2d.h
//
// Registers every public gameplay2d component with ComponentMetaRegistry.
// Header-only inline functions — both the demo and any future consumer can
// include and call this without pulling in any game/ symbols.
//
// Components with custom serializers (SpriteAnimator2D, anything that holds
// a std::vector or non-reflected enum) supply explicit toJson/fromJson
// lambdas. Components whose reflected fields are sufficient pass through the
// default registration path.

#pragma once

#include "engine/ecs/component_meta.h"

#include "engine/gameplay2d/components/collider2d.h"
#include "engine/gameplay2d/components/trigger_area2d.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/gameplay2d/components/sprite_animator2d.h"
#include "engine/gameplay2d/components/camera_follow2d.h"
#include "engine/gameplay2d/components/health.h"
#include "engine/gameplay2d/components/damageable.h"
#include "engine/gameplay2d/components/attack.h"
#include "engine/gameplay2d/components/targetable.h"
#include "engine/gameplay2d/components/nameplate.h"
#include "engine/gameplay2d/components/interactable.h"
#include "engine/gameplay2d/components/npc2d.h"
#include "engine/gameplay2d/components/zone2d.h"
#include "engine/gameplay2d/components/portal2d.h"
#include "engine/gameplay2d/components/spawn_point2d.h"
#include "engine/gameplay2d/components/spawn_zone2d.h"
#include "engine/gameplay2d/components/network_identity.h"
#include "engine/gameplay2d/components/replicated_transform2d.h"
#include "engine/gameplay2d/components/interest_source.h"
#include "engine/gameplay2d/components/mob2d.h"

#include <nlohmann/json.hpp>

namespace fate {

inline void registerGameplay2dComponents() {
    auto& reg = ComponentMetaRegistry::instance();

    reg.registerComponent<Collider2D>();
    reg.registerComponent<TriggerArea2D>();
    reg.registerComponent<CharacterController2D>();
    reg.registerComponent<CameraFollow2D>();
    reg.registerComponent<Health>();
    reg.registerComponent<Damageable>();
    reg.registerComponent<Attack>();
    reg.registerComponent<Targetable>();
    reg.registerComponent<Nameplate>();
    reg.registerComponent<Interactable>();
    reg.registerComponent<NPC2D>();
    reg.registerComponent<Zone2D>();
    reg.registerComponent<Portal2D>();
    reg.registerComponent<SpawnPoint2D>();
    reg.registerComponent<SpawnZone2D>();
    reg.registerComponent<NetworkIdentity>();
    reg.registerComponent<ReplicatedTransform2D>();
    reg.registerComponent<InterestSource>();
    reg.registerComponent<Mob2D>();

    // SpriteAnimator2D needs a custom serializer because the reflected fields
    // omit the animations vector — the rest of the component is dead state
    // without that list of clips.
    reg.registerComponent<SpriteAnimator2D>(
        // toJson
        [](const void* data, nlohmann::json& j) {
            const auto* a = static_cast<const SpriteAnimator2D*>(data);
            j["currentAnimation"] = a->currentAnimation;
            j["returnAnimation"]  = a->returnAnimation;
            j["timer"]            = a->timer;
            j["playing"]          = a->playing;
            nlohmann::json clips = nlohmann::json::array();
            for (const auto& c : a->animations) {
                clips.push_back({
                    {"name",       c.name},
                    {"startFrame", c.startFrame},
                    {"frameCount", c.frameCount},
                    {"fps",        c.fps},
                    {"loop",       c.loop},
                    {"hitFrame",   c.hitFrame},
                });
            }
            j["animations"] = std::move(clips);
        },
        // fromJson
        [](const nlohmann::json& j, void* data) {
            auto* a = static_cast<SpriteAnimator2D*>(data);
            if (j.contains("currentAnimation"))
                a->currentAnimation = j["currentAnimation"].get<std::string>();
            if (j.contains("returnAnimation"))
                a->returnAnimation = j["returnAnimation"].get<std::string>();
            if (j.contains("timer"))   a->timer   = j["timer"].get<float>();
            if (j.contains("playing")) a->playing = j["playing"].get<bool>();
            a->animations.clear();
            if (j.contains("animations") && j["animations"].is_array()) {
                for (const auto& c : j["animations"]) {
                    SpriteAnimation2D clip;
                    clip.name       = c.value("name", std::string{});
                    clip.startFrame = c.value("startFrame", 0);
                    clip.frameCount = c.value("frameCount", 1);
                    clip.fps        = c.value("fps", 8.0f);
                    clip.loop       = c.value("loop", true);
                    clip.hitFrame   = c.value("hitFrame", -1);
                    a->animations.push_back(std::move(clip));
                }
            }
            a->hitFrameFired_ = false;
        });
}

} // namespace fate
