#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
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
#include "game/shared/npc_types.h"
#include "game/shared/quest_manager.h"
#include "game/shared/bank_storage.h"
#include "game/shared/faction.h"
#include "game/shared/pet_system.h"
#include "game/shared/collection_system.h"
#include "engine/render/texture.h"
#include "game/components/dropped_item_component.h"
#include "game/components/boss_spawn_point_component.h"

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
    bool showAttackRange = false;     // Editor toggle: draw attack range circle in viewport
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
    bool clickConsumed = false;      // Set by NPCInteractionSystem, checked by CombatActionSystem

    bool hasTarget() const { return selectedTargetId != 0; }
    void clearTarget() { selectedTargetId = 0; targetType = TargetType::None; }
};

struct EquipVisualsComponent {
    FATE_COMPONENT(EquipVisualsComponent)
    uint16_t weaponVisualIdx = 0;
    uint16_t armorVisualIdx  = 0;
    uint16_t hatVisualIdx    = 0;
};

struct AppearanceComponent {
    FATE_COMPONENT(AppearanceComponent)
    uint8_t gender    = 0;  // 0=male, 1=female
    uint8_t hairstyle = 0;  // 0-2 per gender, expandable

    // Resolved textures (runtime only, rebuilt when dirty)
    std::shared_ptr<Texture> bodyTexture;
    std::shared_ptr<Texture> hairTexture;
    std::shared_ptr<Texture> armorTexture;
    std::shared_ptr<Texture> hatTexture;
    std::shared_ptr<Texture> weaponTexture;
    bool dirty = true;
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
    std::string roleSubtitle;        // e.g., "[Merchant]", "[Quest]"
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

// ============================================================================
// NPC Components
// ============================================================================

struct NPCComponent {
    FATE_COMPONENT_COLD(NPCComponent)
    uint32_t npcId = 0;
    std::string displayName;
    std::string dialogueGreeting;
    std::string sceneId;              // Scene this NPC belongs to (for replication filtering)
    float interactionRadius = 2.0f;
    FaceDirection faceDirection = FaceDirection::Down;
};

struct QuestGiverComponent {
    FATE_COMPONENT_COLD(QuestGiverComponent)
    std::vector<uint32_t> questIds;
};

enum class MarkerState : uint8_t {
    None = 0,
    Available = 1,
    TurnIn = 2
};

struct QuestMarkerComponent {
    FATE_COMPONENT(QuestMarkerComponent)
    MarkerState currentState = MarkerState::None;
    QuestTier highestTier = QuestTier::Starter;
};

struct ShopComponent {
    FATE_COMPONENT_COLD(ShopComponent)
    std::string shopName;
    std::vector<ShopItem> inventory;
};

struct BankerComponent {
    FATE_COMPONENT_COLD(BankerComponent)
    uint16_t storageSlots = 30;
};

struct GuildNPCComponent {
    FATE_COMPONENT_COLD(GuildNPCComponent)
    int64_t creationCost = 0;
    uint16_t requiredLevel = 0;
};

struct TeleporterComponent {
    FATE_COMPONENT_COLD(TeleporterComponent)
    std::vector<TeleportDestination> destinations;
};

struct StoryNPCComponent {
    FATE_COMPONENT_COLD(StoryNPCComponent)
    std::vector<DialogueNode> dialogueTree;
    uint32_t rootNodeId = 0;
};

struct DungeonNPCComponent {
    FATE_COMPONENT_COLD(DungeonNPCComponent)
    std::string dungeonSceneId;   // scene cache key (e.g., "AetherDungeon_T1")
};

struct ArenaNPCComponent {
    FATE_COMPONENT_COLD(ArenaNPCComponent)
    // Marker component — presence on NPC entity enables Arena registration dialogue
};

struct BattlefieldNPCComponent {
    FATE_COMPONENT_COLD(BattlefieldNPCComponent)
    // Marker component — presence on NPC entity enables Battlefield registration dialogue
};

struct MarketplaceNPCComponent {
    FATE_COMPONENT_COLD(MarketplaceNPCComponent)
};

struct LeaderboardNPCComponent {
    FATE_COMPONENT_COLD(LeaderboardNPCComponent)
    std::string loreSnippet;
};

// ============================================================================
// Player Quest & Bank Components
// ============================================================================

struct QuestComponent {
    FATE_COMPONENT_COLD(QuestComponent)
    QuestManager quests;
};

struct BankStorageComponent {
    FATE_COMPONENT_COLD(BankStorageComponent)
    BankStorage storage;
};

struct CollectionComponent {
    FATE_COMPONENT_COLD(CollectionComponent)
    CollectionState collections;
};

} // namespace fate

// ============================================================================
// Reflection declarations — OUTSIDE namespace fate
// ============================================================================

// --- Player Components ---

// CharacterStatsComponent wraps complex CharacterStats — custom serializer in Task 6
FATE_REFLECT_EMPTY(fate::CharacterStatsComponent)

FATE_REFLECT(fate::CombatControllerComponent,
    FATE_FIELD(baseAttackCooldown, Float),
    FATE_FIELD(attackCooldownRemaining, Float),
    FATE_FIELD(showAttackRange, Bool)
)

// Marker component — no data fields
FATE_REFLECT_EMPTY(fate::DamageableComponent)

// Complex inner types — custom serializers in Task 6
FATE_REFLECT_EMPTY(fate::InventoryComponent)
FATE_REFLECT_EMPTY(fate::SkillManagerComponent)
FATE_REFLECT_EMPTY(fate::StatusEffectComponent)
FATE_REFLECT_EMPTY(fate::CrowdControlComponent)

FATE_REFLECT(fate::TargetingComponent,
    FATE_FIELD(selectedTargetId, UInt),
    FATE_FIELD(maxTargetRange, Float),
    FATE_FIELD(clickConsumed, Bool)
)

FATE_REFLECT(fate::EquipVisualsComponent,
    FATE_FIELD(weaponVisualIdx, UInt),
    FATE_FIELD(armorVisualIdx, UInt),
    FATE_FIELD(hatVisualIdx, UInt)
)

FATE_REFLECT(fate::AppearanceComponent,
    FATE_FIELD(gender, UInt),
    FATE_FIELD(hairstyle, UInt)
)

FATE_REFLECT_EMPTY(fate::ChatComponent)
FATE_REFLECT_EMPTY(fate::GuildComponent)
FATE_REFLECT_EMPTY(fate::PartyComponent)
FATE_REFLECT_EMPTY(fate::FriendsComponent)
FATE_REFLECT_EMPTY(fate::MarketComponent)
FATE_REFLECT_EMPTY(fate::TradeComponent)

FATE_REFLECT(fate::NameplateComponent,
    FATE_FIELD(displayName, String),
    FATE_FIELD(displayLevel, Int),
    FATE_FIELD(nameColor, Color),
    FATE_FIELD(showGuildSymbol, Bool),
    FATE_FIELD(guildName, String),
    FATE_FIELD(visible, Bool),
    FATE_FIELD(showLevel, Bool),
    FATE_FIELD(roleSubtitle, String),
    FATE_FIELD(fontSize, Float)
)

// --- Mob/Enemy Components ---

// EnemyStatsComponent wraps complex EnemyStats — custom serializer in Task 6
FATE_REFLECT_EMPTY(fate::EnemyStatsComponent)

// MobAIComponent wraps complex MobAI — custom serializer in Task 6
FATE_REFLECT_EMPTY(fate::MobAIComponent)

FATE_REFLECT(fate::MobNameplateComponent,
    FATE_FIELD(displayName, String),
    FATE_FIELD(level, Int),
    FATE_FIELD(isBoss, Bool),
    FATE_FIELD(isElite, Bool),
    FATE_FIELD(visible, Bool),
    FATE_FIELD(showLevel, Bool),
    FATE_FIELD(fontSize, Float)
)

// --- NPC Components ---

// NPCComponent: custom serializer handles faceDirection (uint8_t enum)
FATE_REFLECT_EMPTY(fate::NPCComponent)

// QuestGiverComponent has vector<uint32_t> — custom serializer in Task 6
FATE_REFLECT_EMPTY(fate::QuestGiverComponent)

FATE_REFLECT_EMPTY(fate::QuestMarkerComponent)

// ShopComponent has complex vector<ShopItem> — custom serializer in Task 6
FATE_REFLECT_EMPTY(fate::ShopComponent)

FATE_REFLECT(fate::BankerComponent,
    FATE_FIELD(storageSlots, UInt)
)

FATE_REFLECT(fate::GuildNPCComponent,
    FATE_FIELD(creationCost, Int),
    FATE_FIELD(requiredLevel, UInt)
)

// TeleporterComponent has complex vector<TeleportDestination> — custom serializer in Task 6
FATE_REFLECT_EMPTY(fate::TeleporterComponent)

// StoryNPCComponent has complex vector<DialogueNode> — custom serializer in Task 6
FATE_REFLECT_EMPTY(fate::StoryNPCComponent)

FATE_REFLECT_EMPTY(fate::DungeonNPCComponent)
FATE_REFLECT_EMPTY(fate::ArenaNPCComponent)
FATE_REFLECT_EMPTY(fate::BattlefieldNPCComponent)

// --- Player Quest & Bank Components ---

FATE_REFLECT_EMPTY(fate::QuestComponent)
FATE_REFLECT_EMPTY(fate::BankStorageComponent)
FATE_REFLECT_EMPTY(fate::CollectionComponent)
