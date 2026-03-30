# Server-Side Test Bots Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add interactive server-side bot players that can be spawned via GM commands, persist across restarts, and auto-accept party/trade/guild/friend/duel interactions for testing multiplayer systems.

**Architecture:** Bots are real DB-backed entities created through the same path as real players. A virtual connection adapter (isBot flag on ClientConnection + packet callback on NetServer) routes server-to-bot packets into a BotBrain that auto-responds through the normal handler dispatch. No network sockets involved.

**Tech Stack:** C++17, ECS (World/Entity/Component), PostgreSQL (pqxx), doctest, existing EntityFactory + CharacterRepository + AccountRepository

**Spec:** `Docs/superpowers/specs/2026-03-30-server-side-test-bots-design.md`

---

## File Structure

**New files:**
- `server/bot_brain.h` — BotBrain class declaration (packet router + auto-accept logic)
- `server/bot_brain.cpp` — BotBrain implementation
- `tests/test_bot_component.cpp` — Unit tests for BotComponent and ConnectionManager bot support

**Modified files:**
- `game/components/game_components.h` — Add BotComponent struct + reflection
- `engine/net/connection.h` — Add `isBot` flag to ClientConnection, `addBotClient()` to ConnectionManager
- `engine/net/connection.cpp` — Implement `addBotClient()`
- `engine/net/net_server.h` — Add `BotPacketCallback` type + `setBotPacketCallback()`
- `engine/net/net_server.cpp` — Early-return in `sendPacket()` for bot clients
- `server/server_app.h` — Add BotBrain member, `spawnBot()`/`despawnBot()`/`autoSpawnBots()` declarations, `activeBots_` tracking, `injectBotPacket()`
- `server/server_app.cpp` — Implement bot spawning/despawning, startup auto-spawn, brain wiring
- `server/handlers/gm_handler.cpp` — Register `/spawnbot`, `/despawnbot`, `/despawnbots`, `/botlist`

**CMake:** Server sources use `GLOB_RECURSE server/*.cpp` — new files in `server/` are picked up automatically on reconfigure. Test sources use `GLOB_RECURSE tests/*.cpp` (excluding scenarios) — new test files are also auto-detected.

---

### Task 1: BotComponent

**Files:**
- Modify: `game/components/game_components.h`
- Test: `tests/test_bot_component.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_bot_component.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/components/game_components.h"

using namespace fate;

TEST_CASE("BotComponent: default auto-accept flags are all true") {
    BotComponent bot;
    CHECK(bot.accountId == 0);
    CHECK(bot.characterId.empty());
    CHECK(bot.autoAcceptParty == true);
    CHECK(bot.autoAcceptTrade == true);
    CHECK(bot.autoAcceptGuild == true);
    CHECK(bot.autoAcceptFriend == true);
    CHECK(bot.autoAcceptDuel == true);
}

TEST_CASE("BotComponent: has correct component metadata") {
    CHECK(std::string(BotComponent::COMPONENT_NAME) == "BotComponent");
    CHECK(BotComponent::COMPONENT_TYPE_ID != 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
touch tests/test_bot_component.cpp && cmake --build build --target fate_tests 2>&1 | head -30
```

Expected: Compilation error — `BotComponent` not defined.

- [ ] **Step 3: Add BotComponent to game_components.h**

In `game/components/game_components.h`, add the struct before the closing `} // namespace fate` (after `CostumeComponent`, around line 293):

```cpp
struct BotComponent {
    FATE_COMPONENT_COLD(BotComponent)
    int accountId = 0;
    std::string characterId;
    bool autoAcceptParty = true;
    bool autoAcceptTrade = true;
    bool autoAcceptGuild = true;
    bool autoAcceptFriend = true;
    bool autoAcceptDuel = true;
};
```

Then add the reflection macro at the end of the file (after `FATE_REFLECT_EMPTY(fate::CostumeComponent)`):

```cpp
FATE_REFLECT(fate::BotComponent,
    FATE_FIELD(accountId, Int),
    FATE_FIELD(characterId, String),
    FATE_FIELD(autoAcceptParty, Bool),
    FATE_FIELD(autoAcceptTrade, Bool),
    FATE_FIELD(autoAcceptGuild, Bool),
    FATE_FIELD(autoAcceptFriend, Bool),
    FATE_FIELD(autoAcceptDuel, Bool)
)
```

- [ ] **Step 4: Build and run tests**

```bash
touch game/components/game_components.h tests/test_bot_component.cpp && cmake --build build --target fate_tests && ./build/fate_tests -tc="BotComponent*"
```

Expected: Both tests PASS.

- [ ] **Step 5: Commit**

```bash
git add game/components/game_components.h tests/test_bot_component.cpp
git commit -m "feat(bots): add BotComponent with auto-accept flags and reflection"
```

---

### Task 2: Virtual Connection Adapter

**Files:**
- Modify: `engine/net/connection.h`
- Modify: `engine/net/connection.cpp`
- Modify: `engine/net/net_server.h`
- Modify: `engine/net/net_server.cpp`
- Test: `tests/test_bot_component.cpp` (append)

- [ ] **Step 1: Write failing tests for addBotClient**

Append to `tests/test_bot_component.cpp`:

```cpp
#include "engine/net/connection.h"

TEST_CASE("ConnectionManager: addBotClient creates a bot connection") {
    ConnectionManager mgr;
    uint16_t botId = mgr.addBotClient();
    CHECK(botId != 0);

    ClientConnection* conn = mgr.findById(botId);
    REQUIRE(conn != nullptr);
    CHECK(conn->isBot == true);
    CHECK(conn->clientId == botId);
    CHECK(conn->sessionToken != 0);
}

TEST_CASE("ConnectionManager: addBotClient bypasses per-IP limit") {
    ConnectionManager mgr;
    mgr.setMaxConnectionsPerIP(2);
    // Should be able to create more than 2 bot clients (no IP tracking)
    uint16_t id1 = mgr.addBotClient();
    uint16_t id2 = mgr.addBotClient();
    uint16_t id3 = mgr.addBotClient();
    CHECK(id1 != 0);
    CHECK(id2 != 0);
    CHECK(id3 != 0);
    CHECK(id1 != id2);
    CHECK(id2 != id3);
}

TEST_CASE("ConnectionManager: bot client does not affect clientCount for real clients") {
    ConnectionManager mgr;
    uint16_t botId = mgr.addBotClient();
    CHECK(mgr.clientCount() == 1); // bot counts as a client for entity tracking
    mgr.removeClient(botId);
    CHECK(mgr.clientCount() == 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
touch tests/test_bot_component.cpp && cmake --build build --target fate_tests 2>&1 | head -20
```

Expected: Compilation error — `addBotClient` and `isBot` not defined.

- [ ] **Step 3: Add isBot to ClientConnection**

In `engine/net/connection.h`, add to the `ClientConnection` struct (after the `tradePartnerCharId` field, around line 50):

```cpp
    bool isBot = false;
```

- [ ] **Step 4: Add addBotClient to ConnectionManager**

In `engine/net/connection.h`, add the declaration to `ConnectionManager` (after `addClient`, around line 57):

```cpp
    uint16_t addBotClient();
```

In `engine/net/connection.cpp`, add the implementation (after `addClient`, around line 40):

```cpp
uint16_t ConnectionManager::addBotClient() {
    if (clients_.size() >= maxClients_) {
        return 0;
    }

    while (nextClientId_ == 0 || clients_.count(nextClientId_)) {
        ++nextClientId_;
        if (nextClientId_ == 0) nextClientId_ = 1;
    }

    uint16_t id = nextClientId_++;
    if (nextClientId_ == 0) nextClientId_ = 1;

    ClientConnection conn;
    conn.clientId = id;
    conn.sessionToken = generateToken();
    conn.isBot = true;
    conn.lastHeartbeat = std::numeric_limits<float>::max(); // never times out

    clients_.emplace(id, std::move(conn));
    return id;
}
```

- [ ] **Step 5: Add bot packet callback to NetServer**

In `engine/net/net_server.h`, add the callback type and setter (in the public section):

```cpp
    using BotPacketCallback = std::function<void(uint16_t clientId, uint8_t packetType,
                                                  const uint8_t* data, size_t size)>;
    void setBotPacketCallback(BotPacketCallback cb);
```

Add the member (in the private section):

```cpp
    BotPacketCallback botPacketCallback_;
```

In `engine/net/net_server.cpp`, add the setter implementation:

```cpp
void NetServer::setBotPacketCallback(BotPacketCallback cb) {
    botPacketCallback_ = std::move(cb);
}
```

In `engine/net/net_server.cpp`, at the top of `sendPacket()` (around line 208), add the bot interception before any socket work:

```cpp
void NetServer::sendPacket(ClientConnection& client, Channel channel, uint8_t packetType,
                           const uint8_t* payload, size_t payloadSize) {
    if (client.isBot) {
        if (botPacketCallback_) {
            botPacketCallback_(client.clientId, packetType, payload, payloadSize);
        }
        return;
    }
    // ... existing code unchanged ...
```

- [ ] **Step 6: Build and run tests**

```bash
touch engine/net/connection.h engine/net/connection.cpp engine/net/net_server.h engine/net/net_server.cpp tests/test_bot_component.cpp && cmake --build build --target fate_tests && ./build/fate_tests -tc="ConnectionManager*bot*"
```

Expected: All 3 new tests PASS.

- [ ] **Step 7: Commit**

```bash
git add engine/net/connection.h engine/net/connection.cpp engine/net/net_server.h engine/net/net_server.cpp tests/test_bot_component.cpp
git commit -m "feat(bots): virtual connection adapter — isBot flag, addBotClient, packet callback"
```

---

### Task 3: BotBrain

**Files:**
- Create: `server/bot_brain.h`
- Create: `server/bot_brain.cpp`

- [ ] **Step 1: Create bot_brain.h**

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace fate {

class World;
class Entity;
struct BotComponent;

class BotBrain {
public:
    using PacketInjector = std::function<void(uint16_t clientId, uint8_t packetType,
                                              const uint8_t* data, size_t size)>;

    void setPacketInjector(PacketInjector injector);

    void registerBot(uint16_t clientId, uint32_t entityHandleValue);
    void unregisterBot(uint16_t clientId);

    // Called by NetServer's bot packet callback when the server sends a packet to a bot
    void onPacketFromServer(uint16_t clientId, uint8_t packetType,
                            const uint8_t* data, size_t size);

    // Tick for delayed responses (e.g. trade confirm delay)
    void tick(float deltaTime, World& world);

    size_t botCount() const { return bots_.size(); }

private:
    struct BotState {
        uint32_t entityHandleValue = 0;
        uint64_t tradeNonce = 0;
        bool tradePartnerLocked = false;
        float pendingConfirmTimer = -1.0f; // negative = no pending confirm
    };

    void handleTradeUpdate(uint16_t clientId, BotState& state,
                           const uint8_t* data, size_t size);
    void handleSocialUpdate(uint16_t clientId, BotState& state,
                            const uint8_t* data, size_t size);
    void handleGuildUpdate(uint16_t clientId, BotState& state,
                           const uint8_t* data, size_t size);

    void sendTradeAction(uint16_t clientId, uint8_t subAction);
    void sendTradeConfirm(uint16_t clientId, uint64_t nonce);

    PacketInjector injectPacket_;
    std::unordered_map<uint16_t, BotState> bots_; // clientId -> state
};

} // namespace fate
```

- [ ] **Step 2: Create bot_brain.cpp**

```cpp
#include "server/bot_brain.h"
#include "engine/net/packet.h"
#include "engine/net/protocol.h"
#include "engine/ecs/world.h"
#include "game/components/game_components.h"

namespace fate {

void BotBrain::setPacketInjector(PacketInjector injector) {
    injectPacket_ = std::move(injector);
}

void BotBrain::registerBot(uint16_t clientId, uint32_t entityHandleValue) {
    BotState state;
    state.entityHandleValue = entityHandleValue;
    bots_[clientId] = state;
}

void BotBrain::unregisterBot(uint16_t clientId) {
    bots_.erase(clientId);
}

void BotBrain::onPacketFromServer(uint16_t clientId, uint8_t packetType,
                                   const uint8_t* data, size_t size) {
    auto it = bots_.find(clientId);
    if (it == bots_.end()) return;

    switch (packetType) {
        case PacketType::SvTradeUpdate:
            handleTradeUpdate(clientId, it->second, data, size);
            break;
        default:
            break;
    }
}

void BotBrain::tick(float deltaTime, World& world) {
    for (auto& [clientId, state] : bots_) {
        if (state.pendingConfirmTimer >= 0.0f) {
            state.pendingConfirmTimer -= deltaTime;
            if (state.pendingConfirmTimer <= 0.0f) {
                state.pendingConfirmTimer = -1.0f;
                // Auto-lock first, then confirm will happen after we get our nonce
                sendTradeAction(clientId, 5); // TradeAction::Lock
            }
        }
    }
}

void BotBrain::handleTradeUpdate(uint16_t clientId, BotState& state,
                                  const uint8_t* data, size_t size) {
    if (size < 1) return;
    ByteReader r(data, size);
    auto msg = SvTradeUpdateMsg::read(r);

    switch (msg.updateType) {
        case 0: // Invited — trade session created, bot is the target
            // Trade auto-starts, nothing to do — wait for partner to add items and lock
            break;

        case 3: // Locked
            if (msg.nonce != 0) {
                // This is OUR lock acknowledgment with our nonce — now confirm
                state.tradeNonce = msg.nonce;
                sendTradeConfirm(clientId, state.tradeNonce);
            } else {
                // Partner locked — auto-lock after a short delay (0.5s)
                state.tradePartnerLocked = true;
                state.pendingConfirmTimer = 0.5f;
            }
            break;

        case 5: // Completed
        case 6: // Cancelled
            state.tradeNonce = 0;
            state.tradePartnerLocked = false;
            state.pendingConfirmTimer = -1.0f;
            break;

        default:
            break;
    }
}

void BotBrain::handleSocialUpdate(uint16_t clientId, BotState& state,
                                   const uint8_t* data, size_t size) {
    // Stub — will handle friend request auto-accept when social packet protocol is verified
}

void BotBrain::handleGuildUpdate(uint16_t clientId, BotState& state,
                                  const uint8_t* data, size_t size) {
    // Stub — will handle guild invite auto-accept when guild packet protocol is verified
}

void BotBrain::sendTradeAction(uint16_t clientId, uint8_t subAction) {
    if (!injectPacket_) return;
    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(subAction);
    injectPacket_(clientId, PacketType::CmdTrade, buf, w.size());
}

void BotBrain::sendTradeConfirm(uint16_t clientId, uint64_t nonce) {
    if (!injectPacket_) return;
    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(7); // TradeAction::Confirm
    w.writeU64(nonce);
    injectPacket_(clientId, PacketType::CmdTrade, buf, w.size());
}

} // namespace fate
```

- [ ] **Step 3: Build to verify compilation**

```bash
touch server/bot_brain.h server/bot_brain.cpp && cmake --build build --target FateServer 2>&1 | tail -20
```

Expected: Clean compilation (or warnings only).

- [ ] **Step 4: Commit**

```bash
git add server/bot_brain.h server/bot_brain.cpp
git commit -m "feat(bots): BotBrain packet router with trade auto-accept"
```

---

### Task 4: Bot Spawn and Despawn

**Files:**
- Modify: `server/server_app.h`
- Modify: `server/server_app.cpp`

This is the largest task. `spawnBot()` reuses the same entity creation path as `onClientConnected()` — create DB records, load CharacterRecord, call `EntityFactory::createPlayer`, override stats from DB, attach BotComponent, create virtual connection, register with replication.

- [ ] **Step 1: Add declarations to server_app.h**

Add includes at the top of `server/server_app.h`:

```cpp
#include "server/bot_brain.h"
```

Add to the `ServerApp` class (in the public section, near other handler declarations):

```cpp
    // Bot system
    bool spawnBot(uint16_t callerClientId, ClassType classType, Faction faction,
                  int level, const std::string& botName);
    bool despawnBot(const std::string& botName);
    int despawnAllBots(const std::string& scene);
    std::vector<std::string> listBots() const;
    void autoSpawnBots();
    void injectBotPacket(uint16_t clientId, uint8_t packetType,
                         const uint8_t* data, size_t size);
```

Add to private members (near other system members):

```cpp
    BotBrain botBrain_;
    struct ActiveBot {
        uint16_t clientId = 0;
        uint64_t persistentId = 0;
        std::string botName;
        std::string characterId;
        int accountId = 0;
    };
    std::unordered_map<std::string, ActiveBot> activeBots_; // botName -> ActiveBot
```

- [ ] **Step 2: Implement spawnBot in server_app.cpp**

Add to `server/server_app.cpp` (at the end of the file, or in a logical section near `onClientConnected`):

```cpp
bool ServerApp::spawnBot(uint16_t callerClientId, ClassType classType, Faction faction,
                          int level, const std::string& botName) {
    // Check for duplicate name
    if (activeBots_.count(botName)) return false;

    // --- DB: Create account ---
    std::string username = "bot_" + botName;
    std::string placeholderHash = "$2a$12$botplaceholderplaceholderplaceholderplaceholde"; // not a real login
    std::string email = username + "@bot.local";
    int accountId = accountRepo_->createAccount(username, placeholderHash, email);
    if (accountId < 0) return false;

    // --- DB: Create character ---
    std::string className;
    switch (classType) {
        case ClassType::Warrior: className = "Warrior"; break;
        case ClassType::Mage:    className = "Mage";    break;
        case ClassType::Archer:  className = "Archer";  break;
        default: className = "Warrior"; break;
    }
    std::string characterId = characterRepo_->createDefaultCharacter(accountId, botName, className);
    if (characterId.empty()) {
        // Cleanup account on failure
        // (No deleteAccount method, but this shouldn't happen in practice)
        return false;
    }

    // --- DB: Set faction, level, gender, hairstyle ---
    {
        auto conn = dbPool_->acquire();
        pqxx::work txn(*conn);
        txn.exec_params(
            "UPDATE characters SET faction = $1, level = $2, gender = 0, hairstyle = 0 WHERE character_id = $3",
            static_cast<int>(faction), level, characterId);
        txn.commit();
    }

    // --- DB: Grant starter equipment ---
    grantStarterEquipment(characterId, className);

    // --- Load CharacterRecord from DB (reuses existing path) ---
    auto recOpt = characterRepo_->loadCharacter(characterId);
    if (!recOpt) return false;
    auto& rec = *recOpt;

    // --- Create entity (same as onClientConnected) ---
    Entity* player = EntityFactory::createPlayer(world_, rec.character_name, classType, false,
        faction, static_cast<uint8_t>(rec.gender), static_cast<uint8_t>(rec.hairstyle));
    if (!player) return false;

    // Override stats from DB
    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    if (statsComp) {
        statsComp->stats.level = rec.level;
        statsComp->stats.currentXP = rec.current_xp;
        statsComp->stats.xpToNextLevel = rec.xp_to_next_level;
        statsComp->stats.currentScene = rec.current_scene;
        statsComp->stats.recallScene = rec.recall_scene;
        statsComp->stats.isDead = rec.is_dead;
        statsComp->stats.recalculateStats();
        statsComp->stats.currentHP = statsComp->stats.maxHP;
        statsComp->stats.currentMP = statsComp->stats.maxMP;
    }

    // Set faction
    auto* factionComp = player->getComponent<FactionComponent>();
    if (factionComp) {
        factionComp->faction = faction;
    }

    // Position at caller's location
    auto* callerClient = server_.connections().findById(callerClientId);
    if (callerClient) {
        auto callerHandle = replication_.getEntityHandle(PersistentId(callerClient->playerEntityId));
        Entity* callerEntity = world_.getEntity(callerHandle);
        if (callerEntity) {
            auto* callerTransform = callerEntity->getComponent<Transform>();
            auto* transform = player->getComponent<Transform>();
            auto* callerStats = callerEntity->getComponent<CharacterStatsComponent>();
            if (callerTransform && transform) {
                transform->position = callerTransform->position + Vec2{48.0f, 0.0f};
            }
            if (callerStats && statsComp) {
                statsComp->stats.currentScene = callerStats->stats.currentScene;
            }
        }
    }

    // Load inventory from DB
    auto items = inventoryRepo_->loadInventory(characterId);
    auto* inv = player->getComponent<InventoryComponent>();
    if (inv) {
        inv->inventory.setGold(rec.gold);
        for (auto& item : items) {
            if (item.is_equipped) {
                inv->inventory.equipFromDB(item.slot_type, item.item_def_id,
                    item.instance_id, item.quantity, item.enchant_level, item.socket_gems);
            } else {
                inv->inventory.addItemToSlot(item.slot_index, item.item_def_id,
                    item.instance_id, item.quantity, item.enchant_level, item.socket_gems);
            }
        }
        recalcEquipmentBonuses(player);
    }

    // Clamp HP/MP after equipment
    if (statsComp) {
        if (statsComp->stats.currentHP > statsComp->stats.maxHP)
            statsComp->stats.currentHP = statsComp->stats.maxHP;
        if (statsComp->stats.currentMP > statsComp->stats.maxMP)
            statsComp->stats.currentMP = statsComp->stats.maxMP;
    }

    // Attach BotComponent
    auto* botComp = player->addComponent<BotComponent>();
    botComp->accountId = accountId;
    botComp->characterId = characterId;

    // --- Virtual connection ---
    uint16_t botClientId = server_.connections().addBotClient();
    if (botClientId == 0) {
        world_.destroyEntity(player->handle());
        return false;
    }
    auto* botClient = server_.connections().findById(botClientId);
    botClient->account_id = accountId;
    botClient->character_id = characterId;

    // --- Replication ---
    uint64_t pidVal = std::hash<std::string>{}(characterId);
    if (pidVal == 0) pidVal = 1;
    PersistentId pid(pidVal);
    replication_.registerEntity(player->handle(), pid);
    botClient->playerEntityId = pid.value();
    server_.connections().mapEntity(pid.value(), botClientId);

    // --- Register with brain ---
    botBrain_.registerBot(botClientId, player->handle().value);

    // --- Track ---
    ActiveBot ab;
    ab.clientId = botClientId;
    ab.persistentId = pid.value();
    ab.botName = botName;
    ab.characterId = characterId;
    ab.accountId = accountId;
    activeBots_[botName] = ab;

    return true;
}
```

- [ ] **Step 3: Implement despawnBot**

```cpp
bool ServerApp::despawnBot(const std::string& botName) {
    auto it = activeBots_.find(botName);
    if (it == activeBots_.end()) return false;

    const auto& ab = it->second;

    // Unregister from brain
    botBrain_.unregisterBot(ab.clientId);

    // Unregister from replication (sends entity-leave to nearby clients)
    PersistentId pid(ab.persistentId);
    auto handle = replication_.getEntityHandle(pid);
    replication_.unregisterEntity(handle);

    // Remove virtual connection
    server_.connections().unmapEntity(ab.persistentId);
    server_.connections().removeClient(ab.clientId);

    // Destroy entity
    world_.destroyEntity(handle);

    // Delete DB rows (cascade: character_inventory, character_skills, etc.)
    {
        auto conn = dbPool_->acquire();
        pqxx::work txn(*conn);
        txn.exec_params("DELETE FROM characters WHERE character_id = $1", ab.characterId);
        txn.exec_params("DELETE FROM accounts WHERE account_id = $1", ab.accountId);
        txn.commit();
    }

    activeBots_.erase(it);
    return true;
}

int ServerApp::despawnAllBots(const std::string& scene) {
    std::vector<std::string> toRemove;
    for (auto& [name, ab] : activeBots_) {
        auto handle = replication_.getEntityHandle(PersistentId(ab.persistentId));
        Entity* ent = world_.getEntity(handle);
        if (!ent) { toRemove.push_back(name); continue; }
        auto* stats = ent->getComponent<CharacterStatsComponent>();
        if (stats && stats->stats.currentScene == scene) {
            toRemove.push_back(name);
        }
    }
    for (auto& name : toRemove) {
        despawnBot(name);
    }
    return static_cast<int>(toRemove.size());
}

std::vector<std::string> ServerApp::listBots() const {
    std::vector<std::string> result;
    for (auto& [name, ab] : activeBots_) {
        auto handle = replication_.getEntityHandle(PersistentId(ab.persistentId));
        Entity* ent = world_.getEntity(handle);
        std::string info = name;
        if (ent) {
            auto* stats = ent->getComponent<CharacterStatsComponent>();
            auto* faction = ent->getComponent<FactionComponent>();
            if (stats) {
                info += " | Lv" + std::to_string(stats->stats.level)
                      + " " + stats->stats.className
                      + " | " + stats->stats.currentScene;
            }
            if (faction) {
                static const char* factionNames[] = {"None", "Xyros", "Fenor", "Zethos", "Solis"};
                uint8_t fi = static_cast<uint8_t>(faction->faction);
                if (fi <= 4) info += std::string(" | ") + factionNames[fi];
            }
        }
        result.push_back(info);
    }
    return result;
}
```

- [ ] **Step 4: Implement injectBotPacket**

```cpp
void ServerApp::injectBotPacket(uint16_t clientId, uint8_t packetType,
                                 const uint8_t* data, size_t size) {
    ByteReader reader(data, size);
    onPacketReceived(clientId, packetType, reader);
}
```

- [ ] **Step 5: Wire BotBrain in ServerApp initialization**

In `server/server_app.cpp`, in the `init()` or `start()` method (wherever the server sets up callbacks), add:

```cpp
// Bot brain wiring
server_.setBotPacketCallback([this](uint16_t clientId, uint8_t packetType,
                                     const uint8_t* data, size_t size) {
    botBrain_.onPacketFromServer(clientId, packetType, data, size);
});

botBrain_.setPacketInjector([this](uint16_t clientId, uint8_t packetType,
                                    const uint8_t* data, size_t size) {
    injectBotPacket(clientId, packetType, data, size);
});
```

In the main `tick()` / `update()` method, add the brain tick:

```cpp
botBrain_.tick(deltaTime, world_);
```

- [ ] **Step 6: Build server**

```bash
touch server/server_app.h server/server_app.cpp server/bot_brain.h server/bot_brain.cpp && cmake --build build --target FateServer 2>&1 | tail -30
```

Expected: Clean build. Fix any compilation errors (missing includes, method signature mismatches).

**Note:** The exact inventory loading and equipment bonus calls may need adjustment based on the actual method signatures in `InventoryRepository` and `InventoryComponent`. Read those files during implementation and match the patterns used in `onClientConnected`.

- [ ] **Step 7: Commit**

```bash
git add server/server_app.h server/server_app.cpp
git commit -m "feat(bots): spawnBot/despawnBot with full entity creation and virtual connections"
```

---

### Task 5: GM Commands

**Files:**
- Modify: `server/handlers/gm_handler.cpp`

- [ ] **Step 1: Register /spawnbot command**

In `server/handlers/gm_handler.cpp`, inside `ServerApp::initGMCommands()`, add:

```cpp
// /spawnbot <class> <faction> <level> [name]
{
    GMCommand cmd;
    cmd.name = "spawnbot";
    cmd.category = "Bots";
    cmd.usage = "/spawnbot <class> <faction> <level> [name]";
    cmd.description = "Spawn a test bot at your position";
    cmd.minRole = AdminRole::GM;
    cmd.handler = [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.size() < 3) {
            sendSystemMsg(callerId, "Usage: /spawnbot <Warrior|Mage|Archer> <Xyros|Fenor|Zethos|Solis> <level> [name]");
            return;
        }

        // Parse class
        ClassType classType = ClassType::Warrior;
        std::string classStr = args[0];
        if (classStr == "Warrior" || classStr == "warrior") classType = ClassType::Warrior;
        else if (classStr == "Mage" || classStr == "mage" || classStr == "Magician" || classStr == "magician") classType = ClassType::Mage;
        else if (classStr == "Archer" || classStr == "archer") classType = ClassType::Archer;
        else { sendSystemMsg(callerId, "Unknown class: " + classStr); return; }

        // Parse faction
        Faction faction = Faction::Xyros;
        std::string facStr = args[1];
        if (facStr == "Xyros" || facStr == "xyros" || facStr == "1") faction = Faction::Xyros;
        else if (facStr == "Fenor" || facStr == "fenor" || facStr == "2") faction = Faction::Fenor;
        else if (facStr == "Zethos" || facStr == "zethos" || facStr == "3") faction = Faction::Zethos;
        else if (facStr == "Solis" || facStr == "solis" || facStr == "4") faction = Faction::Solis;
        else { sendSystemMsg(callerId, "Unknown faction: " + facStr); return; }

        // Parse level
        int level = std::atoi(args[2].c_str());
        if (level < 1) level = 1;
        if (level > 99) level = 99;

        // Parse or generate name
        std::string botName;
        if (args.size() > 3) {
            botName = args[3];
        } else {
            // Auto-generate: Bot_Warrior_01
            static int autoCounter = 0;
            botName = "Bot_" + classStr + "_" + std::to_string(++autoCounter);
        }

        if (botName.size() > 10) {
            sendSystemMsg(callerId, "Bot name too long (max 10 chars)");
            return;
        }

        bool ok = spawnBot(callerId, classType, faction, level, botName);
        if (ok) {
            static const char* factionNames[] = {"None", "Xyros", "Fenor", "Zethos", "Solis"};
            sendSystemMsg(callerId, "Bot '" + botName + "' (" + classStr + ", "
                + factionNames[static_cast<int>(faction)] + ", Lv" + std::to_string(level) + ") spawned");
        } else {
            sendSystemMsg(callerId, "Failed to spawn bot '" + botName + "' (duplicate name or DB error)");
        }
    };
    gmCommands_.registerCommand(std::move(cmd));
}
```

- [ ] **Step 2: Register /despawnbot command**

```cpp
// /despawnbot <name>
{
    GMCommand cmd;
    cmd.name = "despawnbot";
    cmd.category = "Bots";
    cmd.usage = "/despawnbot <name>";
    cmd.description = "Remove a test bot and delete its DB records";
    cmd.minRole = AdminRole::GM;
    cmd.handler = [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        if (args.empty()) { sendSystemMsg(callerId, "Usage: /despawnbot <name>"); return; }
        bool ok = despawnBot(args[0]);
        if (ok) sendSystemMsg(callerId, "Bot '" + args[0] + "' despawned and deleted");
        else sendSystemMsg(callerId, "No bot named '" + args[0] + "' found");
    };
    gmCommands_.registerCommand(std::move(cmd));
}
```

- [ ] **Step 3: Register /despawnbots command**

```cpp
// /despawnbots
{
    GMCommand cmd;
    cmd.name = "despawnbots";
    cmd.category = "Bots";
    cmd.usage = "/despawnbots";
    cmd.description = "Remove all bots in your current scene";
    cmd.minRole = AdminRole::GM;
    cmd.handler = [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        auto* client = server_.connections().findById(callerId);
        if (!client) return;
        auto handle = replication_.getEntityHandle(PersistentId(client->playerEntityId));
        Entity* caller = world_.getEntity(handle);
        if (!caller) return;
        auto* stats = caller->getComponent<CharacterStatsComponent>();
        if (!stats) return;

        int removed = despawnAllBots(stats->stats.currentScene);
        sendSystemMsg(callerId, "Despawned " + std::to_string(removed) + " bot(s)");
    };
    gmCommands_.registerCommand(std::move(cmd));
}
```

- [ ] **Step 4: Register /botlist command**

```cpp
// /botlist
{
    GMCommand cmd;
    cmd.name = "botlist";
    cmd.category = "Bots";
    cmd.usage = "/botlist";
    cmd.description = "List all active bots";
    cmd.minRole = AdminRole::GM;
    cmd.handler = [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
        auto bots = listBots();
        if (bots.empty()) {
            sendSystemMsg(callerId, "No active bots");
            return;
        }
        sendSystemMsg(callerId, "Active bots (" + std::to_string(bots.size()) + "):");
        for (auto& info : bots) {
            sendSystemMsg(callerId, "  " + info);
        }
    };
    gmCommands_.registerCommand(std::move(cmd));
}
```

- [ ] **Step 5: Build**

```bash
touch server/handlers/gm_handler.cpp && cmake --build build --target FateServer 2>&1 | tail -20
```

Expected: Clean build.

- [ ] **Step 6: Commit**

```bash
git add server/handlers/gm_handler.cpp
git commit -m "feat(bots): register /spawnbot, /despawnbot, /despawnbots, /botlist GM commands"
```

---

### Task 6: Server Startup Auto-Spawn

**Files:**
- Modify: `server/server_app.cpp`

- [ ] **Step 1: Implement autoSpawnBots**

Add to `server/server_app.cpp`:

```cpp
void ServerApp::autoSpawnBots() {
    auto conn = dbPool_->acquire();
    pqxx::work txn(*conn);
    auto rows = txn.exec("SELECT a.account_id, c.character_id, c.character_name, c.class_name, c.level, c.faction "
                          "FROM accounts a JOIN characters c ON a.account_id = c.account_id "
                          "WHERE a.username LIKE 'bot_%' LIMIT 100");
    txn.commit();

    int spawned = 0;
    for (auto& row : rows) {
        int accountId = row[0].as<int>();
        std::string characterId = row[1].as<std::string>();
        std::string charName = row[2].as<std::string>();
        std::string className = row[3].as<std::string>();
        int level = row[4].as<int>();
        int factionInt = row[5].as<int>();

        if (activeBots_.count(charName)) continue; // already spawned

        // Determine ClassType
        ClassType classType = ClassType::Warrior;
        if (className == "Mage") classType = ClassType::Mage;
        else if (className == "Archer") classType = ClassType::Archer;

        Faction faction = static_cast<Faction>(factionInt);

        // Load CharacterRecord
        auto recOpt = characterRepo_->loadCharacter(characterId);
        if (!recOpt) continue;
        auto& rec = *recOpt;

        // Create entity
        Entity* player = EntityFactory::createPlayer(world_, rec.character_name, classType, false,
            faction, static_cast<uint8_t>(rec.gender), static_cast<uint8_t>(rec.hairstyle));
        if (!player) continue;

        // Override stats from DB
        auto* statsComp = player->getComponent<CharacterStatsComponent>();
        if (statsComp) {
            statsComp->stats.level = rec.level;
            statsComp->stats.currentXP = rec.current_xp;
            statsComp->stats.xpToNextLevel = rec.xp_to_next_level;
            statsComp->stats.currentScene = rec.current_scene;
            statsComp->stats.isDead = rec.is_dead;
            statsComp->stats.recalculateStats();
            statsComp->stats.currentHP = statsComp->stats.maxHP;
            statsComp->stats.currentMP = statsComp->stats.maxMP;
        }

        auto* factionComp = player->getComponent<FactionComponent>();
        if (factionComp) factionComp->faction = faction;

        // Position from DB
        auto* transform = player->getComponent<Transform>();
        if (transform) {
            transform->position = Vec2{
                static_cast<float>(rec.position_x * 32),
                static_cast<float>(rec.position_y * 32)
            };
        }

        // Load inventory
        auto items = inventoryRepo_->loadInventory(characterId);
        auto* inv = player->getComponent<InventoryComponent>();
        if (inv) {
            inv->inventory.setGold(rec.gold);
            for (auto& item : items) {
                if (item.is_equipped) {
                    inv->inventory.equipFromDB(item.slot_type, item.item_def_id,
                        item.instance_id, item.quantity, item.enchant_level, item.socket_gems);
                } else {
                    inv->inventory.addItemToSlot(item.slot_index, item.item_def_id,
                        item.instance_id, item.quantity, item.enchant_level, item.socket_gems);
                }
            }
            recalcEquipmentBonuses(player);
        }

        // Attach BotComponent
        auto* botComp = player->addComponent<BotComponent>();
        botComp->accountId = accountId;
        botComp->characterId = characterId;

        // Virtual connection
        uint16_t botClientId = server_.connections().addBotClient();
        if (botClientId == 0) { world_.destroyEntity(player->handle()); continue; }
        auto* botClient = server_.connections().findById(botClientId);
        botClient->account_id = accountId;
        botClient->character_id = characterId;

        // Replication
        uint64_t pidVal = std::hash<std::string>{}(characterId);
        if (pidVal == 0) pidVal = 1;
        PersistentId pid(pidVal);
        replication_.registerEntity(player->handle(), pid);
        botClient->playerEntityId = pid.value();
        server_.connections().mapEntity(pid.value(), botClientId);

        // Register with brain
        botBrain_.registerBot(botClientId, player->handle().value);

        // Track
        ActiveBot ab;
        ab.clientId = botClientId;
        ab.persistentId = pid.value();
        ab.botName = charName;
        ab.characterId = characterId;
        ab.accountId = accountId;
        activeBots_[charName] = ab;

        ++spawned;
    }

    if (spawned > 0) {
        LOG_INFO("Bots", "Auto-spawned %d bot(s) from database", spawned);
    }
}
```

- [ ] **Step 2: Call autoSpawnBots during server startup**

In the `init()` or `start()` method of ServerApp (after DB initialization and world setup are complete, after GM commands are registered), add:

```cpp
autoSpawnBots();
```

- [ ] **Step 3: Build and verify**

```bash
touch server/server_app.cpp && cmake --build build --target FateServer 2>&1 | tail -20
```

Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add server/server_app.cpp
git commit -m "feat(bots): auto-spawn persistent bots from DB on server startup"
```

---

### Task 7: Build, Touch Files, and Smoke Test

**Files:** All modified files from Tasks 1-6

- [ ] **Step 1: Touch all modified .cpp files**

Critical per project convention — CMake may miss changes without explicit touch:

```bash
touch game/components/game_components.h \
      engine/net/connection.h engine/net/connection.cpp \
      engine/net/net_server.h engine/net/net_server.cpp \
      server/bot_brain.h server/bot_brain.cpp \
      server/server_app.h server/server_app.cpp \
      server/handlers/gm_handler.cpp \
      tests/test_bot_component.cpp
```

- [ ] **Step 2: Full build of all targets**

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake && cmake --build build 2>&1 | tail -40
```

Fix any compilation errors. Common issues:
- Missing includes (add `#include "game/components/game_components.h"` where BotComponent is used)
- Method signature mismatches between header declarations and .cpp implementations
- `recalcEquipmentBonuses` or inventory loading methods having different signatures than shown — read the actual signatures in `onClientConnected` and match them exactly

- [ ] **Step 3: Run unit tests**

```bash
./build/fate_tests -tc="BotComponent*" -tc="ConnectionManager*bot*"
```

Expected: All tests PASS.

- [ ] **Step 4: Manual smoke test**

1. Start `FateServer.exe`
2. Connect with game client
3. Type `/spawnbot Warrior Xyros 30 TestKnight`
4. Verify: bot appears near your character, visible in-game with nameplate
5. Type `/botlist` — verify it shows the bot
6. Open editor inspector, click bot entity — verify BotComponent is visible with editable fields
7. Type `/despawnbot TestKnight` — verify bot disappears
8. Restart server — if bot was not despawned, verify it auto-spawns from DB

- [ ] **Step 5: Final commit (if any fixups needed)**

```bash
git add -u
git commit -m "fix(bots): build fixups from smoke testing"
```

---

## Known Limitations

1. **Party auto-accept is not functional** — party networking is not yet implemented (parties are local data structures). The `autoAcceptParty` flag exists on BotComponent and will work once party server protocol is built.

2. **Guild/friend/duel auto-accept** — `BotBrain::handleSocialUpdate` and `handleGuildUpdate` are stubs. These will be implemented once the guild invite and friend request packet protocols are verified and wired into the brain's packet switch.

3. **No movement AI** — bots stand where spawned. Movement commands (`/botmove`, `/botfollow`) are future enhancements.

4. **No level-appropriate gear** — bots get starter equipment regardless of level. Gear can be added manually via `/additem` or through the inspector.

5. **Inventory loading in spawnBot** — the exact method signatures for `loadInventory`, `equipFromDB`, and `addItemToSlot` may differ from what's shown. During implementation, read the actual `onClientConnected` code (server_app.cpp lines 1658-1743) and match the pattern exactly.
