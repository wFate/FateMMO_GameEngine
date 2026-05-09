/**************************************************************************/
/*  npc2d.h                                                               */
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
// engine/gameplay2d/components/npc2d.h
//
// Public NPC marker for the open-source demo. Carries display data and a
// loose role flag set so the demo's Interaction system can branch on
// "shopkeeper vs questgiver vs ambient flavor" without pulling in proprietary
// shop/quest data.
//
// Authors typically pair NPC2D with Interactable + Nameplate; the
// Interaction2DSystem will fire actionId when the player presses interact in
// range, and the demo can route on roleFlags to open a panel.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <cstdint>
#include <string>

namespace fate {

enum class NPC2DRole : uint8_t {
    None         = 0,
    QuestGiver   = 1 << 0,
    Shopkeeper   = 1 << 1,
    Banker       = 1 << 2,
    Trainer      = 1 << 3,
    Innkeeper    = 1 << 4,
    AmbientChat  = 1 << 5,
};

struct NPC2D {
    FATE_COMPONENT(NPC2D)

    std::string displayName   = "NPC";
    std::string greeting      = "Hello, traveler.";
    std::string dialogueId;        // demo logs; full game routes to dialogue tree
    uint32_t    roleFlags     = 0; // bitmask of NPC2DRole values
    float       facingTowardPlayer = false; // turn to face the player on hover
    bool        canTrade      = false;
};

inline bool hasRole(const NPC2D& npc, NPC2DRole r) {
    return (npc.roleFlags & static_cast<uint32_t>(r)) != 0u;
}

inline void setRole(NPC2D& npc, NPC2DRole r, bool on) {
    if (on) npc.roleFlags |= static_cast<uint32_t>(r);
    else    npc.roleFlags &= ~static_cast<uint32_t>(r);
}

} // namespace fate

FATE_REFLECT(fate::NPC2D,
    FATE_FIELD(displayName, String),
    FATE_FIELD(greeting, String),
    FATE_FIELD(dialogueId, String),
    FATE_FIELD(roleFlags, UInt),
    FATE_FIELD(facingTowardPlayer, Bool),
    FATE_FIELD(canTrade, Bool)
)
