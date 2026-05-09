/**************************************************************************/
/*  damageable.h                                                          */
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
// engine/gameplay2d/components/damageable.h
//
// Marker + faction tag that lets the HealthDamageSystem resolve "can A hit B?"
// without baking team rules into Health itself. faction is a small uint so
// authors can pick any IDs they want; factionMask says which factions THIS
// entity considers hostile. Self-friendly-fire is opt-in via canBeHitBySameFaction.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <cstdint>

namespace fate {

struct Damageable {
    FATE_COMPONENT(Damageable)

    uint32_t faction     = 0;            // 0 = neutral / unaligned
    uint32_t hostileMask = 0xFFFFFFFFu;  // any non-zero faction can hurt this
    bool     canBeHitBySameFaction = false;
    float    incomingMultiplier    = 1.0f;
};

// Single source of truth for "is this attacker allowed to hit this target?".
// Used by HealthDamageSystem when resolving an attack and by Mob2DSystem when
// acquiring an aggro target so the two never disagree (a mob that aggroes a
// target it cannot damage is a stuck-in-chase bug).
inline bool factionAllowed(uint32_t attackerFaction, const Damageable& target) {
    if (target.faction == attackerFaction) return target.canBeHitBySameFaction;
    return ((target.hostileMask & (1u << (attackerFaction & 31u))) != 0u) ||
           target.hostileMask == 0xFFFFFFFFu;
}

} // namespace fate

FATE_REFLECT(fate::Damageable,
    FATE_FIELD(faction, UInt),
    FATE_FIELD(hostileMask, UInt),
    FATE_FIELD(canBeHitBySameFaction, Bool),
    FATE_FIELD(incomingMultiplier, Float)
)
