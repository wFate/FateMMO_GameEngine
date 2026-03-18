# Phase 6D: Gameplay Integration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the multiplayer gameplay loop — server-side combat resolution, chat routing through the server, movement validation with rubber-banding, and client-side handling of combat events and player state updates.

**Architecture:** Server processes CmdAction/CmdChat/CmdMove packets, resolves combat using existing ECS systems, routes chat by channel, validates movement speed, and sends SvCombatEvent/SvChatMessage/SvPlayerState/SvMovementCorrection back to clients. Client receives these and updates local UI/state.

**Tech Stack:** C++20, existing ECS systems, NetServer, NetClient, protocol messages

**Spec:** `Docs/superpowers/specs/2026-03-17-phase6-networking-design.md` (Client-Server Message Protocol + Server Architecture sections)

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine
```

---

## File Structure

**Modified files:**
- `server/server_app.h/.cpp` — Add movement validation, combat processing, chat routing, player state broadcasting
- `engine/net/net_client.h/.cpp` — Add handlers for combat events, player state, movement correction
- `game/game_app.cpp` — Wire client-side callbacks for combat events, chat, player state, rubber-banding

**No new files** — all gameplay integration uses existing protocol messages and server/client infrastructure.

---

### Task 1: Movement Validation + Rubber-Banding

**Files:**
- Modify: `server/server_app.h`
- Modify: `server/server_app.cpp`

Add movement validation to the CmdMove handler in ServerApp:

```cpp
// In server_app.h, add constants:
static constexpr float MAX_MOVE_SPEED = 160.0f; // pixels/sec (~5 tiles/sec)
static constexpr float RUBBER_BAND_THRESHOLD = 200.0f; // pixels — max allowed deviation

// Per-client tracking (add to a map or extend ClientConnection):
std::unordered_map<uint16_t, Vec2> lastValidPositions_; // clientId → last accepted position
std::unordered_map<uint16_t, float> lastMoveTime_; // clientId → timestamp of last CmdMove
```

In `onPacketReceived` for CmdMove:
1. Get the client's player entity Transform position (server's version)
2. Compute distance from server position to client-reported position
3. Compute max allowed distance = MAX_MOVE_SPEED * timeSinceLastMove
4. If client position is further than max allowed distance + tolerance:
   - Send SvMovementCorrection with the server's last valid position, rubberBand=true
   - Do NOT update server position
   - LOG_WARN about speed violation
5. If position is within teleport threshold (RUBBER_BAND_THRESHOLD pixels from last valid):
   - Accept the position, update Transform
   - Update lastValidPositions_
6. If beyond RUBBER_BAND_THRESHOLD (teleport hack):
   - Send SvMovementCorrection with last valid position
   - LOG_WARN about teleport violation

Also add rate limiting: max 30 CmdMove per second per client. Track count per client, reset each tick.

Build, commit: `feat(net): add server-side movement validation with rubber-banding`

---

### Task 2: Server-Side Combat Processing

**Files:**
- Modify: `server/server_app.h`
- Modify: `server/server_app.cpp`

Handle CmdAction packets on the server:

```cpp
case PacketType::CmdAction: {
    auto action = CmdAction::read(payload);

    // Find attacker (client's player entity)
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) break;
    PersistentId attackerPid(client->playerEntityId);
    EntityHandle attackerH = replication_.getEntityHandle(attackerPid);
    Entity* attacker = world_.getEntity(attackerH);
    if (!attacker) break;

    // Find target
    PersistentId targetPid(action.targetId);
    EntityHandle targetH = replication_.getEntityHandle(targetPid);
    Entity* target = world_.getEntity(targetH);
    if (!target) break;

    if (action.actionType == 0) { // basic attack
        // Validate range
        auto* attackerT = attacker->getComponent<Transform>();
        auto* targetT = target->getComponent<Transform>();
        if (!attackerT || !targetT) break;

        float dist = attackerT->position.distance(targetT->position);
        auto* stats = attacker->getComponent<CharacterStatsComponent>();
        float range = stats ? stats->stats.classDef.attackRange * 32.0f : 32.0f;
        if (dist > range + 16.0f) break; // out of range

        // Resolve damage
        auto* targetEnemy = target->getComponent<EnemyStatsComponent>();
        if (targetEnemy && targetEnemy->stats.isAlive) {
            int damage = stats ? stats->stats.rollBasicAttackDamage() : 10;
            bool isCrit = false; // simplified for now
            targetEnemy->stats.takeDamage(damage);
            bool isKill = !targetEnemy->stats.isAlive;

            // Build and broadcast combat event
            SvCombatEventMsg evt;
            evt.attackerId = attackerPid.value();
            evt.targetId = targetPid.value();
            evt.damage = damage;
            evt.skillId = 0;
            evt.isCrit = isCrit;
            evt.isKill = isKill;

            uint8_t buf[128];
            ByteWriter w(buf, sizeof(buf));
            evt.write(w);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvCombatEvent, buf, w.size());

            if (isKill) {
                // Award XP/gold to attacker
                // TODO: integrate with existing quest system onMobDeath
            }
        }
    }
    break;
}
```

Note: Check if CharacterStats has a `rollBasicAttackDamage()` method. If not, use a simplified damage formula (baseDamage from classDef or a flat value). The key is that damage resolution happens on the server, not the client.

Also check if EnemyStats has a `takeDamage(int)` method. Read the existing combat_action_system.h to understand the current damage formula and replicate the server-side version.

Build FateServer, commit: `feat(net): add server-side combat resolution for CmdAction`

---

### Task 3: Chat Routing Through Server

**Files:**
- Modify: `server/server_app.cpp`

Handle CmdChat packets — route messages by channel:

```cpp
case PacketType::CmdChat: {
    auto chat = CmdChat::read(payload);

    // Rate limit: max 2 chat messages per second per client
    // (simplified — just check timestamp delta)

    // Build server chat message
    SvChatMessageMsg msg;
    msg.channel = chat.channel;
    msg.message = chat.message;

    // Get sender name from player entity
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) break;
    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* player = world_.getEntity(h);
    if (!player) break;

    auto* np = player->getComponent<NameplateComponent>();
    msg.senderName = np ? np->displayName : "Unknown";

    auto* fc = player->getComponent<FactionComponent>();
    msg.faction = fc ? static_cast<uint8_t>(fc->faction) : 0;

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);

    // Route by channel
    switch (chat.channel) {
        case 0: // Map — broadcast to all (proximity filtering is future work)
        case 1: // Global
        case 2: // Trade
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
            break;
        case 6: // System — server only, ignore from clients
            break;
        default:
            // Party/Guild/Private — broadcast for now, proper routing is future work
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
            break;
    }
    break;
}
```

Build, commit: `feat(net): add server-side chat routing by channel`

---

### Task 4: Player State Broadcasting

**Files:**
- Modify: `server/server_app.cpp`

After combat resolution or stat changes, send SvPlayerState to the affected player's client. Add a helper method:

```cpp
void ServerApp::sendPlayerState(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* player = world_.getEntity(h);
    if (!player) return;

    auto* stats = player->getComponent<CharacterStatsComponent>();
    if (!stats) return;

    SvPlayerStateMsg msg;
    msg.currentHP = stats->stats.currentHP;
    msg.maxHP = stats->stats.maxHP;
    msg.currentMP = stats->stats.currentMP;
    msg.maxMP = stats->stats.maxMP;
    msg.currentFury = stats->stats.currentFury;
    msg.currentXP = stats->stats.currentXP;
    msg.gold = 0; // TODO: get from inventory
    msg.level = stats->stats.level;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvPlayerState, buf, w.size());
}
```

Call `sendPlayerState(clientId)` after connecting (initial state) and after any stat change (damage taken, level up, etc.).

Also send initial player state to newly connected clients at the end of `onClientConnected`.

Build, commit: `feat(net): add SvPlayerState broadcasting on connect and stat changes`

---

### Task 5: Client-Side Combat Event + Player State Handling

**Files:**
- Modify: `game/game_app.cpp`

Wire the remaining NetClient callbacks:

**onCombatEvent:** Display floating damage text using the existing CombatActionSystem's floating text system. Find the target ghost entity by PersistentId, get its position, show damage number.

```cpp
netClient_.onCombatEvent = [this](const SvCombatEventMsg& msg) {
    // Find target entity (could be ghost or local player)
    auto& world = SceneManager::instance().currentScene()->world();

    // Look up in ghost entities
    auto it = ghostEntities_.find(msg.targetId);
    Entity* target = nullptr;
    if (it != ghostEntities_.end()) {
        target = world.getEntity(it->second);
    }

    // TODO: Show floating damage text at target position
    // This requires access to CombatActionSystem's text rendering
    // For now, just log
    LOG_INFO("Combat", "Damage: %d to entity %llu%s%s",
             msg.damage, msg.targetId,
             msg.isCrit ? " (CRIT)" : "",
             msg.isKill ? " (KILL)" : "");
};
```

**onPlayerState:** Update local player's stats display.

```cpp
netClient_.onPlayerState = [this](const SvPlayerStateMsg& msg) {
    // Update local player stats (for HUD display)
    // Find local player entity and update CharacterStatsComponent
    auto* scene = SceneManager::instance().currentScene();
    if (!scene) return;
    scene->world().forEach<CharacterStatsComponent, PlayerController>(
        [&](Entity*, CharacterStatsComponent* stats, PlayerController* ctrl) {
            if (!ctrl->isLocalPlayer) return;
            stats->stats.currentHP = msg.currentHP;
            stats->stats.maxHP = msg.maxHP;
            stats->stats.currentMP = msg.currentMP;
            stats->stats.maxMP = msg.maxMP;
            stats->stats.currentFury = msg.currentFury;
            stats->stats.currentXP = msg.currentXP;
            stats->stats.level = msg.level;
        }
    );
};
```

**onMovementCorrection:** Rubber-band local player to corrected position.

```cpp
netClient_.onMovementCorrection = [this](const SvMovementCorrectionMsg& msg) {
    auto* scene = SceneManager::instance().currentScene();
    if (!scene) return;
    scene->world().forEach<Transform, PlayerController>(
        [&](Entity*, Transform* t, PlayerController* ctrl) {
            if (!ctrl->isLocalPlayer) return;
            if (msg.rubberBand) {
                t->position = msg.correctedPosition;
                LOG_WARN("Net", "Rubber-banded to (%.0f, %.0f)", msg.correctedPosition.x, msg.correctedPosition.y);
            }
        }
    );
};
```

**onChatMessage:** Route to the existing ChatManager.

```cpp
netClient_.onChatMessage = [this](const SvChatMessageMsg& msg) {
    // TODO: Route to ChatManager/UI
    LOG_INFO("Chat", "[%s] %s", msg.senderName.c_str(), msg.message.c_str());
};
```

Build, commit: `feat(net): add client-side handling for combat events, player state, and rubber-banding`

---

### Task 6: Integration Test — Full Gameplay Loop

**Files:**
- Create: `tests/test_gameplay_integration.cpp`

Test combat over the network:
1. Start NetServer with a World
2. Connect client via loopback
3. Server creates player entity + a mob entity near the player
4. Run replication — client receives SvEntityEnter for the mob
5. Client sends CmdAction (attack the mob)
6. Server polls, processes combat, sends SvCombatEvent
7. Client receives SvCombatEvent with damage value
8. Verify damage > 0

```cpp
#include <doctest/doctest.h>
#include "engine/net/net_server.h"
#include "engine/net/replication.h"
#include "engine/net/protocol.h"
#include "engine/ecs/world.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/transform.h"
#include "game/components/game_components.h"
#include <thread>
#include <chrono>

using namespace fate;

TEST_CASE("Gameplay Integration: client attacks mob via server") {
    NetSocket::initPlatform();

    World world;
    ReplicationManager replication;
    NetServer server;
    REQUIRE(server.start(0));
    NetAddress serverAddr{0x7F000001, server.port()};

    // Create mob entity near origin
    Entity* mob = world.createEntity("Slime");
    auto* mobT = mob->addComponent<Transform>(50.0f, 0.0f);
    auto* mobNp = mob->addComponent<MobNameplateComponent>();
    mobNp->displayName = "Slime";
    auto* mobStats = mob->addComponent<EnemyStatsComponent>();
    mobStats->stats.currentHP = 100;
    mobStats->stats.maxHP = 100;
    mobStats->stats.isAlive = true;
    PersistentId mobPid = PersistentId::generate(1);
    replication.registerEntity(mob->handle(), mobPid);

    // Track what the server sends
    uint16_t connectedClientId = 0;
    PersistentId playerPid;

    server.onClientConnected = [&](uint16_t clientId) {
        connectedClientId = clientId;
        Entity* player = world.createEntity("Player1");
        player->addComponent<Transform>(0.0f, 0.0f);
        auto* np = player->addComponent<NameplateComponent>();
        np->displayName = "Player1";
        auto* cs = player->addComponent<CharacterStatsComponent>();
        cs->stats.currentHP = 100;
        cs->stats.maxHP = 100;

        playerPid = PersistentId::generate(1);
        replication.registerEntity(player->handle(), playerPid);

        auto* client = server.connections().findById(clientId);
        if (client) client->playerEntityId = playerPid.value();
    };

    // Handle CmdAction on server (simplified combat)
    server.onPacketReceived = [&](uint16_t clientId, uint8_t type, ByteReader& payload) {
        if (type == PacketType::CmdAction) {
            auto action = CmdAction::read(payload);
            if (action.actionType == 0) { // attack
                PersistentId targetPid(action.targetId);
                EntityHandle targetH = replication.getEntityHandle(targetPid);
                Entity* target = world.getEntity(targetH);
                if (!target) return;

                auto* es = target->getComponent<EnemyStatsComponent>();
                if (!es || !es->stats.isAlive) return;

                int damage = 15; // simplified
                es->stats.takeDamage(damage);

                SvCombatEventMsg evt;
                evt.attackerId = playerPid.value();
                evt.targetId = targetPid.value();
                evt.damage = damage;
                evt.isKill = !es->stats.isAlive;

                uint8_t buf[128];
                ByteWriter w(buf, sizeof(buf));
                evt.write(w);
                server.broadcast(Channel::ReliableOrdered, PacketType::SvCombatEvent, buf, w.size());
            }
        }
    };

    // Connect client
    NetSocket clientSock;
    REQUIRE(clientSock.open(0));
    {
        uint8_t buf[MAX_PACKET_SIZE];
        ByteWriter w(buf, sizeof(buf));
        PacketHeader hdr;
        hdr.packetType = PacketType::Connect;
        hdr.channel = Channel::ReliableOrdered;
        hdr.payloadSize = 0;
        hdr.write(w);
        clientSock.sendTo(buf, w.size(), serverAddr);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll(0.0f);
    CHECK(connectedClientId != 0);

    // Drain ConnectAccept
    {
        uint8_t buf[MAX_PACKET_SIZE];
        NetAddress from;
        clientSock.recvFrom(buf, sizeof(buf), from);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Get session token for subsequent packets
    auto* clientConn = server.connections().findById(connectedClientId);
    REQUIRE(clientConn != nullptr);
    uint32_t token = clientConn->sessionToken;

    // Client sends CmdAction — attack the mob
    {
        CmdAction action;
        action.actionType = 0; // attack
        action.targetId = mobPid.value();
        action.skillId = 0;

        uint8_t payload[32];
        ByteWriter pw(payload, sizeof(payload));
        action.write(pw);

        uint8_t buf[MAX_PACKET_SIZE];
        ByteWriter w(buf, sizeof(buf));
        PacketHeader hdr;
        hdr.sessionToken = token;
        hdr.channel = Channel::ReliableOrdered;
        hdr.packetType = PacketType::CmdAction;
        hdr.payloadSize = static_cast<uint16_t>(pw.size());
        hdr.write(w);
        w.writeBytes(payload, pw.size());

        clientSock.sendTo(buf, w.size(), serverAddr);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll(0.1f);

    // Client should receive SvCombatEvent
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool gotCombatEvent = false;
    int receivedDamage = 0;
    {
        uint8_t buf[MAX_PACKET_SIZE];
        NetAddress from;
        for (int i = 0; i < 10; ++i) {
            int received = clientSock.recvFrom(buf, sizeof(buf), from);
            if (received <= 0) break;
            ByteReader r(buf, received);
            PacketHeader hdr = PacketHeader::read(r);
            if (hdr.packetType == PacketType::SvCombatEvent) {
                auto evt = SvCombatEventMsg::read(r);
                gotCombatEvent = true;
                receivedDamage = evt.damage;
            }
        }
    }

    CHECK(gotCombatEvent);
    CHECK(receivedDamage == 15);
    CHECK(mobStats->stats.currentHP == 85); // 100 - 15

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}
```

Build, commit: `test(net): add gameplay integration test — combat over network`
