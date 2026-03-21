#include <doctest/doctest.h>
#include "engine/net/net_server.h"
#include <thread>
#include <chrono>

using namespace fate;

TEST_CASE("NetServer: client connect and receive accept") {
    NetSocket::initPlatform();

    NetServer server;
    REQUIRE(server.start(0));

    uint16_t connectedId = 0;
    server.onClientConnected = [&](uint16_t id) { connectedId = id; };

    // Client sends Connect
    NetSocket clientSock;
    REQUIRE(clientSock.open(0));
    NetAddress serverAddr = NetAddress::makeIPv4(0x7F000001, server.port());

    uint8_t buf[MAX_PACKET_SIZE];
    ByteWriter w(buf, sizeof(buf));
    PacketHeader hdr;
    hdr.packetType = PacketType::Connect;
    hdr.channel = Channel::ReliableOrdered;
    hdr.payloadSize = 0;
    hdr.write(w);
    clientSock.sendTo(buf, w.size(), serverAddr);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll(0.0f);

    CHECK(connectedId != 0);
    CHECK(server.connections().clientCount() == 1);

    // Client receives ConnectAccept
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    NetAddress from;
    int received = clientSock.recvFrom(buf, sizeof(buf), from);
    REQUIRE(received > 0);

    ByteReader r(buf, received);
    PacketHeader respHdr = PacketHeader::read(r);
    CHECK(respHdr.packetType == PacketType::ConnectAccept);

    uint16_t assignedId = r.readU16();
    uint32_t token = r.readU32();
    CHECK(assignedId == connectedId);
    CHECK(token != 0);

    clientSock.close();
    server.stop();
    NetSocket::shutdownPlatform();
}
