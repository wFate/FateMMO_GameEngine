/**************************************************************************/
/*  health_damage_system.cpp                                              */
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
#include "engine/gameplay2d/systems/health_damage_system.h"
#include "engine/gameplay2d/components/health.h"
#include "engine/gameplay2d/components/damageable.h"
#include "engine/gameplay2d/components/attack.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/components/transform.h"
#include <cmath>

namespace fate {

namespace {

Vec2 facingVec(Direction d) {
    Vec2 v = directionToVec(d);
    if (v.lengthSq() == 0.0f) return {0.0f, -1.0f};  // default down
    return v;
}

float angleBetween(const Vec2& a, const Vec2& b) {
    float dot = a.x * b.x + a.y * b.y;
    float magA = std::sqrt(a.x * a.x + a.y * a.y);
    float magB = std::sqrt(b.x * b.x + b.y * b.y);
    if (magA == 0.0f || magB == 0.0f) return 180.0f;
    float c = dot / (magA * magB);
    if (c < -1.0f) c = -1.0f;
    if (c >  1.0f) c =  1.0f;
    return std::acos(c) * 57.2957795f;
}

} // anonymous

void HealthDamageSystem::update(float dt) {
    if (!world_) return;

    // 1. Regen + cooldown ticks.
    world_->forEach<Health>([&](Entity*, Health* h) {
        if (h->isDead) return;
        if (h->regenPerSec > 0.0f) {
            h->heal(h->regenPerSec * dt);
        }
    });

    world_->forEach<Attack>([&](Entity*, Attack* atk) {
        if (atk->cooldownTimer_ > 0.0f) atk->cooldownTimer_ -= dt;
    });

    // 2. Resolve attacks that asked to fire.
    world_->forEach<Transform, Attack>([&](Entity* attackerEnt, Transform* attackerTx, Attack* atk) {
        if (!atk->wantsToAttack_) return;
        atk->wantsToAttack_ = false;
        if (atk->cooldownTimer_ > 0.0f) return;

        Direction facing = Direction::Down;
        if (auto* cc = attackerEnt->getComponent<CharacterController2D>()) {
            facing = cc->facing;
        }
        Vec2 fdir = facingVec(facing);

        Entity* bestTarget = nullptr;
        Health* bestHealth = nullptr;
        float   bestDistSq = atk->range * atk->range;

        world_->forEach<Transform, Health>(
            [&](Entity* tEnt, Transform* tTx, Health* tHealth) {
                if (tEnt == attackerEnt) return;
                if (tHealth->isDead || tHealth->invulnerable) return;
                auto* tDmg = tEnt->getComponent<Damageable>();
                if (!tDmg || !factionAllowed(atk->faction, *tDmg)) return;

                float dx = tTx->position.x - attackerTx->position.x;
                float dy = tTx->position.y - attackerTx->position.y;
                float distSq = dx * dx + dy * dy;
                if (distSq > bestDistSq) return;

                Vec2 toTarget{dx, dy};
                if (atk->hitArcDegrees < 360.0f) {
                    float ang = angleBetween(fdir, toTarget);
                    if (ang > atk->hitArcDegrees * 0.5f) return;
                }

                bestTarget = tEnt;
                bestHealth = tHealth;
                bestDistSq = distSq;
            });

        if (bestTarget && bestHealth) {
            auto* tDmg = bestTarget->getComponent<Damageable>();
            float effective = atk->damageOnHit * (tDmg ? tDmg->incomingMultiplier : 1.0f);
            float dealt = bestHealth->applyDamage(effective);
            atk->cooldownTimer_ = atk->cooldownSec;
            if (onHit_) onHit_(attackerEnt, bestTarget, dealt);
            if (bestHealth->isDead && onDeath_) onDeath_(bestTarget, attackerEnt);
        }
    });
}

float HealthDamageSystem::applyDirectDamage(Entity* target, float amount, uint32_t attackerFaction) {
    if (!target) return 0.0f;
    auto* h   = target->getComponent<Health>();
    auto* dmg = target->getComponent<Damageable>();
    if (!h || h->isDead) return 0.0f;
    if (dmg && !factionAllowed(attackerFaction, *dmg)) return 0.0f;
    float effective = amount * (dmg ? dmg->incomingMultiplier : 1.0f);
    float dealt = h->applyDamage(effective);
    if (h->isDead && onDeath_) onDeath_(target, nullptr);
    return dealt;
}

} // namespace fate
