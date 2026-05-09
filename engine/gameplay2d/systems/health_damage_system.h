/**************************************************************************/
/*  health_damage_system.h                                                */
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
// engine/gameplay2d/systems/health_damage_system.h
//
// Two responsibilities:
//   1. Regenerate Health.currentHP from regenPerSec on every tick, clamped to
//      maxHP. Skipped when isDead.
//   2. Resolve any Attack entity whose wantsToAttack_ flag is set. Picks the
//      single closest target inside (range, hitArcDegrees) along the entity's
//      facing, applies damageOnHit through Health.applyDamage, and starts the
//      cooldown.
//
// Faction filtering: an attacker hits a target when
//   target.Damageable.hostileMask matches attacker.Attack.faction OR
//   (canBeHitBySameFaction == true AND factions are equal).
//
// Death is reported via setOnDeath callback (one-shot per entity); the demo
// uses this to spawn floating text or trigger a respawn timer.

#pragma once

#include "engine/ecs/world.h"
#include <functional>

namespace fate {

class Entity;

class HealthDamageSystem : public System {
public:
    const char* name() const override { return "HealthDamageSystem"; }
    void update(float dt) override;

    // Attempt to deal damage to a specific target entity. Returns the actual
    // damage dealt (0 if filtered/invulnerable/dead).
    float applyDirectDamage(Entity* target, float amount, uint32_t attackerFaction);

    using DeathCallback = std::function<void(Entity* killed, Entity* killerOrNull)>;
    void setOnDeath(DeathCallback cb) { onDeath_ = std::move(cb); }

    using HitCallback = std::function<void(Entity* attacker, Entity* target, float damage)>;
    void setOnHit(HitCallback cb) { onHit_ = std::move(cb); }

private:
    DeathCallback onDeath_;
    HitCallback   onHit_;
};

} // namespace fate
