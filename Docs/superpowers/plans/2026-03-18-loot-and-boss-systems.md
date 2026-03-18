# Loot, Ground Items, and Boss Spawn Systems — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the C# loot/drop and boss spawning systems into C++ following existing engine conventions.

**Architecture:** Server-authoritative loot pipeline. Server rolls loot on kill (in `server_app.cpp::processAction()`), spawns ground item entities, registers with `ReplicationManager`. Clients receive `SvEntityEnter(type=3)` and create ghost sprites. Pickup via `CmdAction(actionType=3)`. DB caches live in `server/cache/` (pqxx dependency), components in `game/components/` (shared). Boss spawning is an independent track.

**Tech Stack:** C++17, ECS (fate engine), PostgreSQL via pqxx, nlohmann/json, doctest for tests

**Spec:** `docs/superpowers/specs/2026-03-18-loot-and-boss-systems-design.md`

---

## File Layout

| File | Purpose | Build Target |
|------|---------|-------------|
| `server/cache/item_definition_cache.h` | Item def cache class + CachedItemDefinition struct | Server |
| `server/cache/item_definition_cache.cpp` | DB loading implementation | Server |
| `server/cache/loot_table_cache.h` | Loot table cache class + LootDropEntry/Result structs | Server |
| `server/cache/loot_table_cache.cpp` | DB loading + loot rolling implementation | Server |
| `game/components/dropped_item_component.h` | DroppedItemComponent struct | Client + Server |
| `game/components/boss_spawn_point_component.h` | BossSpawnPointComponent struct | Client + Server |
| `server/db/zone_mob_state_repository.h` | DeadMobRecord + ZoneMobStateRepository class | Server |
| `server/db/zone_mob_state_repository.cpp` | DB persistence implementation | Server |
| `tests/test_loot_protocol.cpp` | Protocol round-trip tests for new messages | Tests |
| `tests/test_loot_tables.cpp` | Loot rolling logic tests | Tests |

| Modified File | Change |
|---------------|--------|
| `engine/net/protocol.h` | Entity type 3 conditional fields in SvEntityEnterMsg, new SvLootPickupMsg |
| `engine/net/packet.h` | New SvLootPickup packet type |
| `engine/net/replication.cpp` | Entity type 3 in buildEnterMessage |
| `game/entity_factory.h` | createDroppedItem(), createGhostDroppedItem() |
| `game/components/game_components.h` | Include new component headers |
| `game/register_components.h` | Register new components + traits |
| `game/game_app.cpp` | Client entity type 3 handling, SvLootPickup handler |
| `server/server_app.h` | New member pointers for caches, repos |
| `server/server_app.cpp` | Initialize caches, loot on kill, pickup handling |

---

## Track A — Loot Pipeline

### Task 1: ItemDefinitionCache

**Files:**
- Create: `server/cache/item_definition_cache.h`
- Create: `server/cache/item_definition_cache.cpp`

- [ ] **Step 1: Create the header with CachedItemDefinition struct and cache class**

```cpp
// server/cache/item_definition_cache.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "game/shared/item_stat_roller.h"
#include <nlohmann/json.hpp>

namespace pqxx { class connection; }

namespace fate {

struct CachedItemDefinition {
    // Identity
    std::string itemId;
    std::string displayName;
    std::string description;

    // Classification
    std::string itemType;   // Weapon, Armor, Consumable, etc.
    std::string subtype;    // Sword, Wand, Bow, Chest, Ring, etc.
    std::string rarity;     // Common, Uncommon, Rare, Epic, Legendary, Mythic

    // Requirements
    int levelReq = 1;
    std::string classReq = "All";

    // Combat
    int damageMin = 0;
    int damageMax = 0;

    // Defense
    int armor = 0;

    // Enchantment
    int maxEnchant = 12;
    bool isSocketable = false;

    // Binding
    bool isSoulbound = false;

    // Stacking & Economy
    int maxStack = 1;
    int goldValue = 0;

    // Visual
    std::string iconPath;

    // Rolled stats
    std::vector<PossibleStat> possibleStats;

    // Attributes (parsed JSONB)
    nlohmann::json attributes;

    // Helpers
    bool isWeapon() const {
        return itemType == "Weapon";
    }
    bool isArmor() const {
        return itemType == "Armor";
    }
    bool isAccessory() const {
        return subtype == "Necklace" || subtype == "Ring" ||
               subtype == "Cloak" || subtype == "Belt";
    }
    bool isEquipment() const {
        return isWeapon() || isArmor() || isAccessory();
    }
    bool hasPossibleStats() const {
        return !possibleStats.empty();
    }

    int getIntAttribute(const std::string& key, int defaultVal = 0) const {
        auto it = attributes.find(key);
        if (it != attributes.end() && it->is_number_integer()) return it->get<int>();
        return defaultVal;
    }
    float getFloatAttribute(const std::string& key, float defaultVal = 0.0f) const {
        auto it = attributes.find(key);
        if (it != attributes.end() && it->is_number()) return it->get<float>();
        return defaultVal;
    }
    std::string getStringAttribute(const std::string& key, const std::string& defaultVal = "") const {
        auto it = attributes.find(key);
        if (it != attributes.end() && it->is_string()) return it->get<std::string>();
        return defaultVal;
    }
};

class ItemDefinitionCache {
public:
    void initialize(pqxx::connection& conn);

    const CachedItemDefinition* getDefinition(const std::string& itemId) const {
        auto it = definitions_.find(itemId);
        return it != definitions_.end() ? &it->second : nullptr;
    }

    std::vector<const CachedItemDefinition*> getItemsByType(const std::string& itemType) const;

    size_t size() const { return definitions_.size(); }

private:
    std::unordered_map<std::string, CachedItemDefinition> definitions_;
    static std::vector<PossibleStat> parsePossibleStats(const std::string& json);
};

} // namespace fate
```

- [ ] **Step 2: Create the implementation file**

```cpp
// server/cache/item_definition_cache.cpp
#include "server/cache/item_definition_cache.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>

namespace fate {

void ItemDefinitionCache::initialize(pqxx::connection& conn) {
    definitions_.clear();

    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT item_id, name, type, subtype, class_req, level_req, "
            "damage_min, damage_max, armor, attributes, description, "
            "gold_value, max_stack, icon_path, possible_stats, "
            "is_socketable, is_soulbound, rarity, max_enchant "
            "FROM item_definitions"
        );
        txn.commit();

        for (const auto& row : result) {
            CachedItemDefinition def;
            def.itemId      = row["item_id"].as<std::string>();
            def.displayName = row["name"].as<std::string>();
            def.itemType    = row["type"].as<std::string>();
            def.subtype     = row["subtype"].as<std::string>("");
            def.classReq    = row["class_req"].as<std::string>("All");
            def.levelReq    = row["level_req"].as<int>(1);
            def.damageMin   = row["damage_min"].as<int>(0);
            def.damageMax   = row["damage_max"].as<int>(0);
            def.armor       = row["armor"].as<int>(0);
            def.description = row["description"].as<std::string>("");
            def.goldValue   = row["gold_value"].as<int>(0);
            def.maxStack    = row["max_stack"].as<int>(1);
            def.iconPath    = row["icon_path"].as<std::string>("");
            def.isSocketable = row["is_socketable"].as<bool>(false);
            def.isSoulbound  = row["is_soulbound"].as<bool>(false);
            def.rarity       = row["rarity"].as<std::string>("Common");
            def.maxEnchant   = row["max_enchant"].as<int>(12);

            // Parse possible_stats JSONB
            std::string possibleStatsJson = row["possible_stats"].as<std::string>("[]");
            def.possibleStats = parsePossibleStats(possibleStatsJson);

            // Parse attributes JSONB
            std::string attrJson = row["attributes"].as<std::string>("{}");
            def.attributes = nlohmann::json::parse(attrJson, nullptr, false);
            if (def.attributes.is_discarded()) {
                def.attributes = nlohmann::json::object();
            }

            definitions_[def.itemId] = std::move(def);
        }

        LOG_INFO("ItemDefCache", "Loaded %zu item definitions", definitions_.size());
    } catch (const std::exception& e) {
        LOG_ERROR("ItemDefCache", "Failed to load item definitions: %s", e.what());
    }
}

std::vector<const CachedItemDefinition*> ItemDefinitionCache::getItemsByType(
    const std::string& itemType) const
{
    std::vector<const CachedItemDefinition*> result;
    for (const auto& [id, def] : definitions_) {
        if (def.itemType == itemType) result.push_back(&def);
    }
    return result;
}

std::vector<PossibleStat> ItemDefinitionCache::parsePossibleStats(const std::string& json) {
    std::vector<PossibleStat> stats;
    auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) return stats;

    for (const auto& entry : parsed) {
        PossibleStat ps;
        ps.stat     = entry.value("stat", "");
        ps.min      = entry.value("min", 0);
        ps.max      = entry.value("max", 0);
        ps.weighted = entry.value("weighted", false);
        if (!ps.stat.empty()) stats.push_back(ps);
    }
    return stats;
}

} // namespace fate
```

- [ ] **Step 3: Verify it compiles in the server build**

Run: `"C:\Program Files\Microsoft Visual Studio\2025\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --target FateServer 2>&1 | tail -5`

Expected: Compiles successfully (may warn about unused function if not wired yet)

- [ ] **Step 4: Commit**

```bash
git add server/cache/item_definition_cache.h server/cache/item_definition_cache.cpp
git commit -m "feat: add ItemDefinitionCache for server-side item def loading from PostgreSQL"
```

---

### Task 2: LootTableCache

**Files:**
- Create: `server/cache/loot_table_cache.h`
- Create: `server/cache/loot_table_cache.cpp`

- [ ] **Step 1: Write the loot rolling test**

```cpp
// tests/test_loot_tables.cpp
#include <doctest/doctest.h>
#include "server/cache/loot_table_cache.h"

TEST_CASE("rollEnchantLevel returns valid range") {
    // Enchantable subtype should return 0-7
    for (int i = 0; i < 100; ++i) {
        int level = fate::LootTableCache::rollEnchantLevel("Sword");
        CHECK(level >= 0);
        CHECK(level <= 7);
    }
}

TEST_CASE("rollEnchantLevel returns 0 for non-enchantable subtypes") {
    CHECK(fate::LootTableCache::rollEnchantLevel("Necklace") == 0);
    CHECK(fate::LootTableCache::rollEnchantLevel("Ring") == 0);
    CHECK(fate::LootTableCache::rollEnchantLevel("Cloak") == 0);
    CHECK(fate::LootTableCache::rollEnchantLevel("Belt") == 0);
}

TEST_CASE("rollEnchantLevel enchantable subtypes") {
    // These should sometimes return > 0
    bool gotNonZero = false;
    for (int i = 0; i < 200; ++i) {
        if (fate::LootTableCache::rollEnchantLevel("Armor") > 0) {
            gotNonZero = true;
            break;
        }
    }
    CHECK(gotNonZero);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `"C:\Program Files\Microsoft Visual Studio\2025\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --target FateTests 2>&1 | tail -10`

Expected: FAIL — `loot_table_cache.h` not found

- [ ] **Step 3: Create the header**

```cpp
// server/cache/loot_table_cache.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include "game/shared/item_instance.h"

namespace pqxx { class connection; }

namespace fate {

class ItemDefinitionCache;

struct LootDropEntry {
    std::string itemId;
    float dropChance = 0.0f;  // 0.0–1.0
    int minQuantity = 1;
    int maxQuantity = 1;
};

struct LootDropResult {
    ItemInstance item;
    std::string itemName;  // for logging
};

class LootTableCache {
public:
    void initialize(pqxx::connection& conn, const ItemDefinitionCache& itemDefs);

    std::vector<LootDropResult> rollLoot(const std::string& lootTableId) const;

    size_t tableCount() const { return tables_.size(); }

    // Public for testing
    static int rollEnchantLevel(const std::string& subtype);

private:
    const ItemDefinitionCache* itemDefs_ = nullptr;
    std::unordered_map<std::string, std::vector<LootDropEntry>> tables_;

    static std::mt19937& rng() {
        thread_local std::mt19937 gen{std::random_device{}()};
        return gen;
    }

    static const std::unordered_set<std::string>& enchantableSubtypes() {
        static const std::unordered_set<std::string> s = {
            "Sword", "Wand", "Bow", "Shield",
            "Head", "Armor", "Gloves", "Boots", "Feet"
        };
        return s;
    }
};

} // namespace fate
```

- [ ] **Step 4: Create the implementation**

```cpp
// server/cache/loot_table_cache.cpp
#include "server/cache/loot_table_cache.h"
#include "server/cache/item_definition_cache.h"
#include "game/shared/item_stat_roller.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>
#include <chrono>

namespace fate {

void LootTableCache::initialize(pqxx::connection& conn, const ItemDefinitionCache& itemDefs) {
    itemDefs_ = &itemDefs;
    tables_.clear();

    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT loot_table_id, item_id, drop_chance, min_quantity, max_quantity "
            "FROM loot_drops ORDER BY loot_table_id"
        );
        txn.commit();

        for (const auto& row : result) {
            LootDropEntry entry;
            std::string tableId = row["loot_table_id"].as<std::string>();
            entry.itemId       = row["item_id"].as<std::string>();
            entry.dropChance   = row["drop_chance"].as<float>(0.0f);
            entry.minQuantity  = row["min_quantity"].as<int>(1);
            entry.maxQuantity  = row["max_quantity"].as<int>(1);
            tables_[tableId].push_back(entry);
        }

        LOG_INFO("LootTableCache", "Loaded %zu loot tables", tables_.size());
    } catch (const std::exception& e) {
        LOG_ERROR("LootTableCache", "Failed to load loot tables: %s", e.what());
    }
}

std::vector<LootDropResult> LootTableCache::rollLoot(const std::string& lootTableId) const {
    std::vector<LootDropResult> results;

    auto it = tables_.find(lootTableId);
    if (it == tables_.end()) return results;

    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);

    for (const auto& entry : it->second) {
        float roll = chanceDist(rng());
        if (roll > entry.dropChance) continue;

        // Passed drop chance — create the item
        const CachedItemDefinition* def = itemDefs_ ? itemDefs_->getDefinition(entry.itemId) : nullptr;
        if (!def) {
            LOG_WARN("LootTableCache", "Unknown item_id '%s' in loot table '%s'",
                     entry.itemId.c_str(), lootTableId.c_str());
            continue;
        }

        // Roll quantity
        int qty = entry.minQuantity;
        if (entry.maxQuantity > entry.minQuantity) {
            std::uniform_int_distribution<int> qtyDist(entry.minQuantity, entry.maxQuantity);
            qty = qtyDist(rng());
        }

        // Create ItemInstance
        ItemInstance item;
        item.instanceId = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
            "_" + std::to_string(reinterpret_cast<uintptr_t>(&entry));
        item.itemId = entry.itemId;
        item.quantity = qty;

        // Roll stats if the item has possible stats
        if (def->hasPossibleStats()) {
            item.rolledStats = ItemStatRoller::rollStats(def->possibleStats);
        }

        // Roll enchant level for enchantable subtypes
        item.enchantLevel = rollEnchantLevel(def->subtype);

        // Roll socket for accessories
        if (def->isSocketable && def->isAccessory()) {
            // Accessories get STR/DEX/INT socket randomly
            StatType socketTypes[] = {StatType::Strength, StatType::Dexterity, StatType::Intelligence};
            std::uniform_int_distribution<int> socketDist(0, 2);
            item.socket = ItemStatRoller::rollSocket(socketTypes[socketDist(rng())]);
        }

        item.isSoulbound = def->isSoulbound;

        LootDropResult result;
        result.item = std::move(item);
        result.itemName = def->displayName;
        results.push_back(std::move(result));
    }

    return results;
}

int LootTableCache::rollEnchantLevel(const std::string& subtype) {
    if (enchantableSubtypes().count(subtype) == 0) return 0;

    // Weighted: +0=40%, +1=25%, +2=15%, +3=10%, +4=5%, +5=3%, +6=1.5%, +7=0.5%
    std::uniform_real_distribution<float> dist(0.0f, 100.0f);
    float roll = dist(rng());

    if (roll < 40.0f) return 0;
    if (roll < 65.0f) return 1;
    if (roll < 80.0f) return 2;
    if (roll < 90.0f) return 3;
    if (roll < 95.0f) return 4;
    if (roll < 98.0f) return 5;
    if (roll < 99.5f) return 6;
    return 7;
}

} // namespace fate
```

- [ ] **Step 5: Run tests to verify they pass**

Run: Build and run tests
Expected: All 3 loot table tests PASS

- [ ] **Step 6: Commit**

```bash
git add server/cache/loot_table_cache.h server/cache/loot_table_cache.cpp tests/test_loot_tables.cpp
git commit -m "feat: add LootTableCache with weighted enchant rolling and loot generation"
```

---

### Task 3: Protocol Changes

**Files:**
- Modify: `engine/net/protocol.h:101-134` (SvEntityEnterMsg)
- Modify: `engine/net/packet.h:44-52` (PacketType)
- Create: `tests/test_loot_protocol.cpp`

- [ ] **Step 1: Write the protocol round-trip test**

```cpp
// tests/test_loot_protocol.cpp
#include <doctest/doctest.h>
#include "engine/net/protocol.h"

TEST_CASE("SvEntityEnterMsg round-trip for dropped item (entityType=3)") {
    uint8_t buf[512];
    fate::SvEntityEnterMsg src;
    src.persistentId = 12345;
    src.entityType   = 3;
    src.position     = {100.5f, 200.25f};
    src.name         = "Iron Sword";
    src.level        = 0;
    src.currentHP    = 0;
    src.maxHP        = 0;
    src.faction      = 0;
    // Dropped item fields
    src.itemId       = "sword_iron_01";
    src.quantity     = 1;
    src.isGold       = 0;
    src.goldAmount   = 0;
    src.enchantLevel = 3;
    src.rarity       = "Rare";

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityEnterMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.entityType == 3);
    CHECK(dst.itemId == "sword_iron_01");
    CHECK(dst.quantity == 1);
    CHECK(dst.isGold == 0);
    CHECK(dst.goldAmount == 0);
    CHECK(dst.enchantLevel == 3);
    CHECK(dst.rarity == "Rare");
}

TEST_CASE("SvEntityEnterMsg round-trip for gold drop") {
    uint8_t buf[512];
    fate::SvEntityEnterMsg src;
    src.persistentId = 99999;
    src.entityType   = 3;
    src.position     = {50.0f, 75.0f};
    src.name         = "Gold";
    src.itemId       = "";
    src.quantity     = 0;
    src.isGold       = 1;
    src.goldAmount   = 250;
    src.enchantLevel = 0;
    src.rarity       = "";

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityEnterMsg::read(r);
    CHECK(dst.isGold == 1);
    CHECK(dst.goldAmount == 250);
}

TEST_CASE("SvEntityEnterMsg round-trip for player (entityType=0) unchanged") {
    uint8_t buf[512];
    fate::SvEntityEnterMsg src;
    src.persistentId = 555;
    src.entityType   = 0;
    src.position     = {10.0f, 20.0f};
    src.name         = "TestPlayer";
    src.level        = 42;
    src.currentHP    = 500;
    src.maxHP        = 1000;
    src.faction      = 1;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityEnterMsg::read(r);
    CHECK(r.remaining() == 0);
    CHECK(dst.entityType == 0);
    CHECK(dst.name == "TestPlayer");
    CHECK(dst.level == 42);
    // Item fields should be defaults
    CHECK(dst.itemId.empty());
    CHECK(dst.isGold == 0);
}

TEST_CASE("SvLootPickupMsg round-trip") {
    uint8_t buf[512];
    fate::SvLootPickupMsg src;
    src.itemId      = "armor_plate_02";
    src.displayName = "Plate Armor +2";
    src.quantity    = 1;
    src.isGold      = 0;
    src.goldAmount  = 0;
    src.rarity      = "Epic";

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvLootPickupMsg::read(r);
    CHECK(dst.itemId == "armor_plate_02");
    CHECK(dst.displayName == "Plate Armor +2");
    CHECK(dst.quantity == 1);
    CHECK(dst.rarity == "Epic");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: FAIL — `SvLootPickupMsg` and item fields don't exist yet

- [ ] **Step 3: Update SvEntityEnterMsg with conditional item fields**

In `engine/net/protocol.h`, modify `SvEntityEnterMsg` (after line 109, before `write()`):

Add fields:
```cpp
    // Dropped item fields (only serialized when entityType == 3)
    std::string itemId;
    int32_t     quantity     = 0;
    uint8_t     isGold       = 0;
    int32_t     goldAmount   = 0;
    int32_t     enchantLevel = 0;
    std::string rarity;
```

Replace `write()`:
```cpp
    void write(ByteWriter& w) const {
        detail::writeU64(w, persistentId);
        w.writeU8(entityType);
        w.writeVec2(position);
        w.writeString(name);
        w.writeI32(level);
        w.writeI32(currentHP);
        w.writeI32(maxHP);
        w.writeU8(faction);
        if (entityType == 3) {
            w.writeString(itemId);
            w.writeI32(quantity);
            w.writeU8(isGold);
            w.writeI32(goldAmount);
            w.writeI32(enchantLevel);
            w.writeString(rarity);
        }
    }
```

Replace `read()`:
```cpp
    static SvEntityEnterMsg read(ByteReader& r) {
        SvEntityEnterMsg m;
        m.persistentId = detail::readU64(r);
        m.entityType   = r.readU8();
        m.position     = r.readVec2();
        m.name         = r.readString();
        m.level        = r.readI32();
        m.currentHP    = r.readI32();
        m.maxHP        = r.readI32();
        m.faction      = r.readU8();
        if (m.entityType == 3) {
            m.itemId       = r.readString();
            m.quantity     = r.readI32();
            m.isGold       = r.readU8();
            m.goldAmount   = r.readI32();
            m.enchantLevel = r.readI32();
            m.rarity       = r.readString();
        }
        return m;
    }
```

- [ ] **Step 4: Add SvLootPickupMsg**

Add after `SvMovementCorrectionMsg` in `engine/net/protocol.h`:

```cpp
struct SvLootPickupMsg {
    std::string itemId;
    std::string displayName;
    int32_t     quantity   = 0;
    uint8_t     isGold     = 0;
    int32_t     goldAmount = 0;
    std::string rarity;

    void write(ByteWriter& w) const {
        w.writeString(itemId);
        w.writeString(displayName);
        w.writeI32(quantity);
        w.writeU8(isGold);
        w.writeI32(goldAmount);
        w.writeString(rarity);
    }

    static SvLootPickupMsg read(ByteReader& r) {
        SvLootPickupMsg m;
        m.itemId      = r.readString();
        m.displayName = r.readString();
        m.quantity    = r.readI32();
        m.isGold      = r.readU8();
        m.goldAmount  = r.readI32();
        m.rarity      = r.readString();
        return m;
    }
};
```

- [ ] **Step 5: Add SvLootPickup packet type**

In `engine/net/packet.h`, add after line 51:

```cpp
    constexpr uint8_t SvLootPickup         = 0x98;
```

- [ ] **Step 6: Run tests to verify they pass**

Run: Build and run FateTests
Expected: All 4 protocol tests PASS, existing protocol tests still PASS

- [ ] **Step 7: Commit**

```bash
git add engine/net/protocol.h engine/net/packet.h tests/test_loot_protocol.cpp
git commit -m "feat: extend protocol with entity type 3 (dropped items) and SvLootPickupMsg"
```

---

### Task 4: DroppedItemComponent + Factory Methods

**Files:**
- Create: `game/components/dropped_item_component.h`
- Modify: `game/entity_factory.h` (add createDroppedItem, createGhostDroppedItem)
- Modify: `game/components/game_components.h` (add include)

- [ ] **Step 1: Create DroppedItemComponent**

```cpp
// game/components/dropped_item_component.h
#pragma once
#include "engine/ecs/component.h"
#include <string>
#include <cstdint>

namespace fate {

struct DroppedItemComponent {
    FATE_COMPONENT_COLD(DroppedItemComponent)

    std::string itemId;
    int quantity = 1;
    int enchantLevel = 0;
    std::string rolledStatsJson;
    std::string rarity;

    bool isGold = false;
    int goldAmount = 0;

    uint32_t ownerEntityId = 0;  // 0 = free for all
    float spawnTime = 0.0f;
    float despawnAfter = 120.0f; // 2 minutes
};

} // namespace fate
```

- [ ] **Step 2: Add include to game_components.h**

Add at the end of the includes section in `game/components/game_components.h`:

```cpp
#include "game/components/dropped_item_component.h"
```

- [ ] **Step 3: Add createDroppedItem to EntityFactory**

Add to `game/entity_factory.h` after the existing `createGhostMob()` method:

```cpp
    // Creates a dropped item entity on the ground (server-side)
    static Entity* createDroppedItem(World& world, Vec2 position, bool isGold) {
        Entity* entity = world.createEntity(isGold ? "gold_drop" : "item_drop");
        entity->setTag("dropped_item");

        auto* transform = entity->addComponent<Transform>(position.x, position.y);

        auto* sprite = entity->addComponent<SpriteComponent>();
        sprite->size = {16.0f, 16.0f};

        entity->addComponent<DroppedItemComponent>();

        return entity;
    }

    // Creates a ghost dropped item (client-side, for rendering)
    static Entity* createGhostDroppedItem(World& world, const std::string& name, Vec2 position) {
        Entity* entity = world.createEntity(name);
        entity->setTag("dropped_item");

        auto* transform = entity->addComponent<Transform>(position.x, position.y);

        auto* sprite = entity->addComponent<SpriteComponent>();
        sprite->size = {16.0f, 16.0f};

        return entity;
    }
```

- [ ] **Step 4: Verify it compiles**

Run: Build FateEngine target
Expected: Compiles successfully

- [ ] **Step 5: Commit**

```bash
git add game/components/dropped_item_component.h game/components/game_components.h game/entity_factory.h
git commit -m "feat: add DroppedItemComponent and factory methods for ground loot entities"
```

---

### Task 5: Replication Integration (Entity Type 3)

**Files:**
- Modify: `engine/net/replication.cpp:212-259` (buildEnterMessage)

- [ ] **Step 1: Add entity type 3 handling in buildEnterMessage**

In `engine/net/replication.cpp`, add a new branch in `buildEnterMessage()` after the NPC check (after line 250, before the faction block). Add the include at the top of the file:

```cpp
#include "game/components/dropped_item_component.h"
```

Add the branch in `buildEnterMessage()` after the `npcComp` block:

```cpp
    auto* droppedItem = entity->getComponent<DroppedItemComponent>();

    if (charStats) {
        // ... existing player code ...
    } else if (enemyStats) {
        // ... existing mob code ...
    } else if (npcComp) {
        // ... existing npc code ...
    } else if (droppedItem) {
        msg.entityType = 3; // dropped item
        msg.name = droppedItem->isGold ? "Gold" : droppedItem->itemId;
        msg.itemId = droppedItem->itemId;
        msg.quantity = droppedItem->quantity;
        msg.isGold = droppedItem->isGold ? 1 : 0;
        msg.goldAmount = droppedItem->goldAmount;
        msg.enchantLevel = droppedItem->enchantLevel;
        msg.rarity = droppedItem->rarity;
    }
```

- [ ] **Step 2: Verify it compiles**

Run: Build FateEngine and FateServer targets
Expected: Both compile successfully

- [ ] **Step 3: Commit**

```bash
git add engine/net/replication.cpp
git commit -m "feat: replicate dropped item entities as entity type 3"
```

---

### Task 6: Component Registration

**Files:**
- Modify: `game/register_components.h`

- [ ] **Step 1: Add component traits for DroppedItemComponent**

In `game/register_components.h`, add trait specialization in the traits section (after the existing trait blocks, before `registerAllComponents()`):

```cpp
template<> struct component_traits<DroppedItemComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked;
};
```

- [ ] **Step 2: Add registerComponent call inside registerAllComponents()**

Inside the `registerAllComponents()` function body (after the existing `registerComponent` calls):

```cpp
    reg.registerComponent<DroppedItemComponent>();
```

- [ ] **Step 3: Add FATE_REFLECT_EMPTY at the end of the file**

Add in the reflection declarations section:

```cpp
FATE_REFLECT_EMPTY(fate::DroppedItemComponent)
```

- [ ] **Step 4: Verify it compiles**

Run: Build both targets
Expected: Compiles successfully

- [ ] **Step 5: Commit**

```bash
git add game/register_components.h
git commit -m "feat: register DroppedItemComponent with Serializable|Networked traits"
```

---

### Task 7: Server-Side Loot on Kill + Pickup

**Files:**
- Modify: `server/server_app.h` (add members)
- Modify: `server/server_app.cpp` (initialize caches, loot on kill, pickup handling)

- [ ] **Step 1: Read server_app.h to understand the current class layout**

Read the file to find where to add new members.

- [ ] **Step 2: Add cache members to server_app.h**

Add includes:
```cpp
#include "server/cache/item_definition_cache.h"
#include "server/cache/loot_table_cache.h"
```

Add private members:
```cpp
    ItemDefinitionCache itemDefCache_;
    LootTableCache lootTableCache_;
```

- [ ] **Step 3: Initialize caches in ServerApp::init()**

In `server_app.cpp`, after the repository creation (after line 70), add:

```cpp
    // Initialize definition caches
    itemDefCache_.initialize(gameDbConn_.connection());
    lootTableCache_.initialize(gameDbConn_.connection(), itemDefCache_);
    LOG_INFO("Server", "Loaded %zu item definitions, %zu loot tables",
             itemDefCache_.size(), lootTableCache_.tableCount());
```

- [ ] **Step 4: Add loot spawning in processAction() on kill**

In `server_app.cpp`, replace the kill block at line 542-546:

First, replace the `takeDamage` call at line 525 so damage is tracked per-attacker on EVERY hit (not just kills):

```cpp
        // Replace: enemyStats->stats.takeDamage(damage);
        // With (takeDamageFrom tracks damageByAttacker AND applies damage):
        enemyStats->stats.takeDamageFrom(attackerHandle.value, damage);
```

Then replace the kill block at line 542-546:

```cpp
        if (killed) {
            auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
            EnemyStats& es = targetEnemyStats->stats;
            auto* targetTransform = target->getComponent<Transform>();
            Vec2 deathPos = targetTransform ? targetTransform->position : Vec2{0, 0};

            // Determine top damager for loot ownership
            uint32_t topDamagerId = attackerHandle.value; // fallback: killer
            int topDamage = 0;
            for (const auto& [attackerId, totalDmg] : es.damageByAttacker) {
                if (totalDmg > topDamage) {
                    topDamage = totalDmg;
                    topDamagerId = attackerId;
                }
            }

            // Roll loot table
            if (!es.lootTableId.empty()) {
                auto drops = lootTableCache_.rollLoot(es.lootTableId);

                // Spawn each drop with grid offset + random jitter
                constexpr float kItemSpacing = 10.0f;  // pixels
                constexpr int kMaxPerRow = 4;
                thread_local std::mt19937 dropRng{std::random_device{}()};
                std::uniform_real_distribution<float> jitter(-3.0f, 3.0f);

                int totalDrops = static_cast<int>(drops.size());
                int cols = std::min(totalDrops, kMaxPerRow);
                float gridWidth = (cols - 1) * kItemSpacing;

                for (size_t i = 0; i < drops.size(); ++i) {
                    int col = static_cast<int>(i) % kMaxPerRow;
                    int row = static_cast<int>(i) / kMaxPerRow;
                    Vec2 offset = {
                        (col * kItemSpacing) - (gridWidth * 0.5f) + jitter(dropRng),
                        row * kItemSpacing + jitter(dropRng)
                    };
                    Vec2 dropPos = {deathPos.x + offset.x, deathPos.y + offset.y};

                    Entity* dropEntity = EntityFactory::createDroppedItem(world_, dropPos, false);
                    auto* dropComp = dropEntity->getComponent<DroppedItemComponent>();
                    if (dropComp) {
                        dropComp->itemId = drops[i].item.itemId;
                        dropComp->quantity = drops[i].item.quantity;
                        dropComp->enchantLevel = drops[i].item.enchantLevel;
                        dropComp->rolledStatsJson = ItemStatRoller::rolledStatsToJson(drops[i].item.rolledStats);
                        dropComp->ownerEntityId = topDamagerId;
                        dropComp->spawnTime = gameTime_;

                        const auto* def = itemDefCache_.getDefinition(drops[i].item.itemId);
                        if (def) dropComp->rarity = def->rarity;
                    }

                    PersistentId dropPid = PersistentId::generate(1);
                    replication_.registerEntity(dropEntity->handle(), dropPid);
                }
            }

            // Roll gold drop
            if (es.goldDropChance > 0.0f) {
                thread_local std::mt19937 goldRng{std::random_device{}()};
                std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                if (chanceDist(goldRng) <= es.goldDropChance && es.maxGoldDrop > 0) {
                    std::uniform_int_distribution<int> goldDist(es.minGoldDrop, es.maxGoldDrop);
                    int goldAmount = goldDist(goldRng);

                    Entity* goldEntity = EntityFactory::createDroppedItem(world_, deathPos, true);
                    auto* goldComp = goldEntity->getComponent<DroppedItemComponent>();
                    if (goldComp) {
                        goldComp->isGold = true;
                        goldComp->goldAmount = goldAmount;
                        goldComp->ownerEntityId = topDamagerId;
                        goldComp->spawnTime = gameTime_;
                    }

                    PersistentId goldPid = PersistentId::generate(1);
                    replication_.registerEntity(goldEntity->handle(), goldPid);
                }
            }

            // Hide mob sprite (SpawnSystem handles respawn)
            auto* mobSprite = target->getComponent<SpriteComponent>();
            if (mobSprite) mobSprite->enabled = false;

            LOG_INFO("Server", "Client %d killed mob '%s'", clientId, es.enemyName.c_str());
        }
```

- [ ] **Step 5: Add pickup handling in processAction()**

In the `processAction()` method, add a new `else if` for `actionType == 3` (after the attack block):

```cpp
    } else if (action.actionType == 3) {
        // Pickup
        PersistentId itemPid(action.targetId);
        EntityHandle itemHandle = replication_.getEntityHandle(itemPid);
        Entity* itemEntity = world_.getEntity(itemHandle);
        if (!itemEntity) return;

        auto* dropComp = itemEntity->getComponent<DroppedItemComponent>();
        if (!dropComp) return;

        // Validate proximity
        auto* playerT = attacker->getComponent<Transform>();
        auto* itemT = itemEntity->getComponent<Transform>();
        if (!playerT || !itemT) return;
        float dist = playerT->position.distance(itemT->position);
        if (dist > 48.0f) return; // ~1.5 tiles in pixels

        // Validate loot rights
        if (dropComp->ownerEntityId != 0 && dropComp->ownerEntityId != attackerHandle.value) {
            // TODO: check party membership
            return;
        }

        // Process pickup
        auto* inv = attacker->getComponent<InventoryComponent>();
        if (!inv) return;

        SvLootPickupMsg pickupMsg;

        if (dropComp->isGold) {
            inv->inventory.addGold(dropComp->goldAmount);
            pickupMsg.isGold = 1;
            pickupMsg.goldAmount = dropComp->goldAmount;
            pickupMsg.displayName = "Gold";
        } else {
            // Reconstruct ItemInstance
            ItemInstance item;
            item.instanceId = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            item.itemId = dropComp->itemId;
            item.quantity = dropComp->quantity;
            item.enchantLevel = dropComp->enchantLevel;
            item.rolledStats = ItemStatRoller::parseRolledStats(dropComp->rolledStatsJson);
            inv->inventory.addItem(item);

            pickupMsg.itemId = dropComp->itemId;
            pickupMsg.quantity = dropComp->quantity;
            pickupMsg.rarity = dropComp->rarity;

            const auto* def = itemDefCache_.getDefinition(dropComp->itemId);
            pickupMsg.displayName = def ? def->displayName : dropComp->itemId;
            if (dropComp->enchantLevel > 0) {
                pickupMsg.displayName += " +" + std::to_string(dropComp->enchantLevel);
            }
        }

        // Send pickup notification to player
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        pickupMsg.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvLootPickup, buf, w.size());

        // Send updated player state (gold, inventory changes)
        sendPlayerState(clientId);

        // Destroy the dropped item entity
        replication_.unregisterEntity(itemHandle);
        world_.destroyEntity(itemHandle);
    }
```

- [ ] **Step 6: Add despawn tick in ServerApp::tick()**

In `server_app.cpp`, add after `world_.update(dt)` (after line 132):

```cpp
    // Despawn expired ground items
    {
        std::vector<EntityHandle> toDestroy;
        world_.forEach<DroppedItemComponent>([&](Entity* e, DroppedItemComponent* drop) {
            if (gameTime_ - drop->spawnTime > drop->despawnAfter) {
                toDestroy.push_back(e->handle());
            }
        });
        for (auto handle : toDestroy) {
            replication_.unregisterEntity(handle);
            world_.destroyEntity(handle);
        }
    }
```

Add at top of file:
```cpp
#include "game/components/dropped_item_component.h"
#include "game/shared/item_stat_roller.h"
#include <chrono>
#include <random>
```

- [ ] **Step 7: Verify it compiles**

Run: Build FateServer
Expected: Compiles successfully

- [ ] **Step 8: Commit**

```bash
git add server/server_app.h server/server_app.cpp
git commit -m "feat: server-side loot rolling on kill, ground item spawning, pickup handling, despawn"
```

---

### Task 8: Client-Side Handling

**Files:**
- Modify: `game/game_app.cpp:789-802` (onEntityEnter handler)

- [ ] **Step 1: Add entity type 3 branch in onEntityEnter**

In `game/game_app.cpp`, update the `onEntityEnter` lambda to handle type 3:

```cpp
    netClient_.onEntityEnter = [this](const SvEntityEnterMsg& msg) {
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        auto& world = sc->world();
        Entity* ghost = nullptr;
        if (msg.entityType == 0) { // player
            ghost = EntityFactory::createGhostPlayer(world, msg.name, msg.position);
        } else if (msg.entityType == 3) { // dropped item
            ghost = EntityFactory::createGhostDroppedItem(world, msg.name, msg.position);
        } else { // mob or npc
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position);
        }
        if (ghost) {
            ghostEntities_[msg.persistentId] = ghost->handle();
        }
    };
```

- [ ] **Step 2: Add SvLootPickup handler**

Find where `onEntityUpdate` is registered and add after it a handler for `SvLootPickup`. In the net client packet dispatch (in `net_client.cpp` or wherever custom packet handlers are registered), add:

```cpp
    // In the packet switch in net_client.cpp, add:
    case PacketType::SvLootPickup: {
        ByteReader payload(data + r.position(), hdr.payloadSize);
        auto msg = SvLootPickupMsg::read(payload);
        if (onLootPickup) onLootPickup(msg);
        break;
    }
```

Add to `net_client.h`:
```cpp
    std::function<void(const SvLootPickupMsg&)> onLootPickup;
```

In `game_app.cpp`, register the handler:
```cpp
    netClient_.onLootPickup = [this](const SvLootPickupMsg& msg) {
        // Display floating pickup text
        LOG_INFO("Client", "Picked up: %s x%d", msg.displayName.c_str(), msg.quantity);
    };
```

- [ ] **Step 3: Verify it compiles**

Run: Build FateEngine
Expected: Compiles successfully

- [ ] **Step 4: Commit**

```bash
git add game/game_app.cpp engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: client-side ghost creation for dropped items, loot pickup notifications"
```

---

## Track B — Boss Spawning (Independent)

### Task 9: ZoneMobStateRepository

**Files:**
- Create: `server/db/zone_mob_state_repository.h`
- Create: `server/db/zone_mob_state_repository.cpp`

- [ ] **Step 1: Create the header**

```cpp
// server/db/zone_mob_state_repository.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace pqxx { class connection; }

namespace fate {

struct DeadMobRecord {
    std::string enemyId;
    int mobIndex = 0;
    int64_t diedAtUnix = 0;
    int respawnSeconds = 0;

    bool hasRespawned() const {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        return now >= (diedAtUnix + respawnSeconds);
    }

    float getRemainingRespawnTime() const {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        int64_t respawnAt = diedAtUnix + respawnSeconds;
        if (now >= respawnAt) return 0.0f;
        return static_cast<float>(respawnAt - now);
    }
};

class ZoneMobStateRepository {
public:
    explicit ZoneMobStateRepository(pqxx::connection& conn);

    bool saveZoneDeaths(const std::string& sceneName, const std::string& zoneName,
                        const std::vector<DeadMobRecord>& deadMobs);

    std::vector<DeadMobRecord> loadZoneDeaths(const std::string& sceneName,
                                               const std::string& zoneName);

    bool clearZoneDeaths(const std::string& sceneName, const std::string& zoneName);

    int cleanupExpiredDeaths();

private:
    pqxx::connection& conn_;
};

} // namespace fate
```

- [ ] **Step 2: Create the implementation**

```cpp
// server/db/zone_mob_state_repository.cpp
#include "server/db/zone_mob_state_repository.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>

namespace fate {

ZoneMobStateRepository::ZoneMobStateRepository(pqxx::connection& conn)
    : conn_(conn) {}

bool ZoneMobStateRepository::saveZoneDeaths(
    const std::string& sceneName, const std::string& zoneName,
    const std::vector<DeadMobRecord>& deadMobs)
{
    try {
        pqxx::work txn(conn_);

        // Clear existing records for this zone
        txn.exec_params(
            "DELETE FROM zone_mob_deaths WHERE scene_name = $1 AND zone_name = $2",
            sceneName, zoneName);

        // Insert all dead mobs
        for (const auto& rec : deadMobs) {
            txn.exec_params(
                "INSERT INTO zone_mob_deaths (scene_name, zone_name, enemy_id, mob_index, died_at_unix, respawn_seconds) "
                "VALUES ($1, $2, $3, $4, $5, $6)",
                sceneName, zoneName, rec.enemyId, rec.mobIndex,
                rec.diedAtUnix, rec.respawnSeconds);
        }

        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to save zone deaths: %s", e.what());
        return false;
    }
}

std::vector<DeadMobRecord> ZoneMobStateRepository::loadZoneDeaths(
    const std::string& sceneName, const std::string& zoneName)
{
    std::vector<DeadMobRecord> records;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT enemy_id, mob_index, died_at_unix, respawn_seconds "
            "FROM zone_mob_deaths WHERE scene_name = $1 AND zone_name = $2",
            sceneName, zoneName);
        txn.commit();

        for (const auto& row : result) {
            DeadMobRecord rec;
            rec.enemyId        = row["enemy_id"].as<std::string>();
            rec.mobIndex       = row["mob_index"].as<int>(0);
            rec.diedAtUnix     = row["died_at_unix"].as<int64_t>(0);
            rec.respawnSeconds = row["respawn_seconds"].as<int>(0);
            records.push_back(rec);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to load zone deaths: %s", e.what());
    }
    return records;
}

bool ZoneMobStateRepository::clearZoneDeaths(
    const std::string& sceneName, const std::string& zoneName)
{
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM zone_mob_deaths WHERE scene_name = $1 AND zone_name = $2",
            sceneName, zoneName);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to clear zone deaths: %s", e.what());
        return false;
    }
}

int ZoneMobStateRepository::cleanupExpiredDeaths() {
    try {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "DELETE FROM zone_mob_deaths WHERE (died_at_unix + respawn_seconds) < $1",
            now);
        txn.commit();
        return static_cast<int>(result.affected_rows());
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to cleanup expired deaths: %s", e.what());
        return 0;
    }
}

} // namespace fate
```

- [ ] **Step 3: Verify it compiles**

Run: Build FateServer
Expected: Compiles successfully

- [ ] **Step 4: Commit**

```bash
git add server/db/zone_mob_state_repository.h server/db/zone_mob_state_repository.cpp
git commit -m "feat: add ZoneMobStateRepository for boss death state persistence"
```

---

### Task 10: BossSpawnPointComponent + BossSpawnSystem

**Files:**
- Create: `game/components/boss_spawn_point_component.h`
- Modify: `game/register_components.h` (add traits)
- Modify: `server/server_app.h` (add boss system state)
- Modify: `server/server_app.cpp` (add boss tick logic)

- [ ] **Step 1: Create BossSpawnPointComponent**

```cpp
// game/components/boss_spawn_point_component.h
#pragma once
#include "engine/ecs/component.h"
#include "engine/core/types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace fate {

struct BossSpawnPointComponent {
    FATE_COMPONENT_COLD(BossSpawnPointComponent)

    std::string bossDefId;
    std::vector<Vec2> spawnCoordinates;
    int levelOverride = 0;  // 0 = use DB range

    // Tracked state
    EntityHandle bossEntityHandle;
    bool bossAlive = false;
    float respawnAt = 0.0f;
    int lastSpawnIndex = -1;
    bool initialized = false;
};

} // namespace fate
```

- [ ] **Step 2: Add component include and registration**

In `game/components/game_components.h`, add:
```cpp
#include "game/components/boss_spawn_point_component.h"
```

In `game/register_components.h`, add trait:
```cpp
template<> struct component_traits<BossSpawnPointComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Persistent;
};
```

Add `registerComponent` call inside `registerAllComponents()`:
```cpp
    reg.registerComponent<BossSpawnPointComponent>();
```

Add reflection at end of file:
```cpp
FATE_REFLECT_EMPTY(fate::BossSpawnPointComponent)
```

- [ ] **Step 3: Add boss spawn logic to server_app**

In `server/server_app.h`, add member:
```cpp
    std::unique_ptr<ZoneMobStateRepository> mobStateRepo_;
    float bossTickTimer_ = 0.0f;
```

Add include:
```cpp
#include "server/db/zone_mob_state_repository.h"
```

In `server/server_app.cpp` `init()`, after loot cache init:
```cpp
    mobStateRepo_ = std::make_unique<ZoneMobStateRepository>(gameDbConn_.connection());
```

In `server/server_app.cpp` `tick()`, add after the despawn block:

```cpp
    // Boss spawn tick (0.25s interval)
    bossTickTimer_ += dt;
    if (bossTickTimer_ >= 0.25f) {
        bossTickTimer_ = 0.0f;

        std::vector<EntityHandle> bossZoneHandles;
        world_.forEach<Transform, BossSpawnPointComponent>(
            [&](Entity* e, Transform*, BossSpawnPointComponent*) {
                bossZoneHandles.push_back(e->handle());
            }
        );

        for (auto handle : bossZoneHandles) {
            Entity* zoneEntity = world_.getEntity(handle);
            if (!zoneEntity) continue;
            auto* bossComp = zoneEntity->getComponent<BossSpawnPointComponent>();
            auto* zoneT = zoneEntity->getComponent<Transform>();
            if (!bossComp || !zoneT) continue;

            // Initialize: load persisted death state
            if (!bossComp->initialized) {
                bossComp->initialized = true;
                auto* sc = SceneManager::instance().currentScene();
                std::string sceneName = sc ? sc->name() : "unknown";
                auto deaths = mobStateRepo_->loadZoneDeaths(sceneName, bossComp->bossDefId);
                for (const auto& death : deaths) {
                    if (!death.hasRespawned()) {
                        bossComp->respawnAt = gameTime_ + death.getRemainingRespawnTime();
                        bossComp->bossAlive = false;
                    }
                }
                // Spawn boss if no pending respawn
                if (bossComp->respawnAt <= 0.0f && !bossComp->spawnCoordinates.empty()) {
                    // Pick random coordinate
                    thread_local std::mt19937 bossRng{std::random_device{}()};
                    std::uniform_int_distribution<int> coordDist(
                        0, static_cast<int>(bossComp->spawnCoordinates.size()) - 1);
                    int idx = coordDist(bossRng);
                    bossComp->lastSpawnIndex = idx;
                    Vec2 spawnPos = bossComp->spawnCoordinates[idx];

                    Entity* boss = EntityFactory::createMob(
                        world_, bossComp->bossDefId, bossComp->levelOverride > 0 ? bossComp->levelOverride : 1,
                        500, 50, spawnPos, true, true);
                    bossComp->bossEntityHandle = boss->handle();
                    bossComp->bossAlive = true;

                    PersistentId pid = PersistentId::generate(1);
                    replication_.registerEntity(boss->handle(), pid);
                }
                continue;
            }

            // Detect death
            if (bossComp->bossAlive && bossComp->bossEntityHandle) {
                Entity* boss = world_.getEntity(bossComp->bossEntityHandle);
                if (boss) {
                    auto* es = boss->getComponent<EnemyStatsComponent>();
                    if (es && !es->stats.isAlive) {
                        bossComp->bossAlive = false;
                        bossComp->respawnAt = gameTime_ + 300.0f; // 5 min default

                        // Persist death state
                        auto* sc = SceneManager::instance().currentScene();
                        std::string sceneName = sc ? sc->name() : "unknown";
                        DeadMobRecord rec;
                        rec.enemyId = bossComp->bossDefId;
                        rec.mobIndex = 0;
                        rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr));
                        rec.respawnSeconds = 300;
                        mobStateRepo_->saveZoneDeaths(sceneName, bossComp->bossDefId, {rec});
                    }
                }
            }

            // Process respawn
            if (!bossComp->bossAlive && bossComp->respawnAt > 0.0f && gameTime_ >= bossComp->respawnAt) {
                bossComp->respawnAt = 0.0f;

                if (!bossComp->spawnCoordinates.empty()) {
                    // Pick different coordinate
                    thread_local std::mt19937 bossRng{std::random_device{}()};
                    int idx = 0;
                    if (bossComp->spawnCoordinates.size() > 1) {
                        do {
                            std::uniform_int_distribution<int> coordDist(
                                0, static_cast<int>(bossComp->spawnCoordinates.size()) - 1);
                            idx = coordDist(bossRng);
                        } while (idx == bossComp->lastSpawnIndex);
                    }
                    bossComp->lastSpawnIndex = idx;
                    Vec2 spawnPos = bossComp->spawnCoordinates[idx];

                    Entity* boss = EntityFactory::createMob(
                        world_, bossComp->bossDefId, bossComp->levelOverride > 0 ? bossComp->levelOverride : 1,
                        500, 50, spawnPos, true, true);
                    bossComp->bossEntityHandle = boss->handle();
                    bossComp->bossAlive = true;

                    PersistentId pid = PersistentId::generate(1);
                    replication_.registerEntity(boss->handle(), pid);

                    // Clear death record
                    auto* sc = SceneManager::instance().currentScene();
                    std::string sceneName = sc ? sc->name() : "unknown";
                    mobStateRepo_->clearZoneDeaths(sceneName, bossComp->bossDefId);
                }
            }
        }
    }
```

- [ ] **Step 4: Verify it compiles**

Run: Build FateServer
Expected: Compiles successfully

- [ ] **Step 5: Commit**

```bash
git add game/components/boss_spawn_point_component.h game/components/game_components.h game/register_components.h server/server_app.h server/server_app.cpp
git commit -m "feat: add BossSpawnPointComponent with fixed-position spawning and death persistence"
```

---

## Final Verification

### Task 11: Full Build + Test

- [ ] **Step 1: Build all targets**

```bash
cmake --build build --target FateEngine 2>&1 | tail -5
cmake --build build --target FateServer 2>&1 | tail -5
cmake --build build --target FateTests 2>&1 | tail -5
```

Expected: All three targets compile without errors

- [ ] **Step 2: Run all tests**

```bash
./build/FateTests 2>&1 | tail -20
```

Expected: All tests pass, including new loot protocol and loot table tests

- [ ] **Step 3: Final commit if any fixes were needed**

```bash
git add -A
git commit -m "fix: resolve build issues from loot and boss systems integration"
```
