/**************************************************************************/
/*  mob2d_system.cpp                                                      */
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
#include "engine/gameplay2d/systems/mob2d_system.h"
#include "engine/gameplay2d/components/mob2d.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/gameplay2d/components/health.h"
#include "engine/gameplay2d/components/damageable.h"
#include "engine/gameplay2d/components/attack.h"
#include "engine/components/transform.h"
#include <cmath>

namespace fate {

namespace {

Vec2 toward(const Vec2& from, const Vec2& to) {
    Vec2 d{ to.x - from.x, to.y - from.y };
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 0.0001f) return {0.0f, 0.0f};
    return { d.x / len, d.y / len };
}

float distSq(const Vec2& a, const Vec2& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Single eligibility predicate, called at both acquisition AND every-tick
// revalidation. If acquisition rejects a target but revalidation lets the
// same target ride, the mob ends up chasing/attacking something
// HealthDamageSystem will refuse to damage — a dead loop visible to players.
// Acquisition has Entity*+Health* in hand from forEach; revalidation looks
// them up via the handle-based wrapper below.
bool isAggroEligible(Entity* e, const Health& h, uint32_t myFaction) {
    if (h.isDead || h.invulnerable) return false;
    auto* d = e->getComponent<Damageable>();
    if (!d) return false;
    return factionAllowed(myFaction, *d);
}

// Handle-based revalidation wrapper. Returns the Entity* when the slot is
// still a valid aggro target, else null.
Entity* validateAggroTarget(World& world, EntityHandle handle, uint32_t myFaction) {
    if (handle.isNull()) return nullptr;
    Entity* e = world.getEntity(handle);
    if (!e) return nullptr;
    auto* h = e->getComponent<Health>();
    if (!h) return nullptr;
    return isAggroEligible(e, *h, myFaction) ? e : nullptr;
}

uint32_t mobAttackerFaction(Entity* mobEnt) {
    auto* atk = mobEnt->getComponent<Attack>();
    if (atk) return atk->faction;
    auto* dmg = mobEnt->getComponent<Damageable>();
    return dmg ? dmg->faction : 0u;
}

} // anonymous

void Mob2DSystem::update(float dt) {
    if (!world_) return;

    // World::forEach only supports up to 2 component types — fetch the
    // CharacterController2D via getComponent.
    world_->forEach<Transform, Mob2D>(
        [&](Entity* mobEnt, Transform* mobTx, Mob2D* m) {
            auto* cc = mobEnt->getComponent<CharacterController2D>();
            if (!cc) return;
            if (!m->enabled) {
                cc->lastInputVec_ = {0.0f, 0.0f};
                return;
            }

            // Capture spawn anchor once.
            if (!m->hasSpawnAnchor) {
                m->spawnPosition = mobTx->position;
                m->hasSpawnAnchor = true;
            }

            // Death check.
            if (auto* h = mobEnt->getComponent<Health>()) {
                if (h->isDead) {
                    m->state = Mob2DState::Dead;
                    cc->lastInputVec_ = {0.0f, 0.0f};
                    return;
                }
            }

            cc->authority = MovementAuthority::AISimulated;
            m->stateTimer_ += dt;

            // The mob's "attacker faction" — same source HealthDamageSystem
            // uses when filtering its hits. Captured once so revalidation and
            // acquisition cannot disagree.
            const uint32_t myFaction = mobAttackerFaction(mobEnt);

            // Revalidate the existing target against the FULL eligibility
            // predicate every tick. Acquisition-time-only checks let a mob
            // keep chasing a target that has since become invulnerable, lost
            // its Damageable, or flipped its hostileMask.
            Entity* target = validateAggroTarget(*world_, m->currentTarget_, myFaction);
            if (!target) m->currentTarget_ = EntityHandle{};

            if (!target) {
                Entity* bestEnt    = nullptr;
                float   bestDistSq = m->aggroRadiusPx * m->aggroRadiusPx;
                world_->forEach<Transform, Health>(
                    [&](Entity* tEnt, Transform* tTx, Health* tHealth) {
                        if (tEnt == mobEnt) return;
                        if (!isAggroEligible(tEnt, *tHealth, myFaction)) return;
                        float ds = distSq(mobTx->position, tTx->position);
                        if (ds < bestDistSq) {
                            bestDistSq = ds;
                            bestEnt    = tEnt;
                        }
                    });
                if (bestEnt) {
                    target = bestEnt;
                    m->currentTarget_ = bestEnt->handle();
                }
            }

            // State transitions.
            float speedToUse = cc->moveSpeed;
            Vec2  dir{0.0f, 0.0f};

            if (m->state == Mob2DState::Return) {
                if (distSq(mobTx->position, m->spawnPosition) <= 64.0f) {
                    m->state = Mob2DState::Idle;
                } else {
                    speedToUse = m->returnSpeed;
                    dir = toward(mobTx->position, m->spawnPosition);
                }
            } else if (target) {
                Transform* tTx = target->getComponent<Transform>();
                if (!tTx) {
                    m->state = Mob2DState::Idle;
                    m->currentTarget_ = EntityHandle{};
                } else {
                    float distToSpawnSq  = distSq(mobTx->position, m->spawnPosition);
                    float distToTargetSq = distSq(mobTx->position, tTx->position);

                    if (distToSpawnSq > m->leashRadiusPx * m->leashRadiusPx) {
                        m->state = Mob2DState::Return;
                        m->currentTarget_ = EntityHandle{};
                        speedToUse = m->returnSpeed;
                        dir = toward(mobTx->position, m->spawnPosition);
                    } else if (distToTargetSq <= m->attackRangePx * m->attackRangePx) {
                        m->state = Mob2DState::Attack;
                        if (auto* atk = mobEnt->getComponent<Attack>()) {
                            atk->wantsToAttack_ = true;
                        }
                    } else {
                        m->state = Mob2DState::Chase;
                        speedToUse = m->chaseSpeed;
                        dir = toward(mobTx->position, tTx->position);
                    }
                }
            } else {
                m->state = Mob2DState::Idle;
                if (m->hasSpawnAnchor &&
                    distSq(mobTx->position, m->spawnPosition) > 64.0f) {
                    m->state = Mob2DState::Return;
                }
            }

            cc->moveSpeed     = speedToUse;
            cc->lastInputVec_ = dir;
        });
}

} // namespace fate
