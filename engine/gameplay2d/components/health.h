/**************************************************************************/
/*  health.h                                                              */
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
// engine/gameplay2d/components/health.h
//
// Demo-safe HP/regen for the open-source build. Numbers are deliberately
// generic — no proprietary level scaling, no resistances, no shields. The
// HealthDamageSystem reads currentHP/maxHP and clamps; consumers (mob AI,
// nameplate render) read isDead/regen.
//
// invulnerable is a hard gate, not a buff stack. Damage applied via
// applyDamage() returns 0 when invulnerable; AOE/skill code outside the demo
// can route around it if it needs piercing.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"

namespace fate {

struct Health {
    FATE_COMPONENT(Health)

    float currentHP   = 100.0f;
    float maxHP       = 100.0f;
    float regenPerSec = 0.0f;
    bool  isDead      = false;
    bool  invulnerable = false;

    float fraction() const {
        if (maxHP <= 0.0f) return 0.0f;
        float f = currentHP / maxHP;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        return f;
    }

    float applyDamage(float amount) {
        if (invulnerable || isDead || amount <= 0.0f) return 0.0f;
        float taken = amount;
        if (taken > currentHP) taken = currentHP;
        currentHP -= taken;
        if (currentHP <= 0.0f) {
            currentHP = 0.0f;
            isDead = true;
        }
        return taken;
    }

    void heal(float amount) {
        if (isDead || amount <= 0.0f) return;
        currentHP += amount;
        if (currentHP > maxHP) currentHP = maxHP;
    }

    void revive(float fraction = 1.0f) {
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
        currentHP = maxHP * fraction;
        isDead    = (currentHP <= 0.0f);
    }
};

} // namespace fate

FATE_REFLECT(fate::Health,
    FATE_FIELD(currentHP, Float),
    FATE_FIELD(maxHP, Float),
    FATE_FIELD(regenPerSec, Float),
    FATE_FIELD(isDead, Bool),
    FATE_FIELD(invulnerable, Bool)
)
