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
    NetAddress serverAddr = NetAddress::makeIPv4(0x7F000001, server.port());

    // Create mob entity near origin
    Entity* mob = world.createEntity("Slime");
    mob->addComponent<Transform>(50.0f, 0.0f);
    auto* mobNp = mob->addComponent<MobNameplateComponent>();
    mobNp->displayName = "Slime";
    auto* mobStats = mob->addComponent<EnemyStatsComponent>();
    mobStats->stats.currentHP = 100;
    mobStats->stats.maxHP = 100;
    mobStats->stats.isAlive = true;
    PersistentId mobPid = PersistentId::generate(1);
    replication.registerEntity(mob->handle(), mobPid);

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

    server.onPacketReceived = [&](uint16_t clientId, uint8_t type, ByteReader& payload) {
        if (type == PacketType::CmdAction) {
            auto action = CmdAction::read(payload);
            if (action.actionType == 0) {
                PersistentId targetPid(action.targetId);
                EntityHandle targetH = replication.getEntityHandle(targetPid);
                Entity* target = world.getEntity(targetH);
                if (!target) return;

                auto* es = target->getComponent<EnemyStatsComponent>();
                if (!es || !es->stats.isAlive) return;

                int damage = 15;
                es->stats.takeDamage(damage);

                SvCombatEventMsg evt;
                evt.attackerId = playerPid.value();
                evt.targetId = targetPid.value();
                evt.damage = damage;
                evt.isKill = !es->stats.isAlive ? 1 : 0;

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
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.poll(0.0f);
    CHECK(connectedClientId != 0);

    // Drain ConnectAccept
    {
        uint8_t buf[MAX_PACKET_SIZE];
        NetAddress from;
        clientSock.recvFrom(buf, sizeof(buf), from);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto* clientConn = server.connections().findById(connectedClientId);
    REQUIRE(clientConn != nullptr);
    uint32_t token = clientConn->sessionToken;

    // Client sends CmdAction — attack the mob
    {
        CmdAction action;
        action.actionType = 0;
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
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
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
    CHECK(mobStats->stats.currentHP == 85);

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}
