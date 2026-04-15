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
    lastPollTime_ = currentTime;
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

    // Any valid packet from a known client resets the heartbeat timeout
    client->lastHeartbeat = currentTime;

    // Process acks on client's reliability layer
    client->reliability.processAck(hdr.ack, hdr.ackBits, currentTime);
    bool isNewPacket = client->reliability.onReceive(hdr.sequence);

    // Skip duplicate reliable packets (retransmits we already processed)
    if (!isNewPacket && hdr.channel != Channel::Unreliable) {
        return;
    }

    if (hdr.packetType == PacketType::Heartbeat) {
        return; // heartbeat is just a keep-alive, no payload to process
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

        // Validate payloadSize against actual remaining bytes in packet
        size_t actualRemaining = static_cast<size_t>(size) - r.position();
        if (payloadLen > actualRemaining) {
            LOG_WARN("NetServer", "Client %d payloadSize mismatch: claimed %zu, actual %zu",
                     client->clientId, payloadLen, actualRemaining);
            return;
        }

        uint8_t decryptedBuf[MAX_PACKET_SIZE];
        if (client->crypto.hasKeys() && !isSystemPacket(hdr.packetType) && payloadLen > 0) {
            if (payloadLen <= PacketCrypto::TAG_SIZE) return; // too short
            size_t decryptedSize = payloadLen - PacketCrypto::TAG_SIZE;
            if (!client->crypto.decrypt(payloadData, payloadLen, client->crypto.buildDecryptNonce(hdr.sequence),
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
        payload += 16;
        payloadSize -= 16;
    }

    // Read client DH public key if present (32 bytes appended after auth token)
    if (payloadSize >= PacketCrypto::PUBLIC_KEY_SIZE && payload) {
        std::memcpy(client->clientPublicKey.data(), payload, PacketCrypto::PUBLIC_KEY_SIZE);
        client->hasClientPublicKey = true;
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

    // Burn sequence 0 so the reliability layer starts at 1.  The raw
    // ConnectAccept above used seq=0 in its header; without this, the
    // first real ReliableOrdered packet would also get seq=0 and the
    // client's duplicate filter would drop it.
    client->reliability.nextLocalSequence();

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
    hdr.channel = Channel::Unreliable; // must be Unreliable: this raw packet bypasses the
                                       // connection reliability layer, so its sequence=0 would
                                       // collide with ConnectAccept and be dropped as duplicate
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
    // Back-pressure: if the client's reliability queue is >75% full, the
    // client is almost certainly dead or severely lagged. Drop new reliable
    // packets to avoid filling the queue and spamming "Dropping oldest" warnings.
    if (channel != Channel::Unreliable && client.reliability.isCongested()) {
        return;
    }

    // Use a larger buffer for payloads that exceed the standard MTU-safe size
    // (e.g. inventory sync with display names and rolled stats).
    constexpr size_t LARGE_PACKET_SIZE = 4096;
    size_t bufSize = (payloadSize + PACKET_HEADER_SIZE + PacketCrypto::TAG_SIZE > MAX_PACKET_SIZE)
                     ? LARGE_PACKET_SIZE : MAX_PACKET_SIZE;
    uint8_t stackBuf[LARGE_PACKET_SIZE];
    ByteWriter w(stackBuf, bufSize);

    uint16_t ack;
    uint32_t ackBits;
    client.reliability.buildAckFields(ack, ackBits);

    PacketHeader hdr;
    hdr.sessionToken = client.sessionToken;
    hdr.sequence = client.reliability.nextLocalSequence();
    hdr.ack = ack;
    hdr.ackBits = ackBits;
    hdr.channel = channel;
    hdr.packetType = packetType;

    // Encrypt payload for non-system packets when keys are available
    uint8_t encryptedBuf[LARGE_PACKET_SIZE];
    const uint8_t* sendPayload = payload;
    size_t sendPayloadSize = payloadSize;

    if (client.crypto.hasKeys() && !isSystemPacket(packetType) && payload && payloadSize > 0) {
        size_t encSize = payloadSize + PacketCrypto::TAG_SIZE;
        if (encSize <= sizeof(encryptedBuf) &&
            client.crypto.encrypt(payload, payloadSize, client.crypto.buildEncryptNonce(hdr.sequence), encryptedBuf, sizeof(encryptedBuf))) {
            sendPayload = encryptedBuf;
            sendPayloadSize = encSize;
        }
    }

    hdr.payloadSize = static_cast<uint16_t>(sendPayloadSize);
    hdr.write(w);

    if (sendPayload && sendPayloadSize > 0) {
        w.writeBytes(sendPayload, sendPayloadSize);
    }

    if (w.overflowed()) {
        LOG_ERROR("NetServer", "Packet overflow: type=0x%02X payloadSize=%zu capacity=%zu for client %d",
                  packetType, payloadSize, bufSize, client.clientId);
        return;
    }

    socket_.sendTo(stackBuf, w.size(), client.address);

    if (channel != Channel::Unreliable) {
        client.reliability.trackReliable(hdr.sequence, stackBuf, w.size(), lastPollTime_);
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
        float retransmitDelay = (std::max)(0.2f, client.reliability.rtt() * 2.0f);
        auto retransmits = client.reliability.getRetransmits(currentTime, retransmitDelay);
        for (auto& pkt : retransmits) {
            // Patch stale ack/ackBits in stored buffer with fresh values
            // Header layout: protocolId(2) + sessionToken(4) + sequence(2) + ack(2) + ackBits(4)
            if (pkt->data.size() >= PACKET_HEADER_SIZE) {
                uint16_t freshAck;
                uint32_t freshAckBits;
                client.reliability.buildAckFields(freshAck, freshAckBits);
                std::memcpy(const_cast<uint8_t*>(pkt->data.data()) + 8, &freshAck, sizeof(freshAck));
                std::memcpy(const_cast<uint8_t*>(pkt->data.data()) + 10, &freshAckBits, sizeof(freshAckBits));
            }
            socket_.sendTo(pkt->data.data(), pkt->data.size(), client.address);
            client.reliability.markRetransmitted(pkt->sequence, currentTime);
        }
    });
}

std::vector<uint16_t> NetServer::checkTimeouts(float currentTime, float timeoutSec) {
    return connections_.getTimedOutClients(currentTime, timeoutSec);
}

} // namespace fate
