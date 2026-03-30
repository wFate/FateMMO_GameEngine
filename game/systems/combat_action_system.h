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
#include "engine/render/camera.h"
#include "game/components/sprite_component.h"
#include "game/shared/spatial_hash.h"
#include "engine/render/sdf_text.h"

#include "game/systems/combat_text_config.h"
#include "game/systems/quest_system.h"
#include "game/systems/npc_interaction_system.h"
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

namespace fate
{

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
    class CombatActionSystem : public System
    {
    public:
        const char *name() const override { return "CombatActionSystem"; }

        Camera *camera = nullptr;                   // Set by GameApp for click-to-target
        NPCInteractionSystem *npcSystem_ = nullptr; // Set by GameApp for NPC interaction

        // Callback: send attack command to server (server is authoritative for damage/kills)
        // GameApp wires this to do a PID reverse-lookup and call netClient_.sendAction().
        std::function<void(Entity *target)> onSendAttack;

        // Audio callback wired by GameApp (plays optimistic hit sound on the attack hit frame)
        std::function<void(const std::string &)> onPlaySFX;

        // Clear current combat target (called by GameApp when server confirms kill)
        void serverClearTarget()
        {
            currentTargetId_ = INVALID_ENTITY;
            autoAttackEnabled_ = false;
            targetIsNPC_ = false;
        }

        // Trigger attack windup animation on the local player sprite.
        // Called by GameApp when a skill is activated via the skill bar.
        // Plays the lunge animation toward the current target (no damage text —
        // skill results come from the server via SvSkillResultMsg).
        void triggerAttackWindup()
        {
            if (!world_)
                return;
            world_->forEach<PlayerController, Animator>(
                [&](Entity *entity, PlayerController *ctrl, Animator *anim)
                {
                    if (!ctrl->isLocalPlayer)
                        return;
                    auto *spr = entity->getComponent<SpriteComponent>();

                    attackPose_ = AttackPose::Channel; // skills always use channeling pose
                    ensureAttackAnimation(anim);

                    anim->onHitFrame = nullptr; // skills don't predict damage
                    anim->onComplete = [this, spr]()
                    {
                        attackAnimActive_ = false;
                        if (spr)
                            spr->renderOffset = {0, 0};
                    };
                    anim->play("attack");
                    attackAnimActive_ = true;
                });
        }

        void update(float dt) override
        {
            gameTime_ += dt;

            // Rebuild mob spatial hash each frame
            mobGrid_.clear();
            world_->forEach<Transform, EnemyStatsComponent>(
                [&](Entity *entity, Transform *t, EnemyStatsComponent *es)
                {
                    if (es->stats.isAlive)
                    {
                        mobGrid_.insert(entity->id(), t->position);
                    }
                });
            mobGrid_.finalize();

            processClickTargeting();
            processPlayerCombat(dt);
            updateAttackLunge();
            updateFloatingTexts(dt);
        }

        // ------------------------------------------------------------------
        // Rendering — called from GameApp::onRender() after the sprite pass
        // ------------------------------------------------------------------
        void renderFloatingTexts(SpriteBatch &batch, Camera &camera, SDFText &sdf)
        {
            Mat4 vp = camera.getViewProjection();
            batch.begin(vp);

            constexpr float NP_DEPTH = 85.0f;  // nameplates
            constexpr float FT_DEPTH = 86.0f;  // floating text
            float uiScale = std::min(camera.getVisibleBounds().h / 270.0f, 2.5f);
            float NP_FONT = 11.0f * uiScale; // nameplate font size (scaled)
            Color outlineColor(0.0f, 0.0f, 0.0f, 0.78f);

            // Helper: draw text centered at worldPos with 1px black outline
            auto drawOutlinedWorld = [&](const std::string &text, Vec2 worldPos,
                                         float fontSize, Color color, float depth)
            {
                Vec2 sz = sdf.measure(text, fontSize);
                Vec2 pos = {worldPos.x - sz.x * 0.5f, worldPos.y};
                for (int ox = -1; ox <= 1; ++ox)
                    for (int oy = -1; oy <= 1; ++oy)
                        if (ox || oy)
                            sdf.drawWorld(batch, text, {pos.x + ox, pos.y + oy},
                                          fontSize, outlineColor, depth);
                sdf.drawWorld(batch, text, pos, fontSize, color, depth + 0.01f);
            };

            // --- Target selection marker (pulsing circle at target's feet) ---
            if (currentTargetId_ != INVALID_ENTITY && world_)
            {
                Entity *target = world_->getEntity(currentTargetId_);
                if (target)
                {
                    auto *targetT = target->getComponent<Transform>();
                    auto *targetS = target->getComponent<SpriteComponent>();
                    if (targetT && targetS && targetS->enabled)
                    {
                        float pulse = 0.5f + 0.3f * std::sin(gameTime_ * 4.0f);
                        // Circle at feet — drawn behind sprite (below sprite depth)
                        float spriteH = targetS->size.y;
                        float spriteW = targetS->size.x;
                        Vec2 feetCenter = {targetT->position.x,
                                           targetT->position.y - spriteH * 0.5f};
                        float radius = spriteW * 0.3f;
                        float depth = targetT->depth - 0.5f;
                        Color ringColor(1.0f, 0.3f, 0.3f, pulse * 0.6f);
                        Color fillColor(1.0f, 0.2f, 0.2f, pulse * 0.02f);
                        batch.drawCircle(feetCenter, radius, fillColor, depth, 12);
                        batch.drawRing(feetCenter, radius, 1.5f, ringColor, depth + 0.1f, 12);
                    }
                }
            }

            // --- Disengage range circle (editor debug visualization) ---
            // Note: main debug ring is drawn by GameApp::renderAttackRange in the
            // DebugOverlays pass. This path handles the in-play overlay when the
            // combat system owns the SpriteBatch.
            if (world_)
            {
                world_->forEach<CombatControllerComponent, Transform>(
                    [&](Entity *, CombatControllerComponent *combat, Transform *t)
                    {
                        if (!combat->showDisengageRange)
                            return;
                        if (combat->disengageRange <= 0.0f)
                            return;
                        float rangePixels = combat->disengageRange * Coords::TILE_SIZE;
                        constexpr int segments = 32;
                        Color rangeColor(0.2f, 0.8f, 1.0f, 0.4f);
                        for (int i = 0; i < segments; ++i)
                        {
                            float a0 = (float)i / segments * 6.2832f;
                            float a1 = (float)(i + 1) / segments * 6.2832f;
                            Vec2 p0 = {t->position.x + std::cos(a0) * rangePixels,
                                       t->position.y + std::sin(a0) * rangePixels};
                            Vec2 p1 = {t->position.x + std::cos(a1) * rangePixels,
                                       t->position.y + std::sin(a1) * rangePixels};
                            Vec2 mid = {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
                            float dx = p1.x - p0.x, dy = p1.y - p0.y;
                            float len = std::sqrt(dx * dx + dy * dy);
                            batch.drawRect(mid, {len, 1.0f}, rangeColor, 96.0f);
                        }
                    });
            }

            // --- Nameplates (world-space SDFText, renders inside Scene FBO) ---
            // Skip nameplates when a UI panel is open
            if (!Input::instance().isUIBlocking() && world_)
            {
                // Player nameplates
                world_->forEach<Transform, NameplateComponent>(
                    [&](Entity *entity, Transform *t, NameplateComponent *np)
                    {
                        if (!np->visible)
                            return;
                        auto *spr = entity->getComponent<SpriteComponent>();
                        float spriteH = spr ? spr->size.y : 32.0f;

                        char npBuf[64];
                        if (np->showLevel)
                            std::snprintf(npBuf, sizeof(npBuf), "%s Lv%d", np->displayName.c_str(), np->displayLevel);
                        else
                            std::snprintf(npBuf, sizeof(npBuf), "%s", np->displayName.c_str());

                        Vec2 npPos = {t->position.x, t->position.y + spriteH * 0.5f + np->yOffset * uiScale};
                        drawOutlinedWorld(npBuf, npPos, NP_FONT, np->nameColor, NP_DEPTH);

                        // Guild tag above player name
                        float topY = npPos.y + NP_FONT; // top of name text
                        if (np->showGuild && !np->guildName.empty())
                        {
                            float guildFont = np->guildFontSize * NP_FONT / 0.7f;
                            Vec2 guildPos = {t->position.x, topY + np->guildYOffset * uiScale};

                            // Guild icon (16x16) to the left of guild name
                            if (!np->guildIconPath.empty())
                            {
                                if (!np->guildIconTex)
                                    np->guildIconTex = TextureCache::instance().load(np->guildIconPath);
                                if (np->guildIconTex)
                                {
                                    Vec2 guildSz = sdf.measure(np->guildName, guildFont);
                                    float iconSize = 16.0f * uiScale;
                                    float totalW = iconSize + 2.0f * uiScale + guildSz.x;
                                    float iconX = t->position.x - totalW * 0.5f + iconSize * 0.5f;
                                    SpriteDrawParams iconP;
                                    iconP.position = {iconX, guildPos.y + guildFont * 0.35f};
                                    iconP.size = {iconSize, iconSize};
                                    iconP.depth = NP_DEPTH;
                                    batch.draw(np->guildIconTex, iconP);
                                    // Shift guild text right to account for icon
                                    guildPos.x = iconX + iconSize * 0.5f + 2.0f * uiScale + guildSz.x * 0.5f;
                                }
                            }

                            drawOutlinedWorld(np->guildName, guildPos, guildFont, np->guildColor, NP_DEPTH);
                            topY = guildPos.y + guildFont;
                        }

                        // Quest marker above nameplate (or guild tag)
                        auto *qm = entity->getComponent<QuestMarkerComponent>();
                        if (qm && qm->currentState != MarkerState::None)
                        {
                            const char *marker = (qm->currentState == MarkerState::Available) ? "?" : "!";
                            Color markerCol = (qm->currentState == MarkerState::TurnIn)
                                                  ? Color(1.0f, 0.86f, 0.0f, 1.0f)
                                                  : Color::white();
                            Vec2 markerPos = {npPos.x, topY + NP_FONT * 0.2f};
                            drawOutlinedWorld(marker, markerPos, NP_FONT, markerCol, NP_DEPTH);
                        }
                    });

                // Mob nameplates (color based on level difference to local player)
                int playerLevel = 1;
                world_->forEach<CharacterStatsComponent, PlayerController>(
                    [&](Entity *, CharacterStatsComponent *psc, PlayerController *pc)
                    {
                        if (pc->isLocalPlayer)
                            playerLevel = psc->stats.level;
                    });

                world_->forEach<Transform, MobNameplateComponent>(
                    [&](Entity *entity, Transform *t, MobNameplateComponent *mnp)
                    {
                        if (!mnp->visible)
                            return;
                        auto *enemyComp = entity->getComponent<EnemyStatsComponent>();
                        if (enemyComp && !enemyComp->stats.isAlive)
                            return;

                        auto *spr = entity->getComponent<SpriteComponent>();
                        float spriteH = spr ? spr->size.y : 32.0f;
                        Color mobColor = MobLevelColors::getColor(mnp->level, playerLevel);

                        char mnpBuf[64];
                        if (mnp->showLevel)
                        {
                            if (mnp->isBoss)
                                std::snprintf(mnpBuf, sizeof(mnpBuf), "[Boss] %s Lv%d", mnp->displayName.c_str(), mnp->level);
                            else
                                std::snprintf(mnpBuf, sizeof(mnpBuf), "%s Lv%d", mnp->displayName.c_str(), mnp->level);
                        }
                        else
                        {
                            if (mnp->isBoss)
                                std::snprintf(mnpBuf, sizeof(mnpBuf), "[Boss] %s", mnp->displayName.c_str());
                            else
                                std::snprintf(mnpBuf, sizeof(mnpBuf), "%s", mnp->displayName.c_str());
                        }

                        Vec2 npPos = {t->position.x, t->position.y + spriteH * 0.5f + mnp->yOffset * uiScale};
                        drawOutlinedWorld(mnpBuf, npPos, NP_FONT, mobColor, NP_DEPTH);

                        // HP bar below name
                        if (enemyComp && enemyComp->stats.isAlive)
                        {
                            Vec2 barPos = {t->position.x, t->position.y + spriteH * 0.5f + 1.0f};
                            float barW = 32.0f * uiScale;
                            float barH = 4.0f * uiScale;
                            float hpPct = (enemyComp->stats.maxHP > 0)
                                              ? (float)enemyComp->stats.currentHP / (float)enemyComp->stats.maxHP
                                              : 0.0f;
                            batch.drawRect(barPos, {barW + 2.0f, barH + 2.0f}, Color(0.08f, 0.08f, 0.1f, 0.85f), 83.5f);
                            batch.drawRect(barPos, {barW, barH}, Color(0.2f, 0.08f, 0.08f, 0.8f), 84.0f);
                            float fillW = barW * hpPct;
                            Color hpColor = hpPct > 0.5f ? Color(0.2f, 0.8f, 0.2f, 0.9f) : Color(0.8f, 0.2f, 0.2f, 0.9f);
                            batch.drawRect({barPos.x - (barW - fillW) * 0.5f, barPos.y}, {fillW, barH}, hpColor, 84.5f);
                        }
                    });
            }

            // --- Floating damage/XP text (world-space SDFText) ---
            for (auto &ft : floatingTexts_)
            {
                // Fade: full opacity until fadeDelay, then linear fade
                float fadeStart = ft.fadeDelay;
                float fadeWindow = ft.lifetime - fadeStart;
                float alpha;
                if (ft.elapsed < fadeStart)
                {
                    alpha = 1.0f;
                }
                else if (fadeWindow > 0.0f)
                {
                    alpha = 1.0f - (ft.elapsed - fadeStart) / fadeWindow;
                }
                else
                {
                    alpha = 0.0f;
                }
                if (alpha <= 0.0f)
                    continue;

                // Pop: lerp from popScale to scale over popDuration
                float currentScale;
                if (ft.popScale != ft.scale && ft.elapsed < ft.popDuration && ft.popDuration > 0.0f)
                {
                    float t = ft.elapsed / ft.popDuration;
                    currentScale = ft.popScale + (ft.scale - ft.popScale) * t;
                }
                else
                {
                    currentScale = ft.scale;
                }

                float fontSize = ft.fontSize * uiScale * currentScale;
                Color c = ft.color;
                c.a = alpha;
                Color outline = ft.outlineColor;
                outline.a = alpha * ft.outlineColor.a;
                Vec2 sz = sdf.measure(ft.text, fontSize);
                Vec2 pos = {ft.position.x - sz.x * 0.5f, ft.position.y};
                // 1px outline via 8-directional offset
                for (int ox = -1; ox <= 1; ++ox)
                    for (int oy = -1; oy <= 1; ++oy)
                        if (ox || oy)
                            sdf.drawWorld(batch, ft.text, {pos.x + ox, pos.y + oy},
                                          fontSize, outline, FT_DEPTH);
                sdf.drawWorld(batch, ft.text, pos, fontSize, c, FT_DEPTH + 0.01f);
            }

            batch.end();
        }

        // ------------------------------------------------------------------
        // Target info accessors (for HUD)
        // ------------------------------------------------------------------
        bool hasTarget() const { return currentTargetId_ != INVALID_ENTITY; }
        bool isTargetNPC() const { return targetIsNPC_; }
        EntityId getTargetEntityId() const { return currentTargetId_; }

        std::string getTargetName() const
        {
            if (!hasTarget() || !world_)
                return "";
            Entity *target = world_->getEntity(currentTargetId_);
            if (!target)
                return "";
            auto *npc = target->getComponent<NPCComponent>();
            if (npc)
                return npc->displayName;
            auto *es = target->getComponent<EnemyStatsComponent>();
            if (es)
                return es->stats.enemyName;
            auto *cs = target->getComponent<CharacterStatsComponent>();
            if (cs)
                return cs->stats.characterName;
            return target->name();
        }

        int getTargetHP() const
        {
            if (!hasTarget() || !world_)
                return 0;
            Entity *target = world_->getEntity(currentTargetId_);
            if (!target)
                return 0;
            auto *es = target->getComponent<EnemyStatsComponent>();
            if (es)
                return es->stats.currentHP;
            auto *cs = target->getComponent<CharacterStatsComponent>();
            if (cs)
                return cs->stats.currentHP;
            return 0;
        }

        int getTargetMaxHP() const
        {
            if (!hasTarget() || !world_)
                return 0;
            Entity *target = world_->getEntity(currentTargetId_);
            if (!target)
                return 0;
            auto *es = target->getComponent<EnemyStatsComponent>();
            if (es)
                return es->stats.maxHP;
            auto *cs = target->getComponent<CharacterStatsComponent>();
            if (cs)
                return cs->stats.maxHP;
            return 0;
        }

        int getTargetLevel() const
        {
            if (!hasTarget() || !world_)
                return 0;
            Entity *target = world_->getEntity(currentTargetId_);
            if (!target)
                return 0;
            auto *es = target->getComponent<EnemyStatsComponent>();
            if (es)
                return es->stats.level;
            auto *cs = target->getComponent<CharacterStatsComponent>();
            if (cs)
                return cs->stats.level;
            return 0;
        }

        // Public floating text spawning — used by GameApp::onCombatEvent for server-driven combat
        void showDamageText(Vec2 pos, int damage, bool isCrit) { spawnDamageText(pos, damage, isCrit); }
        void showMissText(Vec2 pos) { spawnMissText(pos); }
        void showResistText(Vec2 pos) { spawnResistText(pos); }

    private:
        // ------------------------------------------------------------------
        // Floating damage text
        // ------------------------------------------------------------------
        struct FloatingText
        {
            Vec2 position;
            std::string text;
            Color color;
            Color outlineColor = Color(0.0f, 0.0f, 0.0f, 0.78f);
            float fontSize = 14.0f;
            float lifetime = 1.2f;
            float elapsed = 0.0f;
            float startY = 0.0f;
            float scale = 1.0f;
            float floatDx = 0.0f;  // precomputed cos(angle) * speed
            float floatDy = 30.0f; // precomputed sin(angle) * speed
            float fadeDelay = 0.0f;
            float popScale = 1.0f;
            float popDuration = 0.15f;
        };

        // ------------------------------------------------------------------
        // State
        // ------------------------------------------------------------------
        float gameTime_ = 0.0f;

        // Current target tracking (for the local player)
        EntityId currentTargetId_ = INVALID_ENTITY;
        bool autoAttackEnabled_ = false;
        bool targetIsNPC_ = false; // true when current target is an NPC (no combat)
        float attackCooldownRemaining_ = 0.0f;

        // Attack windup animation state
        Vec2 lungeDirection_ = {0, 0}; // normalized direction toward target
        bool attackAnimActive_ = false;
        enum class AttackPose
        {
            Melee,
            BowDraw,
            Channel
        };
        AttackPose attackPose_ = AttackPose::Melee;

        // Debug: monotonic counter for attack sends (verify no double-fire)
        int attackSendSeq_ = 0;

        // Archer deferred release: SFX + miss/resist fires at 80% of animation
        bool archerReleaseFired_ = false;
        std::function<void()> pendingReleaseCallback_;

        // Spatial hash for mob lookups (rebuilt each frame)
        SpatialHash mobGrid_{128.0f}; // 4-tile cells

        // Floating texts
        std::vector<FloatingText> floatingTexts_;

        // ------------------------------------------------------------------
        // Tuning constants
        // ------------------------------------------------------------------
        static constexpr float kMageRange = 7.0f;          // tiles
        static constexpr float kTargetSearchRange = 10.0f; // tiles

        // Procedural attack offsets (temporary — replaced by real sprite frames later)
        static constexpr float kPullbackPx = 2.0f;   // melee: pixels to pull back during windup
        static constexpr float kLungePx = 3.0f;      // melee: pixels to lunge forward on strike
        static constexpr float kChannelDipPx = 1.5f; // mage: pixels to settle downward while channeling

        // ------------------------------------------------------------------
        // processClickTargeting — left-click mob to target, click empty to deselect
        // ------------------------------------------------------------------
        void processClickTargeting()
        {
            auto &input = Input::instance();

            // Check for left-click or touch
            bool clicked = input.isMousePressed(SDL_BUTTON_LEFT);
            bool touched = input.isTouchPressed(0);
            if (!clicked && !touched)
                return;

            // In editor mode, ImGui reports WantCaptureMouse=true for the game viewport
            // (it IS an ImGui window). Only block clicks that are outside the viewport.
#ifndef FATE_SHIPPING
            if (Editor::instance().isOpen())
            {
                // Block all targeting when scene is not playing
                if (!Editor::instance().inPlayMode())
                    return;
                Vec2 mpos = input.mousePosition();
                Vec2 vp = Editor::instance().viewportPos();
                Vec2 vs = Editor::instance().viewportSize();
                bool inViewport = mpos.x >= vp.x && mpos.x <= vp.x + vs.x &&
                                  mpos.y >= vp.y && mpos.y <= vp.y + vs.y;
                if (!inViewport)
                    return;
            }
            else
#endif
            {
                if (ImGui::GetIO().WantCaptureMouse)
                    return;
            }

            if (!camera || !world_)
                return;

            // Don't process clicks while NPC dialogue is open
            if (npcSystem_ && npcSystem_->dialogueOpen)
                return;

            // Use touch position if touched, mouse position if clicked
            Vec2 screenPos = touched ? input.touchPosition(0) : input.mousePosition();

            // Convert screen coords to viewport-relative coords for editor mode
            Vec2 vpPos = {0, 0};
            Vec2 vpSize = {0, 0};
#ifndef FATE_SHIPPING
            auto &ed = Editor::instance();
            vpPos = ed.viewportPos();
            vpSize = ed.viewportSize();
            if (ed.isOpen() && vpSize.x > 0 && vpSize.y > 0)
            {
                screenPos = screenPos - vpPos;
            }
            else
#endif
            {
                ImVec2 displaySize = ImGui::GetIO().DisplaySize;
                vpSize = {displaySize.x, displaySize.y};
            }
            Vec2 worldClick = camera->screenToWorld(screenPos, (int)vpSize.x, (int)vpSize.y);

            // Find mob under click via spatial hash + sprite bounds check
            EntityId hitId = mobGrid_.findAtPoint(worldClick,
                                                  [&](EntityId id, Vec2 point) -> bool
                                                  {
                                                      Entity *e = world_->getEntity(id);
                                                      if (!e)
                                                          return false;
                                                      auto *spr = e->getComponent<SpriteComponent>();
                                                      auto *t = e->getComponent<Transform>();
                                                      if (!spr || !spr->enabled || !t)
                                                          return false;
                                                      Vec2 half = spr->size * 0.5f;
                                                      return point.x >= t->position.x - half.x &&
                                                             point.x <= t->position.x + half.x &&
                                                             point.y >= t->position.y - half.y &&
                                                             point.y <= t->position.y + half.y;
                                                  });

            Entity *hitMob = (hitId != INVALID_ENTITY) ? world_->getEntity(hitId) : nullptr;

            if (hitMob)
            {
                // Clicked on a mob — target it (switch target if already targeting something)
                currentTargetId_ = hitMob->id();
                autoAttackEnabled_ = false;
                targetIsNPC_ = false;
                LOG_INFO("Combat", "Click-target: %s (Lv %d)",
                         hitMob->name().c_str(), getTargetLevel());
            }
            else
            {
                // No mob found — try to find a player (ghost) under the click
                Entity *hitPlayer = findPlayerAtPoint(worldClick);
                if (hitPlayer)
                {
                    currentTargetId_ = hitPlayer->id();
                    autoAttackEnabled_ = false;
                    targetIsNPC_ = false;
                    LOG_INFO("Combat", "Click-target player: %s (Lv %d)",
                             hitPlayer->name().c_str(), getTargetLevel());
                }
                else
                {
                    // No player found — try to find an NPC under the click
                    Entity *hitNPC = findNPCAtPoint(worldClick);
                    if (hitNPC)
                    {
                        currentTargetId_ = hitNPC->id();
                        autoAttackEnabled_ = false;
                        targetIsNPC_ = true;
                        auto *npcComp = hitNPC->getComponent<NPCComponent>();
                        LOG_INFO("Combat", "Click-target NPC: %s",
                                 npcComp ? npcComp->displayName.c_str() : "unknown");
                    }
                    else
                    {
                        // Clicked on empty space — clear target
                        if (currentTargetId_ != INVALID_ENTITY)
                        {
                            clearTarget();
                            LOG_INFO("Combat", "Target cleared");
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------------
        // processPlayerCombat — main combat state machine
        // ------------------------------------------------------------------
        void processPlayerCombat(float dt)
        {
            auto &input = Input::instance();

            world_->forEach<Transform, PlayerController>(
                [&](Entity *entity, Transform *transform, PlayerController *ctrl)
                {
                    if (!ctrl->isLocalPlayer)
                        return;

                    auto *statsComp = entity->getComponent<CharacterStatsComponent>();
                    if (!statsComp)
                        return;
                    CharacterStats &playerStats = statsComp->stats;

                    // Dead players cannot attack
                    if (playerStats.isDead)
                        return;

                    // Tick attack cooldown
                    if (attackCooldownRemaining_ > 0.0f)
                    {
                        attackCooldownRemaining_ -= dt;
                    }

                    // ---- Escape clears target ----
                    if (input.isActionPressed(ActionId::Cancel))
                    {
                        clearTarget();
                        return;
                    }

                    // Validate current target is still alive / exists / on screen
                    if (currentTargetId_ != INVALID_ENTITY)
                    {
                        Entity *target = world_->getEntity(currentTargetId_);
                        if (!target)
                        {
                            clearTarget();
                        }
                        else if (targetIsNPC_)
                        {
                            // NPCs don't have alive/dead state — only clear if off screen
                            if (camera)
                            {
                                auto *targetT = target->getComponent<Transform>();
                                if (targetT)
                                {
                                    Rect view = camera->getVisibleBounds();
                                    Vec2 p = targetT->position;
                                    if (p.x < view.x || p.x > view.x + view.w ||
                                        p.y < view.y || p.y > view.y + view.h)
                                    {
                                        LOG_INFO("Combat", "NPC target left view — cleared");
                                        clearTarget();
                                    }
                                }
                            }
                        }
                        else
                        {
                            auto *enemyComp = target->getComponent<EnemyStatsComponent>();
                            auto *targetCharStats = target->getComponent<CharacterStatsComponent>();
                            bool targetAlive = false;
                            if (enemyComp)
                            {
                                targetAlive = enemyComp->stats.isAlive;
                            }
                            else if (targetCharStats)
                            {
                                targetAlive = !targetCharStats->stats.isDead;
                            }
                            if (!targetAlive)
                            {
                                clearTarget();
                            }
                            else if (camera)
                            {
                                // Clear target if target is off screen
                                auto *targetT = target->getComponent<Transform>();
                                if (targetT)
                                {
                                    Rect view = camera->getVisibleBounds();
                                    Vec2 p = targetT->position;
                                    if (p.x < view.x || p.x > view.x + view.w ||
                                        p.y < view.y || p.y > view.y + view.h)
                                    {
                                        LOG_INFO("Combat", "Target left view — cleared");
                                        clearTarget();
                                    }
                                }
                            }
                        }
                    }

                    bool isMage = (playerStats.classDef.classType == ClassType::Mage);

                    // ---- Attack action (buffered) ----
                    if (input.consumeBuffered(ActionId::Attack))
                    {
                        if (currentTargetId_ == INVALID_ENTITY)
                        {
                            // No target — select nearest mob
                            Entity *nearest = findNearestMob(
                                transform->position, kTargetSearchRange);
                            if (nearest)
                            {
                                currentTargetId_ = nearest->id();
                                autoAttackEnabled_ = false;
                                targetIsNPC_ = false;
                                LOG_INFO("Combat",
                                         "Target selected: %s (Lv %d)",
                                         nearest->name().c_str(),
                                         getTargetLevel());
                            }
                        }
                        else if (targetIsNPC_)
                        {
                            // NPC targeted — pressing action opens dialogue
                            Entity *npcEntity = world_->getEntity(currentTargetId_);
                            if (npcEntity && npcSystem_)
                            {
                                auto *npcComp = npcEntity->getComponent<NPCComponent>();
                                auto *npcT = npcEntity->getComponent<Transform>();
                                if (npcComp && npcT)
                                {
                                    float dx = transform->position.x - npcT->position.x;
                                    float dy = transform->position.y - npcT->position.y;
                                    float distTiles = std::sqrt(dx * dx + dy * dy) / Coords::TILE_SIZE;
                                    if (distTiles <= npcComp->interactionRadius)
                                    {
                                        npcSystem_->openDialogue(npcEntity);
                                    }
                                    else
                                    {
                                        LOG_INFO("NPC", "Too far from %s (%.1f tiles > %.1f)",
                                                 npcComp->displayName.c_str(), distTiles,
                                                 npcComp->interactionRadius);
                                    }
                                }
                            }
                        }
                        else
                        {
                            // Already have a combat target
                            if (isMage)
                            {
                                // Mage: each Space press = one cast
                                Entity *target = world_->getEntity(currentTargetId_);
                                if (target && attackCooldownRemaining_ <= 0.0f)
                                {
                                    tryAttackTarget(entity, target);
                                }
                            }
                            else
                            {
                                // Warrior / Archer: enable auto-attack
                                if (!autoAttackEnabled_)
                                {
                                    autoAttackEnabled_ = true;
                                    LOG_INFO("Combat", "Auto-attack enabled");
                                }
                            }
                        }
                    }

                    // ---- Auto-attack tick (Warriors / Archers only) ----
                    if (!isMage && autoAttackEnabled_ && currentTargetId_ != INVALID_ENTITY && attackCooldownRemaining_ <= 0.0f)
                    {
                        Entity *target = world_->getEntity(currentTargetId_);
                        if (target)
                        {
                            tryAttackTarget(entity, target);
                        }
                    }
                });
        }

        // ------------------------------------------------------------------
        // tryAttackTarget — start attack windup animation + send to server
        //
        // Prediction (damage text, audio) is deferred to the animation hit frame
        // (~100ms after button press). The server message is sent immediately so
        // the response arrives by the time the hit frame fires.
        // ------------------------------------------------------------------
        void tryAttackTarget(Entity *player, Entity *target)
        {
            auto *statsComp = player->getComponent<CharacterStatsComponent>();
            auto *playerT = player->getComponent<Transform>();
            auto *targetT = target->getComponent<Transform>();

            if (!statsComp || !playerT || !targetT)
                return;

            // Determine target type: mob or player
            auto *enemyComp = target->getComponent<EnemyStatsComponent>();
            auto *targetCharStats = target->getComponent<CharacterStatsComponent>();
            bool targetIsMob = (enemyComp != nullptr);
            bool targetIsPlayer = (!targetIsMob && targetCharStats != nullptr && target->getComponent<DamageableComponent>() != nullptr);

            if (!targetIsMob && !targetIsPlayer)
                return;

            // ---- Faction check: block same-faction PvP ----
            if (targetIsPlayer)
            {
                auto *attackerFaction = player->getComponent<FactionComponent>();
                auto *targetFaction = target->getComponent<FactionComponent>();
                if (attackerFaction && targetFaction)
                {
                    if (FactionRegistry::isSameFaction(attackerFaction->faction, targetFaction->faction))
                    {
                        LOG_DEBUG("Combat", "Cannot attack same-faction player");
                        return;
                    }
                }
            }

            CharacterStats &ps = statsComp->stats;

            // Check target alive
            if (targetIsMob && !enemyComp->stats.isAlive)
            {
                clearTarget();
                return;
            }
            if (targetIsPlayer && targetCharStats->stats.isDead)
            {
                clearTarget();
                return;
            }

            // ---- CC check: crowd-controlled players cannot attack ----
            auto *playerCCComp = player->getComponent<CrowdControlComponent>();
            if (playerCCComp && !playerCCComp->cc.canAct())
            {
                LOG_DEBUG("Combat", "Cannot attack — crowd controlled");
                return;
            }

            // ---- Range check (in tiles) ----
            auto *combatCtrl = player->getComponent<CombatControllerComponent>();
            float distPixels = playerT->position.distance(targetT->position);
            float distTiles = distPixels / Coords::TILE_SIZE;

            bool isMage = (ps.classDef.classType == ClassType::Mage);
            float baseRange = isMage ? kMageRange : ps.classDef.attackRange;
            float requiredRange = (combatCtrl && !isMage) ? combatCtrl->disengageRange : baseRange;

            if (distTiles > requiredRange)
            {
                if (!isMage)
                {
                    autoAttackEnabled_ = false;
                    LOG_INFO("Combat", "Target out of range (%.1f tiles > %.1f)",
                             distTiles, requiredRange);
                }
                return;
            }

            // ---- Set cooldown ----
            float cooldown = combatCtrl ? combatCtrl->baseAttackCooldown : 1.5f;
            attackCooldownRemaining_ = cooldown;

            // ---- Predict damage for deferred display on hit frame ----
            Vec2 textPos = targetT->position;
            struct PendingHit
            {
                Vec2 pos;
                int damage = 0;
                bool isCrit = false;
                bool hit = true;
                bool resisted = false;
            };
            PendingHit pending;
            pending.pos = textPos;

            if (targetIsMob)
            {
                EnemyStats &es = enemyComp->stats;
                if (isMage)
                {
                    pending.resisted = CombatSystem::rollSpellResist(
                        ps.level, ps.getIntelligence(), es.level, es.magicResist);
                    pending.damage = ps.calculateDamage(false, pending.isCrit);
                    float mr = CombatSystem::getMobMagicDamageReduction(es.magicResist);
                    pending.damage = static_cast<int>(pending.damage * (1.0f - mr));
                    if (pending.damage < 1)
                        pending.damage = 1;
                }
                else
                {
                    pending.hit = CombatSystem::rollToHit(
                        ps.level, static_cast<int>(ps.getHitRate()), es.level, 0);
                    pending.damage = ps.calculateDamage(false, pending.isCrit);
                    pending.damage = CombatSystem::applyArmorReduction(pending.damage, es.armor);
                    if (pending.damage < 1)
                        pending.damage = 1;
                }
            }
            else
            {
                CharacterStats &ts = targetCharStats->stats;
                if (isMage)
                {
                    pending.resisted = CombatSystem::rollSpellResist(
                        ps.level, ps.getIntelligence(), ts.level, ts.getMagicResist());
                    pending.damage = ps.calculateDamage(false, pending.isCrit);
                    pending.damage = static_cast<int>(std::round(pending.damage * CombatSystem::getPvPDamageMultiplier()));
                    float mr = CombatSystem::getPlayerMagicDamageReduction(ts.getMagicResist());
                    pending.damage = static_cast<int>(pending.damage * (1.0f - mr));
                    if (pending.damage < 1)
                        pending.damage = 1;
                }
                else
                {
                    pending.hit = CombatSystem::rollToHit(
                        ps.level, static_cast<int>(ps.getHitRate()),
                        ts.level, static_cast<int>(ts.getEvasion()));
                    pending.damage = ps.calculateDamage(false, pending.isCrit);
                    pending.damage = static_cast<int>(std::round(pending.damage * CombatSystem::getPvPDamageMultiplier()));
                    pending.damage = CombatSystem::applyArmorReduction(pending.damage, ts.getArmor());
                    if (pending.damage < 1)
                        pending.damage = 1;
                }
            }

            // ---- Send attack to server immediately (in-flight during windup) ----
            ++attackSendSeq_;
            LOG_INFO("Combat", "Attack send #%d (cd=%.3fs, target=%s)",
                     attackSendSeq_, cooldown, target->name().c_str());
            if (onSendAttack)
                onSendAttack(target);

            // ---- Start windup animation with deferred hit-frame callback ----
            auto *playerAnim = player->getComponent<Animator>();
            auto *playerSpr = player->getComponent<SpriteComponent>();

            if (playerAnim)
            {
                bool isArcher = (ps.classDef.classType == ClassType::Archer);
                if (isMage)
                {
                    attackPose_ = AttackPose::Channel;
                }
                else if (isArcher)
                {
                    attackPose_ = AttackPose::BowDraw;
                    computeLungeDirection(player);
                }
                else
                {
                    attackPose_ = AttackPose::Melee;
                    computeLungeDirection(player);
                }

                // Archer: stretch animation to fill full cooldown window
                if (isArcher)
                {
                    ensureAttackAnimation(playerAnim, cooldown);
                    archerReleaseFired_ = false;

                    // Defer SFX to 80% progress (release point).
                    // Miss/resist TEXT is server-authoritative (shown by onCombatEvent).
                    // Client prediction miss text was causing false "Miss" alongside real damage.
                    pendingReleaseCallback_ = [this, pending]()
                    {
                        if (onPlaySFX)
                        {
                            if (pending.isCrit)
                                onPlaySFX("hit_crit");
                            else
                                onPlaySFX("hit_melee");
                        }
                    };
                    playerAnim->onHitFrame = nullptr;
                }
                else
                {
                    ensureAttackAnimation(playerAnim);

                    // On hit frame (~100ms): play audio only.
                    // Miss/resist/damage TEXT is server-authoritative (shown by onCombatEvent).
                    // Client prediction miss text was causing false "Miss" alongside real damage.
                    playerAnim->onHitFrame = [this, pending, isMage]()
                    {
                        if (onPlaySFX)
                        {
                            if (pending.isCrit)
                                onPlaySFX("hit_crit");
                            else
                                onPlaySFX(isMage ? "hit_skill" : "hit_melee");
                        }
                    };
                }

                playerAnim->onComplete = [this, playerSpr]()
                {
                    attackAnimActive_ = false;
                    if (playerSpr)
                        playerSpr->renderOffset = {0, 0};
                };

                playerAnim->play("attack");
                attackAnimActive_ = true;
            }
            else
            {
                // No animator — all text is server-authoritative via onCombatEvent
            }
        }

        // ------------------------------------------------------------------
        // ensureAttackAnimation — register placeholder if no real sprite sheet yet
        //
        // Uses the current sprite frame as the base so the sprite doesn't visually
        // change (no attack frames exist yet). The windup feel comes entirely from
        // the procedural renderOffset lunge. When real sprites arrive, register
        // a proper "attack" animation with real frames and remove the lunge code.
        //
        // If duration > 0, stretches a single-frame animation to fill that many
        // seconds (used by Archer to fill the full cooldown window). The hitFrame
        // is disabled (-1) because Archer handles release timing manually.
        // ------------------------------------------------------------------
        void ensureAttackAnimation(Animator *anim, float duration = 0.0f)
        {
            if (duration > 0.0f)
            {
                // Single-frame, stretched to fill cooldown — no sprite-frame cycling
                int baseFrame = anim->getCurrentFrame();
                anim->animations["attack"] = {"attack", baseFrame, 1,
                                              1.0f / duration, false, -1};
            }
            else if (!anim->animations.count("attack"))
            {
                int baseFrame = anim->getCurrentFrame();
                anim->addAnimation("attack", baseFrame, 3, 10.0f, false, 1);
            }
        }

        // ------------------------------------------------------------------
        // computeLungeDirection — normalized vector from player toward target
        // ------------------------------------------------------------------
        void computeLungeDirection(Entity *player)
        {
            lungeDirection_ = {0, -1}; // default: lunge upward
            if (currentTargetId_ == INVALID_ENTITY || !world_)
                return;
            Entity *target = world_->getEntity(currentTargetId_);
            if (!target)
                return;
            auto *pt = player->getComponent<Transform>();
            auto *tt = target->getComponent<Transform>();
            if (!pt || !tt)
                return;
            Vec2 dir = tt->position - pt->position;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 0.001f)
            {
                lungeDirection_ = {dir.x / len, dir.y / len};
            }
        }

        // ------------------------------------------------------------------
        // updateAttackLunge — per-frame procedural offset during attack animation
        //
        // Temporary until real sprite animation frames exist.
        //   Warrior: directional pullback → lunge → recover
        //   Archer:  directional draw-back → hold → release → recover (bow-draw)
        //   Mage:    stationary channeling settle (slight downward dip → release)
        // ------------------------------------------------------------------
        void updateAttackLunge()
        {
            if (!attackAnimActive_)
                return;

            world_->forEach<PlayerController, Animator>(
                [&](Entity *entity, PlayerController *ctrl, Animator *anim)
                {
                    if (!ctrl->isLocalPlayer)
                        return;
                    auto *spr = entity->getComponent<SpriteComponent>();
                    if (!spr)
                        return;

                    if (anim->currentAnimation != "attack" || !anim->playing)
                    {
                        spr->renderOffset = {0, 0};
                        attackAnimActive_ = false;
                        return;
                    }

                    auto it = anim->animations.find("attack");
                    if (it == anim->animations.end())
                        return;
                    const auto &def = it->second;

                    // Normalized progress through the animation (0 to 1)
                    float progress = anim->timer * def.frameRate / def.frameCount;
                    if (progress > 1.0f)
                        progress = 1.0f;

                    if (attackPose_ == AttackPose::Channel)
                    {
                        // Mage channeling: settle into casting stance then release.
                        // Slight downward dip (positive Y = down in screen space)
                        // that holds through the channel and snaps back on cast.
                        //   0.00–0.33  Settle into stance (0 → +dip)
                        //   0.33–0.80  Hold channel (stay at +dip)
                        //   0.80–1.00  Release (dip → 0)
                        float dip;
                        if (progress < 0.33f)
                        {
                            float t = progress / 0.33f;
                            dip = kChannelDipPx * t * t; // ease-in
                        }
                        else if (progress < 0.80f)
                        {
                            dip = kChannelDipPx;
                        }
                        else
                        {
                            float t = (progress - 0.80f) / 0.20f;
                            dip = kChannelDipPx * (1.0f - t);
                        }
                        spr->renderOffset = {0, dip};
                    }
                    else if (attackPose_ == AttackPose::BowDraw)
                    {
                        // Archer bow-draw: fills the full cooldown window.
                        // Draw → hold → release → recover, with SFX at release (80%).
                        //   0.00–0.30  Draw:    ease from 0 → -pullback (draw bowstring back)
                        //   0.30–0.78  Hold:    steady aim at -pullback
                        //   0.78–0.82  Release: snap from -pullback → +1px (arrow fired)
                        //   0.82–1.00  Recover: ease from +1px → 0
                        float offset;
                        if (progress < 0.30f)
                        {
                            float t = progress / 0.30f;
                            offset = -kPullbackPx * t * t; // ease-in draw
                        }
                        else if (progress < 0.78f)
                        {
                            offset = -kPullbackPx; // hold aim
                        }
                        else if (progress < 0.82f)
                        {
                            float t = (progress - 0.78f) / 0.04f;
                            offset = -kPullbackPx + (kPullbackPx + 1.0f) * t; // snap release
                        }
                        else
                        {
                            float t = (progress - 0.82f) / 0.18f;
                            offset = 1.0f * (1.0f - t); // gentle recover
                        }
                        spr->renderOffset = lungeDirection_ * offset;

                        // Fire release callback at 80% — SFX + miss/resist text
                        if (!archerReleaseFired_ && progress >= 0.80f)
                        {
                            archerReleaseFired_ = true;
                            if (pendingReleaseCallback_)
                            {
                                pendingReleaseCallback_();
                                pendingReleaseCallback_ = nullptr;
                            }
                        }
                    }
                    else
                    {
                        // Melee lunge: pullback → strike → recover along
                        // the direction vector from player to target.
                        //   0.00–0.33  Windup:  ease from 0 → -pullback
                        //   0.33–0.50  Strike:  snap from -pullback → +lunge
                        //   0.50–1.00  Recover: ease from +lunge → 0
                        float offset;
                        if (progress < 0.33f)
                        {
                            float t = progress / 0.33f;
                            offset = -kPullbackPx * t;
                        }
                        else if (progress < 0.50f)
                        {
                            float t = (progress - 0.33f) / 0.17f;
                            offset = -kPullbackPx + (kPullbackPx + kLungePx) * t;
                        }
                        else
                        {
                            float t = (progress - 0.50f) / 0.50f;
                            offset = kLungePx * (1.0f - t * t); // ease-out
                        }
                        spr->renderOffset = lungeDirection_ * offset;
                    }
                });
        }

        // ------------------------------------------------------------------
        // onMobDeath — visual-only (all rewards come from server via SvPlayerState/SvCombatEvent)
        // ------------------------------------------------------------------
        void onMobDeath(Entity * /*player*/, Entity *mob)
        {
            auto *enemyComp = mob->getComponent<EnemyStatsComponent>();
            auto *mobT = mob->getComponent<Transform>();

            if (!enemyComp || !mobT)
                return;

            EnemyStats &es = enemyComp->stats;

            LOG_INFO("Combat", "Mob %s (Lv %d) died — server awards XP/gold/honor",
                     es.enemyName.c_str(), es.level);

            // Hide the mob sprite (SpawnSystem handles respawn)
            auto *sprite = mob->getComponent<SpriteComponent>();
            if (sprite)
            {
                sprite->enabled = false;
            }

            // Clear our target since the mob is dead
            clearTarget();
        }

        // ------------------------------------------------------------------
        // findNearestMob — spatial hash lookup for closest living enemy
        // ------------------------------------------------------------------
        Entity *findNearestMob(Vec2 playerPos, float rangeTiles)
        {
            float rangePixels = rangeTiles * Coords::TILE_SIZE;
            EntityId id = mobGrid_.findNearest(playerPos, rangePixels);
            return (id != INVALID_ENTITY) ? world_->getEntity(id) : nullptr;
        }

        // ------------------------------------------------------------------
        // findNPCAtPoint — find an NPC entity under the click position
        // ------------------------------------------------------------------
        Entity *findNPCAtPoint(Vec2 worldClick)
        {
            if (!world_)
                return nullptr;
            Entity *hit = nullptr;
            world_->forEach<NPCComponent, Transform>(
                [&](Entity *e, NPCComponent *, Transform *t)
                {
                    if (hit)
                        return;
                    auto *spr = e->getComponent<SpriteComponent>();
                    if (!spr || !spr->enabled)
                        return;
                    Vec2 half = spr->size * 0.5f;
                    if (worldClick.x >= t->position.x - half.x &&
                        worldClick.x <= t->position.x + half.x &&
                        worldClick.y >= t->position.y - half.y &&
                        worldClick.y <= t->position.y + half.y)
                    {
                        hit = e;
                    }
                });
            return hit;
        }

        // ------------------------------------------------------------------
        // findPlayerAtPoint — find a targetable ghost player under click
        // Skips the local player and entities without DamageableComponent.
        // ------------------------------------------------------------------
        Entity *findPlayerAtPoint(Vec2 worldClick)
        {
            if (!world_)
                return nullptr;
            Entity *hit = nullptr;
            world_->forEach<Transform, CharacterStatsComponent>(
                [&](Entity *e, Transform *t, CharacterStatsComponent *cs)
                {
                    if (hit)
                        return;
                    // Must be damageable
                    if (!e->getComponent<DamageableComponent>())
                        return;
                    // Skip the local player
                    auto *pc = e->getComponent<PlayerController>();
                    if (pc && pc->isLocalPlayer)
                        return;
                    // Skip dead players
                    if (cs->stats.isDead)
                        return;
                    // Skip mobs (they have EnemyStatsComponent, handled separately)
                    if (e->getComponent<EnemyStatsComponent>())
                        return;
                    auto *spr = e->getComponent<SpriteComponent>();
                    if (!spr || !spr->enabled)
                        return;
                    Vec2 half = spr->size * 0.5f;
                    if (worldClick.x >= t->position.x - half.x &&
                        worldClick.x <= t->position.x + half.x &&
                        worldClick.y >= t->position.y - half.y &&
                        worldClick.y <= t->position.y + half.y)
                    {
                        hit = e;
                    }
                });
            return hit;
        }

        // ------------------------------------------------------------------
        // clearTarget
        // ------------------------------------------------------------------
        void clearTarget()
        {
            currentTargetId_ = INVALID_ENTITY;
            autoAttackEnabled_ = false;
            targetIsNPC_ = false;
        }

        // ------------------------------------------------------------------
        // isInHomeVillage — check if attacker is in their faction's home zone
        // Used for PK exception: no Red penalty for killing enemy faction in your village.
        // Called by future PvP kill handler before processPvPKill().
        // ------------------------------------------------------------------
        bool isInHomeVillage(Entity *attacker)
        {
            auto *factionComp = attacker->getComponent<FactionComponent>();
            if (!factionComp || factionComp->faction == Faction::None)
                return false;

            auto *playerT = attacker->getComponent<Transform>();
            if (!playerT)
                return false;

            // Scan all zone entities to find which zone the attacker is in
            bool inHomeVillage = false;
            world_->forEach<ZoneComponent, Transform>(
                [&](Entity *zoneEntity, ZoneComponent *zone, Transform *zoneT)
                {
                    if (inHomeVillage)
                        return; // already found
                    if (zone->contains(zoneT->position, playerT->position))
                    {
                        if (FactionRegistry::isHomeVillage(factionComp->faction, zone->zoneName))
                        {
                            inHomeVillage = true;
                        }
                    }
                });
            return inHomeVillage;
        }

        // ------------------------------------------------------------------
        // Floating text management
        // ------------------------------------------------------------------
        void updateFloatingTexts(float dt)
        {
            for (auto &ft : floatingTexts_)
            {
                ft.elapsed += dt;
                ft.position.x += ft.floatDx * dt;
                ft.position.y = ft.startY + (ft.floatDy * ft.elapsed);
            }
            floatingTexts_.erase(
                std::remove_if(floatingTexts_.begin(), floatingTexts_.end(),
                               [](const FloatingText &ft)
                               { return ft.elapsed >= ft.lifetime; }),
                floatingTexts_.end());
        }

        FloatingText buildFromStyle(Vec2 pos, const std::string &displayText,
                                    const CombatTextStyle &style)
        {
            constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;
            float rad = style.floatAngle * DEG_TO_RAD;

            FloatingText ft;
            ft.position = pos;
            ft.position.y += style.startOffsetY;
            ft.text = displayText;
            ft.color = style.color;
            ft.outlineColor = style.outlineColor;
            ft.fontSize = style.fontSize;
            ft.lifetime = style.lifetime;
            ft.elapsed = 0.0f;
            ft.startY = ft.position.y;
            ft.scale = style.scale;
            ft.floatDx = std::cos(rad) * style.floatSpeed;
            ft.floatDy = std::sin(rad) * style.floatSpeed;
            ft.fadeDelay = style.fadeDelay;
            ft.popScale = style.popScale;
            ft.popDuration = style.popDuration;

            if (style.randomSpreadX > 0.0f)
            {
                float spread = style.randomSpreadX;
                float r = static_cast<float>(std::rand()) / RAND_MAX;
                ft.position.x += (r * 2.0f - 1.0f) * spread;
            }

            return ft;
        }

        void spawnDamageText(Vec2 pos, int damage, bool isCrit)
        {
            const auto &cfg = CombatTextConfig::instance();
            const auto &style = isCrit ? cfg.crit : cfg.damage;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d", damage);
            std::string display = style.text.empty() ? std::string(buf) : style.text;
            floatingTexts_.push_back(buildFromStyle(pos, display, style));
        }

        void spawnMissText(Vec2 pos)
        {
            const auto &style = CombatTextConfig::instance().miss;
            std::string display = style.text.empty() ? "Miss" : style.text;
            floatingTexts_.push_back(buildFromStyle(pos, display, style));
        }

        void spawnResistText(Vec2 pos)
        {
            const auto &style = CombatTextConfig::instance().resist;
            std::string display = style.text.empty() ? "Resist" : style.text;
            floatingTexts_.push_back(buildFromStyle(pos, display, style));
        }

        void spawnXPText(Vec2 pos, int amount)
        {
            const auto &style = CombatTextConfig::instance().xp;
            std::string display = style.text;
            if (display.empty())
            {
                display = "+" + std::to_string(amount) + " XP";
            }
            else
            {
                size_t idx = display.find("{amount}");
                if (idx != std::string::npos)
                {
                    display.replace(idx, 8, std::to_string(amount));
                }
            }
            floatingTexts_.push_back(buildFromStyle(pos, display, style));
        }

        void spawnLevelUpText(Vec2 pos)
        {
            const auto &style = CombatTextConfig::instance().levelUp;
            std::string display = style.text.empty() ? "LEVEL UP!" : style.text;
            floatingTexts_.push_back(buildFromStyle(pos, display, style));
        }

        void spawnHealText(Vec2 pos, int amount)
        {
            const auto &style = CombatTextConfig::instance().heal;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "+%d", amount);
            std::string display = style.text.empty() ? std::string(buf) : style.text;
            size_t idx = display.find("{amount}");
            if (idx != std::string::npos)
            {
                display.replace(idx, 8, std::to_string(amount));
            }
            floatingTexts_.push_back(buildFromStyle(pos, display, style));
        }

        void spawnBlockText(Vec2 pos)
        {
            const auto &style = CombatTextConfig::instance().block;
            std::string display = style.text.empty() ? "Block" : style.text;
            floatingTexts_.push_back(buildFromStyle(pos, display, style));
        }
    };

} // namespace fate
