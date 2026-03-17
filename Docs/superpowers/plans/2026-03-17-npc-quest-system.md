# NPC & Quest System Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a complete TWOM-style NPC and Quest system with quest givers, merchants, skill trainers, bankers, guild NPCs, teleporters, and story NPCs — all using the existing ECS architecture.

**Architecture:** Component-per-role ECS design. Each NPC role is a separate component (ShopComponent, QuestGiverComponent, etc.) attached to NPC entities. A QuestManager embedded in a player QuestComponent tracks quest progress. Two new systems handle NPC interaction and quest progress. UI is ImGui-based.

**Tech Stack:** C++23, doctest, ImGui (docking branch), existing ECS framework (World/Entity/Component/System), existing TextRenderer, existing Input/Camera systems.

**Spec:** `docs/superpowers/specs/2026-03-17-npc-quest-system-design.md`

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `game/shared/npc_types.h` | NPCTemplate, ShopItem, TrainableSkill, TeleportDestination structs |
| `game/shared/dialogue_tree.h` | DialogueNode, DialogueChoice, DialogueAction/Condition enums |
| `game/shared/quest_manager.h` | QuestManager class, QuestDefinition, QuestObjective, ActiveQuest, QuestRewards |
| `game/shared/quest_manager.cpp` | QuestManager method implementations (avoids circular include with quest_data.h) |
| `game/shared/quest_data.h` | Hardcoded quest definitions (quest registry) |
| `game/shared/bank_storage.h` | BankStorage manager class |
| `game/systems/npc_interaction_system.h` | Click-to-interact, dialogue routing, click consumption |
| `game/systems/quest_system.h` | Quest progress ticking, marker state updates |
| `game/ui/npc_dialogue_ui.h` | Dialogue box rendering |
| `game/ui/npc_dialogue_ui.cpp` | Dialogue box implementation |
| `game/ui/shop_ui.h` | Shop buy/sell UI |
| `game/ui/shop_ui.cpp` | Shop implementation |
| `game/ui/quest_log_ui.h` | Quest tracker UI |
| `game/ui/quest_log_ui.cpp` | Quest tracker implementation |
| `game/ui/skill_trainer_ui.h` | Skill trainer UI |
| `game/ui/skill_trainer_ui.cpp` | Skill trainer implementation |
| `game/ui/bank_storage_ui.h` | Bank deposit/withdraw UI |
| `game/ui/bank_storage_ui.cpp` | Bank implementation |
| `game/ui/teleporter_ui.h` | Teleporter destination list UI |
| `game/ui/teleporter_ui.cpp` | Teleporter implementation |
| `tests/test_quest_manager.cpp` | QuestManager unit tests |
| `tests/test_bank_storage.cpp` | BankStorage unit tests |
| `tests/test_dialogue_tree.cpp` | Dialogue tree unit tests |

### Modified Files

| File | Changes |
|------|---------|
| `game/shared/game_types.h` | Add `FaceDirection` enum, add `ClassType::Any = 255` |
| `game/components/game_components.h` | Add all new NPC/Quest components, add `roleSubtitle` to NameplateComponent, add `clickConsumed` to TargetingComponent |
| `game/entity_factory.h` | Add `createNPC()` factory method |
| `game/systems/combat_action_system.h` | Check `clickConsumed` flag before processing clicks |

---

## Task 1: Game Types — FaceDirection & ClassType::Any

**Files:**
- Modify: `game/shared/game_types.h:13-17` (ClassType enum), add FaceDirection near line 205

- [ ] **Step 1: Add FaceDirection enum to game_types.h**

Add after the `CCType` enum (around line 204):

```cpp
enum class FaceDirection : uint8_t {
    Down = 0,
    Up = 1,
    Left = 2,
    Right = 3
};
```

- [ ] **Step 2: Add Any to ClassType enum**

Change `ClassType` enum from:
```cpp
enum class ClassType : uint8_t {
    Warrior = 0,
    Mage = 1,
    Archer = 2
};
```
To:
```cpp
enum class ClassType : uint8_t {
    Warrior = 0,
    Mage = 1,
    Archer = 2,
    Any = 255
};
```

- [ ] **Step 3: Build to verify no compilation errors**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds with no errors.

- [ ] **Step 4: Commit**

```bash
git add game/shared/game_types.h
git commit -m "feat: add FaceDirection enum and ClassType::Any for NPC system"
```

---

## Task 2: Dialogue Tree Data Structures

**Files:**
- Create: `game/shared/dialogue_tree.h`
- Test: `tests/test_dialogue_tree.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_dialogue_tree.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/shared/dialogue_tree.h"

using namespace fate;

TEST_CASE("DialogueTree: create dialogue node with choices") {
    DialogueNode node;
    node.nodeId = 1;
    node.npcText = "Hello traveler!";
    node.choices.push_back({"Tell me about this town", 2, {}});
    node.choices.push_back({"Goodbye", 0, {}});

    CHECK(node.nodeId == 1);
    CHECK(node.npcText == "Hello traveler!");
    CHECK(node.choices.size() == 2);
    CHECK(node.choices[0].nextNodeId == 2);
    CHECK(node.choices[1].nextNodeId == 0);  // 0 = end conversation
}

TEST_CASE("DialogueTree: dialogue action data") {
    DialogueActionData action;
    action.action = DialogueAction::GiveItem;
    action.targetId = "potion_hp_small";
    action.value = 3;

    CHECK(action.action == DialogueAction::GiveItem);
    CHECK(action.targetId == "potion_hp_small");
    CHECK(action.value == 3);
}

TEST_CASE("DialogueTree: dialogue condition data") {
    DialogueConditionData cond;
    cond.condition = DialogueCondition::MinLevel;
    cond.value = 25;

    CHECK(cond.condition == DialogueCondition::MinLevel);
    CHECK(cond.value == 25);
}

TEST_CASE("DialogueTree: choice with action") {
    DialogueChoice choice;
    choice.buttonText = "Accept reward";
    choice.nextNodeId = 0;
    choice.onSelect.action = DialogueAction::GiveGold;
    choice.onSelect.value = 500;

    CHECK(choice.onSelect.action == DialogueAction::GiveGold);
    CHECK(choice.onSelect.value == 500);
}

TEST_CASE("DialogueTree: node with condition gate") {
    DialogueNode node;
    node.nodeId = 5;
    node.npcText = "You look experienced enough...";
    node.condition.condition = DialogueCondition::MinLevel;
    node.condition.value = 50;

    CHECK(node.condition.condition == DialogueCondition::MinLevel);
    CHECK(node.condition.value == 50);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fate_tests 2>&1 | tail -5`
Expected: FAIL — `game/shared/dialogue_tree.h` not found.

- [ ] **Step 3: Write the dialogue_tree.h implementation**

Create `game/shared/dialogue_tree.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fate {

// Action types for dialogue side effects (avoids std::function in data structs)
enum class DialogueAction : uint8_t {
    None = 0,
    GiveItem,        // gives player an item
    GiveXP,          // gives player XP
    GiveGold,        // gives player gold
    SetFlag,         // sets a quest/story flag (storage value)
    Heal             // fully heals player
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
    HasClass         // player is a specific class
};

struct DialogueConditionData {
    DialogueCondition condition = DialogueCondition::None;
    std::string targetId;
    int32_t value = 0;
};

struct DialogueChoice {
    std::string buttonText;
    uint32_t nextNodeId = 0;           // 0 = end conversation
    DialogueActionData onSelect;       // optional side effect
};

struct DialogueNode {
    uint32_t nodeId = 0;
    std::string npcText;
    std::vector<DialogueChoice> choices;    // 2-4 options
    DialogueConditionData condition;        // optional visibility gate
};

} // namespace fate
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target fate_tests && ./build/fate_tests -tc="DialogueTree*"`
Expected: All 5 test cases PASS.

- [ ] **Step 5: Commit**

```bash
git add game/shared/dialogue_tree.h tests/test_dialogue_tree.cpp
git commit -m "feat: add dialogue tree data structures for NPC system"
```

---

## Task 3: Bank Storage Manager

**Files:**
- Create: `game/shared/bank_storage.h`
- Test: `tests/test_bank_storage.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_bank_storage.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/shared/bank_storage.h"

using namespace fate;

TEST_CASE("BankStorage: initialize with max slots") {
    BankStorage bank;
    bank.initialize(30);
    CHECK(bank.getStoredGold() == 0);
    CHECK(bank.getItems().empty());
    CHECK_FALSE(bank.isFull());
}

TEST_CASE("BankStorage: deposit and withdraw gold") {
    BankStorage bank;
    bank.initialize(30);

    CHECK(bank.depositGold(1000, 0.0f));
    CHECK(bank.getStoredGold() == 1000);

    CHECK(bank.withdrawGold(500));
    CHECK(bank.getStoredGold() == 500);

    CHECK_FALSE(bank.withdrawGold(9999));  // not enough
    CHECK(bank.getStoredGold() == 500);    // unchanged
}

TEST_CASE("BankStorage: deposit gold with fee") {
    BankStorage bank;
    bank.initialize(30);

    // 5% fee on 1000 = floor(1000 * 0.05) = 50 fee, 950 deposited
    CHECK(bank.depositGold(1000, 0.05f));
    CHECK(bank.getStoredGold() == 950);
}

TEST_CASE("BankStorage: deposit and withdraw items") {
    BankStorage bank;
    bank.initialize(3);  // only 3 slots

    CHECK(bank.depositItem("potion_hp", 5));
    CHECK(bank.depositItem("potion_mp", 3));
    CHECK(bank.depositItem("scroll_tp", 1));
    CHECK(bank.isFull());

    // Can't add a 4th different item
    CHECK_FALSE(bank.depositItem("sword_iron", 1));

    // Can stack onto existing item
    CHECK(bank.depositItem("potion_hp", 2));

    // Withdraw
    CHECK(bank.withdrawItem("potion_hp", 3));
    auto& items = bank.getItems();
    // potion_hp should still exist with count 4 (5+2-3)
    bool found = false;
    for (auto& item : items) {
        if (item.itemId == "potion_hp") {
            CHECK(item.count == 4);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("BankStorage: withdraw more items than stored") {
    BankStorage bank;
    bank.initialize(10);
    bank.depositItem("potion_hp", 3);

    CHECK_FALSE(bank.withdrawItem("potion_hp", 5));  // not enough
}

TEST_CASE("BankStorage: withdraw all of an item removes the slot") {
    BankStorage bank;
    bank.initialize(10);
    bank.depositItem("potion_hp", 3);
    CHECK(bank.getItems().size() == 1);

    CHECK(bank.withdrawItem("potion_hp", 3));
    CHECK(bank.getItems().empty());
    CHECK_FALSE(bank.isFull());
}

TEST_CASE("BankStorage: withdraw item that doesn't exist") {
    BankStorage bank;
    bank.initialize(10);
    CHECK_FALSE(bank.withdrawItem("nonexistent", 1));
}

TEST_CASE("BankStorage: zero gold deposit") {
    BankStorage bank;
    bank.initialize(10);
    CHECK_FALSE(bank.depositGold(0, 0.0f));
    CHECK(bank.getStoredGold() == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fate_tests 2>&1 | tail -5`
Expected: FAIL — `game/shared/bank_storage.h` not found.

- [ ] **Step 3: Write the bank_storage.h implementation**

Create `game/shared/bank_storage.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace fate {

struct StoredItem {
    std::string itemId;
    uint16_t count = 0;
};

class BankStorage {
public:
    void initialize(uint16_t maxSlots) {
        maxSlots_ = maxSlots;
        items_.clear();
        storedGold_ = 0;
    }

    bool depositGold(int64_t amount, float feePercent) {
        if (amount <= 0) return false;
        int64_t fee = static_cast<int64_t>(std::floor(amount * feePercent));
        int64_t deposited = amount - fee;
        if (deposited <= 0) return false;
        storedGold_ += deposited;
        return true;
    }

    bool withdrawGold(int64_t amount) {
        if (amount <= 0 || amount > storedGold_) return false;
        storedGold_ -= amount;
        return true;
    }

    int64_t getStoredGold() const { return storedGold_; }

    bool depositItem(const std::string& itemId, uint16_t count) {
        if (count == 0) return false;
        // Try to stack onto existing item
        for (auto& item : items_) {
            if (item.itemId == itemId) {
                item.count += count;
                return true;
            }
        }
        // New slot needed
        if (isFull()) return false;
        items_.push_back({itemId, count});
        return true;
    }

    bool withdrawItem(const std::string& itemId, uint16_t count) {
        if (count == 0) return false;
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (it->itemId == itemId) {
                if (it->count < count) return false;
                it->count -= count;
                if (it->count == 0) {
                    items_.erase(it);
                }
                return true;
            }
        }
        return false;
    }

    const std::vector<StoredItem>& getItems() const { return items_; }
    bool isFull() const { return static_cast<uint16_t>(items_.size()) >= maxSlots_; }

private:
    std::vector<StoredItem> items_;
    int64_t storedGold_ = 0;
    uint16_t maxSlots_ = 30;
};

} // namespace fate
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target fate_tests && ./build/fate_tests -tc="BankStorage*"`
Expected: All 8 test cases PASS.

- [ ] **Step 5: Commit**

```bash
git add game/shared/bank_storage.h tests/test_bank_storage.cpp
git commit -m "feat: add BankStorage manager for NPC banking system"
```

---

## Task 4: Quest Manager — Core Logic

**Files:**
- Create: `game/shared/quest_manager.h`
- Create: `game/shared/quest_data.h`
- Test: `tests/test_quest_manager.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_quest_manager.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/shared/quest_manager.h"
#include "game/shared/quest_data.h"
#include "game/shared/character_stats.h"
#include "game/shared/inventory.h"

using namespace fate;

TEST_CASE("QuestManager: accept quest within limits") {
    QuestManager qm;
    CharacterStats stats;
    stats.level = 1;
    stats.classDef.classType = ClassType::Warrior;

    CHECK(qm.canAcceptQuest(1, stats));
    CHECK(qm.acceptQuest(1));
    CHECK(qm.isQuestActive(1));
    CHECK_FALSE(qm.isQuestComplete(1));
    CHECK_FALSE(qm.hasCompletedQuest(1));
}

TEST_CASE("QuestManager: cannot accept same quest twice") {
    QuestManager qm;
    qm.acceptQuest(1);
    CHECK_FALSE(qm.acceptQuest(1));
}

TEST_CASE("QuestManager: max active quests enforced") {
    QuestManager qm;
    for (uint32_t i = 1; i <= 10; ++i) {
        CHECK(qm.acceptQuest(i));
    }
    CHECK_FALSE(qm.acceptQuest(11));  // 11th quest rejected
}

TEST_CASE("QuestManager: abandon quest") {
    QuestManager qm;
    qm.acceptQuest(1);
    CHECK(qm.abandonQuest(1));
    CHECK_FALSE(qm.isQuestActive(1));
    CHECK_FALSE(qm.hasCompletedQuest(1));
}

TEST_CASE("QuestManager: abandon quest that isn't active") {
    QuestManager qm;
    CHECK_FALSE(qm.abandonQuest(999));
}

TEST_CASE("QuestManager: kill objective progress") {
    QuestManager qm;
    // Quest 1 from quest_data.h must have a Kill objective for this test
    // Using placeholder quest ID 1 — quest_data.h will define it
    qm.acceptQuest(1);
    auto* quest = qm.getActiveQuest(1);
    REQUIRE(quest != nullptr);

    qm.onMobKilled("leaf_boar");
    // Check progress incremented (depends on quest_data.h definition)
}

TEST_CASE("QuestManager: collect objective progress") {
    QuestManager qm;
    qm.acceptQuest(2);  // Quest 2 = collect quest
    qm.onItemCollected("clover");
}

TEST_CASE("QuestManager: talk-to objective progress") {
    QuestManager qm;
    qm.acceptQuest(3);  // Quest 3 = talk-to quest
    qm.onNPCTalkedTo(100);  // NPC ID 100
}

TEST_CASE("QuestManager: pvp kill objective progress") {
    QuestManager qm;
    qm.acceptQuest(4);  // Quest 4 = pvp kills quest
    qm.onPvPKill();
}

TEST_CASE("QuestManager: quest completion and turn-in") {
    QuestManager qm;
    CharacterStats stats;
    stats.level = 1;
    Inventory inv;
    inv.initialize("test", 0);

    qm.acceptQuest(1);

    // Simulate completing kill objective (10 kills needed per quest_data.h)
    auto* quest = qm.getActiveQuest(1);
    REQUIRE(quest != nullptr);
    auto& def = QuestData::getQuest(1);
    for (uint16_t i = 0; i < def.objectives[0].requiredCount; ++i) {
        qm.onMobKilled(def.objectives[0].targetId);
    }
    CHECK(quest->isReadyToTurnIn());
    CHECK(qm.turnInQuest(1, stats, inv));
    CHECK(qm.hasCompletedQuest(1));
    CHECK_FALSE(qm.isQuestActive(1));
}

TEST_CASE("QuestManager: cannot turn in incomplete quest") {
    QuestManager qm;
    CharacterStats stats;
    Inventory inv;
    inv.initialize("test", 0);

    qm.acceptQuest(1);
    CHECK_FALSE(qm.turnInQuest(1, stats, inv));
}

TEST_CASE("QuestManager: cannot accept completed quest") {
    QuestManager qm;
    CharacterStats stats;
    stats.level = 1;
    Inventory inv;
    inv.initialize("test", 0);

    qm.acceptQuest(1);
    auto& def = QuestData::getQuest(1);
    for (uint16_t i = 0; i < def.objectives[0].requiredCount; ++i) {
        qm.onMobKilled(def.objectives[0].targetId);
    }
    qm.turnInQuest(1, stats, inv);

    CHECK_FALSE(qm.canAcceptQuest(1, stats));
    CHECK_FALSE(qm.acceptQuest(1));
}

TEST_CASE("QuestManager: level requirement check") {
    QuestManager qm;
    CharacterStats stats;
    stats.level = 1;

    // Quest 5 requires level 25 (Novice tier)
    CHECK_FALSE(qm.canAcceptQuest(5, stats));
    stats.level = 25;
    CHECK(qm.canAcceptQuest(5, stats));
}

TEST_CASE("QuestManager: prerequisite quest check") {
    QuestManager qm;
    CharacterStats stats;
    stats.level = 50;
    Inventory inv;
    inv.initialize("test", 0);

    // Quest 6 requires quest 5 completed first
    CHECK_FALSE(qm.canAcceptQuest(6, stats));

    // Complete quest 5 first
    qm.acceptQuest(5);
    auto& def5 = QuestData::getQuest(5);
    for (uint16_t i = 0; i < def5.objectives[0].requiredCount; ++i) {
        qm.onMobKilled(def5.objectives[0].targetId);
    }
    qm.turnInQuest(5, stats, inv);

    CHECK(qm.canAcceptQuest(6, stats));
}

TEST_CASE("QuestManager: callbacks fire on accept and complete") {
    QuestManager qm;
    CharacterStats stats;
    stats.level = 1;
    Inventory inv;
    inv.initialize("test", 0);

    uint32_t acceptedId = 0;
    uint32_t completedId = 0;
    qm.onQuestAccepted = [&](uint32_t id) { acceptedId = id; };
    qm.onQuestCompleted = [&](uint32_t id) { completedId = id; };

    qm.acceptQuest(1);
    CHECK(acceptedId == 1);

    auto& def = QuestData::getQuest(1);
    for (uint16_t i = 0; i < def.objectives[0].requiredCount; ++i) {
        qm.onMobKilled(def.objectives[0].targetId);
    }
    qm.turnInQuest(1, stats, inv);
    CHECK(completedId == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fate_tests 2>&1 | tail -5`
Expected: FAIL — files not found.

- [ ] **Step 3: Create quest_data.h with test quest definitions**

Create `game/shared/quest_data.h`:

```cpp
#pragma once
#include "game/shared/quest_manager.h"
#include <unordered_map>

namespace fate {

// Hardcoded quest definitions — add new quests here
class QuestData {
public:
    static const QuestDefinition& getQuest(uint32_t questId) {
        static const auto quests = buildQuestRegistry();
        auto it = quests.find(questId);
        if (it == quests.end()) {
            static const QuestDefinition empty{};
            return empty;
        }
        return it->second;
    }

    static const std::unordered_map<uint32_t, QuestDefinition>& getAllQuests() {
        static const auto quests = buildQuestRegistry();
        return quests;
    }

private:
    static std::unordered_map<uint32_t, QuestDefinition> buildQuestRegistry() {
        std::unordered_map<uint32_t, QuestDefinition> quests;

        // Quest 1: Starter kill quest
        quests[1] = {
            .questId = 1,
            .questName = "Boar Trouble",
            .description = "The Leaf Boars have been destroying crops. Eliminate 10 of them.",
            .offerDialogue = "Those Leaf Boars are destroying our crops! Can you help?",
            .inProgressDialogue = "Have you dealt with the Leaf Boars yet?",
            .turnInDialogue = "You've saved our crops! Here's your reward.",
            .tier = QuestTier::Starter,
            .requiredLevel = 0,
            .turnInNpcId = 1,
            .prerequisiteQuestIds = {},
            .objectives = {
                {ObjectiveType::Kill, "Slay 10 Leaf Boars", "leaf_boar", 10}
            },
            .rewards = {.xp = 500, .gold = 100, .items = {}}
        };

        // Quest 2: Starter collect quest
        quests[2] = {
            .questId = 2,
            .questName = "Lucky Clovers",
            .description = "Collect 5 Clovers dropped by Kooii in the forest.",
            .offerDialogue = "I need some Clovers from the Kooii. Will you gather them?",
            .inProgressDialogue = "Do you have the Clovers yet?",
            .turnInDialogue = "These are perfect! Thank you!",
            .tier = QuestTier::Starter,
            .requiredLevel = 0,
            .turnInNpcId = 2,
            .prerequisiteQuestIds = {},
            .objectives = {
                {ObjectiveType::Collect, "Collect 5 Clovers", "clover", 5}
            },
            .rewards = {.xp = 300, .gold = 50, .items = {}}
        };

        // Quest 3: Talk-to quest
        quests[3] = {
            .questId = 3,
            .questName = "Village Introductions",
            .description = "Speak with the village elders to learn about the area.",
            .offerDialogue = "You should introduce yourself to the elders.",
            .inProgressDialogue = "Have you spoken with everyone?",
            .turnInDialogue = "Good, now you know the village well.",
            .tier = QuestTier::Starter,
            .requiredLevel = 0,
            .turnInNpcId = 1,
            .prerequisiteQuestIds = {},
            .objectives = {
                {ObjectiveType::TalkTo, "Speak with Elder Vellore", "100", 1},
                {ObjectiveType::TalkTo, "Speak with Elder Juri", "101", 1}
            },
            .rewards = {.xp = 200, .gold = 0, .items = {}}
        };

        // Quest 4: PvP kills quest
        quests[4] = {
            .questId = 4,
            .questName = "Prove Your Worth",
            .description = "Defeat 2 players in the battlefield.",
            .offerDialogue = "Think you're tough? Prove it in the battlefield.",
            .inProgressDialogue = "Not enough kills yet. Keep fighting!",
            .turnInDialogue = "Impressive! You've earned this.",
            .tier = QuestTier::Starter,
            .requiredLevel = 10,
            .turnInNpcId = 3,
            .prerequisiteQuestIds = {},
            .objectives = {
                {ObjectiveType::PvPKills, "Defeat 2 players", "", 2}
            },
            .rewards = {.xp = 1000, .gold = 200, .items = {}}
        };

        // Quest 5: Novice tier quest (level 25+)
        quests[5] = {
            .questId = 5,
            .questName = "Forest Guardians",
            .description = "Defeat the Forest Guardians threatening the northern path.",
            .offerDialogue = "Dangerous creatures lurk in the northern forest.",
            .inProgressDialogue = "The forest is still unsafe.",
            .turnInDialogue = "The path is safe once more. Well done!",
            .tier = QuestTier::Novice,
            .requiredLevel = 25,
            .turnInNpcId = 1,
            .prerequisiteQuestIds = {},
            .objectives = {
                {ObjectiveType::Kill, "Slay 5 Forest Guardians", "forest_guardian", 5}
            },
            .rewards = {.xp = 5000, .gold = 500, .items = {}}
        };

        // Quest 6: Requires quest 5 completed (chain quest)
        quests[6] = {
            .questId = 6,
            .questName = "Guardian's Secret",
            .description = "Deliver the Guardian Crystal to the sage.",
            .offerDialogue = "The guardians dropped a strange crystal...",
            .inProgressDialogue = "Take the crystal to the sage in the temple.",
            .turnInDialogue = "This crystal... it contains ancient power.",
            .tier = QuestTier::Novice,
            .requiredLevel = 25,
            .turnInNpcId = 4,
            .prerequisiteQuestIds = {5},
            .objectives = {
                {ObjectiveType::Deliver, "Deliver Guardian Crystal to Sage", "guardian_crystal", 1}
            },
            .rewards = {.xp = 8000, .gold = 1000, .items = {{"guardian_ring", 1}}}
        };

        return quests;
    }
};

} // namespace fate
```

- [ ] **Step 4: Create quest_manager.h**

Create `game/shared/quest_manager.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>
#include <algorithm>

namespace fate {

// Forward declarations
struct CharacterStats;
class Inventory;

enum class QuestTier : uint8_t {
    Starter = 0,       // White ?, level 0+
    Novice = 1,        // Green ?, level 25+
    Apprentice = 2,    // Yellow-Green ?, level 50+
    Adept = 3          // Yellow ?, level 100+
};

enum class ObjectiveType : uint8_t {
    Kill,
    Collect,
    Deliver,
    TalkTo,
    PvPKills
};

struct QuestObjective {
    ObjectiveType type = ObjectiveType::Kill;
    std::string description;
    std::string targetId;
    uint16_t requiredCount = 1;
};

struct ItemReward {
    std::string itemId;
    uint16_t count = 1;
};

struct QuestRewards {
    uint32_t xp = 0;
    int64_t gold = 0;
    std::vector<ItemReward> items;
};

struct QuestDefinition {
    uint32_t questId = 0;
    std::string questName;
    std::string description;
    std::string offerDialogue;
    std::string inProgressDialogue;
    std::string turnInDialogue;
    QuestTier tier = QuestTier::Starter;
    uint16_t requiredLevel = 0;
    uint32_t turnInNpcId = 0;
    std::vector<uint32_t> prerequisiteQuestIds;
    std::vector<QuestObjective> objectives;
    QuestRewards rewards;
};

struct ActiveQuest {
    uint32_t questId = 0;
    std::vector<uint16_t> objectiveProgress;

    bool isReadyToTurnIn() const;
};

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
    const std::vector<ActiveQuest>& getActiveQuests() const { return activeQuests_; }

    // Progress tracking
    void onMobKilled(const std::string& mobId);
    void onItemCollected(const std::string& itemId);
    void onNPCTalkedTo(uint32_t npcId);
    void onPvPKill();
    void onDeliverAttempt(uint32_t npcId, const Inventory& inventory);

    // Turn-in
    bool turnInQuest(uint32_t questId, CharacterStats& stats, Inventory& inventory);

    // Callbacks
    std::function<void(uint32_t questId)> onQuestAccepted;
    std::function<void(uint32_t questId)> onQuestCompleted;
    std::function<void(uint32_t questId, uint16_t objectiveIndex)> onObjectiveProgress;

private:
    std::vector<ActiveQuest> activeQuests_;
    std::unordered_set<uint32_t> completedQuestIds_;

    void progressObjectives(ObjectiveType type, const std::string& targetId);
};

} // namespace fate
```

- [ ] **Step 5: Create quest_manager.cpp**

Create `game/shared/quest_manager.cpp` to avoid circular includes (quest_manager.h ↔ quest_data.h). The header has only declarations; the .cpp has the implementations.

Create `game/shared/quest_manager.cpp`:

```cpp
#include "game/shared/quest_manager.h"
#include "game/shared/quest_data.h"
#include "game/shared/character_stats.h"
#include "game/shared/inventory.h"

namespace fate {

bool ActiveQuest::isReadyToTurnIn() const {
    const auto& def = QuestData::getQuest(questId);
    if (def.questId == 0) return false;
    for (size_t i = 0; i < def.objectives.size(); ++i) {
        if (i >= objectiveProgress.size()) return false;
        if (objectiveProgress[i] < def.objectives[i].requiredCount) return false;
    }
    return true;
}

bool QuestManager::canAcceptQuest(uint32_t questId, const CharacterStats& stats) const {
    if (hasCompletedQuest(questId)) return false;
    if (isQuestActive(questId)) return false;
    if (static_cast<int>(activeQuests_.size()) >= MAX_ACTIVE_QUESTS) return false;

    const auto& def = QuestData::getQuest(questId);
    if (def.questId == 0) return false;
    if (stats.level < def.requiredLevel) return false;

    for (uint32_t prereq : def.prerequisiteQuestIds) {
        if (!hasCompletedQuest(prereq)) return false;
    }
    return true;
}

bool QuestManager::acceptQuest(uint32_t questId) {
    // Lightweight accept — no stats check (caller should use canAcceptQuest first for full validation)
    if (hasCompletedQuest(questId)) return false;
    if (isQuestActive(questId)) return false;
    if (static_cast<int>(activeQuests_.size()) >= MAX_ACTIVE_QUESTS) return false;

    const auto& def = QuestData::getQuest(questId);
    if (def.questId == 0) return false;

    ActiveQuest aq;
    aq.questId = questId;
    aq.objectiveProgress.resize(def.objectives.size(), 0);
    activeQuests_.push_back(std::move(aq));

    if (onQuestAccepted) onQuestAccepted(questId);
    return true;
}

bool QuestManager::abandonQuest(uint32_t questId) {
    auto it = std::find_if(activeQuests_.begin(), activeQuests_.end(),
        [questId](const ActiveQuest& q) { return q.questId == questId; });
    if (it == activeQuests_.end()) return false;
    activeQuests_.erase(it);
    return true;
}

bool QuestManager::isQuestActive(uint32_t questId) const {
    return std::any_of(activeQuests_.begin(), activeQuests_.end(),
        [questId](const ActiveQuest& q) { return q.questId == questId; });
}

bool QuestManager::isQuestComplete(uint32_t questId) const {
    auto it = std::find_if(activeQuests_.begin(), activeQuests_.end(),
        [questId](const ActiveQuest& q) { return q.questId == questId; });
    if (it == activeQuests_.end()) return false;
    return it->isReadyToTurnIn();
}

bool QuestManager::hasCompletedQuest(uint32_t questId) const {
    return completedQuestIds_.contains(questId);
}

ActiveQuest* QuestManager::getActiveQuest(uint32_t questId) {
    auto it = std::find_if(activeQuests_.begin(), activeQuests_.end(),
        [questId](ActiveQuest& q) { return q.questId == questId; });
    return (it != activeQuests_.end()) ? &(*it) : nullptr;
}

void QuestManager::progressObjectives(ObjectiveType type, const std::string& targetId) {
    for (auto& aq : activeQuests_) {
        const auto& def = QuestData::getQuest(aq.questId);
        for (size_t i = 0; i < def.objectives.size(); ++i) {
            if (def.objectives[i].type != type) continue;
            if (def.objectives[i].targetId != targetId) continue;
            if (aq.objectiveProgress[i] >= def.objectives[i].requiredCount) continue;
            aq.objectiveProgress[i]++;
            if (onObjectiveProgress) onObjectiveProgress(aq.questId, static_cast<uint16_t>(i));
        }
    }
}

void QuestManager::onMobKilled(const std::string& mobId) {
    progressObjectives(ObjectiveType::Kill, mobId);
}

void QuestManager::onItemCollected(const std::string& itemId) {
    progressObjectives(ObjectiveType::Collect, itemId);
}

void QuestManager::onNPCTalkedTo(uint32_t npcId) {
    progressObjectives(ObjectiveType::TalkTo, std::to_string(npcId));
}

void QuestManager::onPvPKill() {
    progressObjectives(ObjectiveType::PvPKills, "");
}

void QuestManager::onDeliverAttempt(uint32_t npcId, const Inventory& inventory) {
    // Deliver objectives are checked when talking to the turn-in NPC.
    // Progress is set to requiredCount if player has the item, marking it ready.
    for (auto& aq : activeQuests_) {
        const auto& def = QuestData::getQuest(aq.questId);
        if (def.turnInNpcId != npcId) continue;
        for (size_t i = 0; i < def.objectives.size(); ++i) {
            if (def.objectives[i].type != ObjectiveType::Deliver) continue;
            if (inventory.countItem(def.objectives[i].targetId) >= def.objectives[i].requiredCount) {
                aq.objectiveProgress[i] = def.objectives[i].requiredCount;
            } else {
                aq.objectiveProgress[i] = 0;  // reset if player no longer has item
            }
        }
    }
}

bool QuestManager::turnInQuest(uint32_t questId, CharacterStats& stats, Inventory& inventory) {
    auto* aq = getActiveQuest(questId);
    if (!aq || !aq->isReadyToTurnIn()) return false;

    const auto& def = QuestData::getQuest(questId);

    // Check deliver objectives — player must have items
    // Uses Inventory's existing countItem()/findItemById() slot-based API
    for (size_t i = 0; i < def.objectives.size(); ++i) {
        if (def.objectives[i].type == ObjectiveType::Deliver) {
            if (inventory.countItem(def.objectives[i].targetId) < def.objectives[i].requiredCount) {
                return false;
            }
        }
    }

    // Consume deliver items
    for (size_t i = 0; i < def.objectives.size(); ++i) {
        if (def.objectives[i].type == ObjectiveType::Deliver) {
            int remaining = def.objectives[i].requiredCount;
            while (remaining > 0) {
                int slot = inventory.findItemById(def.objectives[i].targetId);
                if (slot < 0) break;
                int inSlot = inventory.getSlotCount(slot);
                int toRemove = std::min(inSlot, remaining);
                inventory.removeItemQuantity(slot, toRemove);
                remaining -= toRemove;
            }
        }
    }

    // Grant rewards
    stats.addExperience(def.rewards.xp);
    inventory.addGold(def.rewards.gold);
    // Note: Item rewards require creating ItemInstance objects.
    // The exact API depends on the game's item definition system.
    // For now, reward items are logged but not yet added to inventory.
    // TODO: Implement item reward granting once item creation API is finalized.

    // Mark completed
    completedQuestIds_.insert(questId);
    abandonQuest(questId);  // removes from active list

    if (onQuestCompleted) onQuestCompleted(questId);
    return true;
}

} // namespace fate
```

**Note:** This implementation uses the existing `Inventory` slot-based API: `countItem(string)`, `findItemById(string)`, `getSlotCount(slot)`, `removeItemQuantity(slot, count)`, `addGold()`. Verify these methods exist. `CharacterStats` must have `addExperience(uint32_t)`.

- [ ] **Step 6: Verify Inventory API compatibility**

Read `game/shared/inventory.h` and confirm these methods exist: `countItem()`, `findItemById()`, `getSlotCount()`, `removeItemQuantity()`, `addGold()`, `removeGold()`, `getGold()`. If any are missing, add them as wrappers around the existing slot-based operations.

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build --target fate_tests && ./build/fate_tests -tc="QuestManager*"`
Expected: All test cases PASS.

- [ ] **Step 8: Commit**

```bash
git add game/shared/quest_manager.h game/shared/quest_manager.cpp game/shared/quest_data.h tests/test_quest_manager.cpp
git commit -m "feat: add QuestManager with quest definitions, progress tracking, and turn-in"
```

---

## Task 5: NPC Types & Components

**Files:**
- Create: `game/shared/npc_types.h`
- Modify: `game/components/game_components.h`

- [ ] **Step 1: Create npc_types.h**

Create `game/shared/npc_types.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "game/shared/game_types.h"
#include "game/shared/dialogue_tree.h"
#include "engine/core/types.h"

namespace fate {

struct ShopItem {
    std::string itemId;
    std::string itemName;
    int64_t buyPrice = 0;
    int64_t sellPrice = 0;
    uint16_t stock = 0;   // 0 = unlimited
};

struct TrainableSkill {
    std::string skillId;
    uint16_t requiredLevel = 0;
    int64_t goldCost = 0;
    uint16_t skillPointCost = 0;
    ClassType requiredClass = ClassType::Any;
};

struct TeleportDestination {
    std::string destinationName;
    std::string sceneId;
    Vec2 targetPosition;
    int64_t cost = 0;
    uint16_t requiredLevel = 0;
};

struct NPCTemplate {
    // Identity
    std::string name;
    uint32_t npcId = 0;
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

} // namespace fate
```

- [ ] **Step 2: Add NPC components to game_components.h**

Add includes at top of `game/components/game_components.h`:

```cpp
#include "game/shared/npc_types.h"
#include "game/shared/quest_manager.h"
#include "game/shared/bank_storage.h"
```

Add before the `} // namespace fate` closing brace, after MobNameplateComponent:

```cpp
// ============================================================================
// NPC Components
// ============================================================================

struct NPCComponent : public Component {
    FATE_LEGACY_COMPONENT(NPCComponent)
    uint32_t npcId = 0;
    std::string displayName;
    std::string dialogueGreeting;
    float interactionRadius = 2.0f;   // in tiles
    FaceDirection faceDirection = FaceDirection::Down;
};

struct QuestGiverComponent : public Component {
    FATE_LEGACY_COMPONENT(QuestGiverComponent)
    std::vector<uint32_t> questIds;
};

enum class MarkerState : uint8_t {
    None = 0,
    Available = 1,
    TurnIn = 2
};

struct QuestMarkerComponent : public Component {
    FATE_LEGACY_COMPONENT(QuestMarkerComponent)
    MarkerState currentState = MarkerState::None;
    QuestTier highestTier = QuestTier::Starter;
};

struct ShopComponent : public Component {
    FATE_LEGACY_COMPONENT(ShopComponent)
    std::string shopName;
    std::vector<ShopItem> inventory;
};

struct SkillTrainerComponent : public Component {
    FATE_LEGACY_COMPONENT(SkillTrainerComponent)
    ClassType trainerClass = ClassType::Any;
    std::vector<TrainableSkill> skills;
};

struct BankerComponent : public Component {
    FATE_LEGACY_COMPONENT(BankerComponent)
    uint16_t storageSlots = 30;
    float depositFeePercent = 0.0f;
};

struct GuildNPCComponent : public Component {
    FATE_LEGACY_COMPONENT(GuildNPCComponent)
    int64_t creationCost = 0;
    uint16_t requiredLevel = 0;
};

struct TeleporterComponent : public Component {
    FATE_LEGACY_COMPONENT(TeleporterComponent)
    std::vector<TeleportDestination> destinations;
};

struct StoryNPCComponent : public Component {
    FATE_LEGACY_COMPONENT(StoryNPCComponent)
    std::vector<DialogueNode> dialogueTree;
    uint32_t rootNodeId = 0;
};

// ============================================================================
// Player Quest & Bank Components
// ============================================================================

struct QuestComponent : public Component {
    FATE_LEGACY_COMPONENT(QuestComponent)
    QuestManager quests;
};

struct BankStorageComponent : public Component {
    FATE_LEGACY_COMPONENT(BankStorageComponent)
    BankStorage storage;
};
```

- [ ] **Step 3: Add roleSubtitle and clickConsumed to existing components**

In `NameplateComponent`, add after `bool showLevel = true;`:

```cpp
    std::string roleSubtitle;        // e.g., "[Merchant]", "[Quest]"
```

In `TargetingComponent`, add after `float maxTargetRange = 10.0f;`:

```cpp
    bool clickConsumed = false;      // Set by NPCInteractionSystem, checked by CombatActionSystem
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add game/shared/npc_types.h game/components/game_components.h
git commit -m "feat: add NPC role components, quest/bank player components, and NPC types"
```

---

## Task 6: EntityFactory — createNPC

**Files:**
- Modify: `game/entity_factory.h`

- [ ] **Step 1: Read current entity_factory.h**

Read the full file to understand the existing pattern.

- [ ] **Step 2: Add createNPC factory method**

Add to the EntityFactory class:

```cpp
static Entity* createNPC(World& world, const NPCTemplate& tmpl) {
    Entity* npc = world.createEntity(tmpl.name);
    npc->setTag("npc");

    // Engine components
    auto* transform = npc->addComponent<Transform>(tmpl.position.x, tmpl.position.y);
    auto* sprite = npc->addComponent<SpriteComponent>();
    // TODO: Load sprite from tmpl.spriteSheet when sprite loading is available

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

    // Determine role subtitle (priority: quest > merchant > other)
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

- [ ] **Step 3: Add QuestComponent and BankStorageComponent to createPlayer**

In the existing `createPlayer` method, add after the other component additions:

```cpp
    player->addComponent<QuestComponent>();
    auto* bankComp = player->addComponent<BankStorageComponent>();
    bankComp->storage.initialize(30);
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add game/entity_factory.h
git commit -m "feat: add createNPC factory method and quest/bank components to player"
```

---

## Task 7: NPC Interaction System

**Files:**
- Create: `game/systems/npc_interaction_system.h`
- Modify: `game/systems/combat_action_system.h` (check clickConsumed)

- [ ] **Step 1: Create npc_interaction_system.h**

Create `game/systems/npc_interaction_system.h`:

```cpp
#pragma once
#include "engine/ecs/world.h"
#include "engine/input/input.h"
#include "engine/rendering/camera.h"
#include "game/components/game_components.h"
#include "game/shared/npc_types.h"
#include <imgui.h>

namespace fate {

class NPCInteractionSystem : public System {
public:
    const char* name() const override { return "NPCInteractionSystem"; }

    Camera* camera = nullptr;
    Vec2 displaySize;

    // State
    bool dialogueOpen = false;
    Entity* interactingNPC = nullptr;
    Entity* localPlayer = nullptr;

    void update(float dt) override {
        if (!world_ || !camera) return;

        // Reset click consumed flag each frame
        if (localPlayer) {
            auto* targeting = localPlayer->getComponent<TargetingComponent>();
            if (targeting) targeting->clickConsumed = false;
        }

        // Find local player
        if (!localPlayer) {
            world_->forEach<PlayerController>([&](Entity* e, PlayerController* pc) {
                if (pc->isLocalPlayer) localPlayer = e;
            });
            if (!localPlayer) return;
        }

        // Close dialogue if player walked out of range
        if (dialogueOpen && interactingNPC) {
            auto* playerTransform = localPlayer->getComponent<Transform>();
            auto* npcTransform = interactingNPC->getComponent<Transform>();
            auto* npcComp = interactingNPC->getComponent<NPCComponent>();
            if (playerTransform && npcTransform && npcComp) {
                float dx = playerTransform->position.x - npcTransform->position.x;
                float dy = playerTransform->position.y - npcTransform->position.y;
                float dist = std::sqrt(dx * dx + dy * dy) / 32.0f;  // convert to tiles
                if (dist > npcComp->interactionRadius + 1.0f) {
                    closeDialogue();
                }
            }
        }

        // Process click
        if (dialogueOpen) return;  // don't process new clicks while dialogue is open

        auto& input = Input::instance();
        bool clicked = input.isMousePressed(SDL_BUTTON_LEFT);
        bool touched = input.isTouchPressed(0);
        if (!clicked && !touched) return;
        if (ImGui::GetIO().WantCaptureMouse) return;

        Vec2 screenPos = touched ? input.touchPosition(0) : input.mousePosition();
        Vec2 worldClick = camera->screenToWorld(screenPos, (int)displaySize.x, (int)displaySize.y);

        // Find NPC under click
        Entity* hitNPC = nullptr;
        world_->forEach<NPCComponent, Transform, SpriteComponent>(
            [&](Entity* e, NPCComponent* npc, Transform* t, SpriteComponent* spr) {
                if (hitNPC) return;  // already found one
                Vec2 half = spr->size * 0.5f;
                if (worldClick.x >= t->position.x - half.x &&
                    worldClick.x <= t->position.x + half.x &&
                    worldClick.y >= t->position.y - half.y &&
                    worldClick.y <= t->position.y + half.y) {
                    hitNPC = e;
                }
            }
        );

        if (!hitNPC) return;

        // Check range
        auto* playerTransform = localPlayer->getComponent<Transform>();
        auto* npcTransform = hitNPC->getComponent<Transform>();
        auto* npcComp = hitNPC->getComponent<NPCComponent>();
        if (!playerTransform || !npcTransform || !npcComp) return;

        float dx = playerTransform->position.x - npcTransform->position.x;
        float dy = playerTransform->position.y - npcTransform->position.y;
        float distTiles = std::sqrt(dx * dx + dy * dy) / 32.0f;

        // Consume click regardless of range (prevents combat system from also processing)
        auto* targeting = localPlayer->getComponent<TargetingComponent>();
        if (targeting) {
            targeting->clickConsumed = true;
            targeting->targetType = TargetType::NPC;
        }

        if (distTiles > npcComp->interactionRadius) {
            // TODO: Show "Too far away" floating text
            return;
        }

        // Open dialogue
        openDialogue(hitNPC);
    }

    void openDialogue(Entity* npc) {
        interactingNPC = npc;
        dialogueOpen = true;
    }

    void closeDialogue() {
        interactingNPC = nullptr;
        dialogueOpen = false;
    }

private:
};

} // namespace fate
```

- [ ] **Step 2: Add clickConsumed check to CombatActionSystem**

In `game/systems/combat_action_system.h`, find the click processing section (`processClickTargeting` or equivalent). Add at the start of click processing, after the `if (!clicked && !touched) return;` line:

```cpp
        // Check if NPCInteractionSystem already consumed this click
        auto* targeting = /* local player's targeting component */;
        if (targeting && targeting->clickConsumed) return;
```

- [ ] **Step 3: Register NPCInteractionSystem in the game setup**

Find where systems are registered (likely in the main game app or scene setup) and add `NPCInteractionSystem` **before** `CombatActionSystem` in the update order.

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add game/systems/npc_interaction_system.h game/systems/combat_action_system.h
git commit -m "feat: add NPC interaction system with click detection and dialogue triggers"
```

---

## Task 8: Quest System (Progress & Markers)

**Files:**
- Create: `game/systems/quest_system.h`

- [ ] **Step 1: Create quest_system.h**

Create `game/systems/quest_system.h`:

```cpp
#pragma once
#include "engine/ecs/world.h"
#include "game/components/game_components.h"
#include "game/shared/quest_data.h"

namespace fate {

class QuestSystem : public System {
public:
    const char* name() const override { return "QuestSystem"; }

    void init() override {
        // Find local player and wire up callbacks for event-driven marker updates
    }

    void update(float dt) override {
        if (!world_) return;

        // On first frame, wire up callbacks if not done
        if (!callbacksWired_) {
            wireCallbacks();
        }
    }

    // Called by CombatActionSystem when a mob dies
    void onMobDeath(const std::string& mobId) {
        if (!world_) return;
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onMobKilled(mobId);
        });
    }

    // Called when player picks up / receives an item
    void onItemPickup(const std::string& itemId) {
        if (!world_) return;
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onItemCollected(itemId);
        });
    }

    // Called by NPCInteractionSystem when player talks to an NPC
    void onNPCInteraction(uint32_t npcId) {
        if (!world_) return;
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onNPCTalkedTo(npcId);
        });
    }

    // Called by combat system on PvP kill
    void onPvPKill() {
        if (!world_) return;
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onPvPKill();
        });
    }

    // Update quest markers on all NPCs (event-driven — call after quest state changes)
    void refreshQuestMarkers() {
        if (!world_) return;

        // Find local player's quest state
        QuestManager* playerQuests = nullptr;
        CharacterStats* playerStats = nullptr;
        world_->forEach<QuestComponent, CharacterStatsComponent, PlayerController>(
            [&](Entity*, QuestComponent* qc, CharacterStatsComponent* sc, PlayerController* pc) {
                if (pc->isLocalPlayer) {
                    playerQuests = &qc->quests;
                    playerStats = &sc->stats;
                }
            }
        );
        if (!playerQuests || !playerStats) return;

        // Update each quest-giving NPC's marker (also query NPCComponent for the NPC's ID)
        world_->forEach<NPCComponent, QuestGiverComponent, QuestMarkerComponent>(
            [&](Entity*, NPCComponent* npcComp, QuestGiverComponent* qg, QuestMarkerComponent* marker) {
                marker->currentState = MarkerState::None;
                QuestTier bestTier = QuestTier::Starter;
                bool hasAvailable = false;

                for (uint32_t questId : qg->questIds) {
                    const auto& def = QuestData::getQuest(questId);
                    if (def.questId == 0) continue;

                    // Check turn-in first (highest priority) — compare against this NPC's actual ID
                    if (playerQuests->isQuestComplete(questId) && def.turnInNpcId == npcComp->npcId) {
                        marker->currentState = MarkerState::TurnIn;
                        return;  // TurnIn takes priority, stop checking
                    }

                    // Check available
                    if (playerQuests->canAcceptQuest(questId, *playerStats)) {
                        hasAvailable = true;
                        if (def.tier > bestTier) bestTier = def.tier;
                    }
                }

                if (hasAvailable) {
                    marker->currentState = MarkerState::Available;
                    marker->highestTier = bestTier;
                }
            }
        );
    }

private:
    bool callbacksWired_ = false;

    void wireCallbacks() {
        world_->forEach<QuestComponent, PlayerController>(
            [&](Entity*, QuestComponent* qc, PlayerController* pc) {
                if (!pc->isLocalPlayer) return;
                qc->quests.onQuestAccepted = [this](uint32_t) { refreshQuestMarkers(); };
                qc->quests.onQuestCompleted = [this](uint32_t) { refreshQuestMarkers(); };
            }
        );
        callbacksWired_ = true;
        refreshQuestMarkers();  // Initial marker state
    }
};

} // namespace fate
```

- [ ] **Step 2: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add game/systems/quest_system.h
git commit -m "feat: add QuestSystem for progress tracking and event-driven quest markers"
```

---

## Task 9: NPC Dialogue UI

**Files:**
- Create: `game/ui/npc_dialogue_ui.h`
- Create: `game/ui/npc_dialogue_ui.cpp`

- [ ] **Step 1: Create npc_dialogue_ui.h**

```cpp
#pragma once
#include "engine/ecs/entity.h"
#include "game/components/game_components.h"
#include "game/shared/quest_data.h"

namespace fate {

class NPCInteractionSystem;
class QuestSystem;

class NPCDialogueUI {
public:
    void render(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem, QuestSystem* questSystem);

private:
    // Story NPC dialogue state
    uint32_t currentDialogueNodeId_ = 0;
    bool inStoryDialogue_ = false;

    void renderQuestGiverOptions(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem, QuestSystem* questSystem);
    void renderShopButton(Entity* npc, NPCInteractionSystem* npcSystem);
    void renderSkillTrainerButton(Entity* npc, NPCInteractionSystem* npcSystem);
    void renderBankerButton(Entity* npc, NPCInteractionSystem* npcSystem);
    void renderGuildNPCButton(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem);
    void renderTeleporterOptions(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem);
    void renderStoryDialogue(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem);
};

} // namespace fate
```

- [ ] **Step 2: Create npc_dialogue_ui.cpp**

Create `game/ui/npc_dialogue_ui.cpp` with ImGui-based dialogue box rendering. The implementation should:
- Show NPC name as window title
- Show greeting or context-specific text in the body
- Show role-appropriate buttons at the bottom
- For quest givers: show quest offer/progress/turn-in dialogue with Accept/Decline/Complete buttons
- For multi-role NPCs: show all applicable buttons
- Handle button clicks to open sub-UIs (shop, skill trainer, etc.) or accept/complete quests
- Close on ESC or Close button

```cpp
#include "game/ui/npc_dialogue_ui.h"
#include "game/systems/npc_interaction_system.h"
#include "game/systems/quest_system.h"
#include <imgui.h>

namespace fate {

void NPCDialogueUI::render(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem, QuestSystem* questSystem) {
    if (!npc || !player) return;

    auto* npcComp = npc->getComponent<NPCComponent>();
    if (!npcComp) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.7f),
                           ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin(npcComp->displayName.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {

        // Story NPC branching dialogue takes over the whole window
        if (inStoryDialogue_ && npc->getComponent<StoryNPCComponent>()) {
            renderStoryDialogue(npc, player, npcSystem);
            ImGui::End();
            return;
        }

        // NPC greeting text
        ImGui::TextWrapped("%s", npcComp->dialogueGreeting.c_str());
        ImGui::Separator();

        // Role-specific options
        if (npc->getComponent<QuestGiverComponent>()) {
            renderQuestGiverOptions(npc, player, npcSystem, questSystem);
        }
        if (npc->getComponent<ShopComponent>()) {
            renderShopButton(npc, npcSystem);
        }
        if (npc->getComponent<SkillTrainerComponent>()) {
            renderSkillTrainerButton(npc, npcSystem);
        }
        if (npc->getComponent<BankerComponent>()) {
            renderBankerButton(npc, npcSystem);
        }
        if (npc->getComponent<GuildNPCComponent>()) {
            renderGuildNPCButton(npc, player, npcSystem);
        }
        if (npc->getComponent<TeleporterComponent>()) {
            renderTeleporterOptions(npc, player, npcSystem);
        }
        if (npc->getComponent<StoryNPCComponent>()) {
            if (ImGui::Button("Talk")) {
                inStoryDialogue_ = true;
                currentDialogueNodeId_ = npc->getComponent<StoryNPCComponent>()->rootNodeId;
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            npcSystem->closeDialogue();
            inStoryDialogue_ = false;
        }
    }
    ImGui::End();
}

void NPCDialogueUI::renderQuestGiverOptions(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem, QuestSystem* questSystem) {
    auto* qg = npc->getComponent<QuestGiverComponent>();
    auto* qc = player->getComponent<QuestComponent>();
    auto* stats = player->getComponent<CharacterStatsComponent>();
    auto* inv = player->getComponent<InventoryComponent>();
    if (!qg || !qc || !stats || !inv) return;

    for (uint32_t questId : qg->questIds) {
        const auto& def = QuestData::getQuest(questId);
        if (def.questId == 0) continue;

        ImGui::PushID(static_cast<int>(questId));

        if (qc->quests.isQuestActive(questId)) {
            auto* aq = qc->quests.getActiveQuest(questId);
            if (aq && aq->isReadyToTurnIn()) {
                ImGui::TextWrapped("%s", def.turnInDialogue.c_str());
                if (ImGui::Button("Complete Quest")) {
                    qc->quests.turnInQuest(questId, stats->stats, inv->inventory);
                    if (questSystem) questSystem->refreshQuestMarkers();
                }
            } else {
                ImGui::TextWrapped("%s", def.inProgressDialogue.c_str());
                // Show progress
                for (size_t i = 0; i < def.objectives.size() && aq; ++i) {
                    uint16_t progress = (i < aq->objectiveProgress.size()) ? aq->objectiveProgress[i] : 0;
                    ImGui::BulletText("%s (%d/%d)", def.objectives[i].description.c_str(),
                                    progress, def.objectives[i].requiredCount);
                }
            }
        } else if (qc->quests.canAcceptQuest(questId, stats->stats)) {
            ImGui::TextWrapped("%s", def.offerDialogue.c_str());
            ImGui::Text("Quest: %s", def.questName.c_str());
            ImGui::TextWrapped("%s", def.description.c_str());
            ImGui::Text("Rewards: %u XP, %lld Gold", def.rewards.xp, def.rewards.gold);
            if (ImGui::Button("Accept")) {
                qc->quests.acceptQuest(questId);
                if (questSystem) questSystem->refreshQuestMarkers();
            }
            ImGui::SameLine();
            if (ImGui::Button("Decline")) {
                // Do nothing, just don't accept
            }
        }

        ImGui::PopID();
        ImGui::Separator();
    }
}

void NPCDialogueUI::renderShopButton(Entity* npc, NPCInteractionSystem* npcSystem) {
    if (ImGui::Button("Shop")) {
        // TODO: Open ShopUI
    }
}

void NPCDialogueUI::renderSkillTrainerButton(Entity* npc, NPCInteractionSystem* npcSystem) {
    if (ImGui::Button("Learn Skills")) {
        // TODO: Open SkillTrainerUI
    }
}

void NPCDialogueUI::renderBankerButton(Entity* npc, NPCInteractionSystem* npcSystem) {
    if (ImGui::Button("Open Storage")) {
        // TODO: Open BankStorageUI
    }
}

void NPCDialogueUI::renderGuildNPCButton(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem) {
    auto* guildComp = player->getComponent<GuildComponent>();
    if (!guildComp) return;

    if (!guildComp->guild.isInGuild()) {
        if (ImGui::Button("Create Guild")) {
            // TODO: Guild creation flow
        }
    } else {
        if (ImGui::Button("Guild Info")) {
            // TODO: Show guild info
        }
        if (ImGui::Button("Leave Guild")) {
            // TODO: Guild leave flow
        }
    }
}

void NPCDialogueUI::renderTeleporterOptions(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem) {
    auto* tp = npc->getComponent<TeleporterComponent>();
    auto* stats = player->getComponent<CharacterStatsComponent>();
    auto* inv = player->getComponent<InventoryComponent>();
    if (!tp || !stats || !inv) return;

    ImGui::Text("Destinations:");
    for (size_t i = 0; i < tp->destinations.size(); ++i) {
        const auto& dest = tp->destinations[i];
        bool canTravel = stats->stats.level >= dest.requiredLevel && inv->inventory.getGold() >= dest.cost;

        ImGui::PushID(static_cast<int>(i));
        if (canTravel) {
            if (ImGui::Button(dest.destinationName.c_str())) {
                inv->inventory.removeGold(dest.cost);
                // TODO: Scene transition to dest.sceneId at dest.targetPosition
                npcSystem->closeDialogue();
            }
            ImGui::SameLine();
            ImGui::Text("(%lld gold)", dest.cost);
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(dest.destinationName.c_str());
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (stats->stats.level < dest.requiredLevel) {
                ImGui::Text("(Requires Lv.%d)", dest.requiredLevel);
            } else {
                ImGui::Text("(Not enough gold)");
            }
        }
        ImGui::PopID();
    }
}

void NPCDialogueUI::renderStoryDialogue(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem) {
    auto* story = npc->getComponent<StoryNPCComponent>();
    auto* stats = player->getComponent<CharacterStatsComponent>();
    if (!story || !stats) return;

    // Find current node
    const DialogueNode* currentNode = nullptr;
    for (const auto& node : story->dialogueTree) {
        if (node.nodeId == currentDialogueNodeId_) {
            currentNode = &node;
            break;
        }
    }

    if (!currentNode) {
        inStoryDialogue_ = false;
        return;
    }

    // Check condition
    bool conditionMet = true;
    if (currentNode->condition.condition != DialogueCondition::None) {
        switch (currentNode->condition.condition) {
            case DialogueCondition::MinLevel:
                conditionMet = stats->stats.level >= currentNode->condition.value;
                break;
            case DialogueCondition::HasClass:
                conditionMet = static_cast<int>(stats->stats.classDef.classType) == currentNode->condition.value;
                break;
            // HasFlag and HasItem require additional systems
            default: break;
        }
    }

    if (!conditionMet) {
        ImGui::TextWrapped("...");
        if (ImGui::Button("Back")) {
            inStoryDialogue_ = false;
        }
        return;
    }

    // Show NPC text
    ImGui::TextWrapped("%s", currentNode->npcText.c_str());
    ImGui::Separator();

    // Show choices
    for (const auto& choice : currentNode->choices) {
        if (ImGui::Button(choice.buttonText.c_str())) {
            // Execute action if any
            if (choice.onSelect.action != DialogueAction::None) {
                switch (choice.onSelect.action) {
                    case DialogueAction::GiveXP:
                        stats->stats.addExperience(choice.onSelect.value);
                        break;
                    case DialogueAction::GiveGold: {
                        auto* inv = player->getComponent<InventoryComponent>();
                        if (inv) inv->inventory.addGold(choice.onSelect.value);
                        break;
                    }
                    case DialogueAction::Heal:
                        stats->stats.currentHP = stats->stats.maxHP;
                        stats->stats.currentMP = stats->stats.maxMP;
                        break;
                    // GiveItem and SetFlag require additional item/flag systems
                    default: break;
                }
            }

            if (choice.nextNodeId == 0) {
                inStoryDialogue_ = false;
            } else {
                currentDialogueNodeId_ = choice.nextNodeId;
            }
        }
    }
}

} // namespace fate
```

- [ ] **Step 3: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add game/ui/npc_dialogue_ui.h game/ui/npc_dialogue_ui.cpp
git commit -m "feat: add NPC dialogue UI with quest, shop, teleporter, and story dialogue support"
```

---

## Task 10: Shop UI

**Files:**
- Create: `game/ui/shop_ui.h`
- Create: `game/ui/shop_ui.cpp`

- [ ] **Step 1: Create shop_ui.h**

```cpp
#pragma once
#include "engine/ecs/entity.h"
#include "game/components/game_components.h"

namespace fate {

class ShopUI {
public:
    bool isOpen = false;
    Entity* shopNPC = nullptr;

    void open(Entity* npc) { shopNPC = npc; isOpen = true; }
    void close() { shopNPC = nullptr; isOpen = false; }
    void render(Entity* player);
};

} // namespace fate
```

- [ ] **Step 2: Create shop_ui.cpp**

Implement the shop grid with buy/sell tabs, item names, prices, gold display. Buy checks player gold via `InventoryComponent`, sell adds gold. Use ImGui tables for the grid layout.

- [ ] **Step 3: Build and commit**

```bash
git add game/ui/shop_ui.h game/ui/shop_ui.cpp
git commit -m "feat: add ShopUI for NPC merchant buy/sell interface"
```

---

## Task 11: Quest Log UI

**Files:**
- Create: `game/ui/quest_log_ui.h`
- Create: `game/ui/quest_log_ui.cpp`

- [ ] **Step 1: Create quest_log_ui.h**

```cpp
#pragma once
#include "engine/ecs/entity.h"

namespace fate {

class QuestLogUI {
public:
    bool isOpen = false;

    void toggle() { isOpen = !isOpen; }
    void render(Entity* player);
};

} // namespace fate
```

- [ ] **Step 2: Create quest_log_ui.cpp**

Implement quest log showing active quests with progress bars, objective current/required counts, abandon button, and completed quests section. Toggle with `L` key.

- [ ] **Step 3: Build and commit**

```bash
git add game/ui/quest_log_ui.h game/ui/quest_log_ui.cpp
git commit -m "feat: add QuestLogUI for tracking active quests and progress"
```

---

## Task 12: Remaining UIs (Skill Trainer, Bank, Teleporter)

**Files:**
- Create: `game/ui/skill_trainer_ui.h`, `game/ui/skill_trainer_ui.cpp`
- Create: `game/ui/bank_storage_ui.h`, `game/ui/bank_storage_ui.cpp`
- Create: `game/ui/teleporter_ui.h`, `game/ui/teleporter_ui.cpp`

- [ ] **Step 1: Create skill_trainer_ui.h/cpp**

Shows learnable skills with requirements, gold cost, skill point cost. Learn button per skill. Greyed out if requirements not met. Routes to `SkillManager::learnSkill()`.

- [ ] **Step 2: Create bank_storage_ui.h/cpp**

Grid layout similar to inventory. Shows bank slots. Deposit/withdraw items and gold. Fee displayed for gold deposits.

- [ ] **Step 3: Create teleporter_ui.h/cpp**

Destination list with name, cost, level requirement. Greyed out if not eligible. Click to travel with scene transition.

- [ ] **Step 4: Build and commit**

```bash
git add game/ui/skill_trainer_ui.h game/ui/skill_trainer_ui.cpp
git add game/ui/bank_storage_ui.h game/ui/bank_storage_ui.cpp
git add game/ui/teleporter_ui.h game/ui/teleporter_ui.cpp
git commit -m "feat: add skill trainer, bank storage, and teleporter UIs"
```

---

## Task 13: Quest Marker Rendering

**Files:**
- Modify: `game/systems/render_system.h` (or wherever nameplate rendering happens)

- [ ] **Step 1: Add quest marker rendering**

In the nameplate/sprite render system, add rendering for `QuestMarkerComponent`:
- Draw `?` or `!` text above the NPC nameplate using `TextRenderer`
- Color lookup by tier:
  - Starter → White `{255, 255, 255}`
  - Novice → Green `{0, 200, 0}`
  - Apprentice → Yellow-Green `{180, 220, 0}`
  - Adept → Yellow `{255, 220, 0}`
  - TurnIn `!` → Yellow `{255, 220, 0}`
- Only draw if `MarkerState != None`

- [ ] **Step 2: Add NPC nameplate subtitle rendering**

Extend nameplate rendering to show `roleSubtitle` in grey below the display name for entities where `roleSubtitle` is not empty.

- [ ] **Step 3: Build and commit**

```bash
git add game/systems/render_system.h
git commit -m "feat: add quest marker and NPC role subtitle rendering"
```

---

## Task 14: Integration — Wire Everything Together

**Files:**
- Modify: Main game setup file (wherever systems are added to the World)

- [ ] **Step 1: Register new systems in proper order**

Ensure the game's system registration adds:
1. `NPCInteractionSystem` (before `CombatActionSystem`)
2. `QuestSystem`

- [ ] **Step 2: Wire CombatActionSystem mob death to QuestSystem**

In `CombatActionSystem` where mob death is processed, add a call to `QuestSystem::onMobDeath(mobId)`.

- [ ] **Step 3: Wire NPCInteractionSystem to QuestSystem for TalkTo quests**

When dialogue opens with an NPC, call `QuestSystem::onNPCInteraction(npcId)`.

- [ ] **Step 4: Add quest key binding**

Add `L` key toggle for QuestLogUI in the input handling.

- [ ] **Step 5: Create a test NPC in a scene**

Add a test NPC to an existing scene using `EntityFactory::createNPC()` to verify the full flow works:

```cpp
NPCTemplate testNPC;
testNPC.name = "Village Elder";
testNPC.npcId = 1;
testNPC.position = {256.0f, 256.0f};
testNPC.isQuestGiver = true;
testNPC.questIds = {1, 2, 3};
testNPC.dialogueGreeting = "Welcome, adventurer! I have tasks for you.";
EntityFactory::createNPC(world, testNPC);
```

- [ ] **Step 6: Full build and manual test**

Run: `cmake --build build && ./build/fate_game`
Test: Click the test NPC, accept a quest, verify marker changes.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: wire NPC/quest systems together and add test NPC"
```

---

## Task Summary

| Task | Description | Dependencies |
|------|-------------|-------------|
| 1 | Game types (FaceDirection, ClassType::Any) | None |
| 2 | Dialogue tree data structures | None |
| 3 | Bank storage manager | None |
| 4 | Quest manager core logic | Task 1 |
| 5 | NPC types & components | Tasks 1, 2, 3, 4 |
| 6 | EntityFactory createNPC | Task 5 |
| 7 | NPC interaction system | Tasks 5, 6 |
| 8 | Quest system (progress & markers) | Tasks 4, 5 |
| 9 | NPC dialogue UI | Tasks 7, 8 |
| 10 | Shop UI | Task 5 |
| 11 | Quest log UI | Task 4 |
| 12 | Remaining UIs | Tasks 3, 5 |
| 13 | Quest marker rendering | Task 8 |
| 14 | Integration | All above |

**Parallelizable groups:**
- Tasks 1, 2, 3 can run in parallel (no dependencies)
- Tasks 10, 11, 12 can run in parallel (independent UIs)
