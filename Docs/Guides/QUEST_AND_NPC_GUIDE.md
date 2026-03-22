# Quest & NPC System Guide

How to create quests, NPCs, and wire them together in FateMMO.

---

## Quick Start: Adding a New Quest

All quests are defined in `game/shared/quest_data.h` inside the `buildQuests()` method. Add a new entry to the map:

```cpp
quests[7] = {
    .questId = 7,
    .questName = "Mushroom Harvest",
    .description = "Collect 8 Mushroom Caps from the Woody Forest.",
    .offerDialogue = "The herbalist needs mushroom caps for potions. Can you help?",
    .inProgressDialogue = "Still gathering mushrooms?",
    .turnInDialogue = "Perfect! These will make excellent potions.",
    .tier = QuestTier::Starter,
    .requiredLevel = 0,
    .turnInNpcId = "2",
    .prerequisiteQuestIds = {},
    .objectives = {
        {ObjectiveType::Collect, "Collect 8 Mushroom Caps", "mushroom_cap", 8}
    },
    .rewards = {.xp = 400, .gold = 75, .items = {}}
};
```

Then assign it to an NPC by adding `7` to that NPC's `questIds` list in the scene setup.

---

## Quest Definition Fields

| Field | Type | Description |
|-------|------|-------------|
| `questId` | `uint32_t` | Unique ID. Must match the map key. |
| `questName` | `string` | Shown in quest log and dialogue UI. |
| `description` | `string` | Longer description shown in quest log. |
| `offerDialogue` | `string` | NPC says this when offering the quest. |
| `inProgressDialogue` | `string` | NPC says this while quest is active. |
| `turnInDialogue` | `string` | NPC says this when quest is ready to turn in. |
| `tier` | `QuestTier` | Determines the `?` marker color and level gate. |
| `requiredLevel` | `int` | Minimum player level to accept. |
| `turnInNpcId` | `string` | NPC ID where the quest is turned in (usually the same NPC that gave it). |
| `prerequisiteQuestIds` | `vector<uint32_t>` | All listed quests must be completed first. Empty = no prerequisites. |
| `objectives` | `vector<QuestObjective>` | One or more objectives (see below). |
| `rewards` | `QuestRewards` | XP, gold, and items granted on completion. |

---

## Quest Tiers (TWOM Style)

| Tier | Enum | Level Req | `?` Marker Color |
|------|------|-----------|-----------------|
| Starter | `QuestTier::Starter` | 0+ | White |
| Novice | `QuestTier::Novice` | 25+ | Green |
| Apprentice | `QuestTier::Apprentice` | 50+ | Yellow-Green |
| Adept | `QuestTier::Adept` | 100+ | Yellow |

The `!` marker (quest ready to turn in) is always yellow.

---

## Objective Types

### Kill
Player must kill a specific mob type.

```cpp
{ObjectiveType::Kill, "Slay 10 Leaf Boars", "leaf_boar", 10}
//                     ^ description          ^ mob ID      ^ count
```

Progress is tracked automatically when `QuestSystem::onMobDeath(mobId)` is called (wired to CombatActionSystem).

### Collect
Player must have a specific item in inventory.

```cpp
{ObjectiveType::Collect, "Collect 5 Clovers", "clover", 5}
```

Progress is tracked via `QuestSystem::onItemPickup(itemId)`. Note: requires a loot/drop system to actually give items to players (currently a TODO).

### Deliver
Player must bring a specific item to the turn-in NPC. The item is consumed on turn-in.

```cpp
{ObjectiveType::Deliver, "Deliver Guardian Crystal to Sage", "guardian_crystal", 1}
```

Progress is checked when the player talks to the turn-in NPC via `QuestManager::onDeliverAttempt()`. The item is removed from inventory on quest completion.

### TalkTo
Player must talk to a specific NPC.

```cpp
{ObjectiveType::TalkTo, "Speak with Elder Vellore", "100", 1}
//                                                   ^ NPC ID as string
```

Progress is tracked when `QuestSystem::onNPCInteraction(npcId)` fires (triggered when dialogue opens).

### PvPKills
Player must get PvP kills.

```cpp
{ObjectiveType::PvPKills, "Defeat 2 players", "", 2}
//                                             ^ targetId is empty for PvP
```

Progress is tracked via `QuestSystem::onPvPKill()`.

---

## Quest Rewards

```cpp
.rewards = {
    .xp = 500,              // experience points
    .gold = 100,             // gold (int64_t)
    .items = {               // item rewards (optional)
        {"guardian_ring", 1}, // {itemId, quantity}
        {"potion_hp", 5}
    }
}
```

XP and gold are granted automatically. Item rewards require the item creation API (TODO — items are logged but not yet added to inventory).

---

## Quest Chains (Prerequisites)

To make a quest require completing another quest first:

```cpp
quests[6] = {
    .questId = 6,
    // ...
    .prerequisiteQuestIds = {5},  // must complete quest 5 first
    // ...
};
```

Multiple prerequisites are supported — all must be completed:

```cpp
.prerequisiteQuestIds = {3, 4, 5}  // must complete quests 3, 4, AND 5
```

---

## Creating NPCs

NPCs are created via `EntityFactory::createNPC()` using an `NPCTemplate`. NPCs stand still and face a fixed direction (TWOM style).

### Basic Quest Giver NPC

```cpp
NPCTemplate elder;
elder.name = "Village Elder";
elder.npcId = 1;
elder.position = {256.0f, 256.0f};   // world position in pixels
elder.facing = FaceDirection::Down;
elder.isQuestGiver = true;
elder.questIds = {1, 2, 3};          // quests this NPC offers
elder.dialogueGreeting = "Welcome, adventurer! I have tasks for you.";
EntityFactory::createNPC(world, elder);
```

### Merchant NPC

```cpp
NPCTemplate merchant;
merchant.name = "Potion Merchant";
merchant.npcId = 2;
merchant.position = {320.0f, 256.0f};
merchant.isMerchant = true;
merchant.shopName = "Potion Shop";
merchant.shopItems = {
    // {itemId, displayName, buyPrice, sellPrice, stock}
    {"potion_hp_small", "Small HP Potion", 50, 12, 0},   // 0 = unlimited
    {"potion_mp_small", "Small MP Potion", 50, 12, 0},
    {"potion_hp_medium", "Medium HP Potion", 200, 50, 0}
};
merchant.dialogueGreeting = "Welcome to my shop!";
EntityFactory::createNPC(world, merchant);
```

### Skill Trainer NPC

```cpp
NPCTemplate trainer;
trainer.name = "Warrior Master";
trainer.npcId = 10;
trainer.position = {400.0f, 256.0f};
trainer.isSkillTrainer = true;
trainer.trainerClass = ClassType::Warrior;
trainer.trainableSkills = {
    // {skillId, requiredLevel, goldCost, skillPointCost, requiredClass}
    {"slash", 1, 100, 1, ClassType::Warrior},
    {"shield_bash", 10, 500, 2, ClassType::Warrior},
    {"whirlwind", 25, 2000, 3, ClassType::Warrior}
};
trainer.dialogueGreeting = "I can teach you the way of the sword.";
EntityFactory::createNPC(world, trainer);
```

### Banker NPC

```cpp
NPCTemplate banker;
banker.name = "Bank Teller";
banker.npcId = 20;
banker.position = {500.0f, 256.0f};
banker.isBanker = true;
banker.bankSlots = 30;
banker.bankFeePercent = 0.0f;     // 0% fee, or 0.05 for 5%
banker.dialogueGreeting = "Need to store some items?";
EntityFactory::createNPC(world, banker);
```

### Guild NPC

```cpp
NPCTemplate guildNPC;
guildNPC.name = "Guild Registrar";
guildNPC.npcId = 30;
guildNPC.position = {600.0f, 256.0f};
guildNPC.isGuildNPC = true;
guildNPC.guildCreationCost = 50000;
guildNPC.guildRequiredLevel = 20;
guildNPC.dialogueGreeting = "Looking to form a guild?";
EntityFactory::createNPC(world, guildNPC);
```

### Teleporter NPC

```cpp
NPCTemplate teleporter;
teleporter.name = "Travel Guide";
teleporter.npcId = 40;
teleporter.position = {700.0f, 256.0f};
teleporter.isTeleporter = true;
teleporter.destinations = {
    // {name, sceneId, targetPosition, cost, requiredLevel}
    {"Main Village", "main_village", {128.0f, 128.0f}, 0, 0},
    {"Woody Forest", "woody_forest", {256.0f, 64.0f}, 100, 5},
    {"Dark Cavern", "dark_cavern", {64.0f, 192.0f}, 500, 25}
};
teleporter.dialogueGreeting = "Where would you like to go?";
EntityFactory::createNPC(world, teleporter);
```

### Story NPC (Branching Dialogue)

```cpp
NPCTemplate storyteller;
storyteller.name = "Old Sage";
storyteller.npcId = 50;
storyteller.position = {800.0f, 256.0f};
storyteller.isStoryNPC = true;
storyteller.dialogueRootNodeId = 1;
storyteller.dialogueTree = {
    {1, "I've seen many things in my years... What would you like to know?", {
        {"Tell me about the forest", 2, {}},
        {"Tell me about the dark cavern", 3, {}},
        {"Goodbye", 0, {}}
    }, {}},
    {2, "The Woody Forest is home to many creatures. Be careful of the Leaf Boars.", {
        {"What about deeper in?", 4, {}},
        {"Back", 1, {}}
    }, {}},
    {3, "The Dark Cavern... few return from its depths.", {
        {"Back", 1, {}}
    }, {}},
    {4, "Deep within lies the Forest Guardian. Only the brave dare face it.", {
        {"I'm ready", 0, {.action = DialogueAction::GiveXP, .value = 50}},
        {"Back", 1, {}}
    }, {}}
};
storyteller.dialogueGreeting = "Ah, a young adventurer...";
EntityFactory::createNPC(world, storyteller);
```

### Multi-Role NPC

An NPC can have multiple roles. Just set multiple flags:

```cpp
NPCTemplate multiRole;
multiRole.name = "Village Merchant";
multiRole.npcId = 3;
multiRole.position = {350.0f, 256.0f};
multiRole.isQuestGiver = true;
multiRole.isMerchant = true;
multiRole.questIds = {7};
multiRole.shopName = "General Store";
multiRole.shopItems = { /* ... */ };
multiRole.dialogueGreeting = "Need supplies or have something to report?";
EntityFactory::createNPC(world, multiRole);
```

The dialogue UI will show all applicable options (quest buttons + shop button).

The nameplate subtitle is set by priority: Quest > Merchant > Skill Trainer > Banker > Guild > Teleporter.

---

## NPCTemplate Fields Reference

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `string` | | Display name and entity name |
| `npcId` | `uint32_t` | 0 | Unique NPC identifier |
| `position` | `Vec2` | | World position in pixels |
| `facing` | `FaceDirection` | Down | Direction NPC faces (Down/Up/Left/Right) |
| `spriteSheet` | `string` | | Path to sprite (falls back to default NPC sprite) |
| `interactionRadius` | `float` | 2.0 | Max interaction distance in tiles |
| `dialogueGreeting` | `string` | | What NPC says when clicked |
| **Role Flags** | | | |
| `isQuestGiver` | `bool` | false | Has quests to give |
| `isMerchant` | `bool` | false | Has a shop |
| `isSkillTrainer` | `bool` | false | Can teach skills |
| `isBanker` | `bool` | false | Provides bank storage |
| `isGuildNPC` | `bool` | false | Guild creation/management |
| `isTeleporter` | `bool` | false | Can teleport player |
| `isStoryNPC` | `bool` | false | Has branching dialogue tree |

---

## How It All Connects

```
Player clicks NPC
    → NPCInteractionSystem detects click (consumes it so CombatActionSystem ignores it)
    → Opens NPCDialogueUI
    → QuestSystem::onNPCInteraction(npcId) fires (progresses TalkTo objectives)
    → UI shows greeting + role-specific buttons

Player accepts quest
    → QuestManager::acceptQuest(questId)
    → Quest added to active list (max 10)
    → QuestSystem::refreshQuestMarkers() updates all NPC markers

Player kills a mob
    → CombatActionSystem::onMobDeath()
    → QuestSystem::onMobDeath(mobId)
    → QuestManager::onMobKilled() increments Kill objectives

Player turns in quest
    → QuestManager::turnInQuest() verifies completion
    → Grants XP + gold rewards
    → Moves quest to completed set
    → Markers refresh
```

---

## Key Bindings

| Key | Action |
|-----|--------|
| Click NPC | Open dialogue |
| L | Toggle Quest Log |
| ESC | Close dialogue |

---

## Editor & Persistence

### Adding NPC Components in the Editor

All NPC components are available in the editor's **Add Component** popup (right-click an entity in the inspector). They're listed under two sections:

- **NPC** — NPCComponent, Quest Giver, Quest Marker, Shop, Skill Trainer, Banker, Guild NPC, Teleporter, Story NPC
- **Player Quest/Bank** — QuestComponent, BankStorageComponent

Components with simple fields (NPCComponent, BankerComponent, GuildNPCComponent) display editable fields in the generic reflected inspector. Complex components (shops, dialogue trees, etc.) are best configured in code via `NPCTemplate`.

### Scene Save/Load

NPC entities fully persist across scene save/load cycles. All component data is serialized via custom `toJson`/`fromJson` handlers:

- Shop inventories (item IDs, names, prices, stock)
- Skill trainer skill lists
- Teleporter destinations with positions and costs
- Dialogue trees with nested choices, actions, and conditions
- Quest progress (active quests with objective counts, completed quest IDs)
- Bank storage (gold, items, slot count)

### Prefabs

NPCs can be saved as prefabs and spawned anywhere. The workflow:
1. Create an NPC entity in code or via the editor
2. Right-click in hierarchy → Save as Prefab
3. Spawn copies from the Prefabs tab in the project browser

---

## Files Overview

| File | What It Does |
|------|-------------|
| `game/shared/quest_manager.h` | Quest types, QuestManager class declaration |
| `game/shared/quest_manager.cpp` | QuestManager implementation |
| `game/shared/quest_data.h` | All quest definitions (add new quests here) |
| `game/shared/npc_types.h` | NPCTemplate, ShopItem, TrainableSkill structs |
| `game/shared/dialogue_tree.h` | DialogueNode, DialogueChoice, action/condition enums |
| `game/shared/bank_storage.h` | BankStorage class |
| `game/components/game_components.h` | All NPC/Quest ECS components |
| `game/entity_factory.h` | createNPC() factory method |
| `game/systems/npc_interaction_system.h` | Click detection, dialogue open/close |
| `game/systems/quest_system.h` | Quest progress routing, marker updates |
| `game/ui/npc_dialogue_ui.h/.cpp` | Main dialogue box UI |
| `game/ui/shop_ui.h/.cpp` | Merchant buy/sell UI |
| `game/ui/quest_log_ui.h/.cpp` | Quest tracker UI |
| `game/ui/skill_trainer_ui.h/.cpp` | Skill learning UI |
| `game/ui/bank_storage_ui.h/.cpp` | Bank storage UI |
| `game/ui/teleporter_ui.h/.cpp` | Teleport destination UI |
| `game/register_components.h` | Component registration + custom serializers for all NPC/Quest types |
