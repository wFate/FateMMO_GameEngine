# Four Systems Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Faction System (4 factions), Pet System (leveling + auto-loot), Mage Double-Cast mechanic, and Stat Enchant Scrolls for accessories.

**Architecture:** Four independent systems, each with its own data model, test suite, and integration points. All follow existing patterns: static utility classes for logic, ECS components for entity state, `game/shared/` for shared logic. No modifications to `PKSystem` or `CombatSystem` (pure logic layers stay untouched).

**Tech Stack:** C++23, doctest, ECS archetype storage, FATE_COMPONENT macros, FATE_REFLECT macros.

**Spec:** `Docs/superpowers/specs/2026-03-17-four-systems-design.md`

**Build command:**
```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build --config Debug
```

**Test command:**
```bash
./out/build/Debug/fate_tests.exe
```

**CMake reconfigure** (needed when adding new .cpp or test files — GLOB_RECURSE only picks them up on reconfigure):
```bash
"$CMAKE" out/build
```
Run this before the first build after creating any new `.cpp` file (Tasks 1, 2, 4, 5, 7).

**Deferred integration (not in this plan):** Same-faction PvP rejection, home village PK exception, chat garble routing in ChatManager, pet/stat-enchant stat application in `recalculateStats()`. These require the combat action system and equipment pipeline, which are higher-layer integrations beyond the scope of these core data models and logic. They will be addressed in a follow-up plan.

---

## File Structure

### New Files
| File | Responsibility |
|------|---------------|
| `game/shared/faction.h` | `Faction` enum, `FactionDefinition` struct, `FactionRegistry` (lookup by enum), `FactionChatGarbler` (deterministic garble) |
| `game/shared/pet_system.h` | `PetDefinition`, `PetInstance`, `PetSystem` static utility (leveling, stat calc, XP sharing) |
| `game/shared/pet_system.cpp` | `PetSystem` implementation (leveling logic, stat formulas) |
| `game/shared/stat_enchant_system.h` | `StatEnchantSystem` static utility (roll, apply, slot validation) |
| `game/components/faction_component.h` | `FactionComponent` ECS component |
| `game/components/pet_component.h` | `PetComponent` ECS component |
| `tests/test_faction.cpp` | Faction tests (garble, registry, same-faction check) |
| `tests/test_pet_system.cpp` | Pet tests (leveling, stats, XP cap) |
| `tests/test_double_cast.cpp` | Double-cast tests (window open/close/expire, no chain) |
| `tests/test_stat_enchant.cpp` | Stat enchant tests (roll, apply, replace, slot validation) |

### Modified Files
| File | Changes |
|------|---------|
| `game/shared/chat_manager.h` | Add `Faction senderFaction` to `ChatMessage`, add `garbleForFaction()` method |
| `game/shared/chat_manager.cpp` | Implement garble routing in `addToHistory()` path |
| `game/shared/skill_manager.h` | Add `castTime`, `enablesDoubleCast`, `doubleCastWindow` to `SkillDefinition`; add double-cast state fields to `SkillManager` |
| `game/shared/skill_manager.cpp` | Add double-cast expiry in `tick()`, add `consumeDoubleCast()`/`activateDoubleCast()` methods |
| `game/shared/item_instance.h` | Add `statEnchantType`, `statEnchantValue` fields to `ItemInstance` |
| `game/components/game_components.h` | Add includes + `FactionComponent`, `PetComponent` component structs and FATE_REFLECT declarations |
| `game/register_components.h` | Register `FactionComponent`, `PetComponent` with traits |
| `game/entity_factory.h` | Add `Faction` param to `createPlayer()`, attach `FactionComponent` + `PetComponent` |

---

## Task 1: Faction Core — Enum, Definitions, Registry

**Files:**
- Create: `game/shared/faction.h`
- Test: `tests/test_faction.cpp`

- [ ] **Step 1: Write failing test for faction registry**

```cpp
// tests/test_faction.cpp
#include <doctest/doctest.h>
#include "game/shared/faction.h"

using namespace fate;

TEST_CASE("FactionRegistry: all 4 factions defined") {
    const auto* xyros = FactionRegistry::get(Faction::Xyros);
    REQUIRE(xyros != nullptr);
    CHECK(xyros->displayName == "Xyros");

    const auto* fenor = FactionRegistry::get(Faction::Fenor);
    REQUIRE(fenor != nullptr);
    CHECK(fenor->displayName == "Fenor");

    const auto* zethos = FactionRegistry::get(Faction::Zethos);
    REQUIRE(zethos != nullptr);
    CHECK(zethos->displayName == "Zethos");

    const auto* solis = FactionRegistry::get(Faction::Solis);
    REQUIRE(solis != nullptr);
    CHECK(solis->displayName == "Solis");
}

TEST_CASE("FactionRegistry: None returns nullptr") {
    CHECK(FactionRegistry::get(Faction::None) == nullptr);
}

TEST_CASE("Faction: same-faction check") {
    CHECK(FactionRegistry::isSameFaction(Faction::Xyros, Faction::Xyros) == true);
    CHECK(FactionRegistry::isSameFaction(Faction::Xyros, Faction::Fenor) == false);
    CHECK(FactionRegistry::isSameFaction(Faction::None, Faction::None) == false);
    CHECK(FactionRegistry::isSameFaction(Faction::Xyros, Faction::None) == false);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -5`
Expected: Compile error — `faction.h` not found

- [ ] **Step 3: Implement faction.h**

```cpp
// game/shared/faction.h
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "engine/core/types.h"

namespace fate {

enum class Faction : uint8_t {
    None   = 0,
    Xyros  = 1,
    Fenor  = 2,
    Zethos = 3,
    Solis  = 4
};

struct FactionDefinition {
    Faction faction           = Faction::None;
    std::string displayName;
    Color color               = Color::white();
    std::string homeVillageId;
    std::string factionMerchantNPCId;
};

class FactionRegistry {
public:
    FactionRegistry() = delete;

    static const FactionDefinition* get(Faction f) {
        auto idx = static_cast<uint8_t>(f);
        if (idx == 0 || idx > 4) return nullptr;
        return &s_factions[idx - 1];
    }

    static bool isSameFaction(Faction a, Faction b) {
        if (a == Faction::None || b == Faction::None) return false;
        return a == b;
    }

private:
    // Cannot be constexpr — std::string is not a literal type on MSVC
    static inline const std::array<FactionDefinition, 4> s_factions = {{
        { Faction::Xyros,  "Xyros",  {0.85f, 0.25f, 0.25f}, "zone_xyros_village",  "npc_merchant_xyros"  },
        { Faction::Fenor,  "Fenor",  {0.25f, 0.55f, 0.85f}, "zone_fenor_village",  "npc_merchant_fenor"  },
        { Faction::Zethos, "Zethos", {0.25f, 0.80f, 0.40f}, "zone_zethos_village", "npc_merchant_zethos" },
        { Faction::Solis,  "Solis",  {0.95f, 0.75f, 0.20f}, "zone_solis_village",  "npc_merchant_solis"  },
    }};
};

} // namespace fate
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="Faction*"`
Expected: All 3 test cases PASS

- [ ] **Step 5: Commit**

```bash
git add game/shared/faction.h tests/test_faction.cpp
git commit -m "feat: add faction enum, definitions, and registry with tests"
```

---

## Task 2: Faction Chat Garbler

**Files:**
- Modify: `game/shared/faction.h` (add FactionChatGarbler)
- Test: `tests/test_faction.cpp` (add garble tests)

- [ ] **Step 1: Write failing test for chat garbling**

Add to `tests/test_faction.cpp`:

```cpp
TEST_CASE("FactionChatGarbler: garble produces same-length output") {
    std::string original = "Hello enemy!";
    std::string garbled = FactionChatGarbler::garble(original);
    CHECK(garbled.size() == original.size());
    CHECK(garbled != original);
}

TEST_CASE("FactionChatGarbler: garble is deterministic") {
    std::string msg = "Attack the base!";
    CHECK(FactionChatGarbler::garble(msg) == FactionChatGarbler::garble(msg));
}

TEST_CASE("FactionChatGarbler: empty string returns empty") {
    CHECK(FactionChatGarbler::garble("") == "");
}

TEST_CASE("FactionChatGarbler: spaces preserved") {
    std::string garbled = FactionChatGarbler::garble("a b c");
    CHECK(garbled[1] == ' ');
    CHECK(garbled[3] == ' ');
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: Compile error — `FactionChatGarbler` not defined

- [ ] **Step 3: Implement FactionChatGarbler in faction.h**

Add to `game/shared/faction.h` inside `namespace fate`:

```cpp
class FactionChatGarbler {
public:
    FactionChatGarbler() = delete;

    static std::string garble(const std::string& message) {
        if (message.empty()) return "";

        // Deterministic seed from message content
        uint32_t seed = 0;
        for (char c : message) {
            seed = seed * 31 + static_cast<uint8_t>(c);
        }

        static constexpr std::string_view alphabet = "aeiouzxkwqjfpmt";
        std::string result;
        result.reserve(message.size());

        for (size_t i = 0; i < message.size(); ++i) {
            if (message[i] == ' ') {
                result += ' ';
            } else {
                // Simple LCG per character
                seed = seed * 1103515245 + 12345 + static_cast<uint32_t>(i);
                result += alphabet[(seed >> 16) % alphabet.size()];
            }
        }
        return result;
    }
};
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="FactionChat*"`
Expected: All 4 test cases PASS

- [ ] **Step 5: Commit**

```bash
git add game/shared/faction.h tests/test_faction.cpp
git commit -m "feat: add deterministic cross-faction chat garbler"
```

---

## Task 3: Faction ECS Integration

**Files:**
- Create: `game/components/faction_component.h`
- Modify: `game/components/game_components.h` (add include + FATE_REFLECT)
- Modify: `game/register_components.h` (register + traits)
- Modify: `game/entity_factory.h` (add Faction param to createPlayer)
- Modify: `game/shared/chat_manager.h` (add senderFaction to ChatMessage)

- [ ] **Step 1: Create FactionComponent**

```cpp
// game/components/faction_component.h
#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "game/shared/faction.h"

namespace fate {

struct FactionComponent {
    FATE_COMPONENT_COLD(FactionComponent)
    Faction faction = Faction::None;
};

} // namespace fate

FATE_REFLECT_EMPTY(fate::FactionComponent)
```

- [ ] **Step 2: Add FactionComponent to game_components.h**

At top of `game/components/game_components.h`, add include:
```cpp
#include "game/shared/faction.h"
```

After `BankStorageComponent` (line 231), add:
```cpp
// (FactionComponent and PetComponent are in their own headers)
```

At bottom of file, after `FATE_REFLECT_EMPTY(fate::BankStorageComponent)` (line 340), the reflection for FactionComponent is already in its own header — no addition needed here.

- [ ] **Step 3: Register FactionComponent in register_components.h**

Add include at top:
```cpp
#include "game/components/faction_component.h"
```

Add trait specialization after existing social component traits (after line 86):
```cpp
// --- FactionComponent: saved to DB, replicated ---
template<> struct component_traits<FactionComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};
```

Add registration in `registerAllComponents()` after BankStorageComponent (after line 220):
```cpp
// ----- Faction & Pet components -----
reg.registerComponent<FactionComponent>();
```

- [ ] **Step 4: Add Faction param to EntityFactory::createPlayer()**

In `game/entity_factory.h`, change the `createPlayer` signature (line 20):
```cpp
static Entity* createPlayer(World& world, const std::string& name, ClassType classType, bool isLocal = false, Faction faction = Faction::None) {
```

Add include at top:
```cpp
#include "game/components/faction_component.h"
```

After the `BankStorageComponent` block (after line 136), add:
```cpp
// Faction
auto* factionComp = player->addComponent<FactionComponent>();
factionComp->faction = faction;
```

- [ ] **Step 5: Add senderFaction to ChatMessage**

In `game/shared/chat_manager.h`, add include at top:
```cpp
#include "game/shared/faction.h"
```

Add field to `ChatMessage` struct (after line 18):
```cpp
Faction senderFaction = Faction::None;
```

- [ ] **Step 6: Build to verify compilation**

Run: `"$CMAKE" --build out/build --config Debug`
Expected: Clean build, no errors

- [ ] **Step 7: Commit**

```bash
git add game/components/faction_component.h game/components/game_components.h game/register_components.h game/entity_factory.h game/shared/chat_manager.h
git commit -m "feat: integrate faction system into ECS, entity factory, and chat"
```

---

## Task 4: Stat Enchant System

**Files:**
- Create: `game/shared/stat_enchant_system.h`
- Modify: `game/shared/item_instance.h` (add stat enchant fields)
- Test: `tests/test_stat_enchant.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_stat_enchant.cpp
#include <doctest/doctest.h>
#include "game/shared/stat_enchant_system.h"
#include "game/shared/item_instance.h"

using namespace fate;

TEST_CASE("StatEnchantSystem: canStatEnchant only for accessories") {
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Belt) == true);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Ring) == true);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Necklace) == true);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Cloak) == true);

    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Weapon) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Armor) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Hat) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Shoes) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Gloves) == false);
}

TEST_CASE("StatEnchantSystem: getStatValue for primary stats") {
    CHECK(StatEnchantSystem::getStatValue(StatType::Strength, 0) == 0);
    CHECK(StatEnchantSystem::getStatValue(StatType::Strength, 1) == 1);
    CHECK(StatEnchantSystem::getStatValue(StatType::Dexterity, 5) == 5);
}

TEST_CASE("StatEnchantSystem: getStatValue for HP/MP uses x10 scaling") {
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxHealth, 0) == 0);
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxHealth, 1) == 10);
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxHealth, 5) == 50);
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxMana, 3) == 30);
}

TEST_CASE("StatEnchantSystem: applyStatEnchant sets fields") {
    ItemInstance item = ItemInstance::createSimple("inst1", "ring_iron", 1);
    StatEnchantSystem::applyStatEnchant(item, StatType::Dexterity, 3);
    CHECK(item.statEnchantType == StatType::Dexterity);
    CHECK(item.statEnchantValue == 3);
}

TEST_CASE("StatEnchantSystem: applyStatEnchant replaces previous") {
    ItemInstance item = ItemInstance::createSimple("inst1", "ring_iron", 1);
    StatEnchantSystem::applyStatEnchant(item, StatType::Strength, 4);
    CHECK(item.statEnchantValue == 4);

    StatEnchantSystem::applyStatEnchant(item, StatType::Dexterity, 2);
    CHECK(item.statEnchantType == StatType::Dexterity);
    CHECK(item.statEnchantValue == 2);
}

TEST_CASE("StatEnchantSystem: fail tier removes enchant") {
    ItemInstance item = ItemInstance::createSimple("inst1", "ring_iron", 1);
    StatEnchantSystem::applyStatEnchant(item, StatType::Strength, 5);
    CHECK(item.statEnchantValue == 5);

    StatEnchantSystem::applyStatEnchant(item, StatType::Intelligence, 0);
    CHECK(item.statEnchantValue == 0);
    CHECK(item.statEnchantType == StatType::Strength);  // reset to neutral default
}

TEST_CASE("StatEnchantSystem: rollStatEnchant returns 0-5") {
    for (int i = 0; i < 100; ++i) {
        int tier = StatEnchantSystem::rollStatEnchant();
        CHECK(tier >= 0);
        CHECK(tier <= 5);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: Compile error — `stat_enchant_system.h` not found

- [ ] **Step 3: Add stat enchant fields to ItemInstance**

In `game/shared/item_instance.h`, add to `ItemInstance` struct after `int64_t acquiredAt = 0;` (line 47):
```cpp
// Stat enchant (accessories only — Belt, Ring, Necklace, Cloak)
StatType statEnchantType  = StatType::Strength;
int statEnchantValue      = 0;  // 0 = no enchant active
```

- [ ] **Step 4: Implement stat_enchant_system.h**

```cpp
// game/shared/stat_enchant_system.h
#pragma once
#include <array>
#include <random>
#include "game/shared/game_types.h"
#include "game/shared/item_instance.h"

namespace fate {

class StatEnchantSystem {
public:
    StatEnchantSystem() = delete;

    static bool canStatEnchant(EquipmentSlot slot) {
        switch (slot) {
            case EquipmentSlot::Belt:
            case EquipmentSlot::Ring:
            case EquipmentSlot::Necklace:
            case EquipmentSlot::Cloak:
                return true;
            default:
                return false;
        }
    }

    // Probability table: tier 0=25%, 1=30%, 2=25%, 3=12%, 4=6%, 5=2%
    // Cumulative: 25, 55, 80, 92, 98, 100
    static int rollStatEnchant() {
        thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist(1, 100);
        int roll = dist(rng);

        if (roll <= 25) return 0;
        if (roll <= 55) return 1;
        if (roll <= 80) return 2;
        if (roll <= 92) return 3;
        if (roll <= 98) return 4;
        return 5;
    }

    static int getStatValue(StatType scrollType, int tier) {
        if (tier <= 0) return 0;
        if (scrollType == StatType::MaxHealth || scrollType == StatType::MaxMana) {
            return tier * 10;
        }
        return tier;
    }

    static void applyStatEnchant(ItemInstance& item, StatType type, int tier) {
        int value = getStatValue(type, tier);
        if (value <= 0) {
            // Fail — remove enchant entirely
            item.statEnchantType = StatType::Strength;  // reset to neutral default
            item.statEnchantValue = 0;
        } else {
            item.statEnchantType = type;
            item.statEnchantValue = value;
        }
    }
};

} // namespace fate
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="StatEnchant*"`
Expected: All 7 test cases PASS

- [ ] **Step 6: Commit**

```bash
git add game/shared/stat_enchant_system.h game/shared/item_instance.h tests/test_stat_enchant.cpp
git commit -m "feat: add stat enchant system for accessory enchanting"
```

---

## Task 5: Pet System — Core Data & Leveling

**Files:**
- Create: `game/shared/pet_system.h`
- Create: `game/shared/pet_system.cpp`
- Test: `tests/test_pet_system.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_pet_system.cpp
#include <doctest/doctest.h>
#include "game/shared/pet_system.h"

using namespace fate;

static PetDefinition makeTestDef(ItemRarity rarity = ItemRarity::Common) {
    PetDefinition def;
    def.petId = "pet_wolf";
    def.displayName = "Wolf";
    def.rarity = rarity;
    def.baseHP = 10;
    def.baseCritRate = 0.01f;
    def.baseExpBonus = 0.02f;
    def.hpPerLevel = 2.0f;
    def.critPerLevel = 0.002f;
    def.expBonusPerLevel = 0.005f;
    return def;
}

TEST_CASE("PetSystem: stat calculation at level 1") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 1;

    CHECK(PetSystem::effectiveHP(def, pet) == 10);
    CHECK(PetSystem::effectiveCritRate(def, pet) == doctest::Approx(0.01f));
    CHECK(PetSystem::effectiveExpBonus(def, pet) == doctest::Approx(0.02f));
}

TEST_CASE("PetSystem: stat calculation scales with level") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 10;

    // baseHP + hpPerLevel * (10-1) = 10 + 2*9 = 28
    CHECK(PetSystem::effectiveHP(def, pet) == 28);
    CHECK(PetSystem::effectiveCritRate(def, pet) == doctest::Approx(0.01f + 0.002f * 9));
    CHECK(PetSystem::effectiveExpBonus(def, pet) == doctest::Approx(0.02f + 0.005f * 9));
}

TEST_CASE("PetSystem: addXP levels up pet") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 1;
    pet.currentXP = 0;
    pet.xpToNextLevel = 100;

    int playerLevel = 50;
    PetSystem::addXP(def, pet, 150, playerLevel);

    CHECK(pet.level == 2);
    CHECK(pet.currentXP == 50);
}

TEST_CASE("PetSystem: pet cannot outlevel player") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 5;
    pet.currentXP = 0;
    pet.xpToNextLevel = 100;

    int playerLevel = 5;
    PetSystem::addXP(def, pet, 9999, playerLevel);

    CHECK(pet.level == 5);
    CHECK(pet.currentXP == 0);  // XP not added when at cap
}

TEST_CASE("PetSystem: createInstance sets defaults") {
    auto def = makeTestDef();
    auto pet = PetSystem::createInstance(def, "inst_001");
    CHECK(pet.instanceId == "inst_001");
    CHECK(pet.petDefinitionId == "pet_wolf");
    CHECK(pet.petName == "Wolf");
    CHECK(pet.level == 1);
    CHECK(pet.autoLootEnabled == false);
    CHECK(pet.isSoulbound == false);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: Compile error — `pet_system.h` not found

- [ ] **Step 3: Implement pet_system.h**

```cpp
// game/shared/pet_system.h
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include "game/shared/game_types.h"

namespace fate {

struct PetDefinition {
    std::string petId;
    std::string displayName;
    ItemRarity rarity = ItemRarity::Common;

    int baseHP           = 0;
    float baseCritRate   = 0.0f;
    float baseExpBonus   = 0.0f;

    float hpPerLevel       = 0.0f;
    float critPerLevel     = 0.0f;
    float expBonusPerLevel = 0.0f;
};

struct PetInstance {
    std::string instanceId;
    std::string petDefinitionId;
    std::string petName;
    int level            = 1;
    int64_t currentXP    = 0;
    int64_t xpToNextLevel = 100;
    bool autoLootEnabled = false;
    bool isSoulbound     = false;
};

class PetSystem {
public:
    PetSystem() = delete;

    static constexpr int MAX_PET_LEVEL = 50;
    static constexpr float PET_XP_SHARE = 0.5f;  // Pet gets 50% of player XP

    static int effectiveHP(const PetDefinition& def, const PetInstance& pet) {
        return def.baseHP + static_cast<int>(std::round(def.hpPerLevel * (pet.level - 1)));
    }

    static float effectiveCritRate(const PetDefinition& def, const PetInstance& pet) {
        return def.baseCritRate + def.critPerLevel * (pet.level - 1);
    }

    static float effectiveExpBonus(const PetDefinition& def, const PetInstance& pet) {
        return def.baseExpBonus + def.expBonusPerLevel * (pet.level - 1);
    }

    static void addXP(const PetDefinition& def, PetInstance& pet, int64_t amount, int playerLevel);

    static int64_t calculateXPToNextLevel(int petLevel);

    static PetInstance createInstance(const PetDefinition& def, const std::string& instanceId);
};

} // namespace fate
```

- [ ] **Step 4: Implement pet_system.cpp**

```cpp
// game/shared/pet_system.cpp
#include "game/shared/pet_system.h"
#include <algorithm>
#include <cmath>

namespace fate {

void PetSystem::addXP(const PetDefinition& /*def*/, PetInstance& pet, int64_t amount, int playerLevel) {
    if (amount <= 0) return;

    int levelCap = (std::min)(playerLevel, MAX_PET_LEVEL);
    if (pet.level >= levelCap) return;

    pet.currentXP += amount;

    while (pet.currentXP >= pet.xpToNextLevel && pet.level < levelCap) {
        pet.currentXP -= pet.xpToNextLevel;
        pet.level++;
        pet.xpToNextLevel = calculateXPToNextLevel(pet.level);
    }

    // If we hit the cap, clamp XP
    if (pet.level >= levelCap) {
        pet.currentXP = 0;
    }
}

int64_t PetSystem::calculateXPToNextLevel(int petLevel) {
    // Simple quadratic curve
    int64_t val = static_cast<int64_t>(std::round(50.0 * petLevel * petLevel));
    return (std::max)(val, int64_t{100});
}

PetInstance PetSystem::createInstance(const PetDefinition& def, const std::string& instanceId) {
    PetInstance pet;
    pet.instanceId = instanceId;
    pet.petDefinitionId = def.petId;
    pet.petName = def.displayName;
    pet.level = 1;
    pet.currentXP = 0;
    pet.xpToNextLevel = calculateXPToNextLevel(1);
    pet.autoLootEnabled = false;
    pet.isSoulbound = false;
    return pet;
}

} // namespace fate
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="PetSystem*"`
Expected: All 5 test cases PASS

- [ ] **Step 6: Commit**

```bash
git add game/shared/pet_system.h game/shared/pet_system.cpp tests/test_pet_system.cpp
git commit -m "feat: add pet system with leveling, stat calculation, and XP sharing"
```

---

## Task 6: Pet ECS Integration

**Files:**
- Create: `game/components/pet_component.h`
- Modify: `game/components/game_components.h` (add include)
- Modify: `game/register_components.h` (register + traits)
- Modify: `game/entity_factory.h` (attach PetComponent to players)

- [ ] **Step 1: Create PetComponent**

```cpp
// game/components/pet_component.h
#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "game/shared/pet_system.h"

namespace fate {

struct PetComponent {
    FATE_COMPONENT_COLD(PetComponent)

    PetInstance equippedPet;       // Empty instanceId = no pet equipped
    float autoLootRadius = 64.0f; // Pixels (2 tiles)

    [[nodiscard]] bool hasPet() const { return !equippedPet.instanceId.empty(); }
};

} // namespace fate

FATE_REFLECT_EMPTY(fate::PetComponent)
```

- [ ] **Step 2: Add include to game_components.h**

At top of `game/components/game_components.h`, add:
```cpp
#include "game/shared/pet_system.h"
```

(No struct definition needed — PetComponent lives in its own header.)

- [ ] **Step 3: Register PetComponent in register_components.h**

Add include at top:
```cpp
#include "game/components/pet_component.h"
```

Add trait specialization:
```cpp
// --- PetComponent: saved to DB, replicated ---
template<> struct component_traits<PetComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};
```

Add registration in `registerAllComponents()` after FactionComponent:
```cpp
reg.registerComponent<PetComponent>();
```

- [ ] **Step 4: Attach PetComponent in EntityFactory::createPlayer()**

In `game/entity_factory.h`, add include:
```cpp
#include "game/components/pet_component.h"
```

After the FactionComponent block, add:
```cpp
// Pet (empty by default — no pet equipped)
player->addComponent<PetComponent>();
```

- [ ] **Step 5: Build to verify compilation**

Run: `"$CMAKE" --build out/build --config Debug`
Expected: Clean build

- [ ] **Step 6: Commit**

```bash
git add game/components/pet_component.h game/components/game_components.h game/register_components.h game/entity_factory.h
git commit -m "feat: integrate pet system into ECS and entity factory"
```

---

## Task 7: Mage Double-Cast Mechanic

**Files:**
- Modify: `game/shared/skill_manager.h` (add fields to SkillDefinition + SkillManager)
- Modify: `game/shared/skill_manager.cpp` (add tick expiry + activate/consume methods)
- Test: `tests/test_double_cast.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_double_cast.cpp
#include <doctest/doctest.h>
#include "game/shared/skill_manager.h"
#include "game/shared/character_stats.h"

using namespace fate;

TEST_CASE("DoubleCast: activateDoubleCast opens window") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    CHECK_FALSE(mgr.isDoubleCastReady());

    mgr.activateDoubleCast("skill_flare", 2.0f);
    CHECK(mgr.isDoubleCastReady());
}

TEST_CASE("DoubleCast: consumeDoubleCast closes window") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    mgr.activateDoubleCast("skill_flare", 2.0f);
    CHECK(mgr.isDoubleCastReady());

    mgr.consumeDoubleCast();
    CHECK_FALSE(mgr.isDoubleCastReady());
}

TEST_CASE("DoubleCast: window expires after duration") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    mgr.activateDoubleCast("skill_flare", 2.0f);
    CHECK(mgr.isDoubleCastReady());

    // Advance past the window
    mgr.tick(2.5f);
    CHECK_FALSE(mgr.isDoubleCastReady());
}

TEST_CASE("DoubleCast: window does not expire before duration") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    mgr.activateDoubleCast("skill_flare", 2.0f);
    mgr.tick(1.5f);
    CHECK(mgr.isDoubleCastReady());
}

TEST_CASE("SkillDefinition: castTime and doubleCast fields default correctly") {
    SkillDefinition def;
    CHECK(def.castTime == 0.0f);
    CHECK(def.enablesDoubleCast == false);
    CHECK(def.doubleCastWindow == doctest::Approx(2.0f));
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: Compile error — `isDoubleCastReady` not found

- [ ] **Step 3: Add fields to SkillDefinition in skill_manager.h**

In `game/shared/skill_manager.h`, add to `SkillDefinition` struct after `std::string description;` (line 43):
```cpp
// Cast time (0 = instant)
float castTime = 0.0f;

// Double-cast: casting this skill opens an instant-cast window
bool enablesDoubleCast  = false;
float doubleCastWindow  = 2.0f;
```

- [ ] **Step 4: Add double-cast state to SkillManager in skill_manager.h**

Add public methods after `tick()` (after line 86):
```cpp
// ---- Double-Cast (hidden mechanic) ----
void activateDoubleCast(const std::string& sourceSkillId, float windowDuration);
void consumeDoubleCast();
[[nodiscard]] bool isDoubleCastReady() const { return doubleCastReady_; }
```

Add private fields after `float currentTime = 0.0f;` (after line 105):
```cpp
// ---- Double-cast state (transient, not serialized) ----
bool doubleCastReady_          = false;
float doubleCastExpireTime_    = 0.0f;
std::string doubleCastSourceSkillId_;
```

- [ ] **Step 5: Implement double-cast methods in skill_manager.cpp**

Add at the end of `SkillManager::tick()` (after cooldown pruning, before closing brace at line 230):
```cpp
// Expire double-cast window
if (doubleCastReady_ && currentTime >= doubleCastExpireTime_) {
    doubleCastReady_ = false;
    doubleCastSourceSkillId_.clear();
}
```

Add new methods at the bottom of the file:
```cpp
// ============================================================================
// Double-Cast (hidden mechanic)
// ============================================================================

void SkillManager::activateDoubleCast(const std::string& sourceSkillId, float windowDuration) {
    doubleCastReady_ = true;
    doubleCastExpireTime_ = currentTime + windowDuration;
    doubleCastSourceSkillId_ = sourceSkillId;
}

void SkillManager::consumeDoubleCast() {
    doubleCastReady_ = false;
    doubleCastSourceSkillId_.clear();
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="DoubleCast*,SkillDefinition*"`
Expected: All 5 test cases PASS

- [ ] **Step 7: Commit**

```bash
git add game/shared/skill_manager.h game/shared/skill_manager.cpp tests/test_double_cast.cpp
git commit -m "feat: add mage double-cast mechanic with hidden instant-cast window"
```

---

## Task 8: Final Integration — Build & Full Test Suite

**Files:** None new — verify everything works together.

- [ ] **Step 1: Full build**

Run: `"$CMAKE" --build out/build --config Debug`
Expected: Clean build, 0 errors

- [ ] **Step 2: Run full test suite**

Run: `./out/build/Debug/fate_tests.exe`
Expected: All tests pass (existing + new). New tests should include:
- `test_faction.cpp`: 7 tests (registry, garbler)
- `test_stat_enchant.cpp`: 7 tests (validation, rolling, applying)
- `test_pet_system.cpp`: 5 tests (stats, leveling, XP cap)
- `test_double_cast.cpp`: 5 tests (activate, consume, expire)

- [ ] **Step 3: Commit any final fixes if needed**

- [ ] **Step 4: Final commit if any fixes were needed**

```bash
git status
# Stage only the specific files that were fixed
git commit -m "fix: resolve integration issues from four-system implementation"
```
