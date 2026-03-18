# Phase 6B: Protocol + Server Skeleton — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define all network message serializers and build a headless zone server that accepts UDP connections, runs an ECS world at 20 ticks/sec, and responds to heartbeats.

**Architecture:** Message structs in protocol.h with write/read methods using ByteWriter/ByteReader. ServerApp is a standalone process (no SDL window, no rendering) that runs the game simulation headless. NetServer wraps the socket + connection manager + reliability into a server-side networking facade.

**Tech Stack:** C++20, Winsock2, existing ECS/systems

**Spec:** `Docs/superpowers/specs/2026-03-17-phase6-networking-design.md`

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
  protocol.h           — All message structs with write/read serializers
  protocol.cpp         — (if needed for complex serialization, otherwise header-only)
  net_server.h         — NetServer: server networking facade
  net_server.cpp       — Socket polling, packet dispatch, connection lifecycle

server/
  server_app.h         — ServerApp: headless server with ECS world
  server_app.cpp       — Fixed timestep loop, system registration, no rendering
  server_main.cpp      — Entry point (main function)
```

**Modified files:**
- `CMakeLists.txt` — Add FateServer executable target

---

### Task 1: Protocol Message Types

**Files:**
- Create: `engine/net/protocol.h`
- Create: `tests/test_protocol.cpp`

All message structs with `write(ByteWriter&)` and `static read(ByteReader&)` methods.

**Client → Server messages:**

```cpp
struct CmdMove {
    Vec2 position;
    Vec2 velocity;
    float timestamp = 0.0f;
    void write(ByteWriter& w) const;
    static CmdMove read(ByteReader& r);
};

struct CmdAction {
    uint8_t actionType = 0; // 0=attack, 1=skill, 2=interact, 3=pickup
    uint64_t targetId = 0;  // PersistentId value
    uint16_t skillId = 0;
    void write(ByteWriter& w) const;
    static CmdAction read(ByteReader& r);
};

struct CmdChat {
    uint8_t channel = 0;
    std::string message;
    std::string targetName;
    void write(ByteWriter& w) const;
    static CmdChat read(ByteReader& r);
};
```

**Server → Client messages:**

```cpp
struct SvEntityEnterMsg {
    uint64_t persistentId = 0;
    uint8_t entityType = 0; // 0=player, 1=mob, 2=npc
    Vec2 position;
    std::string name;
    int32_t level = 1;
    int32_t currentHP = 0;
    int32_t maxHP = 0;
    uint8_t faction = 0;
    void write(ByteWriter& w) const;
    static SvEntityEnterMsg read(ByteReader& r);
};

struct SvEntityLeaveMsg {
    uint64_t persistentId = 0;
    void write(ByteWriter& w) const;
    static SvEntityLeaveMsg read(ByteReader& r);
};

struct SvEntityUpdateMsg {
    uint64_t persistentId = 0;
    uint16_t fieldMask = 0;
    Vec2 position;           // bit 0
    uint8_t animFrame = 0;   // bit 1
    bool flipX = false;      // bit 2
    int32_t currentHP = 0;   // bit 3
    void write(ByteWriter& w) const;
    static SvEntityUpdateMsg read(ByteReader& r);
};

struct SvCombatEventMsg {
    uint64_t attackerId = 0;
    uint64_t targetId = 0;
    int32_t damage = 0;
    uint16_t skillId = 0;
    bool isCrit = false;
    bool isKill = false;
    void write(ByteWriter& w) const;
    static SvCombatEventMsg read(ByteReader& r);
};

struct SvChatMessageMsg {
    uint8_t channel = 0;
    std::string senderName;
    std::string message;
    uint8_t faction = 0;
    void write(ByteWriter& w) const;
    static SvChatMessageMsg read(ByteReader& r);
};

struct SvPlayerStateMsg {
    int32_t currentHP = 0, maxHP = 0;
    int32_t currentMP = 0, maxMP = 0;
    float currentFury = 0.0f;
    int64_t currentXP = 0;
    int64_t gold = 0;
    int32_t level = 1;
    void write(ByteWriter& w) const;
    static SvPlayerStateMsg read(ByteReader& r);
};

struct SvMovementCorrectionMsg {
    Vec2 correctedPosition;
    bool rubberBand = false;
    void write(ByteWriter& w) const;
    static SvMovementCorrectionMsg read(ByteReader& r);
};
```

**SvEntityUpdateMsg** uses bitmask encoding — only write fields whose bit is set in fieldMask. On read, only read fields whose bit is set.

**Tests:** Round-trip test for CmdMove, SvEntityEnterMsg, SvEntityUpdateMsg (with partial fields via bitmask), SvCombatEventMsg, CmdChat (with strings).

Commit: `feat(net): add protocol message types with binary serializers`

---

### Task 2: NetServer Facade

**Files:**
- Create: `engine/net/net_server.h`
- Create: `engine/net/net_server.cpp`

NetServer wraps NetSocket + ConnectionManager into a server-side facade:

```cpp
class NetServer {
public:
    bool start(uint16_t port);
    void stop();

    // Call once per tick to drain incoming packets
    void poll(float currentTime);

    // Send a packet to a specific client
    void sendTo(uint16_t clientId, Channel channel, uint8_t packetType,
                const uint8_t* payload, size_t payloadSize);

    // Send to all connected clients
    void broadcast(Channel channel, uint8_t packetType,
                   const uint8_t* payload, size_t payloadSize);

    // Process retransmits for all clients
    void processRetransmits(float currentTime);

    // Check for timed-out clients
    std::vector<uint16_t> checkTimeouts(float currentTime);

    // Callbacks
    std::function<void(uint16_t clientId)> onClientConnected;
    std::function<void(uint16_t clientId)> onClientDisconnected;
    std::function<void(uint16_t clientId, uint8_t packetType,
                       ByteReader& payload)> onPacketReceived;

    ConnectionManager& connections() { return connections_; }

private:
    NetSocket socket_;
    ConnectionManager connections_;
    float currentTime_ = 0.0f;

    void handlePacket(const NetAddress& from, const uint8_t* data, int size);
    void handleConnect(const NetAddress& from);
    void handleDisconnect(uint16_t clientId);
};
```

Key behaviors:
- `poll()`: recvFrom in a loop until no more packets. For each packet, validate protocol ID and session token, process acks, dispatch by packet type.
- `handleConnect()`: Add client, send ConnectAccept with clientId + token payload.
- System packets (Connect/Disconnect/Heartbeat) handled internally. Game packets forwarded via onPacketReceived callback.
- `sendTo()`: Build header with sequence, ack fields from client's ReliabilityLayer, write header + payload, sendTo via socket. If reliable channel, track for retransmit.
- `processRetransmits()`: For each client, get retransmit list and re-send.

Commit: `feat(net): add NetServer facade — socket polling, connection lifecycle, packet dispatch`

---

### Task 3: ServerApp + Headless Entry Point

**Files:**
- Create: `server/server_app.h`
- Create: `server/server_app.cpp`
- Create: `server/server_main.cpp`
- Modify: `CMakeLists.txt`

**ServerApp** is a standalone class (does NOT inherit from App — App requires SDL window). It has its own main loop.

```cpp
class ServerApp {
public:
    bool init(uint16_t port = 7777);
    void run();
    void shutdown();

private:
    static constexpr float TICK_RATE = 20.0f;
    static constexpr float TICK_INTERVAL = 1.0f / TICK_RATE; // 50ms

    World world_;
    NetServer server_;
    float gameTime_ = 0.0f;
    bool running_ = false;

    void tick(float dt);
    void onClientConnected(uint16_t clientId);
    void onClientDisconnected(uint16_t clientId);
    void onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload);
};
```

**init():**
- `NetSocket::initPlatform()`
- `server_.start(port)`
- Register gameplay systems on world_ (MovementSystem, GameplaySystem, MobAISystem, CombatActionSystem, QuestSystem, SpawnSystem — same as GameApp but no render systems)
- Set callbacks on server_

**run():**
```cpp
void ServerApp::run() {
    running_ = true;
    auto lastTick = std::chrono::high_resolution_clock::now();

    // Windows high-res timer
    timeBeginPeriod(1);

    while (running_) {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTick).count();

        if (elapsed >= TICK_INTERVAL) {
            lastTick = now;
            gameTime_ += TICK_INTERVAL;
            tick(TICK_INTERVAL);
        } else {
            Sleep(1); // yield CPU
        }
    }

    timeEndPeriod(1);
}
```

**tick(dt):**
1. `server_.poll(gameTime_)` — drain packets
2. `world_.update(dt)` — run systems
3. `server_.processRetransmits(gameTime_)` — retransmit unacked
4. Check timeouts, disconnect stale clients

**server_main.cpp:**
```cpp
#include "server/server_app.h"
#include "engine/core/logger.h"

int main(int argc, char* argv[]) {
    uint16_t port = 7777;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    fate::ServerApp server;
    if (!server.init(port)) {
        LOG_FATAL("Server", "Failed to initialize");
        return 1;
    }

    server.run();
    server.shutdown();
    return 0;
}
```

**CMakeLists.txt changes:**
Add after the FateEngine executable section:
```cmake
# Server executable (headless, no SDL window)
file(GLOB_RECURSE SERVER_SOURCES server/*.cpp server/*.h)
if(SERVER_SOURCES)
    add_executable(FateServer ${SERVER_SOURCES})
    target_link_libraries(FateServer PRIVATE fate_engine)
    target_include_directories(FateServer PRIVATE ${CMAKE_SOURCE_DIR})
endif()
```

Note: FateServer links fate_engine (which includes SDL2 as a dependency) but never opens an SDL window. The server just uses the ECS, systems, and networking from fate_engine.

Commit: `feat(net): add headless ServerApp with fixed 20 tick/sec loop and CMake target`

---

### Task 4: Server Connect/Disconnect Integration Test

**Files:**
- Create: `tests/test_server_integration.cpp`

Test that a client can connect to NetServer, receive ConnectAccept with valid session token, and that the server detects heartbeat timeout.

```cpp
TEST_CASE("NetServer: client connect and receive accept") {
    NetSocket::initPlatform();

    NetServer server;
    REQUIRE(server.start(0)); // any port

    // Track connect callback
    uint16_t connectedId = 0;
    server.onClientConnected = [&](uint16_t id) { connectedId = id; };

    // Client sends Connect
    NetSocket clientSock;
    REQUIRE(clientSock.open(0));
    NetAddress serverAddr{0x7F000001, server.port()};

    uint8_t buf[MAX_PACKET_SIZE];
    ByteWriter w(buf, sizeof(buf));
    PacketHeader hdr;
    hdr.packetType = PacketType::Connect;
    hdr.channel = Channel::ReliableOrdered;
    hdr.payloadSize = 0;
    hdr.write(w);
    clientSock.sendTo(buf, w.size(), serverAddr);

    // Server polls
    server.poll(0.0f);
    CHECK(connectedId != 0);
    CHECK(server.connections().clientCount() == 1);

    // Client receives ConnectAccept
    sleep briefly...
    NetAddress from;
    int received = clientSock.recvFrom(buf, sizeof(buf), from);
    REQUIRE(received > 0);
    ByteReader r(buf, received);
    PacketHeader respHdr = PacketHeader::read(r);
    CHECK(respHdr.packetType == PacketType::ConnectAccept);

    // Read clientId and token from payload
    uint16_t assignedId = r.readU16();
    uint32_t token = r.readU32();
    CHECK(assignedId == connectedId);
    CHECK(token != 0);

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}
```

Also add `port()` accessor to NetServer if not present.

Commit: `test(net): add NetServer connect/disconnect integration test`
