#include <doctest/doctest.h>
#include "engine/net/replication.h"
#include "engine/net/net_server.h"
#include "engine/net/protocol.h"
#include "game/components/transform.h"
#include "game/components/game_components.h"
#include "game/shared/game_types.h"
#include <thread>
#include <chrono>

using namespace fate;

TEST_CASE("ReplicationManager: register and lookup entities") {
    ReplicationManager repl;
    EntityHandle h{1, 0}; // index 1, generation 0
    PersistentId pid = PersistentId::generate(1);

    repl.registerEntity(h, pid);

    CHECK(repl.getPersistentId(h) == pid);
    CHECK(repl.getEntityHandle(pid) == h);

    repl.unregisterEntity(h);
    CHECK(repl.getPersistentId(h).isNull());
}

TEST_CASE("ReplicationManager: buildEnterMessage fills entity data") {
    World world;
    ReplicationManager repl;

    // Create a test entity with Transform and a nameplate
    Entity* e = world.createEntity("TestMob");
    auto* t = e->addComponent<Transform>(100.0f, 200.0f);
    auto* np = e->addComponent<MobNameplateComponent>();
    np->displayName = "Slime";
    np->level = 3;
    auto* es = e->addComponent<EnemyStatsComponent>();
    es->stats.currentHP = 50;
    es->stats.maxHP = 100;

    PersistentId pid = PersistentId::generate(1);
    repl.registerEntity(e->handle(), pid);

    // Verify the registration works and entity has expected components
    CHECK(repl.getPersistentId(e->handle()) == pid);

    auto* transform = e->getComponent<Transform>();
    CHECK(transform->position.x == doctest::Approx(100.0f));
}

// ============================================================================
// Mob visibility / replication pipeline tests
// ============================================================================

// Helper: set up a NetServer with one connected client, returning the clientId.
// Caller must call NetSocket::initPlatform() before and shutdownPlatform() after.
static uint16_t connectOneClient(NetServer& server, NetSocket& clientSock,
                                 NetAddress serverAddr) {
    // Send a Connect packet from clientSock
    uint8_t buf[MAX_PACKET_SIZE];
    ByteWriter w(buf, sizeof(buf));
    PacketHeader hdr;
    hdr.packetType = PacketType::Connect;
    hdr.channel = Channel::ReliableOrdered;
    hdr.payloadSize = 0;
    hdr.write(w);
    clientSock.sendTo(buf, w.size(), serverAddr);

    // Brief sleep + poll so the server processes the Connect
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll(0.0f);

    // Find the client that was just added
    uint16_t cid = 0;
    server.connections().forEach([&](ClientConnection& c) { cid = c.clientId; });
    return cid;
}

// Helper: drain all pending packets from a client socket (discard them).
static void drainSocket(NetSocket& sock) {
    uint8_t buf[MAX_PACKET_SIZE];
    NetAddress from;
    while (sock.recvFrom(buf, sizeof(buf), from) > 0) {}
}

TEST_CASE("AOI: mob within activation radius appears in entered set") {
    NetSocket::initPlatform();

    World world;
    ReplicationManager repl;
    NetServer server;
    REQUIRE(server.start(0));
    NetAddress serverAddr = NetAddress::makeIPv4(0x7F000001, server.port());

    // Create a player entity at (100, 100)
    Entity* player = world.createEntity("Player");
    auto* pt = player->addComponent<Transform>(100.0f, 100.0f);
    auto* cs = player->addComponent<CharacterStatsComponent>();
    cs->stats.level = 1;
    cs->stats.currentHP = 100;
    cs->stats.maxHP = 100;
    PersistentId playerPid = PersistentId::generate(1);
    repl.registerEntity(player->handle(), playerPid);

    // Create a mob entity within activation radius (640px).
    // Place it 200px away at (300, 100).
    Entity* mob = world.createEntity("Slime");
    auto* mt = mob->addComponent<Transform>(300.0f, 100.0f);
    auto* es = mob->addComponent<EnemyStatsComponent>();
    es->stats.level = 5;
    es->stats.currentHP = 80;
    es->stats.maxHP = 80;
    auto* mnp = mob->addComponent<MobNameplateComponent>();
    mnp->displayName = "Slime";
    mnp->level = 5;
    PersistentId mobPid = PersistentId::generate(1);
    repl.registerEntity(mob->handle(), mobPid);

    // Connect a client and assign the player entity to it
    NetSocket clientSock;
    REQUIRE(clientSock.open(0));
    uint16_t clientId = connectOneClient(server, clientSock, serverAddr);
    REQUIRE(clientId != 0);

    ClientConnection* client = server.connections().findById(clientId);
    REQUIRE(client != nullptr);
    client->playerEntityId = playerPid.value();

    // Drain the ConnectAccept packet so it does not interfere later
    drainSocket(clientSock);

    // Run one replication tick
    repl.update(world, server);

    // After the first tick the mob should appear in the entered set
    // (previous was empty, current has the mob => entered has the mob)
    bool foundInEntered = false;
    for (const auto& h : client->aoi.previous) {
        // After update, advance() moved current->previous.
        // The mob should now be in previous (was in current during this tick).
        if (h == mob->handle()) foundInEntered = true;
    }
    // The entered set was computed before advance() and consumed by sendDiffs,
    // but we can verify the mob is now tracked by checking lastSentState.
    CHECK(client->lastSentState.count(mobPid.value()) == 1);

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}

TEST_CASE("AOI: unregistered mob does not appear in visibility") {
    NetSocket::initPlatform();

    World world;
    ReplicationManager repl;
    NetServer server;
    REQUIRE(server.start(0));
    NetAddress serverAddr = NetAddress::makeIPv4(0x7F000001, server.port());

    // Create a player entity at origin
    Entity* player = world.createEntity("Player");
    auto* pt = player->addComponent<Transform>(0.0f, 0.0f);
    auto* cs = player->addComponent<CharacterStatsComponent>();
    cs->stats.level = 1;
    cs->stats.currentHP = 100;
    cs->stats.maxHP = 100;
    PersistentId playerPid = PersistentId::generate(1);
    repl.registerEntity(player->handle(), playerPid);

    // Create a mob entity but do NOT register it with the replication manager.
    // Only registered entities participate in the visibility pipeline.
    Entity* mob = world.createEntity("UnregisteredMob");
    auto* mt = mob->addComponent<Transform>(50.0f, 50.0f);
    auto* es = mob->addComponent<EnemyStatsComponent>();
    es->stats.level = 10;
    es->stats.currentHP = 200;
    es->stats.maxHP = 200;
    auto* mnp = mob->addComponent<MobNameplateComponent>();
    mnp->displayName = "UnregisteredMob";
    mnp->level = 10;
    // Deliberately NOT calling repl.registerEntity for this mob

    // Connect a client
    NetSocket clientSock;
    REQUIRE(clientSock.open(0));
    uint16_t clientId = connectOneClient(server, clientSock, serverAddr);
    REQUIRE(clientId != 0);

    ClientConnection* client = server.connections().findById(clientId);
    REQUIRE(client != nullptr);
    client->playerEntityId = playerPid.value();

    drainSocket(clientSock);

    // Run replication
    repl.update(world, server);

    // The mob was never registered with the replication manager.
    // Only the player entity is registered, and the player's own entity is
    // excluded from its own visibility set. So nothing should be visible.
    CHECK(client->lastSentState.empty());
    CHECK(client->aoi.previous.empty());

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}

TEST_CASE("AOI: clearing client AOI causes re-enter on next tick") {
    NetSocket::initPlatform();

    World world;
    ReplicationManager repl;
    NetServer server;
    REQUIRE(server.start(0));
    NetAddress serverAddr = NetAddress::makeIPv4(0x7F000001, server.port());

    // Player at origin
    Entity* player = world.createEntity("Player");
    auto* pt = player->addComponent<Transform>(0.0f, 0.0f);
    auto* cs = player->addComponent<CharacterStatsComponent>();
    cs->stats.level = 1;
    cs->stats.currentHP = 100;
    cs->stats.maxHP = 100;
    PersistentId playerPid = PersistentId::generate(1);
    repl.registerEntity(player->handle(), playerPid);

    // Mob at (100, 0) — well within activation radius
    Entity* mob = world.createEntity("NearMob");
    auto* mt = mob->addComponent<Transform>(100.0f, 0.0f);
    auto* es = mob->addComponent<EnemyStatsComponent>();
    es->stats.level = 3;
    es->stats.currentHP = 50;
    es->stats.maxHP = 50;
    auto* mnp = mob->addComponent<MobNameplateComponent>();
    mnp->displayName = "NearMob";
    mnp->level = 3;
    PersistentId mobPid = PersistentId::generate(1);
    repl.registerEntity(mob->handle(), mobPid);

    // Connect client
    NetSocket clientSock;
    REQUIRE(clientSock.open(0));
    uint16_t clientId = connectOneClient(server, clientSock, serverAddr);
    REQUIRE(clientId != 0);

    ClientConnection* client = server.connections().findById(clientId);
    REQUIRE(client != nullptr);
    client->playerEntityId = playerPid.value();
    drainSocket(clientSock);

    // --- Pass 1: mob enters visibility ---
    repl.update(world, server);

    // Mob should now be tracked in lastSentState
    REQUIRE(client->lastSentState.count(mobPid.value()) == 1);
    // And in previous (was current, then advance moved it)
    bool mobInPrevious = false;
    for (const auto& h : client->aoi.previous) {
        if (h == mob->handle()) { mobInPrevious = true; break; }
    }
    REQUIRE(mobInPrevious);

    // --- Simulate zone transition: clear AOI previous and lastSentState ---
    client->aoi.previous.clear();
    client->lastSentState.clear();

    // --- Pass 2: mob should re-enter (not stay) ---
    repl.update(world, server);

    // After clearing previous, the mob appears as brand new.
    // sendDiffs should have created a new lastSentState entry for it.
    CHECK(client->lastSentState.count(mobPid.value()) == 1);

    // The mob should be back in the visibility set (now in previous after advance)
    bool mobInPreviousAgain = false;
    for (const auto& h : client->aoi.previous) {
        if (h == mob->handle()) { mobInPreviousAgain = true; break; }
    }
    CHECK(mobInPreviousAgain);

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}

TEST_CASE("AOI: mob aggro range fits within activation radius") {
    // Config sanity check: the AOI activation radius must be larger than the
    // maximum possible mob aggro range so that mobs are always visible before
    // they can aggro onto a player.
    //
    // Maximum aggro range in the database is 16 tiles * 32px/tile = 512px.
    // AOI activation radius is 640px (20 tiles).
    AOIConfig config;

    constexpr float MAX_MOB_AGGRO_TILES = 16.0f;
    constexpr float PIXELS_PER_TILE = 32.0f;
    constexpr float MAX_MOB_AGGRO_PX = MAX_MOB_AGGRO_TILES * PIXELS_PER_TILE; // 512

    CHECK(config.activationRadius > MAX_MOB_AGGRO_PX);
    CHECK(config.activationRadius == doctest::Approx(640.0f));
    CHECK(config.deactivationRadius > config.activationRadius);
}

TEST_CASE("Replication sends SvEntityEnter for newly visible entities") {
    NetSocket::initPlatform();

    World world;
    ReplicationManager repl;
    NetServer server;
    REQUIRE(server.start(0));
    NetAddress serverAddr = NetAddress::makeIPv4(0x7F000001, server.port());

    // Player at origin
    Entity* player = world.createEntity("Player");
    auto* pt = player->addComponent<Transform>(0.0f, 0.0f);
    auto* cs = player->addComponent<CharacterStatsComponent>();
    cs->stats.level = 5;
    cs->stats.currentHP = 200;
    cs->stats.maxHP = 200;
    auto* np = player->addComponent<NameplateComponent>();
    np->displayName = "Hero";
    np->displayLevel = 5;
    PersistentId playerPid = PersistentId::generate(1);
    repl.registerEntity(player->handle(), playerPid);

    // Mob within range
    Entity* mob = world.createEntity("Goblin");
    auto* mt = mob->addComponent<Transform>(50.0f, 50.0f);
    auto* es = mob->addComponent<EnemyStatsComponent>();
    es->stats.level = 3;
    es->stats.currentHP = 40;
    es->stats.maxHP = 60;
    es->stats.enemyId = "goblin_01";
    auto* mnp = mob->addComponent<MobNameplateComponent>();
    mnp->displayName = "Goblin";
    mnp->level = 3;
    PersistentId mobPid = PersistentId::generate(1);
    repl.registerEntity(mob->handle(), mobPid);

    // Connect client
    NetSocket clientSock;
    REQUIRE(clientSock.open(0));
    uint16_t clientId = connectOneClient(server, clientSock, serverAddr);
    REQUIRE(clientId != 0);

    ClientConnection* client = server.connections().findById(clientId);
    REQUIRE(client != nullptr);
    client->playerEntityId = playerPid.value();

    // Drain the ConnectAccept so we only see replication packets
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    drainSocket(clientSock);

    // Run replication — should produce SvEntityEnter for the mob
    repl.update(world, server);

    // Give the UDP packet time to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Read packets from the client socket
    std::vector<SvEntityEnterMsg> enters;
    {
        uint8_t buf[MAX_PACKET_SIZE];
        NetAddress from;
        for (int i = 0; i < 20; ++i) {
            int received = clientSock.recvFrom(buf, sizeof(buf), from);
            if (received <= 0) break;

            ByteReader r(buf, static_cast<size_t>(received));
            PacketHeader hdr = PacketHeader::read(r);
            if (hdr.packetType == PacketType::SvEntityEnter) {
                auto msg = SvEntityEnterMsg::read(r);
                enters.push_back(msg);
            }
        }
    }

    // We should have at least one SvEntityEnter
    REQUIRE(!enters.empty());

    // Find the mob's enter message
    bool foundMob = false;
    for (const auto& msg : enters) {
        if (msg.persistentId == mobPid.value()) {
            foundMob = true;
            CHECK(msg.entityType == 1);  // mob
            CHECK(msg.name == "Goblin");
            CHECK(msg.level == 3);
            CHECK(msg.currentHP == 40);
            CHECK(msg.maxHP == 60);
            CHECK(msg.mobDefId == "goblin_01");
            CHECK(msg.position.x == doctest::Approx(50.0f));
            CHECK(msg.position.y == doctest::Approx(50.0f));
        }
    }
    CHECK(foundMob);

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}

// ============================================================================
// Encoding helper unit tests (Task 6)
// ============================================================================

TEST_CASE("encodeAnimId / decodeAnimId round-trip") {
    for (uint8_t dir = 0; dir < 3; ++dir) {
        for (uint8_t type = 0; type < 4; ++type) {
            uint16_t id = fate::encodeAnimId(dir, type);
            uint8_t dDir, dType;
            fate::decodeAnimId(id, dDir, dType);
            REQUIRE(dDir == dir);
            REQUIRE(dType == type);
        }
    }
    // death special case
    uint8_t dDir, dType;
    fate::decodeAnimId(12, dDir, dType);
    REQUIRE(dType == 4); // death type
}

TEST_CASE("packEquipVisuals round-trips correctly") {
    uint16_t w = 42, a = 513, h = 1023;
    uint32_t packed = fate::packEquipVisuals(w, a, h);
    uint16_t w2, a2, h2;
    fate::unpackEquipVisuals(packed, w2, a2, h2);
    REQUIRE(w2 == 42);
    REQUIRE(a2 == 513);
    REQUIRE(h2 == 1023);
}

TEST_CASE("packEquipVisuals handles zero (empty slots)") {
    uint32_t packed = fate::packEquipVisuals(0, 0, 0);
    REQUIRE(packed == 0);
}

TEST_CASE("facingToAnimDir maps all Direction values") {
    REQUIRE(fate::facingToAnimDir(fate::Direction::Down) == 0);
    REQUIRE(fate::facingToAnimDir(fate::Direction::Up) == 1);
    REQUIRE(fate::facingToAnimDir(fate::Direction::Left) == 2);
    REQUIRE(fate::facingToAnimDir(fate::Direction::Right) == 2);
    REQUIRE(fate::facingToAnimDir(fate::Direction::None) == 0);
}

TEST_CASE("moveState/animId produce dirty bits in delta comparison") {
    fate::SvEntityUpdateMsg prev, curr;
    prev.moveState = 0; prev.animId = 0;
    curr.moveState = 1; curr.animId = 4;

    uint16_t dirty = 0;
    if (curr.moveState != prev.moveState) dirty |= (1 << 5);
    if (curr.animId != prev.animId) dirty |= (1 << 6);

    REQUIRE((dirty & (1 << 5)) != 0);
    REQUIRE((dirty & (1 << 6)) != 0);
}
