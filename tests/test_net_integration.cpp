#include <doctest/doctest.h>
#include "engine/net/socket.h"
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"
#include "engine/net/reliability.h"
#include "engine/net/connection.h"
#include <thread>
#include <chrono>

using namespace fate;

TEST_CASE("Net Integration: reliable connect handshake via loopback") {
    NetSocket::initPlatform();

    NetSocket serverSock, clientSock;
    REQUIRE(serverSock.open(0));
    REQUIRE(clientSock.open(0));

    NetAddress serverAddr = NetAddress::makeIPv4(0x7F000001, serverSock.port());

    // Client sends Connect
    ReliabilityLayer clientRel;
    uint16_t seq = clientRel.nextLocalSequence();

    uint8_t sendBuf[MAX_PACKET_SIZE];
    ByteWriter w(sendBuf, sizeof(sendBuf));

    PacketHeader hdr;
    hdr.sequence = seq;
    hdr.channel = Channel::ReliableOrdered;
    hdr.packetType = PacketType::Connect;
    hdr.payloadSize = 0;
    hdr.write(w);

    clientSock.sendTo(sendBuf, w.size(), serverAddr);
    clientRel.trackReliable(seq, sendBuf, w.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Server receives
    uint8_t recvBuf[MAX_PACKET_SIZE];
    NetAddress fromAddr;
    int received = serverSock.recvFrom(recvBuf, sizeof(recvBuf), fromAddr);
    REQUIRE(received > 0);

    ByteReader r(recvBuf, received);
    PacketHeader recvHdr = PacketHeader::read(r);
    CHECK(recvHdr.protocolId == PROTOCOL_ID);
    CHECK(recvHdr.packetType == PacketType::Connect);

    // Server processes
    ReliabilityLayer serverRel;
    serverRel.onReceive(recvHdr.sequence);

    ConnectionManager mgr;
    uint16_t clientId = mgr.addClient(fromAddr);
    auto* client = mgr.findById(clientId);
    REQUIRE(client != nullptr);

    // Server sends ConnectAccept
    uint16_t ack; uint32_t ackBits;
    serverRel.buildAckFields(ack, ackBits);

    uint8_t respBuf[MAX_PACKET_SIZE];
    ByteWriter rw(respBuf, sizeof(respBuf));

    PacketHeader respHdr;
    respHdr.sessionToken = client->sessionToken;
    respHdr.sequence = serverRel.nextLocalSequence();
    respHdr.ack = ack;
    respHdr.ackBits = ackBits;
    respHdr.channel = Channel::ReliableOrdered;
    respHdr.packetType = PacketType::ConnectAccept;

    // Build payload
    uint8_t payload[8];
    ByteWriter pw(payload, sizeof(payload));
    pw.writeU16(clientId);
    pw.writeU32(client->sessionToken);
    respHdr.payloadSize = static_cast<uint16_t>(pw.size());

    respHdr.write(rw);
    rw.writeBytes(payload, pw.size());

    serverSock.sendTo(respBuf, rw.size(), fromAddr);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Client receives ConnectAccept
    received = clientSock.recvFrom(recvBuf, sizeof(recvBuf), fromAddr);
    REQUIRE(received > 0);

    ByteReader cr(recvBuf, received);
    PacketHeader acceptHdr = PacketHeader::read(cr);
    CHECK(acceptHdr.packetType == PacketType::ConnectAccept);

    // Ack clears pending
    clientRel.processAck(acceptHdr.ack, acceptHdr.ackBits);
    CHECK(clientRel.pendingReliableCount() == 0);

    // Verify payload
    uint16_t assignedId = cr.readU16();
    uint32_t token = cr.readU32();
    CHECK(assignedId == clientId);
    CHECK(token == client->sessionToken);

    serverSock.close();
    clientSock.close();
    NetSocket::shutdownPlatform();
}
