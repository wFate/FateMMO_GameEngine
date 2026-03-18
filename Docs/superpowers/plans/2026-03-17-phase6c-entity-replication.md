# Phase 6C: Entity Replication — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two players can connect to the server, see each other move in the same zone, with AOI-driven enter/leave events and delta-compressed position updates.

**Architecture:** ReplicationManager runs on the server each tick — computes per-client AOI diffs, builds entity enter/leave/update messages, and sends them via NetServer. Client receives messages and creates/destroys/updates ghost entities in its local ECS world. Ghost entities render identically to local entities via existing SpriteComponent.

**Tech Stack:** C++20, existing ECS, AOI system, NetServer, protocol messages

**Spec:** `Docs/superpowers/specs/2026-03-17-phase6-networking-design.md` (Entity Replication section)

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine
```

---

## File Structure

```
engine/net/
  replication.h        — ReplicationManager: per-client AOI tracking, snapshot diffing, message building
  replication.cpp      — AOI update, entity enter/leave/update logic
  net_client.h         — NetClient: client-side networking (connect, send, receive, dispatch)
  net_client.cpp       — Socket management, packet parsing, ghost entity management
```

**Modified files:**
- `engine/net/connection.h` — Add VisibilitySet and lastAckedState to ClientConnection
- `server/server_app.cpp` — Integrate ReplicationManager into server tick
- `game/game_app.h/.cpp` — Integrate NetClient for multiplayer mode
- `game/entity_factory.h` — Add createGhostPlayer() for remote player entities

---

### Task 1: Extend ClientConnection with Replication State

**Files:**
- Modify: `engine/net/connection.h`

Add to ClientConnection struct:
- `VisibilitySet aoi;` — per-client AOI tracking
- `std::unordered_map<uint64_t, SvEntityUpdateMsg> lastAckedState;` — keyed by PersistentId value, stores last-sent state for delta compression
- `uint64_t playerEntityId = 0;` — PersistentId of this client's player entity

Add includes for `engine/net/aoi.h` and `engine/net/protocol.h`.

Commit: `feat(net): extend ClientConnection with AOI and replication state`

---

### Task 2: ReplicationManager — Server-Side

**Files:**
- Create: `engine/net/replication.h`
- Create: `engine/net/replication.cpp`
- Create: `tests/test_replication.cpp`

ReplicationManager runs on the server each tick. It:
1. For each connected client, rebuilds their VisibilitySet based on their player entity's position
2. Computes AOI diffs (entered/left/stayed)
3. For entered entities: builds SvEntityEnterMsg and sends reliable
4. For left entities: builds SvEntityLeaveMsg, sends reliable, removes from lastAckedState
5. For stayed entities: compares current state vs lastAckedState, builds SvEntityUpdateMsg with only changed fields, sends unreliable

```cpp
class ReplicationManager {
public:
    // Call once per server tick after world update
    void update(World& world, NetServer& server, float gameTime);

private:
    AOIConfig aoiConfig_;

    // Build the visibility set for a client based on their player position
    void buildVisibility(World& world, ClientConnection& client);

    // Send enter/leave/update messages for a client
    void sendDiffs(World& world, NetServer& server, ClientConnection& client);

    // Build SvEntityEnterMsg from an entity
    SvEntityEnterMsg buildEnterMessage(World& world, Entity* entity);

    // Build delta-compressed SvEntityUpdateMsg
    SvEntityUpdateMsg buildUpdateMessage(World& world, Entity* entity,
                                          const SvEntityUpdateMsg& lastState);

    // Helper: get PersistentId for an entity (stored as component or lookup)
    uint64_t getEntityPersistentId(Entity* entity);
};
```

**buildVisibility:** Iterate all entities with Transform, compute distance to client's player position. If within activationRadius, add to current set. Then computeDiff and advance.

**PersistentId bridging:** Each networked entity needs a PersistentId. On the server, when an entity is created (player spawn, mob spawn), assign a PersistentId via `PersistentId::generate(zoneId)`. Store it in a simple component or a server-side lookup map. For Phase 6C, use a `std::unordered_map<EntityHandle, PersistentId>` on ReplicationManager (simplest approach — no new component needed).

**Tests:**
- Create a World with 3 entities at known positions
- Create a ReplicationManager
- Verify buildVisibility produces correct entered/left sets based on distance
- Verify buildUpdateMessage produces correct field masks for changed vs unchanged fields

Commit: `feat(net): add ReplicationManager with AOI-driven entity enter/leave/update`

---

### Task 3: Integrate ReplicationManager into ServerApp

**Files:**
- Modify: `server/server_app.h` — Add ReplicationManager member
- Modify: `server/server_app.cpp` — Call replication.update() in tick, create player entities on connect

In `onClientConnected`: Create a player entity in the world at a spawn point, assign PersistentId, store in ClientConnection.

In `tick()`: After `world_.update(dt)`, call `replication_.update(world_, server_, gameTime_)`.

In `onClientDisconnected`: Destroy the player entity from the world.

In `onPacketReceived` for CmdMove: Update the player entity's Transform position (client-authoritative movement).

Commit: `feat(net): integrate ReplicationManager into server tick — players spawn and replicate`

---

### Task 4: NetClient — Client-Side Networking

**Files:**
- Create: `engine/net/net_client.h`
- Create: `engine/net/net_client.cpp`

```cpp
class NetClient {
public:
    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    void poll();

    // Send movement update (unreliable)
    void sendMove(const Vec2& position, const Vec2& velocity, float timestamp);

    // Send action (reliable)
    void sendAction(uint8_t actionType, uint64_t targetId, uint16_t skillId);

    // Send chat (reliable)
    void sendChat(uint8_t channel, const std::string& message, const std::string& target);

    bool isConnected() const { return connected_; }
    uint16_t clientId() const { return clientId_; }

    // Callbacks
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const SvEntityEnterMsg&)> onEntityEnter;
    std::function<void(const SvEntityLeaveMsg&)> onEntityLeave;
    std::function<void(const SvEntityUpdateMsg&)> onEntityUpdate;
    std::function<void(const SvCombatEventMsg&)> onCombatEvent;
    std::function<void(const SvChatMessageMsg&)> onChatMessage;
    std::function<void(const SvPlayerStateMsg&)> onPlayerState;
    std::function<void(const SvMovementCorrectionMsg&)> onMovementCorrection;

private:
    NetSocket socket_;
    ReliabilityLayer reliability_;
    NetAddress serverAddress_;
    uint16_t clientId_ = 0;
    uint32_t sessionToken_ = 0;
    bool connected_ = false;
    float lastHeartbeat_ = 0.0f;

    void sendPacket(Channel channel, uint8_t packetType,
                    const uint8_t* payload, size_t payloadSize);
    void handlePacket(const uint8_t* data, int size);
};
```

**connect():** Open socket, send Connect packet, start polling for ConnectAccept.

**poll():** recvFrom loop, parse packets, dispatch by type. For SvEntityEnter/Leave/Update, call the corresponding callback.

**sendMove():** Build CmdMove, write to buffer, send unreliable.

Commit: `feat(net): add NetClient — client-side connect, send, and receive with callbacks`

---

### Task 5: Ghost Entity Management on Client

**Files:**
- Modify: `game/entity_factory.h` — Add createGhostPlayer() and createGhostMob()
- Modify: `game/game_app.h` — Add NetClient member and ghost entity tracking
- Modify: `game/game_app.cpp` — Wire NetClient callbacks to create/destroy/update ghost entities

**createGhostPlayer(world, name, position):** Creates an entity with Transform + SpriteComponent + NameplateComponent but NO PlayerController, NO combat components. Uses the default player sprite. Tagged "ghost".

**Ghost entity tracking:** `std::unordered_map<uint64_t, EntityHandle> ghostEntities_` on GameApp — maps PersistentId to local entity handle.

**NetClient callback wiring:**
- `onEntityEnter`: Create ghost entity via factory, add to ghostEntities_ map, set position/name/level from message
- `onEntityLeave`: Find in ghostEntities_, destroy entity, remove from map
- `onEntityUpdate`: Find in ghostEntities_, update Transform position (with interpolation buffer — simple lerp for now), update SpriteComponent flipX/currentFrame, update HP if applicable

**Movement sending:** In GameApp::onUpdate, if NetClient is connected, send CmdMove with local player position each frame (rate-limited to 30/sec max).

Commit: `feat(net): add ghost entity management — create/destroy/update remote players on client`

---

### Task 6: Client-Side Interpolation

**Files:**
- Create: `engine/net/interpolation.h`
- Modify: `game/game_app.cpp`

Simple position interpolation for ghost entities:

```cpp
struct InterpolationState {
    Vec2 previousPosition;
    Vec2 targetPosition;
    float interpolationTime = 0.0f; // time since last update
    float updateInterval = 0.05f;   // 50ms (20 tick/sec)
};
```

On each `onEntityUpdate` with position: store current position as previous, new position as target, reset interpolationTime.

On each frame: advance interpolationTime, lerp position between previous and target. Clamp at target when interpolationTime >= updateInterval.

Store `std::unordered_map<uint64_t, InterpolationState> ghostInterpolation_` on GameApp.

Commit: `feat(net): add client-side position interpolation for ghost entities`

---

### Task 7: End-to-End Integration Test

**Files:**
- Create: `tests/test_replication_integration.cpp`

Test the full flow:
1. Start a NetServer
2. Connect two "clients" (NetSocket + ReliabilityLayer) via loopback
3. Server creates player entities for both
4. Server runs ReplicationManager update
5. Verify client 1 receives SvEntityEnter for client 2's player (and vice versa)
6. Client 1 sends CmdMove to update position
7. Server processes, runs replication
8. Verify client 2 receives SvEntityUpdate with client 1's new position

Commit: `test(net): add end-to-end entity replication integration test`
