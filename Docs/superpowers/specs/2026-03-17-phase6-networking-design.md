# Phase 6: Networking — Design Spec

**Date:** 2026-03-17
**Status:** Approved

## Overview

Add multiplayer networking to the engine: custom reliable UDP transport, entity replication with AOI and delta compression, a headless zone server, and gameplay integration. Single zone server initially, designed for future multi-zone sharding.

**Target:** 100-500+ concurrent players per zone, 20 tick/sec server simulation.

---

## Architecture

**Single-process zone server** running the authoritative game simulation (ECS world, AI, combat, spawning, quests). Headless — no rendering, no SDL window. Listens on a UDP port.

**Clients** connect to the zone server, send movement + action inputs, receive world state updates. The client runs the full render pipeline and a local ECS world that mirrors the server's visible slice via AOI.

**Authority split:**
- **Client-authoritative:** Own player movement (position/velocity). Server validates speed/teleport bounds, rubber-bands on violation.
- **Server-authoritative:** Combat, HP/damage, inventory, quests, NPC state, mob AI, spawning, loot, chat, trade — everything except local movement.

**Tick model:** Server runs a fixed 20 tick/sec simulation loop. Each tick:
1. Drain incoming UDP packets
2. Process client inputs (movement, actions)
3. Validate movement (speed/teleport checks, rubber-band on violation)
4. World update — AI, combat, spawning, status effects, quest triggers
5. Per-client AOI diff — compute entity enter/leave/stay
6. Build outbound packets — delta-compressed updates, combat events, chat
7. Send all outbound packets
8. Sleep until next tick (`timeBeginPeriod(1)` on Windows for 1ms timer resolution; spin-wait the final millisecond for accurate 50ms ticks)

**Future sharding:** The zone server is a single process per zone. A zone gateway (future work, not this spec) would route players between zone servers on scene transitions. The protocol includes `zoneId` in persistent IDs so entities are zone-aware from day one.

---

## Transport Layer — Custom Reliable UDP

### Socket

`NetSocket` wraps platform UDP sockets (`WSAStartup`/`socket`/`sendto`/`recvfrom` on Windows). Non-blocking mode. One socket per server, one per client.

### Packet Structure

```
[Header: 12 bytes]
  protocol_id:  uint16  (magic number, reject garbage)
  session_token: uint32 (per-connection shared secret, set during handshake)
  sequence:     uint16  (wrapping sequence number)
  ack:          uint16  (last received remote sequence)
  ack_bits:     uint16  (bitfield of 16 previous acks)
  channel:      uint8   (0=unreliable, 1=reliable-ordered, 2=reliable-unordered)
  packet_type:  uint8   (enum: Connect, Disconnect, Heartbeat, GameData, etc.)
  payload_size: uint16  (bytes following header)
[Payload: 0-1188 bytes]
```

**Max packet size:** 1200 bytes total (header + payload). Fits in a single UDP datagram without fragmentation. Fragmentation deferred — all messages must fit in 1188 bytes for Phase 6. Zone snapshots sent as multiple individual entity messages.

**Session token:** Server generates a random 32-bit token during `ConnectAccept` handshake. All subsequent packets from that client must include the token. Packets with wrong tokens are silently dropped. This prevents trivial packet spoofing (attacker would need to observe the handshake to learn the token). Full authentication (login server, TLS) is future work.

**Sequence comparison:** Uses wrap-safe signed comparison: `(int16_t)(a - b) > 0` to correctly handle the uint16 wrap boundary (~55 minutes at 20 packets/sec).

### Channels

- **Channel 0 — Unreliable:** Position updates, entity state. Fire-and-forget. Stale packets dropped via sequence comparison.
- **Channel 1 — Reliable ordered:** Chat, quest updates, inventory changes, combat events. Retransmitted until acked, delivered in order.
- **Channel 2 — Reliable unordered:** Zone snapshots, asset loading. Retransmitted but can arrive out of order.

### Reliability

Sender tracks unacked reliable packets. If not acked within 200ms (4 ticks), retransmit. Ack bitfield covers 16 previous packets, so most acks piggyback on regular traffic without dedicated ack packets.

### Connection Management

Client sends `Connect` packet with protocol version. Server responds with `ConnectAccept` + assigned `clientId`. Heartbeat every 1 second. Timeout after 10 seconds of silence.

---

## Entity Replication — AOI + Delta Compression

### AOI-Driven Visibility

Each connected player has a `VisibilitySet` (existing `engine/net/aoi.h`). Every server tick, the server computes which entities are within the activation radius (320px) and builds enter/leave/stay diffs.

**EntityHandle → PersistentId bridging:** The existing `VisibilitySet` stores `EntityHandle` (local to one ECS world). The `ReplicationManager` translates handles to `PersistentId` for the wire format. Each networked entity must carry a `PersistentId` component (or the server maintains a handle→PersistentId lookup table). The translation happens in ReplicationManager, not in VisibilitySet itself.

### Replication Events (per tick, per client)

- **Entity Enter:** Full snapshot — persistent ID, entity type, position, all networked component data. Sent reliable (channel 1). Client creates a ghost entity.
- **Entity Leave:** Persistent ID only. Sent reliable (channel 1). Client destroys the ghost entity.
- **Entity Update:** Delta-compressed state for visible entities. Sent unreliable (channel 0). Only changed fields since last acked state.

### Delta Compression

Server maintains per-client "last acked state" for each visible entity. Each tick, compare current vs last acked. Only changed fields get packed: field bitmask (1 bit per field) + changed values. Position updates = 2 bytes bitmask + 8 bytes (two floats) = 10 bytes per moving entity.

### Packet Batching

Multiple entity updates are packed into a single UDP datagram per client per tick. Each `SvEntityUpdate` is prefixed by its PersistentId + bitmask, and multiple updates are concatenated until the 1188-byte payload limit. This amortizes the 12-byte header cost. Typically 1-3 packets per client per tick for updates, plus separate reliable packets for events.

### Bandwidth Budget (500 players)

**Average case:**
- ~50 entities visible per player (AOI radius)
- ~30 moving per tick
- 30 × 10 bytes = 300 bytes payload + 12 bytes header = ~312 bytes/tick
- 312 × 20 ticks/sec = 6.2 KB/sec per player outbound
- 500 × 6.2 KB = 3.1 MB/sec total server outbound

**Worst case (dense PvP):**
- 100 visible entities, 80 moving, 20 combat events/tick
- 80 × 10 + 20 × 30 (combat events) = 1400 bytes payload = 2 packets
- + 10 entity enter/leave per second × 150 bytes = 1.5 KB/sec
- ~20 KB/sec per player, 10 MB/sec total — still manageable

### Rate Limiting

Server enforces per-client input rate limits:
- `CmdMove`: max 30 per second (drop excess silently)
- Reliable messages: max 20 per second (excess → disconnect warning, then kick)
- Total inbound bytes: max 10 KB/sec per client

### Client-Side Interpolation

Ghost entities (other players, mobs) are interpolated between received states with a ~100ms buffer delay. This smooths movement despite 50ms tick intervals and packet loss. Implemented in Phase 6C as part of ghost entity rendering.

### lastAckedState Cleanup

Entries in `ClientConnection::lastAckedState` are removed when an entity leaves the client's AOI (on `Entity Leave` event). This prevents unbounded memory growth.

### Networked Components

Components marked with `ComponentFlags::Networked`:
- `Transform` (position only)
- `SpriteComponent` (animation frame, flipX)
- `CharacterStatsComponent` (HP, MP, level, class, name, PK status)
- `EnemyStatsComponent` (HP, alive state)
- `FactionComponent` (faction enum)
- `PetComponent` (equipped pet)
- `NameplateComponent` (display name, level, guild)
- `MobNameplateComponent` (display name, level, boss/elite flags)

---

## Client-Server Message Protocol

### Client → Server

| Message | Channel | Data |
|---------|---------|------|
| `CmdMove` | 0 (unreliable) | position, velocity, timestamp |
| `CmdAction` | 1 (reliable) | action type, target ID, skill ID |
| `CmdChat` | 1 (reliable) | channel, message, target name |
| `CmdTrade` | 1 (reliable) | trade action, slot, item |
| `CmdQuestAction` | 1 (reliable) | accept/turn-in, quest ID |

### Server → Client

| Message | Channel | Data |
|---------|---------|------|
| `SvEntityEnter` | 1 (reliable) | persistent ID, type, full component snapshot |
| `SvEntityLeave` | 1 (reliable) | persistent ID |
| `SvEntityUpdate` | 0 (unreliable) | persistent ID, field bitmask, changed values |
| `SvCombatEvent` | 1 (reliable) | attacker, target, damage, skill, crit, kill |
| `SvChatMessage` | 1 (reliable) | channel, sender, message, faction |
| `SvPlayerState` | 1 (reliable) | HP, MP, fury, XP, gold, level, effects |
| `SvMovementCorrection` | 0 (unreliable) | corrected position, rubber-band flag |
| `SvZoneTransition` | 1 (reliable) | target zone, spawn position |

### Serialization

Each message type has `write(ByteWriter&)` and `static read(ByteReader&)` methods. `ByteWriter`/`ByteReader` are lightweight wrappers over `uint8_t*` buffers with bounds checking in debug builds. Custom binary format — no external dependencies.

Packet type enum: client-to-server types 0x01-0x7F, server-to-client types 0x80-0xFF.

---

## Server Architecture

### Headless Server Process

Separate build target (`FateServer`) linking `fate_engine` without SDL window, renderer, SpriteBatch, or ImGui. Runs ECS world with gameplay systems at 20 ticks/sec fixed timestep.

### Per-Client Connection State

```cpp
struct ClientConnection {
    sockaddr_in address;
    uint16_t clientId;
    PersistentId playerEntityId;
    VisibilitySet aoi;
    std::unordered_map<PersistentId, ComponentSnapshot> lastAckedState;
    uint16_t localSequence = 0;
    uint16_t remoteSequence = 0;
    float lastHeartbeat = 0.0f;
    std::vector<PendingPacket> pendingReliable;
};
```

### Disconnect Handling

On graceful `Disconnect` packet or 10-second heartbeat timeout:
- Player entity is immediately removed from the world (no combat logout protection in Phase 6 — add later if needed)
- `Entity Leave` sent to all clients with the player in their AOI
- `ClientConnection` cleaned up, client slot freed
- No persistent state saving in Phase 6 (server-side persistence is future work)

### Client-Side Integration

Existing `GameApp` stays mostly unchanged. On connect, creates local player entity and sends `CmdMove` packets. Incoming `SvEntityEnter`/`Leave`/`Update` create/destroy/update ghost entities in the local ECS world. The render pipeline draws ghosts identically to local entities.

---

## File Structure

```
engine/net/
  aoi.h                  — (exists) AOI visibility sets
  ghost.h                — (exists) ghost entity flag component
  persistent_id.h        — (exists) 64-bit persistent IDs
  socket.h               — NetSocket: platform UDP socket wrapper
  socket_win32.cpp       — Windows WSA implementation
  packet.h               — Packet header, ByteWriter, ByteReader
  packet.cpp             — Fragment reassembly
  reliability.h          — ReliabilityLayer: sequence tracking, ack bitfields, retransmit
  reliability.cpp        — Retransmit logic, RTT estimation
  connection.h           — ConnectionManager: connect/disconnect/heartbeat/timeout
  connection.cpp         — Connection state machine
  protocol.h             — Message type enums and structs
  protocol.cpp           — Per-message write/read serializers
  replication.h          — ReplicationManager: AOI diffs, delta compression
  replication.cpp        — Per-client state tracking, snapshot diffing
  net_client.h           — NetClient: send inputs, receive state
  net_client.cpp         — Connect, poll, dispatch to ECS
  net_server.h           — NetServer: accept clients, broadcast state
  net_server.cpp         — Server tick, per-client AOI, outbound batching

server/
  server_main.cpp        — Headless entry point
  server_app.h           — ServerApp: ECS world + gameplay systems, no render
  server_app.cpp         — Server lifecycle
```

**Modified files:**
- `CMakeLists.txt` — Add `FateServer` target
- `game/game_app.cpp` — Integrate `NetClient`
- `game/entity_factory.h` — Add `createGhostPlayer()`

---

## Implementation Phases

### Phase 6A — Transport Layer
NetSocket, packet header, ByteWriter/ByteReader, ReliabilityLayer, ConnectionManager. Unit tests. Two test programs exchanging reliable+unreliable packets.

### Phase 6B — Protocol + Server Skeleton
All message types with serializers. ServerApp + server_main.cpp (headless, fixed timestep). CMake FateServer target. Server accepts connections, runs empty world, responds to heartbeats.

### Phase 6C — Entity Replication
ReplicationManager with AOI enter/leave/update. Delta compression with per-client last-acked state. Ghost entity creation/destruction on client. Two players see each other move.

### Phase 6D — Gameplay Integration
CmdAction processing (combat, NPC, quests). Server-side combat resolution. Chat routing. Movement validation + rubber-banding. Full gameplay loop over the network.
