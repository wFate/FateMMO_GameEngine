#pragma once
#include "engine/ecs/component_registry.h"
#include "game/shared/character_stats.h"
#include "game/shared/enemy_stats.h"
#include "game/shared/mob_ai.h"
#include "game/shared/inventory.h"
#include "game/shared/skill_manager.h"
#include "game/shared/status_effects.h"
#include "game/shared/crowd_control.h"
#include "game/shared/combat_system.h"
#include "game/shared/party_manager.h"
#include "game/shared/guild_manager.h"
#include "game/shared/chat_manager.h"
#include "game/shared/trade_manager.h"
#include "game/shared/market_manager.h"
#include "game/shared/friends_manager.h"
#include "game/shared/honor_system.h"

namespace fate {

// ============================================================================
// Player Components
// ============================================================================

struct CharacterStatsComponent {
    FATE_COMPONENT(CharacterStatsComponent)
    CharacterStats stats;
};

struct CombatControllerComponent {
    FATE_COMPONENT(CombatControllerComponent)

    float baseAttackCooldown = 1.5f;  // Seconds between attacks
    float attackCooldownRemaining = 0.0f;
    // Note: targeting and auto-attack state are managed by CombatActionSystem,
    // not stored here. This component holds per-entity combat config only.
};

struct DamageableComponent {
    FATE_COMPONENT(DamageableComponent)
    // Marker component - entity can receive damage
    // Used by targeting system to identify valid targets
};

struct InventoryComponent {
    FATE_COMPONENT_COLD(InventoryComponent)
    Inventory inventory;
};

struct SkillManagerComponent {
    FATE_COMPONENT_COLD(SkillManagerComponent)
    SkillManager skills;
};

struct StatusEffectComponent {
    FATE_COMPONENT_COLD(StatusEffectComponent)
    StatusEffectManager effects;
};

struct CrowdControlComponent {
    FATE_COMPONENT_COLD(CrowdControlComponent)
    CrowdControlSystem cc;
};

struct TargetingComponent {
    FATE_COMPONENT(TargetingComponent)

    uint32_t selectedTargetId = 0;  // Entity ID of selected target
    TargetType targetType = TargetType::None;
    float maxTargetRange = 10.0f;   // Max range to acquire target (tiles)

    bool hasTarget() const { return selectedTargetId != 0; }
    void clearTarget() { selectedTargetId = 0; targetType = TargetType::None; }
};

struct ChatComponent {
    FATE_COMPONENT_COLD(ChatComponent)
    ChatManager chat;
};

struct GuildComponent {
    FATE_COMPONENT_COLD(GuildComponent)
    GuildManager guild;
};

struct PartyComponent {
    FATE_COMPONENT_COLD(PartyComponent)
    PartyManager party;
};

struct FriendsComponent {
    FATE_COMPONENT_COLD(FriendsComponent)
    FriendsManager friends;
};

struct MarketComponent {
    FATE_COMPONENT_COLD(MarketComponent)
    MarketManager market;
};

struct TradeComponent {
    FATE_COMPONENT_COLD(TradeComponent)
    TradeManager trade;
};

struct NameplateComponent {
    FATE_COMPONENT_COLD(NameplateComponent)

    std::string displayName;
    int displayLevel = 1;
    Color nameColor = Color::white();   // Based on PK status
    bool showGuildSymbol = false;
    std::string guildName;
    bool visible = true;
    bool showLevel = true;              // Toggle level display on/off
    float fontSize = 0.7f;              // Scale for nameplate text (0.3 - 2.0)
};

// ============================================================================
// Mob/Enemy Components
// ============================================================================

struct EnemyStatsComponent {
    FATE_COMPONENT(EnemyStatsComponent)
    EnemyStats stats;
};

struct MobAIComponent {
    FATE_COMPONENT(MobAIComponent)
    MobAI ai;
    float tickAccumulator = 0.0f;  // DEAR: time since last AI tick
};

// Mob nameplate (separate from player nameplate for different rendering)
struct MobNameplateComponent {
    FATE_COMPONENT(MobNameplateComponent)

    std::string displayName;
    int level = 1;
    bool isBoss = false;
    bool isElite = false;
    bool visible = true;
    bool showLevel = true;              // Toggle level display on/off
    float fontSize = 0.6f;              // Scale for mob nameplate text (0.3 - 2.0)
    // Color is computed per-viewer based on level difference (MobLevelColors)
};

} // namespace fate
