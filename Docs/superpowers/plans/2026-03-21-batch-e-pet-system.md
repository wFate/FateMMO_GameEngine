# Batch E: Pet System Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the existing pet system into gameplay — stat bonuses, equip/unequip, XP sharing.

**Architecture:** Pet definitions hardcoded in a cache. Pet bonuses applied during equipment recalculation. Equip/unequip via new network commands backed by existing PetRepository. Pet XP shared from mob kills.

**Tech Stack:** C++23, doctest, existing PetRepository (libpqxx).

**Spec:** `Docs/superpowers/specs/2026-03-21-batch-e-pet-system-design.md`

---

### Task 1: Pet Definition Cache + Tests

**Files:**
- Create: `server/cache/pet_definition_cache.h`
- Create: `tests/test_pet_definition_cache.cpp`

- [ ] **Step 1: Create cache + tests**

Create `server/cache/pet_definition_cache.h`:
```cpp
#pragma once
#include "game/shared/pet_system.h"
#include <string>
#include <unordered_map>

namespace fate {

class PetDefinitionCache {
public:
    void addDefinition(const PetDefinition& def) {
        definitions_[def.petId] = def;
    }

    const PetDefinition* getDefinition(const std::string& petDefId) const {
        auto it = definitions_.find(petDefId);
        return it != definitions_.end() ? &it->second : nullptr;
    }

    size_t size() const { return definitions_.size(); }

private:
    std::unordered_map<std::string, PetDefinition> definitions_;
};

} // namespace fate
```

Create `tests/test_pet_definition_cache.cpp`:
```cpp
#include <doctest/doctest.h>
#include "server/cache/pet_definition_cache.h"

using namespace fate;

TEST_SUITE("PetDefinitionCache") {

TEST_CASE("Unknown pet returns nullptr") {
    PetDefinitionCache cache;
    CHECK(cache.getDefinition("nonexistent") == nullptr);
}

TEST_CASE("Can add and retrieve definition") {
    PetDefinitionCache cache;
    PetDefinition wolf;
    wolf.petId = "pet_wolf";
    wolf.displayName = "Wolf";
    wolf.baseHP = 10;
    wolf.baseCritRate = 0.01f;
    cache.addDefinition(wolf);

    auto* found = cache.getDefinition("pet_wolf");
    REQUIRE(found != nullptr);
    CHECK(found->displayName == "Wolf");
    CHECK(found->baseHP == 10);
}

TEST_CASE("Size tracks definitions") {
    PetDefinitionCache cache;
    CHECK(cache.size() == 0);
    PetDefinition p; p.petId = "pet_test";
    cache.addDefinition(p);
    CHECK(cache.size() == 1);
}

} // TEST_SUITE
```

- [ ] **Step 2: Build and test**
```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests --test-suite="PetDefinitionCache"
```

- [ ] **Step 3: Commit**
```bash
git add server/cache/pet_definition_cache.h tests/test_pet_definition_cache.cpp
git commit -m "feat: add PetDefinitionCache with hardcoded pet lookup"
```

---

### Task 2: Pet Protocol Messages + Tests

**Files:**
- Modify: `engine/net/packet.h` — Add CmdPet (0x24), SvPetUpdate (0xAE)
- Modify: `engine/net/game_messages.h` — Add message structs
- Modify: `tests/test_protocol.cpp` — Serialization tests

- [ ] **Step 1: Add packet types and message structs**

In `engine/net/packet.h`:
```cpp
    constexpr uint8_t CmdPet       = 0x24; // Client -> Server
    constexpr uint8_t SvPetUpdate  = 0xAE; // Server -> Client
```

In `engine/net/game_messages.h`:
```cpp
struct CmdPetMsg {
    uint8_t action = 0; // 0=Equip, 1=Unequip
    int32_t petDbId = 0; // character_pets.id

    void write(ByteWriter& w) const { w.writeU8(action); w.writeI32(petDbId); }
    static CmdPetMsg read(ByteReader& r) {
        CmdPetMsg m; m.action = r.readU8(); m.petDbId = r.readI32(); return m;
    }
};

struct SvPetUpdateMsg {
    uint8_t equipped = 0;
    std::string petDefId;
    std::string petName;
    uint8_t level = 0;
    int32_t currentXP = 0;
    int32_t xpToNextLevel = 0;

    void write(ByteWriter& w) const {
        w.writeU8(equipped);
        w.writeString(petDefId);
        w.writeString(petName);
        w.writeU8(level);
        w.writeI32(currentXP);
        w.writeI32(xpToNextLevel);
    }
    static SvPetUpdateMsg read(ByteReader& r) {
        SvPetUpdateMsg m;
        m.equipped = r.readU8();
        m.petDefId = r.readString();
        m.petName = r.readString();
        m.level = r.readU8();
        m.currentXP = r.readI32();
        m.xpToNextLevel = r.readI32();
        return m;
    }
};
```

- [ ] **Step 2: Add serialization tests**

Add to `tests/test_protocol.cpp`:
```cpp
TEST_CASE("CmdPetMsg round-trip") {
    CmdPetMsg orig; orig.action = 0; orig.petDbId = 42;
    uint8_t buf[16]; ByteWriter w(buf, sizeof(buf)); orig.write(w);
    ByteReader r(buf, w.size()); auto d = CmdPetMsg::read(r);
    CHECK(d.action == 0); CHECK(d.petDbId == 42);
}

TEST_CASE("SvPetUpdateMsg round-trip") {
    SvPetUpdateMsg orig;
    orig.equipped = 1; orig.petDefId = "pet_wolf"; orig.petName = "Fang";
    orig.level = 5; orig.currentXP = 1200; orig.xpToNextLevel = 1250;
    uint8_t buf[256]; ByteWriter w(buf, sizeof(buf)); orig.write(w);
    ByteReader r(buf, w.size()); auto d = SvPetUpdateMsg::read(r);
    CHECK(d.equipped == 1); CHECK(d.petDefId == "pet_wolf");
    CHECK(d.petName == "Fang"); CHECK(d.level == 5);
    CHECK(d.currentXP == 1200); CHECK(d.xpToNextLevel == 1250);
}
```

- [ ] **Step 3: Build and test**
```bash
touch engine/net/packet.h engine/net/game_messages.h tests/test_protocol.cpp
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 4: Commit**
```bash
git add engine/net/packet.h engine/net/game_messages.h tests/test_protocol.cpp
git commit -m "feat: add pet equip/unequip protocol messages"
```

---

### Task 3: Server Pet Handlers + Bonus Wiring + XP Sharing

**Files:**
- Modify: `server/server_app.h` — Add PetDefinitionCache member, declare handlers
- Modify: `server/server_app.cpp` — processEquipPet, processUnequipPet, wire bonuses into recalcEquipmentBonuses, XP sharing in mob kill, init pet defs
- Modify: `engine/net/net_client.h/cpp` — Add onPetUpdate callback

- [ ] **Step 1: Add members to ServerApp**

In `server/server_app.h`:
```cpp
#include "server/cache/pet_definition_cache.h"
PetDefinitionCache petDefCache_;
void processPetCommand(uint16_t clientId, const CmdPetMsg& msg);
void sendPetUpdate(uint16_t clientId, Entity* player);
```

- [ ] **Step 2: Initialize pet definitions at startup**

In server init (where other caches load):
```cpp
// Hardcoded pet definitions (DB table deferred)
{
    PetDefinition wolf;
    wolf.petId = "pet_wolf"; wolf.displayName = "Wolf";
    wolf.baseHP = 10; wolf.baseCritRate = 0.01f; wolf.baseExpBonus = 0.0f;
    wolf.hpPerLevel = 2.0f; wolf.critPerLevel = 0.002f; wolf.expBonusPerLevel = 0.0f;
    petDefCache_.addDefinition(wolf);

    PetDefinition hawk;
    hawk.petId = "pet_hawk"; hawk.displayName = "Hawk";
    hawk.baseHP = 5; hawk.baseCritRate = 0.02f; hawk.baseExpBonus = 0.05f;
    hawk.hpPerLevel = 1.0f; hawk.critPerLevel = 0.003f; hawk.expBonusPerLevel = 0.005f;
    petDefCache_.addDefinition(hawk);

    PetDefinition turtle;
    turtle.petId = "pet_turtle"; turtle.displayName = "Turtle";
    turtle.baseHP = 20; turtle.baseCritRate = 0.0f; turtle.baseExpBonus = 0.0f;
    turtle.hpPerLevel = 4.0f; turtle.critPerLevel = 0.0f; turtle.expBonusPerLevel = 0.0f;
    petDefCache_.addDefinition(turtle);

    LOG_INFO("Server", "Loaded %zu pet definitions", petDefCache_.size());
}
```

- [ ] **Step 3: Wire pet bonuses into recalcEquipmentBonuses()**

Find `recalcEquipmentBonuses()` in `server/server_app.cpp`. After equipment bonuses are applied and before `charStats->stats.recalculateStats()`, add:

```cpp
// Apply pet bonuses
auto* petComp = player->getComponent<PetComponent>();
if (petComp && petComp->hasPet()) {
    auto* petDef = petDefCache_.getDefinition(petComp->equippedPet.petDefinitionId);
    if (petDef) {
        int petLevel = petComp->equippedPet.level;
        charStats->stats.equipBonusHP += static_cast<int>(petDef->effectiveHP(petLevel));
        charStats->stats.equipBonusCritRate += petDef->effectiveCritRate(petLevel);
    }
}
```

Read `pet_system.h` to verify `effectiveHP()` and `effectiveCritRate()` method names and signatures. Also check the `PetComponent` include and `PetDefinition` field names.

- [ ] **Step 4: Add packet dispatch + implement processPetCommand()**

Add to packet switch:
```cpp
case PacketType::CmdPet: {
    auto msg = CmdPetMsg::read(payload);
    processPetCommand(clientId, msg);
    break;
}
```

Implement `processPetCommand()`:
- Action 0 (Equip): validate petDbId belongs to this character via `petRepo_->loadPets(charId)`, find matching pet, call `petRepo_->equipPet(charId, petDbId)`, load into PetComponent, recalcEquipmentBonuses, sendPlayerState, sendPetUpdate
- Action 1 (Unequip): call `petRepo_->unequipAllPets(charId)`, clear PetComponent, recalcEquipmentBonuses, sendPlayerState, sendPetUpdate

Implement `sendPetUpdate()`:
```cpp
void ServerApp::sendPetUpdate(uint16_t clientId, Entity* player) {
    auto* petComp = player->getComponent<PetComponent>();
    SvPetUpdateMsg msg;
    if (petComp && petComp->hasPet()) {
        msg.equipped = 1;
        msg.petDefId = petComp->equippedPet.petDefinitionId;
        msg.petName = petComp->equippedPet.petName;
        msg.level = static_cast<uint8_t>(petComp->equippedPet.level);
        msg.currentXP = static_cast<int32_t>(petComp->equippedPet.currentXP);
        msg.xpToNextLevel = static_cast<int32_t>(petComp->equippedPet.xpToNextLevel);
    }
    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvPetUpdate, buf, w.size());
}
```

- [ ] **Step 5: Wire pet XP sharing**

In the mob kill handler (both skill and auto-attack paths), after `charStats->stats.addXP(xp)`:

```cpp
// Pet XP sharing
auto* petComp = player->getComponent<PetComponent>();
if (petComp && petComp->hasPet()) {
    int petXP = static_cast<int>(xp * 0.5f); // PET_XP_SHARE = 50%
    if (petXP > 0) {
        bool leveled = PetSystem::addXP(petComp->equippedPet, petXP, charStats->stats.level);
        if (leveled) {
            recalcEquipmentBonuses(player);
            sendPlayerState(clientId);
        }
        sendPetUpdate(clientId, player);
    }
}
```

Find both mob kill XP paths (skill kill ~line 2665 and auto-attack kill) and add to both.

- [ ] **Step 6: Add client callback**

In `engine/net/net_client.h`:
```cpp
std::function<void(const SvPetUpdateMsg&)> onPetUpdate;
```

In `engine/net/net_client.cpp`:
```cpp
case PacketType::SvPetUpdate: {
    ByteReader payload(payloadData, payloadLen);
    auto msg = SvPetUpdateMsg::read(payload);
    if (onPetUpdate) onPetUpdate(msg);
    break;
}
```

- [ ] **Step 7: Build and test**
```bash
touch server/server_app.cpp server/server_app.h engine/net/net_client.cpp engine/net/net_client.h
cmake --build build --target FateServer && cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 8: Commit**
```bash
git add server/server_app.cpp server/server_app.h engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: wire pet system — equip/unequip, stat bonuses, XP sharing"
```

---

### Task 4: Final Integration

- [ ] **Step 1: Run full test suite**
```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 2: Build both targets**
```bash
cmake --build build --target FateServer && cmake --build build --target FateEngine
```

- [ ] **Step 3: Verify test count**
```bash
./build/Debug/fate_tests -c
```

**REMINDER:** Restart FateServer.exe after deploying server changes.
