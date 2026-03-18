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

TEST_CASE("Replication Integration: two clients see each other") {
    NetSocket::initPlatform();

    World world;
    ReplicationManager replication;
    NetServer server;
    REQUIRE(server.start(0));

    NetAddress serverAddr{0x7F000001, server.port()};

    // Track entity enter messages received
    std::vector<SvEntityEnterMsg> client1Enters, client2Enters;

    // Map clientId to PersistentId of its player entity
    std::unordered_map<uint16_t, PersistentId> clientPlayerPids;

    // Server callbacks
    server.onClientConnected = [&](uint16_t clientId) {
        // Create player entity at origin
        Entity* player = world.createEntity("Player" + std::to_string(clientId));
        auto* t = player->addComponent<Transform>(0.0f, 0.0f);
        auto* cs = player->addComponent<CharacterStatsComponent>();
        cs->stats.level = 1;
        cs->stats.currentHP = 100;
        cs->stats.maxHP = 100;
        auto* np = player->addComponent<NameplateComponent>();
        np->displayName = "Player" + std::to_string(clientId);
        np->displayLevel = 1;

        PersistentId pid = PersistentId::generate(1);
        replication.registerEntity(player->handle(), pid);
        clientPlayerPids[clientId] = pid;

        auto* client = server.connections().findById(clientId);
        if (client) client->playerEntityId = pid.value();
    };

    // Connect client 1
    NetSocket client1Sock;
    REQUIRE(client1Sock.open(0));
    {
        uint8_t buf[MAX_PACKET_SIZE];
        ByteWriter w(buf, sizeof(buf));
        PacketHeader hdr;
        hdr.packetType = PacketType::Connect;
        hdr.channel = Channel::ReliableOrdered;
        hdr.payloadSize = 0;
        hdr.write(w);
        client1Sock.sendTo(buf, w.size(), serverAddr);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll(0.0f);
    CHECK(server.connections().clientCount() == 1);

    // Connect client 2
    NetSocket client2Sock;
    REQUIRE(client2Sock.open(0));
    {
        uint8_t buf[MAX_PACKET_SIZE];
        ByteWriter w(buf, sizeof(buf));
        PacketHeader hdr;
        hdr.packetType = PacketType::Connect;
        hdr.channel = Channel::ReliableOrdered;
        hdr.payloadSize = 0;
        hdr.write(w);
        client2Sock.sendTo(buf, w.size(), serverAddr);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll(0.1f);
    CHECK(server.connections().clientCount() == 2);

    // Run replication — both players should see each other
    replication.update(world, server);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Client 1 should have received SvEntityEnter for player 2
    {
        uint8_t buf[MAX_PACKET_SIZE];
        NetAddress from;
        for (int i = 0; i < 10; ++i) { // drain all packets
            int received = client1Sock.recvFrom(buf, sizeof(buf), from);
            if (received <= 0) break;
            ByteReader r(buf, received);
            PacketHeader hdr = PacketHeader::read(r);
            if (hdr.packetType == PacketType::SvEntityEnter) {
                auto msg = SvEntityEnterMsg::read(r);
                client1Enters.push_back(msg);
            }
        }
    }

    // Verify the enter message has the other player's name
    REQUIRE(!client1Enters.empty());
    // At least one enter should be for "Player" (the other player)
    bool foundPlayer = false;
    for (auto& e : client1Enters) {
        if (e.name.find("Player") != std::string::npos) foundPlayer = true;
    }
    CHECK(foundPlayer);

    client1Sock.close();
    client2Sock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}
