# NPC & Quest System Design Spec

**Date:** 2026-03-17
**Status:** Reviewed
**Inspired by:** The World of Magic (TWOM) + OpenTibia Canary quest/NPC patterns
**Architecture:** Component-Per-Role (Approach A) — each NPC role is a separate ECS component
**Namespace:** All new types are in `namespace fate`, matching the existing codebase.

---

## Overview

Add a complete NPC and Quest system to FateMMO_GameEngine, matching TWOM's gameplay patterns. NPCs stand still, face a fixed direction, and are interacted with via click. Quests follow the TWOM tier system with colored `?`/`!` markers. All quest and NPC data is hardcoded in C++ headers. The system integrates with the existing ECS architecture using the same component/system patterns already established in the codebase.

### New Enum: FaceDirection

Added to `game_types.h` alongside existing enums:

```cpp
enum class FaceDirection : uint8_t {
    Down = 0, Up, Left, Right
};
```

### ClassType Enum Addition

Add `Any` value to the existing `ClassType` enum in `game_types.h`:

```cpp
enum class ClassType : uint8_t {
    Warrior = 0, Mage = 1, Archer = 2, Any = 255
};
```

`Any` is used exclusively for `SkillTrainerComponent` to indicate universal skills available to all classes.

### Gold Type Convention

All gold values throughout this spec use `int64_t` to match the existing `Inventory` class which uses `int64_t` for `addGold()`, `removeGold()`, `getGold()`, and `MAX_GOLD = 999'999'999LL`.

### Out of Scope

- **Minimap NPC markers** — future addition, not part of this spec
- **Repeatable/daily quests** — future addition. Current design tracks `completedQuestIds_` as permanent. A `bool repeatable` flag and cooldown can be added later.
- **Item drop/loot tables** — `Collect` objectives assume items are obtainable. A loot drop system is a separate feature. Until then, Collect objectives can use items placed in the world or obtained from other NPCs.

---

## 1. Core NPC Entity & Interaction

### NPCComponent (base marker — all NPCs have this)

```cpp
struct NPCComponent : public Component {
    FATE_LEGACY_COMPONENT(NPCComponent)
    uint32_t npcId = 0;
    std::string displayName;
    std::string dialogueGreeting;     // what NPC says when clicked
    float interactionRadius = 2.0f;   // in tiles
    FaceDirection faceDirection = FaceDirection::Down;
};
```

- NPCs do **not** move. No AI, no pathing. They stand in place and face a fixed direction.
- NPCs are **not** attackable. They do not have `DamageableComponent`.
- NPCs have a `SpriteComponent`, `Transform`, and `NameplateComponent` like other entities.

### NPCInteractionSystem

- Inherits from `System`, implements `update(float dt)`.
- **Must run before `CombatActionSystem`** in the system execution order. If NPC click is consumed, `CombatActionSystem` skips click processing that frame.
- Click consumption: sets a per-frame `bool clickConsumed` flag on the player's `TargetingComponent`. `CombatActionSystem` checks this flag before processing its own click logic.
- Reuses the click detection pattern from `CombatActionSystem`:
  - `Input::isMousePressed(SDL_BUTTON_LEFT)` / `Input::isTouchPressed(0)`
  - `camera->screenToWorld()` to convert to world coordinates
  - Sprite bounds check on entities with `NPCComponent`
- On click within `interactionRadius`:
  - Sets `TargetingComponent::targetType = TargetType::NPC`
  - Sets `clickConsumed = true`
  - Determines which role components the NPC has
  - Opens `NPCDialogueUI` with context-appropriate buttons
- On click outside range: floating text "Too far away"
- Closes dialogue if player walks out of range

### NPCDialogueUI

- ImGui-based dialogue box (consistent with existing UI approach)
- Layout:
  - NPC name at top
  - Text body (NPC speech)
  - Clickable buttons at bottom, context-dependent:
    - Quest giver: "Accept" / "Decline" / "Complete Quest" / "Quest Info"
    - Merchant: "Shop" / "Close"
    - Skill trainer: "Learn Skills" / "Close"
    - Banker: "Open Storage" / "Close"
    - Guild NPC: "Create Guild" / "Guild Info" / "Leave Guild" / "Close"
    - Teleporter: destination list with costs / "Close"
    - Story NPC: 2-4 branching choice buttons
    - Multi-role NPC: shows all applicable options (e.g., "Shop" + "Quest" + "Close")
- Closes on: "Close" button, ESC key, or player walks out of range

---

## 2. Quest System

### QuestComponent (on player entity)

```cpp
struct QuestComponent : public Component {
    FATE_LEGACY_COMPONENT(QuestComponent)
    QuestManager quests;
};
```

### QuestManager (embedded in QuestComponent)

```cpp
class QuestManager {
public:
    static constexpr int MAX_ACTIVE_QUESTS = 10;

    bool canAcceptQuest(uint32_t questId, const CharacterStats& stats) const;
    bool acceptQuest(uint32_t questId);
    bool abandonQuest(uint32_t questId);
    bool isQuestActive(uint32_t questId) const;
    bool isQuestComplete(uint32_t questId) const;
    bool hasCompletedQuest(uint32_t questId) const;
    ActiveQuest* getActiveQuest(uint32_t questId);
    const std::vector<ActiveQuest>& getActiveQuests() const;

    // Progress tracking — called by QuestSystem
    void onMobKilled(const std::string& mobId);
    void onItemCollected(const std::string& itemId);
    void onNPCTalkedTo(uint32_t npcId);
    void onPvPKill();

    // Turn-in
    bool turnInQuest(uint32_t questId, CharacterStats& stats, Inventory& inventory);

    // Callbacks
    std::function<void(uint32_t questId)> onQuestAccepted;
    std::function<void(uint32_t questId)> onQuestCompleted;
    std::function<void(uint32_t questId, uint16_t objectiveIndex)> onObjectiveProgress;

private:
    std::vector<ActiveQuest> activeQuests_;
    std::unordered_set<uint32_t> completedQuestIds_;
};
```

### QuestDefinition (hardcoded in quest_data.h)

```cpp
struct QuestDefinition {
    uint32_t questId;
    std::string questName;
    std::string description;          // shown in quest log
    std::string offerDialogue;        // NPC speech when offering quest
    std::string inProgressDialogue;   // NPC speech while quest is active
    std::string turnInDialogue;       // NPC speech when quest is ready to turn in
    QuestTier tier;
    uint16_t requiredLevel;
    uint32_t turnInNpcId;
    std::vector<uint32_t> prerequisiteQuestIds;  // empty = none, all must be completed
    std::vector<QuestObjective> objectives;
    QuestRewards rewards;
};
```

### QuestTier (exact TWOM breakpoints)

```cpp
enum class QuestTier : uint8_t {
    Starter = 0,       // White ?, level 0+
    Novice = 1,        // Green ?, level 25+
    Apprentice = 2,    // Yellow-Green ?, level 50+
    Adept = 3          // Yellow ?, level 100+
};
```

### QuestObjective (5 TWOM types)

```cpp
enum class ObjectiveType : uint8_t {
    Kill,       // "Slay 10 Leaf Boars"
    Collect,    // "Bring 5 Clovers from Kooii"
    Deliver,    // "Bring this letter to Vellore"
    TalkTo,     // "Speak with Juri"
    PvPKills    // "Get 2 battlefield kills"
};

struct QuestObjective {
    ObjectiveType type;
    std::string description;
    std::string targetId;        // mob name, item id, or npc id
    uint16_t requiredCount;
};
```

**Deliver Objective Mechanic:** When a player talks to the turn-in NPC for a `Deliver` objective, the system checks `player->getComponent<InventoryComponent>()->inventory` for the required item. If present, the item is consumed (removed from inventory) and the objective is marked complete. This is distinct from `TalkTo` (which requires no item) and `Collect` (which tracks item acquisition, not delivery).

```
```

### ActiveQuest (per-player runtime state)

```cpp
struct ActiveQuest {
    uint32_t questId;
    std::vector<uint16_t> objectiveProgress;  // parallel to QuestDefinition::objectives
    bool isReadyToTurnIn() const;
};
```

### QuestRewards

```cpp
struct QuestRewards {
    uint32_t xp = 0;
    int64_t gold = 0;
    std::vector<ItemReward> items;  // {itemId, count}
};

struct ItemReward {
    std::string itemId;
    uint16_t count;
};
```

### QuestGiverComponent (on NPC entities)

```cpp
struct QuestGiverComponent : public Component {
    FATE_LEGACY_COMPONENT(QuestGiverComponent)
    std::vector<uint32_t> questIds;   // quests this NPC offers
};
```

### QuestSystem

- Inherits from `System`, implements `update(float dt)`.
- Listens for game events and routes to player `QuestManager`:
  - Mob death → `forEach<QuestComponent>` → `quests.onMobKilled(mobId)`
  - Item pickup → `quests.onItemCollected(itemId)`
  - NPC interaction → `quests.onNPCTalkedTo(npcId)`
  - PvP kill → `quests.onPvPKill()`
- Updates `QuestMarkerComponent` state on NPC entities via event-driven callbacks (`onQuestAccepted`, `onQuestCompleted`, `onQuestAbandoned`, and on player level-up) rather than polling each frame

---

## 3. NPC Role Components

### ShopComponent (merchants)

```cpp
struct ShopItem {
    std::string itemId;
    std::string itemName;
    int64_t buyPrice;        // player pays this to NPC
    int64_t sellPrice;       // NPC pays this to player
    uint16_t stock;          // 0 = unlimited
};

struct ShopComponent : public Component {
    FATE_LEGACY_COMPONENT(ShopComponent)
    std::string shopName;
    std::vector<ShopItem> inventory;
};
```

- Buy: checks player gold via `InventoryComponent`, deducts gold, adds item
- Sell: checks player has item, removes item, adds gold
- Fixed prices per NPC, no dynamic pricing

### SkillTrainerComponent

```cpp
struct TrainableSkill {
    std::string skillId;
    uint16_t requiredLevel;
    int64_t goldCost;
    uint16_t skillPointCost;
    ClassType requiredClass;   // ClassType::Any for universal skills
};

struct SkillTrainerComponent : public Component {
    FATE_LEGACY_COMPONENT(SkillTrainerComponent)
    ClassType trainerClass;
    std::vector<TrainableSkill> skills;
};
```

- Validates level, gold, skill points, and class before learning
- Routes to existing `SkillManager::learnSkill()` on success

### BankerComponent

```cpp
struct BankerComponent : public Component {
    FATE_LEGACY_COMPONENT(BankerComponent)
    uint16_t storageSlots;     // e.g., 30
    float depositFeePercent;   // 0.0 = free, 0.05 = 5%. Applied as floor(amount * fee)
};
```

### BankStorageComponent (on player entity)

```cpp
struct BankStorageComponent : public Component {
    FATE_LEGACY_COMPONENT(BankStorageComponent)
    BankStorage storage;
};
```

```cpp
class BankStorage {
public:
    void initialize(uint16_t maxSlots);
    bool depositItem(const std::string& itemId, uint16_t count);
    bool withdrawItem(const std::string& itemId, uint16_t count);
    bool depositGold(int64_t amount, float fee);
    bool withdrawGold(int64_t amount);
    int64_t getStoredGold() const;
    const std::vector<StoredItem>& getItems() const;
    bool isFull() const;

private:
    std::vector<StoredItem> items_;
    int64_t storedGold_ = 0;
    uint16_t maxSlots_ = 30;
};
```

### GuildNPCComponent

```cpp
struct GuildNPCComponent : public Component {
    FATE_LEGACY_COMPONENT(GuildNPCComponent)
    int64_t creationCost;
    uint16_t requiredLevel;
};
```

- Routes to existing `GuildManager` for create/info/leave operations
- Dialogue options: "Create Guild" / "Guild Info" / "Leave Guild" / "Close"

### TeleporterComponent

```cpp
struct TeleportDestination {
    std::string destinationName;
    std::string sceneId;
    Vec2 targetPosition;
    int64_t cost;
    uint16_t requiredLevel;
};

struct TeleporterComponent : public Component {
    FATE_LEGACY_COMPONENT(TeleporterComponent)
    std::vector<TeleportDestination> destinations;
};
```

- Distinct from `PortalComponent` (walk-through automatic). These are NPC-mediated with gold cost and level gate.
- Dialogue shows destination list with prices, player clicks to travel.

### StoryNPCComponent (branching dialogue)

```cpp
// Action types for dialogue side effects (avoids std::function in data structs)
enum class DialogueAction : uint8_t {
    None = 0,
    GiveItem,        // gives player an item
    GiveXP,          // gives player XP
    GiveGold,        // gives player gold
    SetFlag,         // sets a quest/story flag (storage value)
    Heal,            // fully heals player
};

struct DialogueActionData {
    DialogueAction action = DialogueAction::None;
    std::string targetId;    // item id or flag name
    int32_t value = 0;       // amount, or flag value
};

// Condition types for gating dialogue nodes
enum class DialogueCondition : uint8_t {
    None = 0,
    HasFlag,         // player has a storage flag set
    MinLevel,        // player meets minimum level
    HasItem,         // player has item in inventory
    HasClass,        // player is a specific class
};

struct DialogueConditionData {
    DialogueCondition condition = DialogueCondition::None;
    std::string targetId;
    int32_t value = 0;
};

struct DialogueChoice {
    std::string buttonText;
    uint32_t nextNodeId;                     // 0 = end conversation
    DialogueActionData onSelect;             // optional side effect
};

struct DialogueNode {
    uint32_t nodeId;
    std::string npcText;
    std::vector<DialogueChoice> choices;      // 2-4 options
    DialogueConditionData condition;          // optional visibility gate
};

struct StoryNPCComponent : public Component {
    FATE_LEGACY_COMPONENT(StoryNPCComponent)
    std::vector<DialogueNode> dialogueTree;
    uint32_t rootNodeId = 0;
};
```

- For lore/story NPCs with deeper branching conversations
- Quest givers use the simpler `QuestGiverComponent` flow, not this

---

## 4. Quest Markers & NPC Nameplates

### QuestMarkerComponent (on quest-giving NPC entities)

```cpp
enum class MarkerState : uint8_t {
    None = 0,
    Available = 1,    // ? marker
    TurnIn = 2        // ! marker
};

struct QuestMarkerComponent : public Component {
    FATE_LEGACY_COMPONENT(QuestMarkerComponent)
    MarkerState currentState = MarkerState::None;
    QuestTier highestTier = QuestTier::Starter;
};
```

### Marker Colors (exact TWOM)

| Tier | `?` Color | RGB |
|------|-----------|-----|
| Starter | White | (255, 255, 255) |
| Novice | Green | (0, 200, 0) |
| Apprentice | Yellow-Green | (180, 220, 0) |
| Adept | Yellow | (255, 220, 0) |
| Turn-in `!` | Yellow | (255, 220, 0) |

### Marker Update Logic (in QuestSystem)

On quest state change (accept/complete/abandon/level-up), for each NPC with `QuestGiverComponent` + `QuestMarkerComponent`:
1. Check if the local player has a completed quest to turn in at this NPC → `MarkerState::TurnIn`
2. Else check if this NPC has any available quests the player qualifies for (level, prerequisites, not already active/completed) → `MarkerState::Available`, set `highestTier` to the highest available quest's tier
3. Else → `MarkerState::None`

### Rendering

- Drawn as a text glyph or small sprite above the NPC nameplate
- Offset: positioned above the nameplate text (which is already above the NPC sprite)
- Uses existing `TextRenderer` for the `?`/`!` character with the tier color

### NPC Nameplates

- Reuse existing `NameplateComponent` pattern
- Set `nameplate->showLevel = false` — NPCs are not combat entities and should not display a level
- Add `std::string roleSubtitle` field to `NameplateComponent` — rendered in grey below the display name
- Display name in white (neutral, not attackable)
- Role subtitle in grey below the name: `[Merchant]`, `[Skill Trainer]`, `[Quest]`, `[Banker]`, `[Guild]`, `[Teleporter]`
- Multi-role NPCs show the most relevant role (quest > merchant > other)
- `createNPC()` factory sets the subtitle based on which role components are attached

---

## 5. EntityFactory Integration

### NPCTemplate

```cpp
struct NPCTemplate {
    // Identity
    std::string name;
    uint32_t npcId;
    Vec2 position;
    FaceDirection facing = FaceDirection::Down;
    std::string spriteSheet;

    // Role flags
    bool isQuestGiver = false;
    bool isMerchant = false;
    bool isSkillTrainer = false;
    bool isBanker = false;
    bool isGuildNPC = false;
    bool isTeleporter = false;
    bool isStoryNPC = false;

    // Role data (populated based on flags)
    std::vector<uint32_t> questIds;
    std::vector<ShopItem> shopItems;
    std::string shopName;
    std::vector<TrainableSkill> trainableSkills;
    ClassType trainerClass = ClassType::Any;
    uint16_t bankSlots = 30;
    float bankFeePercent = 0.0f;
    int64_t guildCreationCost = 0;
    uint16_t guildRequiredLevel = 0;
    std::vector<TeleportDestination> destinations;
    std::vector<DialogueNode> dialogueTree;
    uint32_t dialogueRootNodeId = 0;

    // Base NPC
    std::string dialogueGreeting;
    float interactionRadius = 2.0f;
};
```

### createNPC Factory Method

```cpp
static Entity* createNPC(World& world, const NPCTemplate& tmpl) {
    Entity* npc = world.createEntity(tmpl.name);
    npc->setTag("npc");

    // Engine components
    auto* transform = npc->addComponent<Transform>(tmpl.position.x, tmpl.position.y);
    auto* sprite = npc->addComponent<SpriteComponent>();
    // Load sprite from tmpl.spriteSheet...

    // Base NPC
    auto* npcComp = npc->addComponent<NPCComponent>();
    npcComp->npcId = tmpl.npcId;
    npcComp->displayName = tmpl.name;
    npcComp->dialogueGreeting = tmpl.dialogueGreeting;
    npcComp->interactionRadius = tmpl.interactionRadius;
    npcComp->faceDirection = tmpl.facing;

    // Nameplate
    auto* nameplate = npc->addComponent<NameplateComponent>();
    nameplate->displayName = tmpl.name;
    nameplate->showLevel = false;
    // Set role subtitle based on attached components (priority: quest > merchant > other)
    if (tmpl.isQuestGiver) nameplate->roleSubtitle = "[Quest]";
    else if (tmpl.isMerchant) nameplate->roleSubtitle = "[Merchant]";
    else if (tmpl.isSkillTrainer) nameplate->roleSubtitle = "[Skill Trainer]";
    else if (tmpl.isBanker) nameplate->roleSubtitle = "[Banker]";
    else if (tmpl.isGuildNPC) nameplate->roleSubtitle = "[Guild]";
    else if (tmpl.isTeleporter) nameplate->roleSubtitle = "[Teleporter]";

    // Attach role components based on flags
    if (tmpl.isQuestGiver) {
        auto* qg = npc->addComponent<QuestGiverComponent>();
        qg->questIds = tmpl.questIds;
        npc->addComponent<QuestMarkerComponent>();
    }
    if (tmpl.isMerchant) {
        auto* shop = npc->addComponent<ShopComponent>();
        shop->shopName = tmpl.shopName;
        shop->inventory = tmpl.shopItems;
    }
    if (tmpl.isSkillTrainer) {
        auto* trainer = npc->addComponent<SkillTrainerComponent>();
        trainer->trainerClass = tmpl.trainerClass;
        trainer->skills = tmpl.trainableSkills;
    }
    if (tmpl.isBanker) {
        auto* banker = npc->addComponent<BankerComponent>();
        banker->storageSlots = tmpl.bankSlots;
        banker->depositFeePercent = tmpl.bankFeePercent;
    }
    if (tmpl.isGuildNPC) {
        auto* guild = npc->addComponent<GuildNPCComponent>();
        guild->creationCost = tmpl.guildCreationCost;
        guild->requiredLevel = tmpl.guildRequiredLevel;
    }
    if (tmpl.isTeleporter) {
        auto* tp = npc->addComponent<TeleporterComponent>();
        tp->destinations = tmpl.destinations;
    }
    if (tmpl.isStoryNPC) {
        auto* story = npc->addComponent<StoryNPCComponent>();
        story->dialogueTree = tmpl.dialogueTree;
        story->rootNodeId = tmpl.dialogueRootNodeId;
    }

    return npc;
}
```

---

## 6. UI Components

### ShopUI

- Grid layout showing merchant's items
- Each item shows: icon, name, buy price, sell price
- "Buy" button per item (checks gold, adds to inventory)
- "Sell" tab — shows player inventory items with sell prices
- Gold display at bottom

### QuestLogUI

- Toggled with a key (e.g., `L` or `J`)
- Lists active quests with progress bars per objective
- Shows quest name, description, objectives with current/required counts
- "Abandon Quest" button per quest
- Completed quests section (collapsible)

### SkillTrainerUI

- Lists available skills for the player's class
- Shows: skill name, required level, gold cost, skill point cost
- Greyed out if requirements not met
- "Learn" button per skill

### BankStorageUI

- Grid layout similar to inventory (player's bank slots)
- Drag-and-drop between inventory and bank
- Gold deposit/withdraw with fee display

### TeleporterUI

- List of destinations with: name, cost, level requirement
- Greyed out if level too low or not enough gold
- Click to travel (with fade transition reusing existing portal fade)

---

## 7. File Layout

```
game/shared/
    quest_manager.h          // QuestManager, ActiveQuest, progress tracking
    quest_data.h             // All hardcoded QuestDefinitions
    npc_types.h              // NPCTemplate, ShopItem, TrainableSkill, TeleportDestination
    dialogue_tree.h          // DialogueNode, DialogueChoice for story NPCs
    bank_storage.h           // BankStorage manager class

game/components/
    game_components.h        // Add: NPCComponent, QuestComponent, QuestGiverComponent,
                             //   ShopComponent, SkillTrainerComponent, BankerComponent,
                             //   GuildNPCComponent, TeleporterComponent, StoryNPCComponent,
                             //   QuestMarkerComponent, BankStorageComponent

game/systems/
    npc_interaction_system.h // Click detection, dialogue routing, NPC UI triggers
    quest_system.h           // Quest progress ticking, marker state updates

game/ui/
    npc_dialogue_ui.h        // Dialogue box rendering (greeting, choices, branching)
    shop_ui.h                // Buy/sell grid UI
    quest_log_ui.h           // Active quest tracker
    skill_trainer_ui.h       // Skill learning UI
    bank_storage_ui.h        // Bank deposit/withdraw UI
    teleporter_ui.h          // Destination list UI

game/
    entity_factory.h         // Add: createNPC(World&, NPCTemplate&)
```

---

## 8. Integration Points with Existing Systems

| New System | Existing System | Integration |
|------------|----------------|-------------|
| QuestManager::onMobKilled | CombatActionSystem mob death | Call on mob death event |
| QuestManager::onItemCollected | InventoryComponent | Call when item added |
| QuestManager::turnInQuest | CharacterStats, Inventory | Add XP/gold/items as rewards |
| ShopComponent buy/sell | InventoryComponent gold/items | Deduct/add gold and items |
| SkillTrainerComponent | SkillManager::learnSkill | Route to existing skill learning |
| GuildNPCComponent | GuildManager | Route to existing guild operations |
| TeleporterComponent | Scene system, fade transitions | Reuse portal fade transition |
| NPCInteractionSystem click | CombatActionSystem click pattern | Same screen-to-world, bounds check pattern. NPC system runs first; sets `clickConsumed` flag on `TargetingComponent` to prevent combat system from also processing the click |
| QuestMarkerComponent render | TextRenderer, NameplateComponent | Draw above existing nameplate |
| BankStorageComponent | InventoryComponent | Transfer items between inventory and bank |

---

## 9. Design Decisions & Rationale

1. **Component-Per-Role over monolithic NPC** — matches existing ECS patterns, composable, queryable
2. **Hardcoded C++ over data files** — consistent with all other game systems (combat, skills, stats), no runtime parsing
3. **NPCs don't move** — exact TWOM behavior, simplifies implementation (no AI, no pathfinding)
4. **Separate QuestGiverComponent from StoryNPCComponent** — quest flow is linear (accept/progress/turn-in), story dialogue is branching. Different complexity, different components.
5. **QuestMarkerComponent separate from QuestGiverComponent** — rendering concern separated from data concern
6. **BankStorage as a new player component** — persistent storage separate from 15-slot inventory, future networking will need to persist this server-side
7. **Fixed shop prices** — TWOM-authentic, no dynamic pricing complexity
8. **Max 10 active quests** — TWOM limit, prevents quest hoarding
