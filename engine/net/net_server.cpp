#include "engine/net/net_server.h"
#include "engine/net/protocol.h"
#include "engine/net/packet_crypto.h"
#include <cstring>

namespace fate {

bool NetServer::start(uint16_t port) {
    if (!socket_.open(port)) {
        LOG_ERROR("NetServer", "Failed to open socket on port %d", port);
        return false;
    }
    LOG_INFO("NetServer", "Listening on port %d", socket_.port());
    return true;
}

void NetServer::stop() {
    socket_.close();
    LOG_INFO("NetServer", "Stopped");
}

void NetServer::poll(float currentTime) {
    uint8_t buf[MAX_PACKET_SIZE];
    NetAddress from;
    int received;
    while ((received = socket_.recvFrom(buf, sizeof(buf), from)) > 0) {
        handleRawPacket(from, buf, received, currentTime);
    }
}

void NetServer::handleRawPacket(const NetAddress& from, const uint8_t* data, int size, float currentTime) {
    if (size < static_cast<int>(PACKET_HEADER_SIZE)) return;

    ByteReader r(data, static_cast<size_t>(size));
    PacketHeader hdr = PacketHeader::read(r);

    if (hdr.protocolId != PROTOCOL_ID) return;

    if (hdr.packetType == PacketType::Connect) {
        const uint8_t* payload = (size > static_cast<int>(PACKET_HEADER_SIZE)) ? data + PACKET_HEADER_SIZE : nullptr;
        size_t payloadSize = (size > static_cast<int>(PACKET_HEADER_SIZE)) ? size - PACKET_HEADER_SIZE : 0;
        handleConnect(from, payload, payloadSize, currentTime);
        return;
    }

    // Find client by address
    ClientConnection* client = connections_.findByAddress(from);
    if (!client) return;

    // Validate session token
    if (hdr.sessionToken != client->sessionToken) return;

    // Process acks on client's reliability layer
    client->reliability.processAck(hdr.ack, hdr.ackBits, currentTime);
    bool isNewPacket = client->reliability.onReceive(hdr.sequence);

    // Skip duplicate reliable packets (retransmits we already processed)
    if (!isNewPacket && hdr.channel != Channel::Unreliable) {
        return;
    }

    if (hdr.packetType == PacketType::Heartbeat) {
        client->lastHeartbeat = currentTime;
        return;
    }

    if (hdr.packetType == PacketType::Disconnect) {
        uint16_t id = client->clientId;
        if (onClientDisconnected) onClientDisconnected(id);
        connections_.removeClient(id);
        LOG_INFO("NetServer", "Client %d disconnected", id);
        return;
    }

    // Game packet — decrypt if crypto is active, then forward to callback
    if (onPacketReceived) {
        const uint8_t* payloadData = data + r.position();
        size_t payloadLen = hdr.payloadSize;

        uint8_t decryptedBuf[MAX_PACKET_SIZE];
        if (client->crypto.hasKeys() && !isSystemPacket(hdr.packetType) && payloadLen > 0) {
            if (payloadLen <= PacketCrypto::TAG_SIZE) return; // too short
            size_t decryptedSize = payloadLen - PacketCrypto::TAG_SIZE;
            if (!client->crypto.decrypt(payloadData, payloadLen, hdr.sequence,
                                        decryptedBuf, sizeof(decryptedBuf))) {
                return; // tampered or wrong key — silently drop
            }
            payloadData = decryptedBuf;
            payloadLen = decryptedSize;
        }

        ByteReader payload(payloadData, payloadLen);
        onPacketReceived(client->clientId, hdr.packetType, payload);
    }
}

void NetServer::handleConnect(const NetAddress& from, const uint8_t* payload, size_t payloadSize, float currentTime) {
    // Check if already connected — re-send ConnectAccept
    ClientConnection* existing = connections_.findByAddress(from);
    if (existing) {
        // Re-send ConnectAccept
        uint8_t payloadBuf[8];
        ByteWriter pw(payloadBuf, sizeof(payloadBuf));
        pw.writeU16(existing->clientId);
        pw.writeU32(existing->sessionToken);

        uint8_t buf[MAX_PACKET_SIZE];
        ByteWriter w(buf, sizeof(buf));
        PacketHeader hdr;
        hdr.packetType = PacketType::ConnectAccept;
        hdr.channel = Channel::ReliableOrdered;
        hdr.payloadSize = static_cast<uint16_t>(pw.size());
        hdr.write(w);
        w.writeBytes(payloadBuf, pw.size());
        socket_.sendTo(buf, w.size(), from);
        return;
    }

    // Check protocol version (first byte of payload)
    if (payloadSize >= 1 && payload) {
        uint8_t clientVersion = payload[0];
        if (clientVersion != PROTOCOL_VERSION) {
            std::string reason = "Version mismatch: server=" + std::to_string(PROTOCOL_VERSION)
                               + " client=" + std::to_string(clientVersion);
            sendConnectReject(from, reason);
            LOG_WARN("NetServer", "%s from %s", reason.c_str(), from.toString().c_str());
            return;
        }
        // Skip version byte for auth token extraction below
        payload += 1;
        payloadSize -= 1;
    }

    // Add new client (initializes lastHeartbeat to currentTime)
    uint16_t clientId = connections_.addClient(from, currentTime);
    ClientConnection* client = connections_.findById(clientId);
    if (!client) return;

    // Read auth token from Connect payload (16 bytes)
    if (payloadSize >= 16 && payload) {
        std::memcpy(client->authToken.data(), payload, 16);
    }

    // Build and send ConnectAccept
    uint8_t payloadBuf[8];
    ByteWriter pw(payloadBuf, sizeof(payloadBuf));
    pw.writeU16(clientId);
    pw.writeU32(client->sessionToken);

    uint8_t buf[MAX_PACKET_SIZE];
    ByteWriter w(buf, sizeof(buf));
    PacketHeader hdr;
    hdr.packetType = PacketType::ConnectAccept;
    hdr.channel = Channel::ReliableOrdered;
    hdr.payloadSize = static_cast<uint16_t>(pw.size());
    hdr.write(w);
    w.writeBytes(payloadBuf, pw.size());
    socket_.sendTo(buf, w.size(), from);

    if (onClientConnected) onClientConnected(clientId);

    LOG_INFO("NetServer", "Client %d connected from %s", clientId, from.toString().c_str());
}

void NetServer::sendConnectReject(const NetAddress& to, const std::string& reason) {
    uint8_t payloadBuf[256];
    ByteWriter pw(payloadBuf, sizeof(payloadBuf));
    pw.writeString(reason);

    uint8_t buf[MAX_PACKET_SIZE];
    ByteWriter w(buf, sizeof(buf));
    PacketHeader hdr;
    hdr.packetType = PacketType::ConnectReject;
    hdr.channel = Channel::ReliableOrdered;
    hdr.payloadSize = static_cast<uint16_t>(pw.size());
    hdr.write(w);
    w.writeBytes(payloadBuf, pw.size());
    socket_.sendTo(buf, w.size(), to);
    LOG_INFO("NetServer", "Sent ConnectReject: %s", reason.c_str());
}

void NetServer::sendTo(uint16_t clientId, Channel channel, uint8_t packetType,
                       const uint8_t* payload, size_t payloadSize) {
    ClientConnection* client = connections_.findById(clientId);
    if (!client) return;
    sendPacket(*client, channel, packetType, payload, payloadSize);
}

void NetServer::sendPacket(ClientConnection& client, Channel channel, uint8_t packetType,
                           const uint8_t* payload, size_t payloadSize) {
    uint8_t buf[MAX_PACKET_SIZE];
    ByteWriter w(buf, sizeof(buf));

    uint16_t ack, ackBits;
    client.reliability.buildAckFields(ack, ackBits);

    PacketHeader hdr;
    hdr.sessionToken = client.sessionToken;
    hdr.sequence = client.reliability.nextLocalSequence();
    hdr.ack = ack;
    hdr.ackBits = ackBits;
    hdr.channel = channel;
    hdr.packetType = packetType;

    // Encrypt payload for non-system packets when keys are available
    uint8_t encryptedBuf[MAX_PACKET_SIZE];
    const uint8_t* sendPayload = payload;
    size_t sendPayloadSize = payloadSize;

    if (client.crypto.hasKeys() && !isSystemPacket(packetType) && payload && payloadSize > 0) {
        size_t encSize = payloadSize + PacketCrypto::TAG_SIZE;
        if (encSize <= sizeof(encryptedBuf) &&
            client.crypto.encrypt(payload, payloadSize, hdr.sequence, encryptedBuf, sizeof(encryptedBuf))) {
            sendPayload = encryptedBuf;
            sendPayloadSize = encSize;
        }
    }

    hdr.payloadSize = static_cast<uint16_t>(sendPayloadSize);
    hdr.write(w);

    if (sendPayload && sendPayloadSize > 0) {
        w.writeBytes(sendPayload, sendPayloadSize);
    }

    socket_.sendTo(buf, w.size(), client.address);

    if (channel != Channel::Unreliable) {
        client.reliability.trackReliable(hdr.sequence, buf, w.size());
    }
}

void NetServer::broadcast(Channel channel, uint8_t packetType,
                          const uint8_t* payload, size_t payloadSize) {
    connections_.forEach([&](ClientConnection& client) {
        sendPacket(client, channel, packetType, payload, payloadSize);
    });
}

void NetServer::processRetransmits(float currentTime) {
    connections_.forEach([&](ClientConnection& client) {
        auto retransmits = client.reliability.getRetransmits(currentTime);
        for (auto& pkt : retransmits) {
            socket_.sendTo(pkt.data.data(), pkt.data.size(), client.address);
            client.reliability.markRetransmitted(pkt.sequence, currentTime);
        }
    });
}

std::vector<uint16_t> NetServer::checkTimeouts(float currentTime, float timeoutSec) {
    return connections_.getTimedOutClients(currentTime, timeoutSec);
}

} // namespace fate
