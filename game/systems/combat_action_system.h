#pragma once
#include "engine/ecs/world.h"
#include "engine/input/input.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/shared/combat_system.h"
// xp_calculator.h no longer needed — server awards XP via SvPlayerState
#include "engine/core/logger.h"
#include "engine/render/sprite_batch.h"
#include "game/ui/game_viewport.h"
#include "engine/render/camera.h"
#include "game/components/sprite_component.h"
#include "game/shared/spatial_hash.h"
#include "imgui.h"
#ifndef FATE_SHIPPING
#include "engine/editor/editor.h"
#endif

#include "game/systems/quest_system.h"
#include "game/components/faction_component.h"
#include "game/components/zone_component.h"
#include "game/components/animator.h"
// pk_system.h no longer needed — server handles PK status via SvPlayerState

#include <vector>
#include <limits>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>

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

    // Callback: send attack command to server (server is authoritative for damage/kills)
    // GameApp wires this to do a PID reverse-lookup and call netClient_.sendAction().
    std::function<void(Entity* target)> onSendAttack;

    // Audio callback wired by GameApp (plays optimistic hit sound on the attack hit frame)
    std::function<void(const std::string&)> onPlaySFX;

    // Clear current combat target (called by GameApp when server confirms kill)
    void serverClearTarget() { currentTargetId_ = INVALID_ENTITY; autoAttackEnabled_ = false; }

    // Trigger attack windup animation on the local player sprite.
    // Called by GameApp when a skill is activated via the skill bar.
    // Plays the lunge animation toward the current target (no damage text —
    // skill results come from the server via SvSkillResultMsg).
    void triggerAttackWindup() {
        if (!world_) return;
        world_->forEach<PlayerController, Animator>(
            [&](Entity* entity, PlayerController* ctrl, Animator* anim) {
                if (!ctrl->isLocalPlayer) return;
                auto* spr = entity->getComponent<SpriteComponent>();

                attackIsMage_ = true;  // skills always use channeling pose
                ensureAttackAnimation(anim);

                anim->onHitFrame = nullptr;  // skills don't predict damage
                anim->onComplete = [this, spr]() {
                    attackAnimActive_ = false;
                    if (spr) spr->renderOffset = {0, 0};
                };
                anim->play("attack");
                attackAnimActive_ = true;
            }
        );
    }

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
        updateAttackLunge();
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

        // --- Attack range circle (editor debug visualization) ---
        if (world_) {
            world_->forEach<CombatControllerComponent, Transform>(
                [&](Entity* entity, CombatControllerComponent* combat, Transform* t) {
                    if (!combat->showAttackRange) return;

                    // Determine range in pixels
                    auto* statsComp = entity->getComponent<CharacterStatsComponent>();
                    float rangeTiles = 1.0f;
                    if (statsComp) {
                        bool isMage = (statsComp->stats.classDef.classType == ClassType::Mage);
                        rangeTiles = isMage ? kMageRange : statsComp->stats.classDef.attackRange;
                    }
                    float rangePixels = rangeTiles * Coords::TILE_SIZE;

                    // Draw circle as line segments
                    constexpr int segments = 32;
                    Color rangeColor(0.2f, 0.8f, 1.0f, 0.4f);
                    float depth = 96.0f;
                    for (int i = 0; i < segments; ++i) {
                        float a0 = (float)i / segments * 6.2832f;
                        float a1 = (float)(i + 1) / segments * 6.2832f;
                        Vec2 p0 = {t->position.x + std::cos(a0) * rangePixels,
                                   t->position.y + std::sin(a0) * rangePixels};
                        Vec2 p1 = {t->position.x + std::cos(a1) * rangePixels,
                                   t->position.y + std::sin(a1) * rangePixels};
                        // Draw each segment as a thin rect between p0 and p1
                        Vec2 mid = {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
                        float dx = p1.x - p0.x, dy = p1.y - p0.y;
                        float len = std::sqrt(dx * dx + dy * dy);
                        batch.drawRect(mid, {len, 1.0f}, rangeColor, depth);
                    }
                }
            );
        }

        // --- Nameplates above entities (ImGui screen-space overlay) ---
        // Use GameViewport for screen-space positioning (accounts for letterboxing)
        float gvpX = GameViewport::x();
        float gvpY = GameViewport::y();
        float gvpW = GameViewport::width();
        float gvpH = GameViewport::height();

        // Clip nameplate text to the game viewport bounds
        auto* fgDL = ImGui::GetForegroundDrawList();
        fgDL->PushClipRect(
            ImVec2(gvpX, gvpY),
            ImVec2(gvpX + gvpW, gvpY + gvpH), true);
        if (world_) {
            // Player nameplates
            world_->forEach<Transform, NameplateComponent>(
                [&](Entity* entity, Transform* t, NameplateComponent* np) {
                    if (!np->visible) return;
                    auto* spr = entity->getComponent<SpriteComponent>();
                    float spriteH = spr ? spr->size.y : 32.0f;

                    // Build nameplate text
                    char npBuf[64];
                    if (np->showLevel) {
                        std::snprintf(npBuf, sizeof(npBuf), "%s Lv%d", np->displayName.c_str(), np->displayLevel);
                    } else {
                        std::snprintf(npBuf, sizeof(npBuf), "%s", np->displayName.c_str());
                    }

                    // Project world position to screen and draw with ImGui
                    Vec2 worldPos = {t->position.x, t->position.y + spriteH * 0.5f + 4.0f};
                    Vec2 screenPos = camera.worldToScreen(worldPos, static_cast<int>(gvpW), static_cast<int>(gvpH));
                    ImVec2 textSize = ImGui::CalcTextSize(npBuf);
                    ImVec2 drawPos(gvpX + screenPos.x - textSize.x * 0.5f,
                                   gvpY + screenPos.y - textSize.y);
                    // Black outline
                    auto* dl = ImGui::GetForegroundDrawList();
                    ImU32 outlineCol = IM_COL32(0, 0, 0, 200);
                    ImU32 textCol = IM_COL32(
                        (int)(np->nameColor.r * 255), (int)(np->nameColor.g * 255),
                        (int)(np->nameColor.b * 255), (int)(np->nameColor.a * 255));
                    for (int ox = -1; ox <= 1; ++ox)
                        for (int oy = -1; oy <= 1; ++oy)
                            if (ox || oy)
                                dl->AddText(ImVec2(drawPos.x + ox, drawPos.y + oy), outlineCol, npBuf);
                    dl->AddText(drawPos, textCol, npBuf);

                    // Quest marker via ImGui
                    auto* qm = entity->getComponent<QuestMarkerComponent>();
                    if (qm && qm->currentState != MarkerState::None) {
                        const char* markerText = (qm->currentState == MarkerState::Available) ? "?" : "!";
                        ImU32 markerCol = (qm->currentState == MarkerState::TurnIn)
                            ? IM_COL32(255, 220, 0, 255) : IM_COL32(255, 255, 255, 255);
                        ImVec2 mSz = ImGui::CalcTextSize(markerText);
                        dl->AddText(ImVec2(drawPos.x + textSize.x * 0.5f - mSz.x * 0.5f, drawPos.y - mSz.y - 2), markerCol, markerText);
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
                    if (!mnp->visible) return;
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
                    // Project to screen and draw with ImGui
                    Vec2 worldPos = {t->position.x, t->position.y + spriteH * 0.5f + 4.0f};
                    Vec2 screenPos = camera.worldToScreen(worldPos, static_cast<int>(gvpW), static_cast<int>(gvpH));
                    ImVec2 textSz = ImGui::CalcTextSize(mnpBuf);
                    ImVec2 drawPos(gvpX + screenPos.x - textSz.x * 0.5f,
                                   gvpY + screenPos.y - textSz.y);
                    auto* dl = ImGui::GetForegroundDrawList();
                    ImU32 mobCol = IM_COL32(
                        (int)(mobColor.r * 255), (int)(mobColor.g * 255),
                        (int)(mobColor.b * 255), 255);
                    ImU32 outCol = IM_COL32(0, 0, 0, 200);
                    for (int ox = -1; ox <= 1; ++ox)
                        for (int oy = -1; oy <= 1; ++oy)
                            if (ox || oy)
                                dl->AddText(ImVec2(drawPos.x + ox, drawPos.y + oy), outCol, mnpBuf);
                    dl->AddText(drawPos, mobCol, mnpBuf);

                    // HP bar below name for all living mobs
                    if (enemyComp && enemyComp->stats.isAlive) {
                        Vec2 barPos = {t->position.x, t->position.y + spriteH * 0.5f + 1.0f};
                        float barW = 28.0f;
                        float barH = 2.0f;
                        float hpPct = (enemyComp->stats.maxHP > 0)
                            ? (float)enemyComp->stats.currentHP / (float)enemyComp->stats.maxHP
                            : 0.0f;

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

        // --- Floating damage/XP text (ImGui overlay, same pattern as nameplates) ---
        for (auto& ft : floatingTexts_) {
            float alpha = 1.0f - (ft.elapsed / ft.lifetime);
            if (alpha <= 0.0f) continue;

            float scale = ft.isCrit ? 1.3f : 1.0f;
            Vec2 screenPos = camera.worldToScreen(ft.position,
                static_cast<int>(gvpW), static_cast<int>(gvpH));

            ImFont* font = ImGui::GetFont();
            float fontSize = font->FontSize * scale;
            ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ft.text.c_str());
            ImVec2 drawPos(gvpX + screenPos.x - textSize.x * 0.5f,
                           gvpY + screenPos.y - textSize.y * 0.5f);

            ImU32 textCol = IM_COL32(
                (int)(ft.color.r * 255), (int)(ft.color.g * 255),
                (int)(ft.color.b * 255), (int)(alpha * 255));
            ImU32 outlineCol = IM_COL32(0, 0, 0, (int)(alpha * 200));

            for (int ox = -1; ox <= 1; ++ox)
                for (int oy = -1; oy <= 1; ++oy)
                    if (ox || oy)
                        fgDL->AddText(font, fontSize, ImVec2(drawPos.x + ox, drawPos.y + oy),
                                      outlineCol, ft.text.c_str());
            fgDL->AddText(font, fontSize, drawPos, textCol, ft.text.c_str());
        }

        fgDL->PopClipRect();
        batch.end();
    }

    // ------------------------------------------------------------------
    // Target info accessors (for HUD)
    // ------------------------------------------------------------------
    bool hasTarget() const { return currentTargetId_ != INVALID_ENTITY; }
    EntityId getTargetEntityId() const { return currentTargetId_; }

    std::string getTargetName() const {
        if (!hasTarget() || !world_) return "";
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return "";
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.enemyName;
        auto* cs = target->getComponent<CharacterStatsComponent>();
        if (cs) return cs->stats.characterName;
        return target->name();
    }

    int getTargetHP() const {
        if (!hasTarget() || !world_) return 0;
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return 0;
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.currentHP;
        auto* cs = target->getComponent<CharacterStatsComponent>();
        if (cs) return cs->stats.currentHP;
        return 0;
    }

    int getTargetMaxHP() const {
        if (!hasTarget() || !world_) return 0;
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return 0;
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.maxHP;
        auto* cs = target->getComponent<CharacterStatsComponent>();
        if (cs) return cs->stats.maxHP;
        return 0;
    }

    int getTargetLevel() const {
        if (!hasTarget() || !world_) return 0;
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return 0;
        auto* es = target->getComponent<EnemyStatsComponent>();
        if (es) return es->stats.level;
        auto* cs = target->getComponent<CharacterStatsComponent>();
        if (cs) return cs->stats.level;
        return 0;
    }

    // Viewport info for world-to-screen projection (set by GameApp before render)
    void setViewportInfo(int w, int h, Vec2 offset) {
        vpWidth_ = w; vpHeight_ = h; vpOffset_ = offset;
    }

    // Public floating text spawning — used by GameApp::onCombatEvent for server-driven combat
    void showDamageText(Vec2 pos, int damage, bool isCrit) { spawnDamageText(pos, damage, isCrit); }
    void showMissText(Vec2 pos) { spawnMissText(pos); }

private:
    int vpWidth_ = 1280;
    int vpHeight_ = 720;
    Vec2 vpOffset_ = {0, 0};

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

    // Attack windup animation state
    Vec2 lungeDirection_ = {0, 0};   // normalized direction toward target
    bool attackAnimActive_ = false;
    bool attackIsMage_ = false;      // true = channeling pose, false = melee lunge

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

    // Procedural attack offsets (temporary — replaced by real sprite frames later)
    static constexpr float kPullbackPx  = 2.0f;  // melee: pixels to pull back during windup
    static constexpr float kLungePx     = 3.0f;  // melee: pixels to lunge forward on strike
    static constexpr float kChannelDipPx = 1.5f;  // mage: pixels to settle downward while channeling

    // ------------------------------------------------------------------
    // processClickTargeting — left-click mob to target, click empty to deselect
    // ------------------------------------------------------------------
    void processClickTargeting() {
        auto& input = Input::instance();

        // Check for left-click or touch
        bool clicked = input.isMousePressed(SDL_BUTTON_LEFT);
        bool touched = input.isTouchPressed(0);
        if (!clicked && !touched) return;

        // In editor mode, ImGui reports WantCaptureMouse=true for the game viewport
        // (it IS an ImGui window). Only block clicks that are outside the viewport.
#ifndef FATE_SHIPPING
        if (Editor::instance().isOpen()) {
            Vec2 mpos = input.mousePosition();
            Vec2 vp = Editor::instance().viewportPos();
            Vec2 vs = Editor::instance().viewportSize();
            bool inViewport = mpos.x >= vp.x && mpos.x <= vp.x + vs.x &&
                              mpos.y >= vp.y && mpos.y <= vp.y + vs.y;
            if (!inViewport) return;
        } else
#endif
        {
            if (ImGui::GetIO().WantCaptureMouse) return;
        }

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

        // Convert screen coords to viewport-relative coords for editor mode
        Vec2 vpPos = {0, 0};
        Vec2 vpSize = {0, 0};
#ifndef FATE_SHIPPING
        auto& ed = Editor::instance();
        vpPos = ed.viewportPos();
        vpSize = ed.viewportSize();
        if (ed.isOpen() && vpSize.x > 0 && vpSize.y > 0) {
            screenPos = screenPos - vpPos;
        } else
#endif
        {
            ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            vpSize = {displaySize.x, displaySize.y};
        }
        Vec2 worldClick = camera->screenToWorld(screenPos, (int)vpSize.x, (int)vpSize.y);

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
            // No mob found — try to find a player (ghost) under the click
            Entity* hitPlayer = findPlayerAtPoint(worldClick);
            if (hitPlayer) {
                currentTargetId_ = hitPlayer->id();
                autoAttackEnabled_ = false;
                LOG_INFO("Combat", "Click-target player: %s (Lv %d)",
                         hitPlayer->name().c_str(), getTargetLevel());
            } else {
                // Clicked on empty space — clear target
                if (currentTargetId_ != INVALID_ENTITY) {
                    clearTarget();
                    LOG_INFO("Combat", "Target cleared");
                }
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
                if (input.isActionPressed(ActionId::Cancel)) {
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
                        auto* targetCharStats = target->getComponent<CharacterStatsComponent>();
                        bool targetAlive = false;
                        if (enemyComp) {
                            targetAlive = enemyComp->stats.isAlive;
                        } else if (targetCharStats) {
                            targetAlive = !targetCharStats->stats.isDead;
                        }
                        if (!targetAlive) {
                            clearTarget();
                        } else if (camera) {
                            // Clear target if target is off screen
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

                // ---- Attack action (buffered) ----
                if (input.consumeBuffered(ActionId::Attack)) {
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
    // tryAttackTarget — start attack windup animation + send to server
    //
    // Prediction (damage text, audio) is deferred to the animation hit frame
    // (~100ms after button press). The server message is sent immediately so
    // the response arrives by the time the hit frame fires.
    // ------------------------------------------------------------------
    void tryAttackTarget(Entity* player, Entity* target) {
        auto* statsComp = player->getComponent<CharacterStatsComponent>();
        auto* playerT   = player->getComponent<Transform>();
        auto* targetT   = target->getComponent<Transform>();

        if (!statsComp || !playerT || !targetT) return;

        // Determine target type: mob or player
        auto* enemyComp = target->getComponent<EnemyStatsComponent>();
        auto* targetCharStats = target->getComponent<CharacterStatsComponent>();
        bool targetIsMob = (enemyComp != nullptr);
        bool targetIsPlayer = (!targetIsMob && targetCharStats != nullptr
                               && target->getComponent<DamageableComponent>() != nullptr);

        if (!targetIsMob && !targetIsPlayer) return;

        // ---- Faction check: block same-faction PvP ----
        if (targetIsPlayer) {
            auto* attackerFaction = player->getComponent<FactionComponent>();
            auto* targetFaction   = target->getComponent<FactionComponent>();
            if (attackerFaction && targetFaction) {
                if (FactionRegistry::isSameFaction(attackerFaction->faction, targetFaction->faction)) {
                    LOG_DEBUG("Combat", "Cannot attack same-faction player");
                    return;
                }
            }
        }

        CharacterStats& ps = statsComp->stats;

        // Check target alive
        if (targetIsMob && !enemyComp->stats.isAlive) { clearTarget(); return; }
        if (targetIsPlayer && targetCharStats->stats.isDead) { clearTarget(); return; }

        // ---- CC check: crowd-controlled players cannot attack ----
        auto* playerCCComp = player->getComponent<CrowdControlComponent>();
        if (playerCCComp && !playerCCComp->cc.canAct()) {
            LOG_DEBUG("Combat", "Cannot attack — crowd controlled");
            return;
        }

        // ---- Range check (in tiles) ----
        float distPixels = playerT->position.distance(targetT->position);
        float distTiles  = distPixels / Coords::TILE_SIZE;

        bool isMage = (ps.classDef.classType == ClassType::Mage);
        float requiredRange = isMage ? kMageRange : ps.classDef.attackRange;

        if (distTiles > requiredRange) {
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

        // ---- Predict damage for deferred display on hit frame ----
        Vec2 textPos = targetT->position;
        struct PendingHit {
            Vec2 pos;
            int damage = 0;
            bool isCrit = false;
            bool hit = true;
            bool resisted = false;
        };
        PendingHit pending;
        pending.pos = textPos;

        if (targetIsMob) {
            EnemyStats& es = enemyComp->stats;
            if (isMage) {
                pending.resisted = CombatSystem::rollSpellResist(
                    ps.level, ps.getIntelligence(), es.level, es.magicResist);
                pending.damage = ps.calculateDamage(false, pending.isCrit);
                float mr = CombatSystem::getMobMagicDamageReduction(es.magicResist);
                pending.damage = static_cast<int>(pending.damage * (1.0f - mr));
                if (pending.damage < 1) pending.damage = 1;
                LOG_DEBUG("Combat", "[predict] Spell on %s for %d%s",
                          es.enemyName.c_str(), pending.damage, pending.isCrit ? " (CRIT)" : "");
            } else {
                pending.hit = CombatSystem::rollToHit(
                    ps.level, static_cast<int>(ps.getHitRate()), es.level, 0);
                pending.damage = ps.calculateDamage(false, pending.isCrit);
                pending.damage = CombatSystem::applyArmorReduction(pending.damage, es.armor);
                if (pending.damage < 1) pending.damage = 1;
                LOG_DEBUG("Combat", "[predict] Physical on %s for %d%s",
                          es.enemyName.c_str(), pending.damage, pending.isCrit ? " (CRIT)" : "");
            }
        } else {
            CharacterStats& ts = targetCharStats->stats;
            if (isMage) {
                pending.resisted = CombatSystem::rollSpellResist(
                    ps.level, ps.getIntelligence(), ts.level, ts.getMagicResist());
                pending.damage = ps.calculateDamage(false, pending.isCrit);
                pending.damage = static_cast<int>(std::round(pending.damage * CombatSystem::getPvPDamageMultiplier()));
                float mr = CombatSystem::getPlayerMagicDamageReduction(ts.getMagicResist());
                pending.damage = static_cast<int>(pending.damage * (1.0f - mr));
                if (pending.damage < 1) pending.damage = 1;
                LOG_DEBUG("Combat", "[predict] PvP spell on %s for %d%s",
                          ts.characterName.c_str(), pending.damage, pending.isCrit ? " (CRIT)" : "");
            } else {
                pending.hit = CombatSystem::rollToHit(
                    ps.level, static_cast<int>(ps.getHitRate()),
                    ts.level, static_cast<int>(ts.getEvasion()));
                pending.damage = ps.calculateDamage(false, pending.isCrit);
                pending.damage = static_cast<int>(std::round(pending.damage * CombatSystem::getPvPDamageMultiplier()));
                pending.damage = CombatSystem::applyArmorReduction(pending.damage, ts.getArmor());
                if (pending.damage < 1) pending.damage = 1;
                LOG_DEBUG("Combat", "[predict] PvP physical on %s for %d%s",
                          ts.characterName.c_str(), pending.damage, pending.isCrit ? " (CRIT)" : "");
            }
        }

        // ---- Send attack to server immediately (in-flight during windup) ----
        if (onSendAttack) onSendAttack(target);

        // ---- Start windup animation with deferred hit-frame callback ----
        auto* playerAnim = player->getComponent<Animator>();
        auto* playerSpr  = player->getComponent<SpriteComponent>();

        if (playerAnim) {
            attackIsMage_ = isMage;
            if (!isMage) computeLungeDirection(player);
            ensureAttackAnimation(playerAnim);

            // On hit frame (~100ms): show predicted damage text + play audio
            playerAnim->onHitFrame = [this, pending, isMage]() {
                if (pending.resisted) {
                    spawnResistText(pending.pos);
                } else if (!pending.hit) {
                    spawnMissText(pending.pos);
                } else {
                    spawnDamageText(pending.pos, pending.damage, pending.isCrit);
                }
                // Optimistic audio on hit frame
                if (onPlaySFX) {
                    if (!pending.hit || pending.resisted) onPlaySFX("miss");
                    else if (pending.isCrit) onPlaySFX("hit_crit");
                    else onPlaySFX(isMage ? "hit_skill" : "hit_melee");
                }
            };

            playerAnim->onComplete = [this, playerSpr]() {
                attackAnimActive_ = false;
                if (playerSpr) playerSpr->renderOffset = {0, 0};
            };

            playerAnim->play("attack");
            attackAnimActive_ = true;
        } else {
            // No animator — immediate fallback (same feel as before, minus flash)
            if (pending.resisted) spawnResistText(pending.pos);
            else if (!pending.hit) spawnMissText(pending.pos);
            else spawnDamageText(pending.pos, pending.damage, pending.isCrit);
        }
    }

    // ------------------------------------------------------------------
    // ensureAttackAnimation — register placeholder if no real sprite sheet yet
    //
    // Uses the current sprite frame as the base so the sprite doesn't visually
    // change (no attack frames exist yet). The windup feel comes entirely from
    // the procedural renderOffset lunge. When real sprites arrive, register
    // a proper "attack" animation with real frames and remove the lunge code.
    // ------------------------------------------------------------------
    void ensureAttackAnimation(Animator* anim) {
        if (anim->animations.count("attack")) return;
        int baseFrame = anim->getCurrentFrame();
        //                name       start  frames  fps   loop   hitFrame
        anim->addAnimation("attack", baseFrame, 3, 10.0f, false, 1);
    }

    // ------------------------------------------------------------------
    // computeLungeDirection — normalized vector from player toward target
    // ------------------------------------------------------------------
    void computeLungeDirection(Entity* player) {
        lungeDirection_ = {0, -1};  // default: lunge upward
        if (currentTargetId_ == INVALID_ENTITY || !world_) return;
        Entity* target = world_->getEntity(currentTargetId_);
        if (!target) return;
        auto* pt = player->getComponent<Transform>();
        auto* tt = target->getComponent<Transform>();
        if (!pt || !tt) return;
        Vec2 dir = tt->position - pt->position;
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 0.001f) {
            lungeDirection_ = {dir.x / len, dir.y / len};
        }
    }

    // ------------------------------------------------------------------
    // updateAttackLunge — per-frame procedural offset during attack animation
    //
    // Temporary until real sprite animation frames exist.
    //   Melee (Warrior/Archer): directional pullback → lunge → recover
    //   Mage:  stationary channeling settle (slight downward dip → release)
    // ------------------------------------------------------------------
    void updateAttackLunge() {
        if (!attackAnimActive_) return;

        world_->forEach<PlayerController, Animator>(
            [&](Entity* entity, PlayerController* ctrl, Animator* anim) {
                if (!ctrl->isLocalPlayer) return;
                auto* spr = entity->getComponent<SpriteComponent>();
                if (!spr) return;

                if (anim->currentAnimation != "attack" || !anim->playing) {
                    spr->renderOffset = {0, 0};
                    attackAnimActive_ = false;
                    return;
                }

                auto it = anim->animations.find("attack");
                if (it == anim->animations.end()) return;
                const auto& def = it->second;

                // Normalized progress through the animation (0 to 1)
                float progress = anim->timer * def.frameRate / def.frameCount;
                if (progress > 1.0f) progress = 1.0f;

                if (attackIsMage_) {
                    // Mage channeling: settle into casting stance then release.
                    // Slight downward dip (positive Y = down in screen space)
                    // that holds through the channel and snaps back on cast.
                    //   0.00–0.33  Settle into stance (0 → +dip)
                    //   0.33–0.80  Hold channel (stay at +dip)
                    //   0.80–1.00  Release (dip → 0)
                    float dip;
                    if (progress < 0.33f) {
                        float t = progress / 0.33f;
                        dip = kChannelDipPx * t * t;  // ease-in
                    } else if (progress < 0.80f) {
                        dip = kChannelDipPx;
                    } else {
                        float t = (progress - 0.80f) / 0.20f;
                        dip = kChannelDipPx * (1.0f - t);
                    }
                    spr->renderOffset = {0, dip};
                } else {
                    // Melee lunge: pullback → strike → recover along
                    // the direction vector from player to target.
                    //   0.00–0.33  Windup:  ease from 0 → -pullback
                    //   0.33–0.50  Strike:  snap from -pullback → +lunge
                    //   0.50–1.00  Recover: ease from +lunge → 0
                    float offset;
                    if (progress < 0.33f) {
                        float t = progress / 0.33f;
                        offset = -kPullbackPx * t;
                    } else if (progress < 0.50f) {
                        float t = (progress - 0.33f) / 0.17f;
                        offset = -kPullbackPx + (kPullbackPx + kLungePx) * t;
                    } else {
                        float t = (progress - 0.50f) / 0.50f;
                        offset = kLungePx * (1.0f - t * t);  // ease-out
                    }
                    spr->renderOffset = lungeDirection_ * offset;
                }
            }
        );
    }

    // ------------------------------------------------------------------
    // onMobDeath — visual-only (all rewards come from server via SvPlayerState/SvCombatEvent)
    // ------------------------------------------------------------------
    void onMobDeath(Entity* /*player*/, Entity* mob) {
        auto* enemyComp = mob->getComponent<EnemyStatsComponent>();
        auto* mobT      = mob->getComponent<Transform>();

        if (!enemyComp || !mobT) return;

        EnemyStats& es = enemyComp->stats;

        LOG_INFO("Combat", "Mob %s (Lv %d) died — server awards XP/gold/honor",
                 es.enemyName.c_str(), es.level);

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
    // findPlayerAtPoint — find a targetable ghost player under click
    // Skips the local player and entities without DamageableComponent.
    // ------------------------------------------------------------------
    Entity* findPlayerAtPoint(Vec2 worldClick) {
        if (!world_) return nullptr;
        Entity* hit = nullptr;
        world_->forEach<Transform, CharacterStatsComponent>(
            [&](Entity* e, Transform* t, CharacterStatsComponent* cs) {
                if (hit) return;
                // Must be damageable
                if (!e->getComponent<DamageableComponent>()) return;
                // Skip the local player
                auto* pc = e->getComponent<PlayerController>();
                if (pc && pc->isLocalPlayer) return;
                // Skip dead players
                if (cs->stats.isDead) return;
                // Skip mobs (they have EnemyStatsComponent, handled separately)
                if (e->getComponent<EnemyStatsComponent>()) return;
                auto* spr = e->getComponent<SpriteComponent>();
                if (!spr || !spr->enabled) return;
                Vec2 half = spr->size * 0.5f;
                if (worldClick.x >= t->position.x - half.x &&
                    worldClick.x <= t->position.x + half.x &&
                    worldClick.y >= t->position.y - half.y &&
                    worldClick.y <= t->position.y + half.y) {
                    hit = e;
                }
            }
        );
        return hit;
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
        ft.color    = isCrit ? Color(1.0f, 0.6f, 0.1f) : Color::white();
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
        ft.color    = Color(0.6f, 0.3f, 0.9f);  // purple
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

    void spawnHealText(Vec2 pos, int amount) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "+%d", amount);

        FloatingText ft;
        ft.position = pos;
        ft.text     = buf;
        ft.color    = Color(0.2f, 0.9f, 0.3f);  // green
        ft.lifetime = kTextLifetime;
        ft.elapsed  = 0.0f;
        ft.startY   = pos.y;
        ft.isCrit   = false;
        floatingTexts_.push_back(ft);
    }

    void spawnBlockText(Vec2 pos) {
        FloatingText ft;
        ft.position = pos;
        ft.text     = "Block";
        ft.color    = Color(0.4f, 0.7f, 1.0f);  // light blue
        ft.lifetime = kTextLifetime;
        ft.elapsed  = 0.0f;
        ft.startY   = pos.y;
        ft.isCrit   = false;
        floatingTexts_.push_back(ft);
    }
};

} // namespace fate
