/**************************************************************************/
/*  attack.h                                                              */
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
// engine/gameplay2d/components/attack.h
//
// Demo melee attack source. The HealthDamageSystem fires a single hit when
// the player presses the attack action and the cooldown has elapsed; range +
// hitArc define a forward cone in the direction of the attacker's facing.
//
// faction is the ATTACKER's affiliation, used by Damageable.hostileMask to
// decide whether the hit lands. damageOnHit is a flat number — there's no
// crit/scaling/resists in the demo by design (those belong in proprietary
// game/ logic).

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <cstdint>

namespace fate {

struct Attack {
    FATE_COMPONENT(Attack)

    uint32_t faction     = 0;
    float    damageOnHit = 10.0f;
    float    range       = 48.0f;     // pixel distance to target center
    float    hitArcDegrees = 90.0f;   // cone width centered on facing
    float    cooldownSec = 0.6f;
    float    cooldownTimer_ = 0.0f;   // runtime
    bool     wantsToAttack_ = false;  // set by player input / AI, consumed by system
};

} // namespace fate

FATE_REFLECT(fate::Attack,
    FATE_FIELD(faction, UInt),
    FATE_FIELD(damageOnHit, Float),
    FATE_FIELD(range, Float),
    FATE_FIELD(hitArcDegrees, Float),
    FATE_FIELD(cooldownSec, Float)
)
