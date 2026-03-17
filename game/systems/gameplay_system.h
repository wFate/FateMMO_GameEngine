#pragma once
#include "engine/ecs/world.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "engine/core/logger.h"

#include <algorithm>
#include <unordered_map>

namespace fate {

// Ticks all game logic components every frame:
//   - Status effects, crowd control, party/trade invite expiry
//   - Combat cooldowns, HP/MP regen, PK status decay
//   - Respawn countdowns, nameplate sync
class GameplaySystem : public System {
public:
    const char* name() const override { return "GameplaySystem"; }

    float respawnCooldown = 5.0f;  // Seconds before respawn

    void update(float dt) override {
        LOG_INFO("TICK", "GameplaySystem::update");
        if (!world_) return;

        regenTimer_   += dt;
        mpRegenTimer_ += dt;

        bool doHPRegen = regenTimer_ >= kHPRegenInterval;
        bool doMPRegen = mpRegenTimer_ >= kMPRegenInterval;

        // ---- Status Effects: tick all entities that have them ----
        world_->forEach<StatusEffectComponent>(
            [&](Entity*, StatusEffectComponent* se) {
                se->effects.tick(dt);
            }
        );

        // ---- Crowd Control: tick all entities that have it ----
        world_->forEach<CrowdControlComponent>(
            [&](Entity*, CrowdControlComponent* ccComp) {
                ccComp->cc.tick(dt);
            }
        );

        // ---- Party invite expiry (player entities) ----
        world_->forEach<PartyComponent>(
            [&](Entity*, PartyComponent* partyComp) {
                partyComp->party.tick(dt);
            }
        );

        // ---- Trade invite expiry (player entities) ----
        world_->forEach<TradeComponent>(
            [&](Entity*, TradeComponent* tradeComp) {
                tradeComp->trade.tick(dt);
            }
        );

        // ---- Combat cooldowns ----
        world_->forEach<CombatControllerComponent>(
            [&](Entity*, CombatControllerComponent* combat) {
                if (combat->attackCooldownRemaining > 0.0f) {
                    combat->attackCooldownRemaining = (std::max)(
                        0.0f, combat->attackCooldownRemaining - dt);
                }
            }
        );

        // ---- Per-player ticks: regen, PK decay, respawn, nameplate sync ----
        world_->forEach<CharacterStatsComponent, PlayerController>(
            [&](Entity* entity, CharacterStatsComponent* statsComp, PlayerController*) {
                auto& stats = statsComp->stats;

                // -- Respawn countdown --
                if (stats.isDead) {
                    // Set respawn timer if not already counting
                    if (stats.respawnTimeRemaining <= 0.0f) {
                        stats.respawnTimeRemaining = respawnCooldown;
                        stats.currentHP = 0;
                        // Gray out sprite while dead
                        auto* spr = entity->getComponent<SpriteComponent>();
                        if (spr) spr->tint = Color(0.3f, 0.3f, 0.3f, 0.6f);
                    }

                    stats.respawnTimeRemaining -= dt;
                    if (stats.respawnTimeRemaining <= 0.0f) {
                        stats.respawnTimeRemaining = 0.0f;
                        stats.respawn();
                        // Restore sprite tint
                        auto* spr = entity->getComponent<SpriteComponent>();
                        if (spr) spr->tint = Color::white();
                    }
                    // Skip regen / PK decay while dead
                    syncNameplate(entity, stats);
                    return;
                }

                // -- HP Regen: every 10 seconds, 1% maxHP + equipment bonus --
                if (doHPRegen) {
                    if (stats.currentHP < stats.maxHP) {
                        int regenAmount = (std::max)(
                            1, static_cast<int>(stats.maxHP * 0.01f
                                                + stats.equipBonusHPRegen));
                        stats.heal(regenAmount);
                    }
                }

                // -- MP Regen (Mages): every 5 seconds, WIS-based + equipment bonus --
                if (doMPRegen) {
                    if (stats.classDef.usesMana()
                        && stats.currentMP < stats.maxMP) {
                        int regenAmount = (std::max)(
                            1, static_cast<int>(stats.getWisdom() * 0.5f
                                                + stats.equipBonusMPRegen));
                        stats.currentMP = (std::min)(
                            stats.maxMP, stats.currentMP + regenAmount);
                    }
                }

                // -- PK Status Decay (per-entity timer) --
                tickPKDecay(entity->id(), stats, dt);

                // -- Sync nameplate from character stats --
                syncNameplate(entity, stats);
            }
        );

        // Reset interval accumulators after the loop
        if (doHPRegen) {
            regenTimer_ -= kHPRegenInterval;
        }
        if (doMPRegen) {
            mpRegenTimer_ -= kMPRegenInterval;
        }
    }

private:
    // ---- Timers ----
    float regenTimer_   = 0.0f;
    float mpRegenTimer_ = 0.0f;

    // ---- Intervals ----
    static constexpr float kHPRegenInterval = 10.0f;
    static constexpr float kMPRegenInterval = 5.0f;

    // ---- PK decay timers per entity ----
    // Maps entity ID to accumulated time in current PK status.
    // Reset when PK status transitions back to White.
    std::unordered_map<EntityId, float> pkDecayTimers_;

    void tickPKDecay(EntityId entityId, CharacterStats& stats, float dt) {
        if (stats.pkStatus == PKStatus::White) {
            pkDecayTimers_.erase(entityId);
            return;
        }

        float& accum = pkDecayTimers_[entityId];
        accum += dt;

        float threshold = 0.0f;
        switch (stats.pkStatus) {
            case PKStatus::Purple:
                threshold = PKCooldowns::PURPLE_DECAY_SECONDS;
                break;
            case PKStatus::Red:
                threshold = PKCooldowns::RED_TO_WHITE_SECONDS;
                break;
            case PKStatus::Black:
                threshold = PKCooldowns::BLACK_TO_WHITE_SECONDS;
                break;
            default:
                break;
        }

        if (threshold > 0.0f && accum >= threshold) {
            stats.pkStatus = PKStatus::White;
            pkDecayTimers_.erase(entityId);
        }
    }

    // ---- Nameplate sync ----
    void syncNameplate(Entity* entity, const CharacterStats& stats) {
        auto* nameplate = entity->getComponent<NameplateComponent>();
        if (!nameplate) return;

        nameplate->displayName = stats.characterName;
        nameplate->displayLevel = stats.level;
        nameplate->nameColor = pkStatusColor(stats.pkStatus);

        auto* guildComp = entity->getComponent<GuildComponent>();
        if (guildComp && guildComp->guild.isInGuild()) {
            nameplate->showGuildSymbol = true;
            nameplate->guildName = guildComp->guild.guildName;
        } else {
            nameplate->showGuildSymbol = false;
            nameplate->guildName.clear();
        }
    }

    // ---- PK status -> nameplate color ----
    static Color pkStatusColor(PKStatus status) {
        switch (status) {
            case PKStatus::White:  return Color::white();
            case PKStatus::Purple: return {0.659f, 0.333f, 0.969f};
            case PKStatus::Red:    return Color::red();
            case PKStatus::Black:  return {0.2f, 0.2f, 0.2f};
            default:               return Color::white();
        }
    }
};

} // namespace fate
