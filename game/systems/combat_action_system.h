#pragma once
#include "engine/ecs/world.h"
#include "engine/input/input.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/shared/combat_system.h"
#include "game/shared/xp_calculator.h"
#include "engine/core/logger.h"
#include "engine/render/text_renderer.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include "game/components/sprite_component.h"
#include "game/shared/spatial_hash.h"
#include "imgui.h"

#include "game/systems/quest_system.h"
#include "game/components/faction_component.h"
#include "game/components/zone_component.h"

#include <vector>
#include <limits>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace fate {

// ============================================================================
// CombatActionSystem — TWOM-style Option B targeting and auto-attack
//
// Warriors & Archers (two-press auto-attack):
//   1. Space with NO target   -> select nearest attackable mob
//   2. Space WITH target      -> enable auto-attack
//   3. Auto-attack fires every cooldown until target dies / out of range / Escape
//
// Mages (manual cast per press):
//   1. Space with NO target   -> select nearest attackable mob
//   2. Space WITH target      -> cast one attack (no auto-attack)
// ============================================================================
class CombatActionSystem : public System {
public:
    const char* name() const override { return "CombatActionSystem"; }

    Camera* camera = nullptr;  // Set by GameApp for click-to-target

    void update(float dt) override {
        gameTime_ += dt;

        // Rebuild mob spatial hash each frame
        mobGrid_.clear();
        world_->forEach<Transform, EnemyStatsComponent>(
            [&](Entity* entity, Transform* t, EnemyStatsComponent* es) {
                if (es->stats.isAlive) {
                    mobGrid_.insert(entity->id(), t->position);
                }
            }
        );
        mobGrid_.finalize();

        processClickTargeting();
        processPlayerCombat(dt);
        updateFloatingTexts(dt);
    }

    // ------------------------------------------------------------------
    // Rendering — called from GameApp::onRender() after the sprite pass
    // ------------------------------------------------------------------
    void renderFloatingTexts(SpriteBatch& batch, Camera& camera) {
        Mat4 vp = camera.getViewProjection();
        batch.begin(vp);

        // --- Target selection marker (pulsing ring around selected mob) ---
        if (currentTargetId_ != INVALID_ENTITY && world_) {
            Entity* target = world_->getEntity(currentTargetId_);
            if (target) {
                auto* targetT = target->getComponent<Transform>();
                auto* targetS = target->getComponent<SpriteComponent>();
                if (targetT && targetS && targetS->enabled) {
                    // Pulsing alpha for the selection indicator
                    float pulse = 0.5f + 0.3f * std::sin(gameTime_ * 4.0f);
                    Color markerColor(1.0f, 0.3f, 0.3f, pulse); // red pulsing

                    // Draw a border/ring around the target sprite
                    Vec2 pos = targetT->position;
                    Vec2 sz = targetS->size * 1.3f; // slightly larger than sprite
                    float t = 1.5f; // border thickness

                    // Top
                    batch.drawRect({pos.x, pos.y + sz.y * 0.5f}, {sz.x, t}, markerColor, 95.0f);
                    // Bottom
                    batch.drawRect({pos.x, pos.y - sz.y * 0.5f}, {sz.x, t}, markerColor, 95.0f);
                    // Left
                    batch.drawRect({pos.x - sz.x * 0.5f, pos.y}, {t, sz.y}, markerColor, 95.0f);
                    // Right
                    batch.drawRect({pos.x + sz.x * 0.5f, pos.y}, {t, sz.y}, markerColor, 95.0f);
                }
            }
        }

        // --- Nameplates above entities ---
        auto* textRenderer = &TextRenderer::instance();
        if (textRenderer->defaultFont() && world_) {
            // Player nameplates
            auto* font = textRenderer->defaultFont();
            world_->forEach<Transform, NameplateComponent>(
                [&](Entity* entity, Transform* t, NameplateComponent* np) {
                    if (!np->visible || !font) return;
                    auto* spr = entity->getComponent<SpriteComponent>();
                    float spriteH = spr ? spr->size.y : 32.0f;

                    // Build nameplate text
                    char npBuf[64];
                    if (np->showLevel) {
                        std::snprintf(npBuf, sizeof(npBuf), "%s Lv%d", np->displayName.c_str(), np->displayLevel);
                    } else {
                        std::snprintf(npBuf, sizeof(npBuf), "%s", np->displayName.c_str());
                    }

                    // Center text above sprite
                    Vec2 textSize = font->measureText(npBuf);
                    float textW = textSize.x * np->fontSize;
                    Vec2 namePos = {t->position.x - textW * 0.5f, t->position.y + spriteH * 0.5f + 4.0f};

                    textRenderer->drawWorldYUp(batch, npBuf, namePos, np->nameColor, np->fontSize, 85.0f);

                    // Track the top of the nameplate stack for quest marker positioning
                    float nextYAbove = namePos.y + font->measureText(npBuf).y * np->fontSize + 2.0f;

                    // Guild name below (centered)
                    if (np->showGuildSymbol && !np->guildName.empty()) {
                        Vec2 guildSize = font->measureText(np->guildName);
                        float guildW = guildSize.x * np->fontSize * 0.85f;
                        Vec2 guildPos = {t->position.x - guildW * 0.5f, namePos.y + 10.0f};
                        textRenderer->drawWorldYUp(batch, np->guildName, guildPos,
                            Color(0.4f, 1.0f, 0.4f), np->fontSize * 0.85f, 85.0f);
                    }

                    // NPC role subtitle (e.g., "[Merchant]", "[Quest]") — grey, below name
                    if (!np->roleSubtitle.empty()) {
                        Vec2 subSize = font->measureText(np->roleSubtitle);
                        float subScale = np->fontSize * 0.75f;
                        float subW = subSize.x * subScale;
                        // Position below the nameplate text (Y-up: subtract to go lower)
                        Vec2 subPos = {t->position.x - subW * 0.5f, namePos.y - subSize.y * subScale - 2.0f};
                        textRenderer->drawWorldYUp(batch, np->roleSubtitle, subPos,
                            Color(0.7f, 0.7f, 0.7f, 1.0f), subScale, 85.0f);
                    }

                    // Quest marker (? or !) above nameplate
                    auto* qm = entity->getComponent<QuestMarkerComponent>();
                    if (qm && qm->currentState != MarkerState::None) {
                        const char* markerText = (qm->currentState == MarkerState::Available) ? "?" : "!";
                        Color markerColor;
                        if (qm->currentState == MarkerState::TurnIn) {
                            markerColor = Color(1.0f, 0.863f, 0.0f, 1.0f); // Yellow {255,220,0}
                        } else {
                            // Available — color by quest tier
                            switch (qm->highestTier) {
                                case QuestTier::Novice:     markerColor = Color(0.0f, 0.784f, 0.0f, 1.0f);   break; // Green {0,200,0}
                                case QuestTier::Apprentice: markerColor = Color(0.706f, 0.863f, 0.0f, 1.0f); break; // Yellow-Green {180,220,0}
                                case QuestTier::Adept:      markerColor = Color(1.0f, 0.863f, 0.0f, 1.0f);   break; // Yellow {255,220,0}
                                default: /* Starter */      markerColor = Color(1.0f, 1.0f, 1.0f, 1.0f);     break; // White
                            }
                        }
                        float markerScale = np->fontSize * 1.3f;
                        Vec2 markerSize = font->measureText(markerText);
                        float markerW = markerSize.x * markerScale;
                        Vec2 markerPos = {t->position.x - markerW * 0.5f, nextYAbove};
                        textRenderer->drawWorldYUp(batch, markerText, markerPos, markerColor, markerScale, 86.0f);
                    }
                }
            );

            // Mob nameplates (color based on level difference to local player)
            int playerLevel = 1;
            world_->forEach<CharacterStatsComponent, PlayerController>(
                [&](Entity*, CharacterStatsComponent* psc, PlayerController* pc) {
                    if (pc->isLocalPlayer) playerLevel = psc->stats.level;
                }
            );

            world_->forEach<Transform, MobNameplateComponent>(
                [&](Entity* entity, Transform* t, MobNameplateComponent* mnp) {
                    if (!mnp->visible || !font) return;
                    auto* enemyComp = entity->getComponent<EnemyStatsComponent>();
                    if (enemyComp && !enemyComp->stats.isAlive) return; // Hide dead mob names

                    auto* spr = entity->getComponent<SpriteComponent>();
                    float spriteH = spr ? spr->size.y : 32.0f;

                    // Color by level difference (TWOM-style)
                    Color mobColor = MobLevelColors::getColor(mnp->level, playerLevel);

                    char mnpBuf[64];
                    if (mnp->showLevel) {
                        if (mnp->isBoss) {
                            std::snprintf(mnpBuf, sizeof(mnpBuf), "[Boss] %s Lv%d", mnp->displayName.c_str(), mnp->level);
                        } else {
                            std::snprintf(mnpBuf, sizeof(mnpBuf), "%s Lv%d", mnp->displayName.c_str(), mnp->level);
                        }
                    } else {
                        if (mnp->isBoss) {
                            std::snprintf(mnpBuf, sizeof(mnpBuf), "[Boss] %s", mnp->displayName.c_str());
                        } else {
                            std::snprintf(mnpBuf, sizeof(mnpBuf), "%s", mnp->displayName.c_str());
                        }
                    }
                    // Center text above sprite
                    Vec2 mnpTextSize = font->measureText(mnpBuf);
                    float mnpTextW = mnpTextSize.x * mnp->fontSize;
                    Vec2 namePos = {t->position.x - mnpTextW * 0.5f, t->position.y + spriteH * 0.5f + 4.0f};

                    textRenderer->drawWorldYUp(batch, mnpBuf, namePos, mobColor, mnp->fontSize, 85.0f);

                    // HP bar below name for targeted or damaged mobs
                    if (enemyComp && enemyComp->stats.currentHP < enemyComp->stats.maxHP) {
                        Vec2 barPos = {t->position.x, namePos.y - 3.0f};
                        float barW = 28.0f;
                        float barH = 2.0f;
                        float hpPct = (float)enemyComp->stats.currentHP / (float)enemyComp->stats.maxHP;

                        // Background (dark red)
                        batch.drawRect(barPos, {barW, barH}, Color(0.3f, 0.0f, 0.0f, 0.8f), 84.0f);
                        // Fill (green to red based on HP)
                        float fillW = barW * hpPct;
                        Color hpColor = hpPct > 0.5f ? Color(0.2f, 0.8f, 0.2f, 0.9f) : Color(0.8f, 0.2f, 0.2f, 0.9f);
                        batch.drawRect({barPos.x - (barW - fillW) * 0.5f, barPos.y}, {fillW, barH}, hpColor, 84.5f);
                    }
                }
            );
        }

        // --- Floating damage/XP text ---
        if (textRenderer->defaultFont()) {
            for (auto& ft : floatingTexts_) {
                float alpha = 1.0f - (ft.elapsed / ft.lifetime);
                if (alpha <= 0.0f) continue;

                Color c = ft.color;
                c.a = alpha;

                float scale = ft.isCrit ? 1.3f : 1.0f;

                textRenderer->drawWorldYUp(batch, ft.text, ft.position, c, scale, 90.0f);
            }
        }

        batch.end();
    }

    // ------------------------------------------------------------------
    // Target info accessors (for HUD)
    // ------------------------------------------------------------------
    bool hasTarget() const { return currentTargetId_ != INVALID_ENTITY; }

    std::string getTargetName() const {
        if (!hasTarget() || !world_) return "";
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return "";
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.enemyName;
        return target->name();
    }

    int getTargetHP() const {
        if (!hasTarget() || !world_) return 0;
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return 0;
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.currentHP;
        return 0;
    }

    int getTargetMaxHP() const {
        if (!hasTarget() || !world_) return 0;
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return 0;
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.maxHP;
        return 0;
    }

    int getTargetLevel() const {
        if (!hasTarget() || !world_) return 0;
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return 0;
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.level;
        return 0;
    }

private:
    // ------------------------------------------------------------------
    // Floating damage text
    // ------------------------------------------------------------------
    struct FloatingText {
        Vec2 position;
        std::string text;
        Color color;
        float lifetime;   // total duration
        float elapsed;     // time since spawn
        float startY;      // original Y for float-up calculation
        bool isCrit;
    };

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------
    float gameTime_ = 0.0f;

    // Current target tracking (for the local player)
    EntityId currentTargetId_ = INVALID_ENTITY;
    bool autoAttackEnabled_ = false;
    float attackCooldownRemaining_ = 0.0f;

    // Spatial hash for mob lookups (rebuilt each frame)
    SpatialHash mobGrid_{128.0f};  // 4-tile cells

    // Floating texts
    std::vector<FloatingText> floatingTexts_;

    // ------------------------------------------------------------------
    // Tuning constants
    // ------------------------------------------------------------------
    static constexpr float kTextLifetime   = 1.2f;
    static constexpr float kTextFloatSpeed = 30.0f;   // pixels per second upward
    static constexpr float kMageRange      = 7.0f;    // tiles
    static constexpr float kTargetSearchRange = 10.0f; // tiles

    // ------------------------------------------------------------------
    // processClickTargeting — left-click mob to target, click empty to deselect
    // ------------------------------------------------------------------
    void processClickTargeting() {
        auto& input = Input::instance();

        // Check for left-click or touch
        bool clicked = input.isMousePressed(SDL_BUTTON_LEFT);
        bool touched = input.isTouchPressed(0);
        if (!clicked && !touched) return;
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (!camera || !world_) return;

        // Check if NPCInteractionSystem already consumed this click
        TargetingComponent* targeting = nullptr;
        world_->forEach<TargetingComponent, PlayerController>(
            [&](Entity* e, TargetingComponent* tc, PlayerController* pc) {
                if (pc->isLocalPlayer) targeting = tc;
            }
        );
        if (targeting && targeting->clickConsumed) return;

        // Use touch position if touched, mouse position if clicked
        Vec2 screenPos = touched ? input.touchPosition(0) : input.mousePosition();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        Vec2 worldClick = camera->screenToWorld(screenPos, (int)displaySize.x, (int)displaySize.y);

        // Find mob under click via spatial hash + sprite bounds check
        EntityId hitId = mobGrid_.findAtPoint(worldClick,
            [&](EntityId id, Vec2 point) -> bool {
                Entity* e = world_->getEntity(id);
                if (!e) return false;
                auto* spr = e->getComponent<SpriteComponent>();
                auto* t = e->getComponent<Transform>();
                if (!spr || !spr->enabled || !t) return false;
                Vec2 half = spr->size * 0.5f;
                return point.x >= t->position.x - half.x &&
                       point.x <= t->position.x + half.x &&
                       point.y >= t->position.y - half.y &&
                       point.y <= t->position.y + half.y;
            });

        Entity* hitMob = (hitId != INVALID_ENTITY) ? world_->getEntity(hitId) : nullptr;

        if (hitMob) {
            // Clicked on a mob — target it (switch target if already targeting something)
            currentTargetId_ = hitMob->id();
            autoAttackEnabled_ = false;
            LOG_INFO("Combat", "Click-target: %s (Lv %d)",
                     hitMob->name().c_str(), getTargetLevel());
        } else {
            // Clicked on empty space — clear target
            if (currentTargetId_ != INVALID_ENTITY) {
                clearTarget();
                LOG_INFO("Combat", "Target cleared");
            }
        }
    }

    // ------------------------------------------------------------------
    // processPlayerCombat — main combat state machine
    // ------------------------------------------------------------------
    void processPlayerCombat(float dt) {
        auto& input = Input::instance();

        world_->forEach<Transform, PlayerController>(
            [&](Entity* entity, Transform* transform, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;

                auto* statsComp = entity->getComponent<CharacterStatsComponent>();
                if (!statsComp) return;
                CharacterStats& playerStats = statsComp->stats;

                // Dead players cannot attack
                if (playerStats.isDead) return;

                // Tick attack cooldown
                if (attackCooldownRemaining_ > 0.0f) {
                    attackCooldownRemaining_ -= dt;
                }

                // ---- Escape clears target ----
                if (input.isKeyPressed(SDL_SCANCODE_ESCAPE)) {
                    clearTarget();
                    return;
                }

                // Validate current target is still alive / exists / on screen
                if (currentTargetId_ != INVALID_ENTITY) {
                    Entity* target = world_->getEntity(currentTargetId_);
                    if (!target) {
                        clearTarget();
                    } else {
                        auto* enemyComp = target->getComponent<EnemyStatsComponent>();
                        if (!enemyComp || !enemyComp->stats.isAlive) {
                            clearTarget();
                        } else if (camera) {
                            // Clear target if mob is off screen
                            auto* targetT = target->getComponent<Transform>();
                            if (targetT) {
                                Rect view = camera->getVisibleBounds();
                                Vec2 p = targetT->position;
                                if (p.x < view.x || p.x > view.x + view.w ||
                                    p.y < view.y || p.y > view.y + view.h) {
                                    LOG_INFO("Combat", "Target left view — cleared");
                                    clearTarget();
                                }
                            }
                        }
                    }
                }

                bool isMage = (playerStats.classDef.classType == ClassType::Mage);

                // ---- Space pressed ----
                if (input.isKeyPressed(SDL_SCANCODE_SPACE)) {
                    if (currentTargetId_ == INVALID_ENTITY) {
                        // No target — select nearest mob
                        Entity* nearest = findNearestMob(
                            transform->position, kTargetSearchRange);
                        if (nearest) {
                            currentTargetId_ = nearest->id();
                            autoAttackEnabled_ = false;
                            LOG_INFO("Combat",
                                     "Target selected: %s (Lv %d)",
                                     nearest->name().c_str(),
                                     getTargetLevel());
                        }
                    } else {
                        // Already have a target
                        if (isMage) {
                            // Mage: each Space press = one cast
                            Entity* target = world_->getEntity(currentTargetId_);
                            if (target && attackCooldownRemaining_ <= 0.0f) {
                                tryAttackTarget(entity, target);
                            }
                        } else {
                            // Warrior / Archer: enable auto-attack
                            if (!autoAttackEnabled_) {
                                autoAttackEnabled_ = true;
                                LOG_INFO("Combat", "Auto-attack enabled");
                            }
                        }
                    }
                }

                // ---- Auto-attack tick (Warriors / Archers only) ----
                if (!isMage && autoAttackEnabled_
                    && currentTargetId_ != INVALID_ENTITY
                    && attackCooldownRemaining_ <= 0.0f)
                {
                    Entity* target = world_->getEntity(currentTargetId_);
                    if (target) {
                        tryAttackTarget(entity, target);
                    }
                }
            }
        );
    }

    // ------------------------------------------------------------------
    // tryAttackTarget — resolve one attack (physical or spell)
    // ------------------------------------------------------------------
    void tryAttackTarget(Entity* player, Entity* target) {
        // ---- Faction check: block same-faction PvP ----
        auto* attackerFaction = player->getComponent<FactionComponent>();
        auto* targetFaction   = target->getComponent<FactionComponent>();
        if (attackerFaction && targetFaction) {
            if (FactionRegistry::isSameFaction(attackerFaction->faction, targetFaction->faction)) {
                LOG_DEBUG("Combat", "Cannot attack same-faction player");
                return;
            }
        }

        auto* statsComp = player->getComponent<CharacterStatsComponent>();
        auto* enemyComp = target->getComponent<EnemyStatsComponent>();
        auto* playerT   = player->getComponent<Transform>();
        auto* targetT   = target->getComponent<Transform>();

        if (!statsComp || !enemyComp || !playerT || !targetT) return;

        CharacterStats& ps = statsComp->stats;
        EnemyStats&     es = enemyComp->stats;

        if (!es.isAlive) { clearTarget(); return; }

        // ---- Range check (in tiles) ----
        float distPixels = playerT->position.distance(targetT->position);
        float distTiles  = distPixels / Coords::TILE_SIZE;

        bool isMage = (ps.classDef.classType == ClassType::Mage);
        float requiredRange = isMage ? kMageRange : ps.classDef.attackRange;

        if (distTiles > requiredRange) {
            // Out of range — disable auto-attack for melee/archer so they
            // don't fire the instant they step back in range.
            if (!isMage) {
                autoAttackEnabled_ = false;
                LOG_INFO("Combat", "Target out of range (%.1f tiles > %.1f)",
                         distTiles, requiredRange);
            }
            return;
        }

        // ---- Set cooldown ----
        auto* combatCtrl = player->getComponent<CombatControllerComponent>();
        float cooldown = combatCtrl ? combatCtrl->baseAttackCooldown : 1.5f;
        attackCooldownRemaining_ = cooldown;

        Vec2 textPos = targetT->position;

        if (isMage) {
            // ---- Spell attack ----
            bool resisted = CombatSystem::rollSpellResist(
                ps.level, ps.getIntelligence(), es.level, es.magicResist);

            if (resisted) {
                spawnResistText(textPos);
                LOG_DEBUG("Combat", "Spell resisted by %s", es.enemyName.c_str());
                return;
            }

            // Calculate spell damage
            bool isCrit = false;
            int damage = ps.calculateDamage(false, isCrit);

            es.takeDamageFrom(player->id(), damage);
            spawnDamageText(textPos, damage, isCrit);

            LOG_DEBUG("Combat", "Spell hit %s for %d%s",
                      es.enemyName.c_str(), damage, isCrit ? " (CRIT)" : "");

            // Mages use mana but do not spend it for basic attack
            // No fury generation for mages

        } else {
            // ---- Physical attack (Warrior / Archer) ----
            bool hit = CombatSystem::rollToHit(
                ps.level,
                static_cast<int>(ps.getHitRate()),
                es.level,
                0);

            if (!hit) {
                spawnMissText(textPos);
                LOG_DEBUG("Combat", "Attack missed %s", es.enemyName.c_str());
                return;
            }

            bool isCrit = false;
            int damage = ps.calculateDamage(false, isCrit);

            es.takeDamageFrom(player->id(), damage);
            spawnDamageText(textPos, damage, isCrit);

            // Fury generation
            float furyGain = isCrit
                ? ps.classDef.furyPerCriticalHit
                : ps.classDef.furyPerBasicAttack;
            ps.addFury(furyGain);

            LOG_DEBUG("Combat", "Hit %s for %d%s (+%.1f fury)",
                      es.enemyName.c_str(), damage,
                      isCrit ? " (CRIT)" : "", furyGain);
        }

        // ---- Check for mob death ----
        if (!es.isAlive) {
            onMobDeath(player, target);
        }
    }

    // ------------------------------------------------------------------
    // onMobDeath — XP, honor, respawn scheduling
    // ------------------------------------------------------------------
    void onMobDeath(Entity* player, Entity* mob) {
        auto* statsComp = player->getComponent<CharacterStatsComponent>();
        auto* enemyComp = mob->getComponent<EnemyStatsComponent>();
        auto* mobT      = mob->getComponent<Transform>();

        if (!statsComp || !enemyComp || !mobT) return;

        CharacterStats& ps = statsComp->stats;
        EnemyStats&     es = enemyComp->stats;

        // Notify quest system of mob death
        auto* questSys = world_->getSystem<QuestSystem>();
        if (questSys) {
            questSys->onMobDeath(es.enemyName);
        }

        // Calculate and award XP
        int xp = XPCalculator::calculateXPReward(es.xpReward, es.level, ps.level);
        int prevLevel = ps.level;
        ps.addXP(xp);

        LOG_INFO("Combat", "Player killed %s (Lv %d) for %d XP",
                 es.enemyName.c_str(), es.level, xp);

        // Spawn XP text
        if (xp > 0) {
            spawnXPText(mobT->position, xp);
        }

        // Check for level up
        if (ps.level > prevLevel) {
            auto* playerT = player->getComponent<Transform>();
            if (playerT) {
                spawnLevelUpText(playerT->position);
            }
            LOG_INFO("Combat", "LEVEL UP! Now level %d", ps.level);
        }

        // Award gold
        if (es.goldDropChance > 0.0f && es.maxGoldDrop > 0) {
            thread_local std::mt19937 goldRng{std::random_device{}()};
            std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
            if (chanceDist(goldRng) <= es.goldDropChance) {
                std::uniform_int_distribution<int> goldDist(es.minGoldDrop, es.maxGoldDrop);
                int gold = goldDist(goldRng);
                if (gold > 0) {
                    auto* invComp = player->getComponent<InventoryComponent>();
                    if (invComp) {
                        invComp->inventory.addGold(gold);
                    }
                    // Gold text
                    char goldBuf[32];
                    std::snprintf(goldBuf, sizeof(goldBuf), "+%d Gold", gold);
                    FloatingText ft;
                    ft.position = {mobT->position.x, mobT->position.y + 12.0f};
                    ft.text = goldBuf;
                    ft.color = Color(1.0f, 0.84f, 0.0f); // gold
                    ft.lifetime = kTextLifetime;
                    ft.elapsed = 0.0f;
                    ft.startY = ft.position.y;
                    ft.isCrit = false;
                    floatingTexts_.push_back(ft);
                    LOG_INFO("Combat", "Gained %d gold from %s", gold, es.enemyName.c_str());
                }
            }
        }

        // Award honor if mob has honorReward
        if (es.honorReward > 0) {
            ps.honor += es.honorReward;
            LOG_INFO("Combat", "Gained %d honor from %s",
                     es.honorReward, es.enemyName.c_str());
        }

        // Hide the mob sprite (SpawnSystem handles respawn)
        auto* sprite = mob->getComponent<SpriteComponent>();
        if (sprite) {
            sprite->enabled = false;
        }

        // Clear our target since the mob is dead
        clearTarget();
    }

    // ------------------------------------------------------------------
    // findNearestMob — spatial hash lookup for closest living enemy
    // ------------------------------------------------------------------
    Entity* findNearestMob(Vec2 playerPos, float rangeTiles) {
        float rangePixels = rangeTiles * Coords::TILE_SIZE;
        EntityId id = mobGrid_.findNearest(playerPos, rangePixels);
        return (id != INVALID_ENTITY) ? world_->getEntity(id) : nullptr;
    }

    // ------------------------------------------------------------------
    // clearTarget
    // ------------------------------------------------------------------
    void clearTarget() {
        currentTargetId_ = INVALID_ENTITY;
        autoAttackEnabled_ = false;
    }

    // ------------------------------------------------------------------
    // isInHomeVillage — check if attacker is in their faction's home zone
    // Used for PK exception: no Red penalty for killing enemy faction in your village.
    // Called by future PvP kill handler before processPvPKill().
    // ------------------------------------------------------------------
    bool isInHomeVillage(Entity* attacker) {
        auto* factionComp = attacker->getComponent<FactionComponent>();
        if (!factionComp || factionComp->faction == Faction::None) return false;

        auto* playerT = attacker->getComponent<Transform>();
        if (!playerT) return false;

        // Scan all zone entities to find which zone the attacker is in
        bool inHomeVillage = false;
        world_->forEach<ZoneComponent, Transform>(
            [&](Entity* zoneEntity, ZoneComponent* zone, Transform* zoneT) {
                if (inHomeVillage) return; // already found
                if (zone->contains(zoneT->position, playerT->position)) {
                    if (FactionRegistry::isHomeVillage(factionComp->faction, zone->zoneName)) {
                        inHomeVillage = true;
                    }
                }
            }
        );
        return inHomeVillage;
    }

    // ------------------------------------------------------------------
    // Floating text management
    // ------------------------------------------------------------------
    void updateFloatingTexts(float dt) {
        for (auto& ft : floatingTexts_) {
            ft.elapsed += dt;
            // World space is Y-up, so floating text goes UP (positive Y)
            ft.position.y = ft.startY + (kTextFloatSpeed * ft.elapsed);
        }

        // Remove expired texts
        floatingTexts_.erase(
            std::remove_if(floatingTexts_.begin(), floatingTexts_.end(),
                [](const FloatingText& ft) { return ft.elapsed >= ft.lifetime; }),
            floatingTexts_.end()
        );
    }

    void spawnDamageText(Vec2 pos, int damage, bool isCrit) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", damage);

        FloatingText ft;
        ft.position = pos;
        ft.text     = buf;
        ft.color    = isCrit ? Color(1.0f, 0.6f, 0.0f) : Color::white();
        ft.lifetime = kTextLifetime;
        ft.elapsed  = 0.0f;
        ft.startY   = pos.y;
        ft.isCrit   = isCrit;
        floatingTexts_.push_back(ft);
    }

    void spawnMissText(Vec2 pos) {
        FloatingText ft;
        ft.position = pos;
        ft.text     = "Miss";
        ft.color    = Color(0.5f, 0.5f, 0.5f);  // gray
        ft.lifetime = kTextLifetime;
        ft.elapsed  = 0.0f;
        ft.startY   = pos.y;
        ft.isCrit   = false;
        floatingTexts_.push_back(ft);
    }

    void spawnResistText(Vec2 pos) {
        FloatingText ft;
        ft.position = pos;
        ft.text     = "Resist";
        ft.color    = Color(0.66f, 0.33f, 0.97f);  // purple
        ft.lifetime = kTextLifetime;
        ft.elapsed  = 0.0f;
        ft.startY   = pos.y;
        ft.isCrit   = false;
        floatingTexts_.push_back(ft);
    }

    void spawnXPText(Vec2 pos, int xp) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "+%d XP", xp);

        FloatingText ft;
        ft.position = pos;
        ft.text     = buf;
        ft.color    = Color::yellow();
        ft.lifetime = kTextLifetime;
        ft.elapsed  = 0.0f;
        ft.startY   = pos.y;
        ft.isCrit   = false;
        floatingTexts_.push_back(ft);
    }

    void spawnLevelUpText(Vec2 pos) {
        FloatingText ft;
        ft.position = pos;
        ft.text     = "LEVEL UP!";
        ft.color    = Color(1.0f, 0.84f, 0.0f);  // gold
        ft.lifetime = kTextLifetime * 1.5f;        // lingers a bit longer
        ft.elapsed  = 0.0f;
        ft.startY   = pos.y;
        ft.isCrit   = true;  // renders at 1.3x scale
        floatingTexts_.push_back(ft);
    }
};

} // namespace fate
