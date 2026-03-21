# Batch B: Equipment & Economy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement enchanting +1 to +15 with break mechanic, core extraction, and combine book crafting.

**Architecture:** Three subsystems sharing the inventory pipeline. Enchanting rewrites `enchant_system.h` with new rates/break logic and adds server handlers. Core extraction is a new shared utility + server handler. Crafting adds a `RecipeCache` loaded from DB and a server handler. All three use the same protocol patterns (CmdXxx → server validates → SvXxxResult) and WAL logging.

**Tech Stack:** C++23, doctest, PostgreSQL (libpqxx), custom UDP networking.

**IMPORTANT BUILD NOTE:** Before building, touch ALL edited `.cpp` files — CMake misses changes silently. After server changes, restart FateServer.exe.

**Spec:** `Docs/superpowers/specs/2026-03-21-batch-b-equipment-economy-design.md`

---

### Task 1: Rewrite Enchant System (Shared Logic + Tests)

**Files:**
- Modify: `game/shared/enchant_system.h`
- Modify: `game/shared/game_types.h` — Update `MAX_ENCHANT_LEVEL` to 15, add `SAFE_ENCHANT_LEVEL = 8`
- Create: `tests/test_enchant_system.cpp`

- [ ] **Step 1: Write enchant system tests**

Create `tests/test_enchant_system.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/shared/enchant_system.h"

using namespace fate;

TEST_SUITE("EnchantSystem") {

TEST_CASE("Safe levels +1 to +8 have 100% success rate") {
    for (int level = 1; level <= 8; ++level) {
        CHECK(EnchantSystem::getSuccessRate(level) == 100);
    }
}

TEST_CASE("Risky levels have correct rates") {
    CHECK(EnchantSystem::getSuccessRate(9) == 50);
    CHECK(EnchantSystem::getSuccessRate(10) == 40);
    CHECK(EnchantSystem::getSuccessRate(11) == 30);
    CHECK(EnchantSystem::getSuccessRate(12) == 20);
    CHECK(EnchantSystem::getSuccessRate(13) == 10);
    CHECK(EnchantSystem::getSuccessRate(14) == 5);
    CHECK(EnchantSystem::getSuccessRate(15) == 2);
}

TEST_CASE("No break risk at or below safe threshold") {
    for (int level = 1; level <= 8; ++level) {
        CHECK_FALSE(EnchantSystem::hasBreakRisk(level));
    }
}

TEST_CASE("Break risk above safe threshold") {
    for (int level = 9; level <= 15; ++level) {
        CHECK(EnchantSystem::hasBreakRisk(level));
    }
}

TEST_CASE("MAX_ENCHANT_LEVEL is 15") {
    CHECK(EnchantConstants::MAX_ENCHANT_LEVEL == 15);
}

TEST_CASE("SAFE_ENCHANT_LEVEL is 8") {
    CHECK(EnchantConstants::SAFE_ENCHANT_LEVEL == 8);
}

TEST_CASE("Gold costs for safe levels") {
    CHECK(EnchantSystem::getGoldCost(1) == 100);
    CHECK(EnchantSystem::getGoldCost(4) == 500);
    CHECK(EnchantSystem::getGoldCost(7) == 2000);
    CHECK(EnchantSystem::getGoldCost(8) == 2000);
}

TEST_CASE("Gold costs for risky levels") {
    CHECK(EnchantSystem::getGoldCost(9) == 10000);
    CHECK(EnchantSystem::getGoldCost(12) == 100000);
    CHECK(EnchantSystem::getGoldCost(15) == 2000000);
}

TEST_CASE("Weapon damage multiplier at +15") {
    float mult = EnchantSystem::getWeaponDamageMultiplier(15);
    // 2.875 base * 1.05 (+11) * 1.10 (+12) * 1.30 (+12 max dmg) * 1.15 (+15)
    CHECK(mult == doctest::Approx(4.97f).epsilon(0.05f));
}

TEST_CASE("Armor bonus at +15 is 29") {
    CHECK(EnchantSystem::getArmorBonus(15) == 29);
}

TEST_CASE("Armor bonus at +8 is 8") {
    CHECK(EnchantSystem::getArmorBonus(8) == 8);
}

TEST_CASE("Secret bonus at +15 is doubled") {
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Hat, 15) == 20);
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Shoes, 15) == 20);
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Armor, 15) == 20);
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::SubWeapon, 15) == 20);
}

TEST_CASE("Secret bonus at +12 is 10") {
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Hat, 12) == 10);
}

TEST_CASE("No secret bonus for Gloves") {
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Gloves, 15) == 0);
}

TEST_CASE("Repair returns level between 1 and SAFE_ENCHANT_LEVEL") {
    for (int i = 0; i < 100; ++i) {
        int repaired = EnchantSystem::rollRepairLevel();
        CHECK(repaired >= 1);
        CHECK(repaired <= EnchantConstants::SAFE_ENCHANT_LEVEL);
    }
}

} // TEST_SUITE
```

- [ ] **Step 2: Update game_types.h constants**

In `game/shared/game_types.h`, update the `EnchantConstants` namespace:

```cpp
namespace EnchantConstants {
    constexpr int MAX_ENCHANT_LEVEL = 15;
    constexpr int SAFE_ENCHANT_LEVEL = 8;
}
```

- [ ] **Step 3: Rewrite enchant_system.h**

Update `game/shared/enchant_system.h`:

- Extend `SUCCESS_RATES` array to 16 entries (index 0-15):
  ```cpp
  static constexpr std::array<int, 16> SUCCESS_RATES = {
      0,                                              // [0] unused
      100, 100, 100, 100, 100, 100, 100, 100,        // +1 to +8 (safe)
      50, 40, 30, 20, 10, 5, 2                        // +9 to +15 (risky)
  };
  ```

- Extend `GOLD_COSTS` array to 16 entries:
  ```cpp
  static constexpr std::array<int, 16> GOLD_COSTS = {
      0,                                              // [0] unused
      100, 100, 100, 500, 500, 500, 2000, 2000,      // +1 to +8
      10000, 25000, 50000, 100000, 500000, 1000000, 2000000  // +9 to +15
  };
  ```

- Add `hasBreakRisk(int targetLevel)`:
  ```cpp
  static bool hasBreakRisk(int targetLevel) {
      return targetLevel > EnchantConstants::SAFE_ENCHANT_LEVEL;
  }
  ```

- Add `rollRepairLevel()`:
  ```cpp
  static int rollRepairLevel() {
      static thread_local std::mt19937 rng(std::random_device{}());
      std::uniform_int_distribution<int> dist(1, EnchantConstants::SAFE_ENCHANT_LEVEL);
      return dist(rng);
  }
  ```

- Update `getWeaponDamageMultiplier()` to add +15 secret (×1.15) and keep +12 max damage bonus (×1.30):
  ```cpp
  static float getWeaponDamageMultiplier(int enchantLevel) {
      if (enchantLevel <= 0) return 1.0f;
      float mult = 1.0f + (enchantLevel * 0.125f);
      if (enchantLevel >= 11) mult *= 1.05f;
      if (enchantLevel >= 12) mult *= 1.10f;
      if (enchantLevel >= 12) mult *= 1.30f; // +12 max damage bonus
      if (enchantLevel >= 15) mult *= 1.15f; // +15 secret
      return mult;
  }
  ```

- Update `getSecretBonusValue()` to accept enchant level and return doubled value at +15:
  ```cpp
  static int getSecretBonusValue(EquipmentSlot slot, int enchantLevel = 12) {
      if (enchantLevel < 12) return 0;
      switch (slot) {
          case EquipmentSlot::Hat:
          case EquipmentSlot::Shoes:
          case EquipmentSlot::Armor:
          case EquipmentSlot::SubWeapon:
              return (enchantLevel >= 15) ? 20 : 10;
          default: return 0;
      }
  }
  ```

- Update `getArmorBonus()` — already correct formula, just verify it works for levels 13-15.

- [ ] **Step 4: Build and run tests**

```bash
touch game/shared/enchant_system.h game/shared/game_types.h
cmake --build build --target fate_tests && ./build/Debug/fate_tests -tc="EnchantSystem"
```

Expected: All enchant tests pass.

- [ ] **Step 5: Run full test suite**

```bash
./build/Debug/fate_tests
```

Expected: All tests pass. Some existing tests may reference old `RISKY_ENCHANT_START` — update if needed.

- [ ] **Step 6: Commit**

```bash
git add game/shared/enchant_system.h game/shared/game_types.h tests/test_enchant_system.cpp
git commit -m "feat: rewrite enchant system for +1 to +15 with break mechanic"
```

---

### Task 2: Add `isBroken` Field to Item Pipeline

**Files:**
- Modify: `game/shared/item_instance.h` — Add `isBroken` field
- Modify: `server/db/inventory_repository.h` — Add `is_broken` to `InventorySlotRecord`
- Modify: `server/db/inventory_repository.cpp` — Add to SELECT/INSERT queries
- Modify: `engine/net/protocol.h` — Add `isBroken` to `InventorySyncSlot` and `InventorySyncEquip`

- [ ] **Step 1: Add `isBroken` to `ItemInstance`**

In `game/shared/item_instance.h`, add after `bool isSoulbound = false;`:

```cpp
    bool isBroken = false;
```

Update `canTrade()` to include broken check:
```cpp
    [[nodiscard]] bool canTrade() const {
        return isValid() && !isBound() && !isBroken;
    }
```

Update `isMaxEnchant()` to use new constant (already references `EnchantConstants::MAX_ENCHANT_LEVEL`).

- [ ] **Step 2: Add `is_broken` to `InventorySlotRecord`**

In `server/db/inventory_repository.h`, add to the `InventorySlotRecord` struct after `is_soulbound`:

```cpp
    bool is_broken = false;
```

- [ ] **Step 3: Update inventory_repository.cpp queries**

In `server/db/inventory_repository.cpp`:
- `loadInventory()` SELECT query: add `is_broken` to the selected columns. Parse it into `record.is_broken`.
- `saveInventory()` INSERT query: add `is_broken` column and bind `slot.is_broken`.
- Where `ItemInstance` is populated from `InventorySlotRecord`: set `item.isBroken = record.is_broken`.
- Where `InventorySlotRecord` is populated from `ItemInstance`: set `record.is_broken = item.isBroken`.

- [ ] **Step 4: Add `isBroken` to inventory sync structs**

In `engine/net/protocol.h`, add `uint8_t isBroken = 0;` to both `InventorySyncSlot` and `InventorySyncEquip` structs. Update their `write()` and `read()` methods to serialize this field (add `w.writeU8(isBroken)` / `m.isBroken = r.readU8()` after existing fields).

- [ ] **Step 5: Build and run tests**

```bash
touch game/shared/item_instance.h server/db/inventory_repository.cpp engine/net/protocol.h
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

Expected: All tests pass. If existing inventory sync serialization tests fail due to new field, update them.

- [ ] **Step 6: Commit**

```bash
git add game/shared/item_instance.h server/db/inventory_repository.h server/db/inventory_repository.cpp engine/net/protocol.h
git commit -m "feat: add isBroken field to item pipeline (instance, DB, sync)"
```

---

### Task 3: Enchant Protocol Messages + Tests

**Files:**
- Modify: `engine/net/packet.h` — Add packet types
- Modify: `engine/net/protocol.h` or `engine/net/game_messages.h` — Add message structs
- Modify: `tests/test_protocol.cpp` — Serialization tests

- [ ] **Step 1: Write serialization tests**

Add to `tests/test_protocol.cpp`:

```cpp
TEST_CASE("CmdEnchantMsg serialization round-trip") {
    CmdEnchantMsg original;
    original.inventorySlot = 5;
    original.useProtectionStone = 1;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = CmdEnchantMsg::read(r);

    CHECK(decoded.inventorySlot == 5);
    CHECK(decoded.useProtectionStone == 1);
}

TEST_CASE("SvEnchantResultMsg serialization round-trip") {
    SvEnchantResultMsg original;
    original.success = 1;
    original.newLevel = 9;
    original.broke = 0;
    original.message = "Enchant succeeded! +9";

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvEnchantResultMsg::read(r);

    CHECK(decoded.success == 1);
    CHECK(decoded.newLevel == 9);
    CHECK(decoded.broke == 0);
    CHECK(decoded.message == "Enchant succeeded! +9");
}

TEST_CASE("CmdRepairMsg serialization round-trip") {
    CmdRepairMsg original;
    original.inventorySlot = 3;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = CmdRepairMsg::read(r);

    CHECK(decoded.inventorySlot == 3);
}

TEST_CASE("SvRepairResultMsg serialization round-trip") {
    SvRepairResultMsg original;
    original.success = 1;
    original.newLevel = 4;
    original.message = "Item repaired to +4";

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvRepairResultMsg::read(r);

    CHECK(decoded.success == 1);
    CHECK(decoded.newLevel == 4);
    CHECK(decoded.message == "Item repaired to +4");
}
```

- [ ] **Step 2: Add packet type constants**

In `engine/net/packet.h`:

```cpp
    // Client -> Server (after CmdEquip = 0x1D)
    constexpr uint8_t CmdEnchant      = 0x1E;
    constexpr uint8_t CmdRepair       = 0x1F;
    constexpr uint8_t CmdExtractCore  = 0x20;
    constexpr uint8_t CmdCraft        = 0x21;

    // Server -> Client (after SvBossLootOwner = 0xA7)
    constexpr uint8_t SvEnchantResult  = 0xA8;
    constexpr uint8_t SvRepairResult   = 0xA9;
    constexpr uint8_t SvExtractResult  = 0xAA;
    constexpr uint8_t SvCraftResult    = 0xAB;
```

- [ ] **Step 3: Add message structs**

In `engine/net/game_messages.h` (or protocol.h, follow existing pattern):

```cpp
struct CmdEnchantMsg {
    uint8_t inventorySlot = 0;
    uint8_t useProtectionStone = 0;

    void write(ByteWriter& w) const {
        w.writeU8(inventorySlot);
        w.writeU8(useProtectionStone);
    }
    static CmdEnchantMsg read(ByteReader& r) {
        CmdEnchantMsg m;
        m.inventorySlot = r.readU8();
        m.useProtectionStone = r.readU8();
        return m;
    }
};

struct SvEnchantResultMsg {
    uint8_t success = 0;
    uint8_t newLevel = 0;
    uint8_t broke = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeU8(newLevel);
        w.writeU8(broke);
        w.writeString(message);
    }
    static SvEnchantResultMsg read(ByteReader& r) {
        SvEnchantResultMsg m;
        m.success = r.readU8();
        m.newLevel = r.readU8();
        m.broke = r.readU8();
        m.message = r.readString();
        return m;
    }
};

struct CmdRepairMsg {
    uint8_t inventorySlot = 0;

    void write(ByteWriter& w) const { w.writeU8(inventorySlot); }
    static CmdRepairMsg read(ByteReader& r) {
        CmdRepairMsg m;
        m.inventorySlot = r.readU8();
        return m;
    }
};

struct SvRepairResultMsg {
    uint8_t success = 0;
    uint8_t newLevel = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeU8(newLevel);
        w.writeString(message);
    }
    static SvRepairResultMsg read(ByteReader& r) {
        SvRepairResultMsg m;
        m.success = r.readU8();
        m.newLevel = r.readU8();
        m.message = r.readString();
        return m;
    }
};
```

- [ ] **Step 4: Build and run tests**

```bash
touch engine/net/packet.h engine/net/protocol.h engine/net/game_messages.h tests/test_protocol.cpp
cmake --build build --target fate_tests && ./build/Debug/fate_tests -tc="*Enchant*" -tc="*Repair*"
```

- [ ] **Step 5: Commit**

```bash
git add engine/net/packet.h engine/net/game_messages.h tests/test_protocol.cpp
git commit -m "feat: add enchant and repair protocol messages"
```

---

### Task 4: Server Enchant + Repair Handlers

**Files:**
- Modify: `server/server_app.h` — Declare `processEnchant()`, `processRepair()`
- Modify: `server/server_app.cpp` — Add handlers and packet dispatch

- [ ] **Step 1: Add handler declarations to server_app.h**

```cpp
void processEnchant(uint16_t clientId, const CmdEnchantMsg& msg);
void processRepair(uint16_t clientId, const CmdRepairMsg& msg);
```

- [ ] **Step 2: Add packet dispatch in onPacketReceived switch**

In `server/server_app.cpp`, in the packet switch, add:

```cpp
case PacketType::CmdEnchant: {
    auto msg = CmdEnchantMsg::read(payload);
    processEnchant(clientId, msg);
    break;
}
case PacketType::CmdRepair: {
    auto msg = CmdRepairMsg::read(payload);
    processRepair(clientId, msg);
    break;
}
```

- [ ] **Step 3: Implement processEnchant()**

Follow the `processEquip()` pattern. Full flow:

```cpp
void ServerApp::processEnchant(uint16_t clientId, const CmdEnchantMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* player = world_.getEntity(h);
    if (!player) return;

    auto* charStats = player->getComponent<CharacterStatsComponent>();
    auto* inv = player->getComponent<InventoryComponent>();
    if (!charStats || !inv) return;

    auto sendResult = [&](bool success, uint8_t newLevel, bool broke, const std::string& message) {
        SvEnchantResultMsg result;
        result.success = success ? 1 : 0;
        result.newLevel = newLevel;
        result.broke = broke ? 1 : 0;
        result.message = message;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        result.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvEnchantResult, buf, w.size());
    };

    // 1. Validate item in inventory slot
    auto item = inv->inventory.getSlot(msg.inventorySlot);
    if (!item.isValid()) { sendResult(false, 0, false, "Invalid item"); return; }
    if (item.isBroken) { sendResult(false, 0, false, "Item is broken"); return; }

    // 2. Look up item definition
    auto* itemDef = itemDefCache_.getDefinition(item.itemId);
    if (!itemDef) { sendResult(false, 0, false, "Unknown item"); return; }
    if (!itemDef->isWeapon() && !itemDef->isArmor()) {
        sendResult(false, 0, false, "Item cannot be enchanted"); return;
    }
    if (itemDef->isAccessory()) {
        sendResult(false, 0, false, "Use stat enchant for accessories"); return;
    }

    int maxLevel = (std::min)(itemDef->maxEnchant, EnchantConstants::MAX_ENCHANT_LEVEL);
    int targetLevel = item.enchantLevel + 1;
    if (targetLevel > maxLevel) {
        sendResult(false, item.enchantLevel, false, "Already at max enchant"); return;
    }

    // 3. Validate enhancement stone
    std::string stoneId = EnchantSystem::getRequiredStone(itemDef->levelReq);
    int stoneSlot = inv->inventory.findItemById(stoneId);
    if (stoneSlot < 0) {
        sendResult(false, item.enchantLevel, false, "Missing enhancement stone"); return;
    }

    // 4. Validate gold
    int goldCost = EnchantSystem::getGoldCost(targetLevel);
    if (inv->inventory.getGold() < goldCost) {
        sendResult(false, item.enchantLevel, false, "Not enough gold"); return;
    }

    // 5. Validate protection stone if requested
    int protSlot = -1;
    if (msg.useProtectionStone) {
        protSlot = inv->inventory.findItemById("mat_protect_stone");
        if (protSlot < 0) {
            sendResult(false, item.enchantLevel, false, "No protection stone"); return;
        }
    }

    // 6. WAL + consume resources
    wal_.appendGoldChange(client->character_id, -goldCost);
    inv->inventory.setGold(inv->inventory.getGold() - goldCost);
    inv->inventory.removeItemQuantity(stoneSlot, 1);
    if (protSlot >= 0) {
        inv->inventory.removeItemQuantity(protSlot, 1);
    }

    // 7. Roll success
    bool success = EnchantSystem::rollSuccess(targetLevel);

    if (success) {
        item.enchantLevel = targetLevel;
        // Update item in inventory slot
        // (implementer: find the correct method to update the item in-place)
        sendResult(true, targetLevel, false, "Enchant succeeded! +" + std::to_string(targetLevel));
    } else if (EnchantSystem::hasBreakRisk(targetLevel)) {
        if (msg.useProtectionStone) {
            // Protected: item stays
            sendResult(false, item.enchantLevel, false, "Enchant failed. Protection stone saved your item.");
        } else {
            // Item breaks
            item.isBroken = true;
            item.isSoulbound = true;
            // Update item in inventory slot
            sendResult(false, item.enchantLevel, true, "Enchant failed! Item has broken.");
        }
    }

    recalcEquipmentBonuses(player);
    sendPlayerState(clientId);
    sendInventorySync(clientId);
}
```

Note: The implementer needs to find the correct method to update an `ItemInstance` in-place within the inventory. Check `inventory.h` for a `setSlot()` or `updateSlot()` method. If none exists, the implementer may need to `removeItem(slot)` then `addItemToSlot(slot, updatedItem)`.

- [ ] **Step 4: Implement processRepair()**

Similar pattern but simpler:

```cpp
void ServerApp::processRepair(uint16_t clientId, const CmdRepairMsg& msg) {
    // Same entity/component boilerplate as processEnchant

    auto sendResult = [&](bool success, uint8_t newLevel, const std::string& message) {
        SvRepairResultMsg result;
        result.success = success ? 1 : 0;
        result.newLevel = newLevel;
        result.message = message;
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        result.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvRepairResult, buf, w.size());
    };

    // Validate: item is broken
    auto item = inv->inventory.getSlot(msg.inventorySlot);
    if (!item.isValid() || !item.isBroken) {
        sendResult(false, 0, "Item is not broken"); return;
    }

    // Validate: has repair scroll
    int scrollSlot = inv->inventory.findItemById("item_repair_scroll");
    if (scrollSlot < 0) { sendResult(false, 0, "Missing repair scroll"); return; }

    // Validate: has 50k gold
    constexpr int REPAIR_COST = 50000;
    if (inv->inventory.getGold() < REPAIR_COST) {
        sendResult(false, 0, "Not enough gold"); return;
    }

    // WAL + consume
    wal_.appendGoldChange(client->character_id, -REPAIR_COST);
    inv->inventory.setGold(inv->inventory.getGold() - REPAIR_COST);
    inv->inventory.removeItemQuantity(scrollSlot, 1);

    // Repair: random level +1 to +8
    int repairedLevel = EnchantSystem::rollRepairLevel();
    item.isBroken = false;
    item.enchantLevel = repairedLevel;
    // item remains soulbound
    // Update item in inventory slot

    sendResult(true, repairedLevel, "Item repaired to +" + std::to_string(repairedLevel));
    sendInventorySync(clientId);
}
```

- [ ] **Step 5: Build and run tests**

```bash
touch server/server_app.cpp server/server_app.h
cmake --build build --target FateServer && cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 6: Commit**

```bash
git add server/server_app.cpp server/server_app.h
git commit -m "feat: add server enchant and repair handlers with break mechanic"
```

---

### Task 5: Client Enchant/Repair Callbacks

**Files:**
- Modify: `engine/net/net_client.h` — Add callbacks
- Modify: `engine/net/net_client.cpp` — Handle packets

- [ ] **Step 1: Add callbacks and packet handling**

In `engine/net/net_client.h`:
```cpp
std::function<void(const SvEnchantResultMsg&)> onEnchantResult;
std::function<void(const SvRepairResultMsg&)> onRepairResult;
```

In `engine/net/net_client.cpp`, add cases:
```cpp
case PacketType::SvEnchantResult: {
    ByteReader payload(data + r.position(), hdr.payloadSize);
    auto msg = SvEnchantResultMsg::read(payload);
    if (onEnchantResult) onEnchantResult(msg);
    break;
}
case PacketType::SvRepairResult: {
    ByteReader payload(data + r.position(), hdr.payloadSize);
    auto msg = SvRepairResultMsg::read(payload);
    if (onRepairResult) onRepairResult(msg);
    break;
}
```

- [ ] **Step 2: Build**

```bash
touch engine/net/net_client.cpp engine/net/net_client.h
cmake --build build --target FateEngine
```

- [ ] **Step 3: Commit**

```bash
git add engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: client callbacks for enchant and repair results"
```

---

### Task 6: Core Extraction Shared Logic + Tests

**Files:**
- Create: `game/shared/core_extraction.h`
- Create: `tests/test_core_extraction.cpp`

- [ ] **Step 1: Write core extraction tests**

```cpp
#include <doctest/doctest.h>
#include "game/shared/core_extraction.h"

using namespace fate;

TEST_SUITE("CoreExtraction") {

TEST_CASE("Common items cannot be extracted") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Common, 5, 0);
    CHECK_FALSE(result.success);
}

TEST_CASE("Green Lv5 yields 1st Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 0);
    CHECK(result.success);
    CHECK(result.coreItemId == "mat_core_1st");
    CHECK(result.quantity == 1);
}

TEST_CASE("Green Lv15 yields 2nd Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 15, 0);
    CHECK(result.coreItemId == "mat_core_2nd");
}

TEST_CASE("Green Lv25 yields 3rd Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 25, 0);
    CHECK(result.coreItemId == "mat_core_3rd");
}

TEST_CASE("Green Lv35 yields 4th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 35, 0);
    CHECK(result.coreItemId == "mat_core_4th");
}

TEST_CASE("Green Lv45 yields 5th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 45, 0);
    CHECK(result.coreItemId == "mat_core_5th");
}

TEST_CASE("Blue item yields 6th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Rare, 10, 0);
    CHECK(result.coreItemId == "mat_core_6th");
}

TEST_CASE("Purple item yields 7th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Epic, 10, 0);
    CHECK(result.coreItemId == "mat_core_7th");
}

TEST_CASE("Legendary item yields 7th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Legendary, 10, 0);
    CHECK(result.coreItemId == "mat_core_7th");
}

TEST_CASE("Enchant +9 yields bonus cores (1 + 9/3 = 4)") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 9);
    CHECK(result.quantity == 4);
}

TEST_CASE("Enchant +3 yields 2 cores (1 + 3/3 = 2)") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 3);
    CHECK(result.quantity == 2);
}

TEST_CASE("Enchant +1 yields 1 core (1 + 1/3 = 1)") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 1);
    CHECK(result.quantity == 1);
}

} // TEST_SUITE
```

- [ ] **Step 2: Implement core_extraction.h**

```cpp
#pragma once
#include "game/shared/game_types.h"
#include <string>

namespace fate {

struct CoreExtractionResult {
    bool success = false;
    std::string coreItemId;
    int quantity = 0;
};

struct CoreExtraction {
    static CoreExtractionResult determineCoreResult(ItemRarity rarity, int itemLevel, int enchantLevel) {
        CoreExtractionResult result;

        if (rarity == ItemRarity::Common) return result; // cannot extract

        result.success = true;

        // Determine core tier
        if (rarity == ItemRarity::Uncommon) {
            if (itemLevel < 10)       result.coreItemId = "mat_core_1st";
            else if (itemLevel < 20)  result.coreItemId = "mat_core_2nd";
            else if (itemLevel < 30)  result.coreItemId = "mat_core_3rd";
            else if (itemLevel < 40)  result.coreItemId = "mat_core_4th";
            else                      result.coreItemId = "mat_core_5th";
        } else if (rarity == ItemRarity::Rare) {
            result.coreItemId = "mat_core_6th";
        } else {
            // Epic, Legendary, Unique → 7th core
            result.coreItemId = "mat_core_7th";
        }

        // Base 1 + bonus from enchant level
        result.quantity = 1 + (enchantLevel / 3);

        return result;
    }
};

} // namespace fate
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests -tc="CoreExtraction"
```

- [ ] **Step 4: Commit**

```bash
git add game/shared/core_extraction.h tests/test_core_extraction.cpp
git commit -m "feat: add core extraction shared logic with tier determination"
```

---

### Task 7: Core Extraction Protocol + Server Handler + Client

**Files:**
- Modify: `engine/net/game_messages.h` — Add CmdExtractCoreMsg, SvExtractResultMsg
- Modify: `server/server_app.h` — Declare processExtractCore()
- Modify: `server/server_app.cpp` — Add handler
- Modify: `engine/net/net_client.h/cpp` — Add callback
- Modify: `tests/test_protocol.cpp` — Serialization tests

- [ ] **Step 1: Add protocol message structs + tests**

Messages follow the same ByteWriter/ByteReader pattern:

```cpp
struct CmdExtractCoreMsg {
    uint8_t itemSlot = 0;
    uint8_t scrollSlot = 0;
    // write/read methods
};

struct SvExtractResultMsg {
    uint8_t success = 0;
    std::string coreItemId;
    uint8_t coreQuantity = 0;
    std::string message;
    // write/read methods
};
```

Add serialization round-trip tests to `tests/test_protocol.cpp`.

- [ ] **Step 2: Add server handler**

`processExtractCore()` follows the same pattern as `processEnchant()`:
1. Validate item (exists, not Common, not broken, not equipped)
2. Validate scroll (exists, is `item_extraction_scroll`)
3. Validate inventory space for cores
4. Call `CoreExtraction::determineCoreResult(item.rarity, itemDef->levelReq, item.enchantLevel)`
5. WAL log, remove item, remove scroll, add cores
6. Send `SvExtractResult` + `SvInventorySync`

- [ ] **Step 3: Add client callback**

Same pattern as enchant: `onExtractResult` callback in `net_client.h/cpp`.

- [ ] **Step 4: Build and run full test suite**

```bash
touch server/server_app.cpp engine/net/net_client.cpp
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 5: Commit**

```bash
git add engine/net/game_messages.h server/server_app.cpp server/server_app.h engine/net/net_client.h engine/net/net_client.cpp tests/test_protocol.cpp
git commit -m "feat: add core extraction server handler and client callback"
```

---

### Task 8: Recipe Cache + Tests

**Files:**
- Create: `server/cache/recipe_cache.h`
- Create: `tests/test_recipe_cache.cpp`

- [ ] **Step 1: Write recipe cache tests**

```cpp
#include <doctest/doctest.h>
#include "server/cache/recipe_cache.h"

using namespace fate;

TEST_SUITE("RecipeCache") {

TEST_CASE("Empty cache returns nullptr for unknown recipe") {
    RecipeCache cache;
    CHECK(cache.getRecipe("nonexistent") == nullptr);
}

TEST_CASE("Can add and retrieve a recipe") {
    RecipeCache cache;
    CachedRecipe recipe;
    recipe.recipeId = "test_recipe";
    recipe.recipeName = "Test Sword";
    recipe.bookTier = 1;
    recipe.resultItemId = "item_test_sword";
    recipe.resultQuantity = 1;
    recipe.levelReq = 5;
    recipe.goldCost = 1000;
    recipe.ingredients.push_back({"mat_core_2nd", 3});

    cache.addRecipe(recipe);

    auto* found = cache.getRecipe("test_recipe");
    REQUIRE(found != nullptr);
    CHECK(found->recipeName == "Test Sword");
    CHECK(found->bookTier == 1);
    CHECK(found->ingredients.size() == 1);
    CHECK(found->ingredients[0].itemId == "mat_core_2nd");
    CHECK(found->ingredients[0].quantity == 3);
}

TEST_CASE("getRecipesForTier returns correct recipes") {
    RecipeCache cache;
    CachedRecipe r1; r1.recipeId = "r1"; r1.bookTier = 0;
    CachedRecipe r2; r2.recipeId = "r2"; r2.bookTier = 1;
    CachedRecipe r3; r3.recipeId = "r3"; r3.bookTier = 1;
    cache.addRecipe(r1);
    cache.addRecipe(r2);
    cache.addRecipe(r3);

    auto tier0 = cache.getRecipesForTier(0);
    CHECK(tier0.size() == 1);

    auto tier1 = cache.getRecipesForTier(1);
    CHECK(tier1.size() == 2);
}

} // TEST_SUITE
```

- [ ] **Step 2: Implement recipe_cache.h**

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace fate {

struct RecipeIngredient {
    std::string itemId;
    int quantity = 1;
};

struct CachedRecipe {
    std::string recipeId;
    std::string recipeName;
    int bookTier = 0;
    std::string resultItemId;
    int resultQuantity = 1;
    int levelReq = 1;
    std::string classReq; // empty = any class
    int goldCost = 0;
    std::vector<RecipeIngredient> ingredients;
};

class RecipeCache {
public:
    void addRecipe(const CachedRecipe& recipe) {
        recipes_[recipe.recipeId] = recipe;
    }

    const CachedRecipe* getRecipe(const std::string& recipeId) const {
        auto it = recipes_.find(recipeId);
        return it != recipes_.end() ? &it->second : nullptr;
    }

    std::vector<const CachedRecipe*> getRecipesForTier(int tier) const {
        std::vector<const CachedRecipe*> result;
        for (const auto& [id, recipe] : recipes_) {
            if (recipe.bookTier == tier) result.push_back(&recipe);
        }
        return result;
    }

    // Called at startup to load from DB
    // bool loadFromDatabase(pqxx::connection& conn);

private:
    std::unordered_map<std::string, CachedRecipe> recipes_;
};

} // namespace fate
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests -tc="RecipeCache"
```

- [ ] **Step 4: Commit**

```bash
git add server/cache/recipe_cache.h tests/test_recipe_cache.cpp
git commit -m "feat: add RecipeCache with tier-based recipe lookup"
```

---

### Task 9: Recipe Cache DB Loading

**Files:**
- Modify: `server/cache/recipe_cache.h` — Implement `loadFromDatabase()`
- Modify: `server/server_app.cpp` — Initialize RecipeCache at startup
- Modify: `server/server_app.h` — Add `RecipeCache recipeCache_` member

- [ ] **Step 1: Implement loadFromDatabase()**

In `recipe_cache.h`, implement the DB loading method following the pattern of `ItemDefinitionCache::loadFromDatabase()`:

```cpp
bool loadFromDatabase(pqxx::connection& conn) {
    pqxx::work txn(conn);

    // Load recipes
    auto recipes = txn.exec("SELECT recipe_id, recipe_name, book_tier, result_item_id, "
                            "result_quantity, level_req, class_req, gold_cost FROM crafting_recipes");
    for (const auto& row : recipes) {
        CachedRecipe recipe;
        recipe.recipeId = row["recipe_id"].as<std::string>();
        recipe.recipeName = row["recipe_name"].as<std::string>();
        recipe.bookTier = row["book_tier"].as<int>(0);
        recipe.resultItemId = row["result_item_id"].as<std::string>();
        recipe.resultQuantity = row["result_quantity"].as<int>(1);
        recipe.levelReq = row["level_req"].as<int>(1);
        recipe.classReq = row["class_req"].is_null() ? "" : row["class_req"].as<std::string>();
        recipe.goldCost = row["gold_cost"].as<int>(0);
        addRecipe(recipe);
    }

    // Load ingredients
    auto ingredients = txn.exec("SELECT recipe_id, item_id, quantity FROM crafting_ingredients");
    for (const auto& row : ingredients) {
        std::string recipeId = row["recipe_id"].as<std::string>();
        auto it = recipes_.find(recipeId);
        if (it != recipes_.end()) {
            RecipeIngredient ing;
            ing.itemId = row["item_id"].as<std::string>();
            ing.quantity = row["quantity"].as<int>(1);
            it->second.ingredients.push_back(ing);
        }
    }

    txn.commit();
    return true;
}
```

Note: The `book_tier` column requires the DB migration `ALTER TABLE crafting_recipes ADD COLUMN book_tier INTEGER DEFAULT 0` to be run first.

- [ ] **Step 2: Add RecipeCache to ServerApp and initialize at startup**

Add `RecipeCache recipeCache_` member to `server_app.h`. In the server startup (where other caches like `itemDefCache_` are loaded), add:

```cpp
recipeCache_.loadFromDatabase(*gameDbConn_);
LOG_INFO("Server", "Loaded %d crafting recipes", recipeCache_.size());
```

Add a `size()` method to RecipeCache: `size_t size() const { return recipes_.size(); }`

- [ ] **Step 3: Build**

```bash
touch server/server_app.cpp server/server_app.h
cmake --build build --target FateServer
```

- [ ] **Step 4: Commit**

```bash
git add server/cache/recipe_cache.h server/server_app.cpp server/server_app.h
git commit -m "feat: load crafting recipes from database at server startup"
```

---

### Task 10: Crafting Protocol + Server Handler + Client

**Files:**
- Modify: `engine/net/game_messages.h` — Add CmdCraftMsg, SvCraftResultMsg
- Modify: `server/server_app.h` — Declare processCraft()
- Modify: `server/server_app.cpp` — Add handler
- Modify: `engine/net/net_client.h/cpp` — Add callback
- Modify: `tests/test_protocol.cpp` — Serialization tests

- [ ] **Step 1: Add message structs + tests**

```cpp
struct CmdCraftMsg {
    uint16_t recipeId = 0; // Note: recipeId is VARCHAR in DB, consider using string
    // Actually, re-check: DB has recipe_id VARCHAR(64). Use std::string.
    std::string recipeId;
    // write/read methods using writeString/readString
};

struct SvCraftResultMsg {
    uint8_t success = 0;
    std::string resultItemId;
    uint8_t resultQuantity = 0;
    std::string message;
    // write/read methods
};
```

**IMPORTANT:** The spec says `uint16_t recipeId` but the DB uses `VARCHAR(64)` for recipe_id. Use `std::string` to match the DB schema and `RecipeCache` lookup.

- [ ] **Step 2: Add server handler**

`processCraft()` flow:
1. Look up recipe from `recipeCache_.getRecipe(msg.recipeId)`
2. Validate book tier: find any `item_combine_*` in inventory, check `bookTier >= recipe.bookTier`
3. Validate level: `charStats->stats.level >= recipe.levelReq`
4. Validate class: if `recipe.classReq` is set, compare to player's class
5. Validate ingredients: for each `recipe.ingredients`, `inv->inventory.countItem(ing.itemId) >= ing.quantity`
6. Validate gold: `inv->inventory.getGold() >= recipe.goldCost`
7. Validate inventory space
8. WAL log
9. Consume ingredients via `removeItemQuantity()` for each
10. Deduct gold via `setGold(currentGold - recipe.goldCost)`
11. Create result item (look up `itemDefCache_`, roll stats if equipment, set quantity)
12. Add to inventory
13. Send `SvCraftResult` + `SvInventorySync`

For book tier validation, define the Combine Book item IDs and their tiers:
```cpp
static int getCombineBookTier(const std::string& itemId) {
    if (itemId == "item_combine_novice") return 0;
    if (itemId == "item_combine_book_1") return 1;
    if (itemId == "item_combine_book_2") return 2;
    if (itemId == "item_combine_book_3") return 3;
    return -1; // not a combine book
}
```

Iterate inventory to find the highest tier book the player owns.

- [ ] **Step 3: Add client callback**

Same pattern: `onCraftResult` in `net_client.h/cpp`.

- [ ] **Step 4: Build and run full test suite**

```bash
touch server/server_app.cpp engine/net/net_client.cpp
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 5: Commit**

```bash
git add engine/net/game_messages.h server/server_app.cpp server/server_app.h engine/net/net_client.h engine/net/net_client.cpp tests/test_protocol.cpp
git commit -m "feat: add crafting system with recipe validation and server handler"
```

---

### Task 11: Final Integration Test

- [ ] **Step 1: Run full test suite**

```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

Expected: All tests pass.

- [ ] **Step 2: Build both targets**

```bash
cmake --build build --target FateServer && cmake --build build --target FateEngine
```

- [ ] **Step 3: Verify test count**

```bash
./build/Debug/fate_tests -c
```

Record final count.

- [ ] **Step 4: DB migration reminder**

The following SQL must be run on the dev database before testing:
```sql
ALTER TABLE character_inventory ADD COLUMN is_broken BOOLEAN DEFAULT FALSE;
ALTER TABLE crafting_recipes ADD COLUMN book_tier INTEGER DEFAULT 0;
```

**REMINDER:** Restart FateServer.exe after deploying server changes.
