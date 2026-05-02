#include "engine/net/net_server.h"
#include "engine/net/protocol.h"
#include "engine/net/packet_crypto.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

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

    // S143 audit — count inbound traffic per opcode after session validation,
    // before any further processing. Lets the 5s stats line prove whether
    // CmdAckExtended (0xE1) is actually arriving during a retransmit storm
    // and whether CmdMove/Heartbeat are flowing (i.e. client is alive).
    ++opcodeStats_.receivedPackets[hdr.packetType];
    opcodeStats_.receivedBytes[hdr.packetType] += static_cast<uint64_t>(size);

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
        return;
    }

    // v9: CmdAckExtended carries seqs that fell outside the inline 64-bit
    // ackBits window. Handled inline because it's unencrypted control data
    // (no payload secrets — just seq numbers) and needs to drain the pending
    // queue before any other ACK-aware processing runs this tick.
    if (hdr.packetType == PacketType::CmdAckExtended) {
        const uint8_t* payloadData = data + r.position();
        size_t payloadLen = hdr.payloadSize;
        size_t actualRemaining = static_cast<size_t>(size) - r.position();
        if (payloadLen > actualRemaining) return;
        if (payloadLen < 2) return; // need at least the u16 count
        ByteReader payload(payloadData, payloadLen);
        uint16_t count = payload.readU16();
        // Bound to the maximum that fits in a 64-bit ackBits window worth of
        // stranded seqs — prevents a malformed/hostile packet from triggering
        // an O(N × pending) scan with huge N.
        constexpr uint16_t kMaxExtendedAcks = 512;
        if (count > kMaxExtendedAcks) return;
        if (payload.remaining() < static_cast<size_t>(count) * sizeof(uint16_t)) return;
        std::vector<uint16_t> seqs(count);
        for (uint16_t i = 0; i < count; ++i) seqs[i] = payload.readU16();
        client->reliability.processExtendedAck(seqs.data(), seqs.size(), currentTime);
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

        // C4: CmdAuthProof carries the 16-byte auth token, encrypted under the
        // Noise_NK session key.  It MUST arrive before any game packets are
        // dispatched — otherwise an attacker could send commands without ever
        // proving they hold a valid auth token.
        if (hdr.packetType == PacketType::CmdAuthProof) {
            if (client->authPhase != ClientConnection::AuthPhase::HandshakePending) {
                LOG_WARN("NetServer", "Client %d sent duplicate AuthProof", client->clientId);
                return;
            }
            if (payloadLen != 16) {
                LOG_WARN("NetServer", "Client %d sent malformed AuthProof (%zu bytes)", client->clientId, payloadLen);
                return;
            }
            AuthToken tok{};
            std::memcpy(tok.data(), payloadData, 16);
            client->authToken = tok;
            // Move to ProofReceived BEFORE calling the verifier; the verifier
            // (ServerApp::onAuthProofReceived) either advances to Authenticated
            // on success or removes the client on failure.
            client->authPhase = ClientConnection::AuthPhase::ProofReceived;
            client->hasCompletedAuthProof = true;
            if (onAuthProofReceived) onAuthProofReceived(client->clientId, tok);
            return;
        }

        // Any non-system game packet before auth finishes is rejected. Prior
        // to the state-machine split this gated on hasCompletedAuthProof, which
        // flipped true as soon as the 16-byte proof arrived — BEFORE the
        // app-level verifier validated the token. We now require the full
        // Authenticated state, which the verifier sets only after token
        // lookup + (async) DB load kicks off successfully.
        if (!isSystemPacket(hdr.packetType) &&
            client->authPhase != ClientConnection::AuthPhase::Authenticated) {
            LOG_WARN("NetServer", "Client %d sent game packet 0x%02X in auth phase %u — dropped",
                     client->clientId, hdr.packetType, static_cast<unsigned>(client->authPhase));
            return;
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
        uint8_t payloadBuf[16];
        ByteWriter pw(payloadBuf, sizeof(payloadBuf));
        pw.writeU16(existing->clientId);
        pw.writeU32(static_cast<uint32_t>(existing->sessionToken & 0xFFFFFFFFULL));
        pw.writeU32(static_cast<uint32_t>(existing->sessionToken >> 32));

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

    // C4: no auth token in plaintext Connect anymore.  Payload is just the DH
    // ephemeral public key.  The auth token arrives encrypted via CmdAuthProof
    // after the Noise_NK handshake completes.
    if (payloadSize >= PacketCrypto::PUBLIC_KEY_SIZE && payload) {
        std::memcpy(client->clientPublicKey.data(), payload, PacketCrypto::PUBLIC_KEY_SIZE);
        client->hasClientPublicKey = true;
    }

    // Build and send ConnectAccept
    uint8_t payloadBuf[16];
    ByteWriter pw(payloadBuf, sizeof(payloadBuf));
    pw.writeU16(clientId);
    pw.writeU32(static_cast<uint32_t>(client->sessionToken & 0xFFFFFFFFULL));
    pw.writeU32(static_cast<uint32_t>(client->sessionToken >> 32));

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
    //
    // Session 71 WU1 — critical-lane bypass. A handful of packet types are
    // load-bearing for playability (entity visibility, death overlay, zone
    // transition, scene-ready) and MUST survive backpressure. Dropping them
    // strands the client in a desynced state (invisible mobs attacking,
    // no death panel on death, stuck on loading screen). Pass them through
    // and let the reliability queue grow past the soft cap if needed; the
    // hard cap at MAX_PENDING_PACKETS still protects the server.
    if (channel != Channel::Unreliable && client.reliability.isCongested()) {
        if (!isCriticalLane(packetType)) {
            client.reliability.noteNonCriticalDrop();
            ++opcodeStats_.backpressureDrops[packetType]; // S141 Phase A
            constexpr float kBackpressureLogIntervalSec = 30.0f;
            if (lastPollTime_ - client.lastBackpressureLogTime >= kBackpressureLogIntervalSec) {
                size_t pending = client.reliability.pendingReliableCount();
                LOG_WARN("Reliability",
                         "Client %d queue congestion %zu/%zu (%zu%%) — dropped %u non-critical, preserved %u critical over last %.0fs",
                         client.clientId, pending, ReliabilityLayer::MAX_PENDING_PACKETS,
                         pending * 100 / ReliabilityLayer::MAX_PENDING_PACKETS,
                         client.reliability.droppedNonCritical(),
                         client.reliability.preservedCritical(),
                         kBackpressureLogIntervalSec);
                // S141 Phase A — dump in-window opcode stats alongside the
                // congestion log so we can see WHICH opcodes are filling the
                // queue at the moment of saturation. No reset here; the 5s
                // tick-stats flush still owns the window.
                std::string netLine = formatOpcodeStats();
                if (!netLine.empty()) {
                    LOG_WARN("Reliability", "%s", netLine.c_str());
                }
                client.reliability.resetBackpressureCounters();
                client.lastBackpressureLogTime = lastPollTime_;
            }
            return;
        }
        client.reliability.noteCriticalPreserve();
    }

    // Use a larger buffer for payloads that exceed the standard MTU-safe size
    // (e.g. inventory sync with display names and rolled stats). Raised from
    // 4K to 16K after observing SvInventorySync hit ~4080 bytes and silently
    // drop: encrypted payload + header exceeded 4K, overflow check returned
    // early, client never received the update.
    constexpr size_t LARGE_PACKET_SIZE = 16384;
    size_t bufSize = (payloadSize + PACKET_HEADER_SIZE + PacketCrypto::TAG_SIZE > MAX_PACKET_SIZE)
                     ? LARGE_PACKET_SIZE : MAX_PACKET_SIZE;
    uint8_t stackBuf[LARGE_PACKET_SIZE];
    ByteWriter w(stackBuf, bufSize);

    uint16_t ack;
    uint64_t ackBits; // v9: widened 32→64 bit ACK window
    client.reliability.buildAckFields(ack, ackBits);

    PacketHeader hdr;
    hdr.sessionToken = client.sessionToken;
    hdr.sequence = client.reliability.nextLocalSequence();
    hdr.ack = ack;
    hdr.ackBits = ackBits;
    hdr.channel = channel;
    hdr.packetType = packetType;

    // Encrypt payload for non-system packets when keys are available.
    // Fail closed if encryption can't be performed on an encrypted session —
    // otherwise we would silently send plaintext to a client expecting AEAD,
    // which the client would reject and the server would treat as lost.
    // Mirrors NetClient::sendPacket.
    uint8_t encryptedBuf[LARGE_PACKET_SIZE];
    const uint8_t* sendPayload = payload;
    size_t sendPayloadSize = payloadSize;

    if (client.crypto.hasKeys() && !isSystemPacket(packetType) && payload && payloadSize > 0) {
        size_t encSize = payloadSize + PacketCrypto::TAG_SIZE;
        if (encSize > sizeof(encryptedBuf) ||
            !client.crypto.encrypt(payload, payloadSize, client.crypto.buildEncryptNonce(hdr.sequence), encryptedBuf, sizeof(encryptedBuf))) {
            LOG_ERROR("NetServer", "Encryption failed: type=0x%02X payloadSize=%zu client=%d — dropped (fail-closed)",
                      packetType, payloadSize, client.clientId);
            return;
        }
        sendPayload = encryptedBuf;
        sendPayloadSize = encSize;
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

    // S141 Phase A — count successful sends per opcode (on-wire bytes incl.
    // PacketHeader + AEAD tag). Reliable packets are also tracked so their
    // potential eviction can be attributed back to this opcode.
    ++opcodeStats_.sentPackets[packetType];
    opcodeStats_.sentBytes[packetType] += w.size();

    if (channel != Channel::Unreliable) {
        uint8_t evicted = client.reliability.trackReliable(
            hdr.sequence, packetType, stackBuf, w.size(), lastPollTime_);
        if (evicted != 0) {
            ++opcodeStats_.hardEvictions[evicted];
        }
        // S141 Phase A — sample maxPendingDepth at every enqueue, not just
        // once per tick. The tick-boundary sample (sampleMaxPendingDepth)
        // misses sub-tick spikes that fill and drain inside a single frame,
        // which is exactly the AOE-burst pattern we want the stats line to
        // surface. Per-enqueue check is one branch and one load — cheap.
        size_t pending = client.reliability.pendingReliableCount();
        if (pending > opcodeStats_.maxPendingDepth) {
            opcodeStats_.maxPendingDepth = pending;
        }
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
            // Patch stale ack/ackBits in stored buffer with fresh values.
            // Header v9 layout: protocolId(2) + sessionToken(8) + sequence(2) + ack(2) + ackBits(8)
            //                   = ack at offset 12, ackBits at offset 14 (8 bytes).
            // Getting these offsets wrong corrupts channel(22) and packetType(23),
            // causing retransmits to arrive as "Unknown packet type 0x00" at the peer.
            if (pkt->data.size() >= PACKET_HEADER_SIZE) {
                uint16_t freshAck;
                uint64_t freshAckBits;
                client.reliability.buildAckFields(freshAck, freshAckBits);
                std::memcpy(const_cast<uint8_t*>(pkt->data.data()) + 12, &freshAck, sizeof(freshAck));
                std::memcpy(const_cast<uint8_t*>(pkt->data.data()) + 14, &freshAckBits, sizeof(freshAckBits));
            }
            socket_.sendTo(pkt->data.data(), pkt->data.size(), client.address);
            // S141 Phase A / S143 audit — attribute retransmits to the
            // originating opcode. Header v9 layout puts packetType at offset
            // 23 (after channel byte at 22). Stored buffer is the same one
            // we sent; safe to read. retransmitBytes carries the retx wire
            // bytes separately from sentBytes (which is fresh-only), so the
            // 5s stats line shows fresh vs retx ratio directly. Total wire
            // load for an opcode = sentBytes + retransmitBytes.
            if (pkt->data.size() > 23) {
                uint8_t opcode = pkt->data[23];
                ++opcodeStats_.retransmits[opcode];
                opcodeStats_.retransmitBytes[opcode] += pkt->data.size();
            }
            client.reliability.markRetransmitted(pkt->sequence, currentTime);
        }
    });
}

std::vector<uint16_t> NetServer::checkTimeouts(float currentTime, float timeoutSec) {
    return connections_.getTimedOutClients(currentTime, timeoutSec);
}

// S141 Phase A — sample the deepest reliable pending queue across all clients
// into the current window. Called once per tick by ServerApp before the 5s
// flush so the worst spike is captured even if it recovers within the window.
//
// S143 audit — also tally the pending queue by opcode and update the per-
// opcode window max + oldest-age max. Without this we could see "queue is
// 200 deep" but not "of those 200, 198 are SvEntityEnterBatch and the
// oldest is 8.4s old" — the latter is the exact stuck-queue evidence the
// EntityEnterBatch retx=3440 audit needs.
void NetServer::sampleMaxPendingDepth(float currentTime) {
    std::array<uint64_t, 256> tickTally{};
    connections_.forEach([&](ClientConnection& client) {
        size_t pending = client.reliability.pendingReliableCount();
        if (pending > opcodeStats_.maxPendingDepth) {
            opcodeStats_.maxPendingDepth = pending;
        }
        client.reliability.tallyPendingByOpcode(tickTally);
        client.reliability.tallyOldestPendingAgeMs(currentTime, opcodeStats_.oldestPendingAgeMsMax);
    });
    for (size_t i = 0; i < 256; ++i) {
        if (tickTally[i] > opcodeStats_.pendingByOpcodeMax[i]) {
            opcodeStats_.pendingByOpcodeMax[i] = tickTally[i];
        }
    }
}

namespace {
const char* opcodeName(uint8_t op) {
    // Table covers the heavyweights; everything else returns nullptr and the
    // formatter falls back to "0xNN" alone. Add entries here as new traffic
    // sources show up in the telemetry.
    switch (op) {
        case PacketType::SvEntityEnter:        return "EntityEnter";
        case PacketType::SvEntityLeave:        return "EntityLeave";
        case PacketType::SvEntityUpdate:       return "EntityUpdate";
        case PacketType::SvEntityUpdateBatch:  return "EntityUpdateBatch";
        case PacketType::SvEntityEnterBatch:   return "EntityEnterBatch";
        case PacketType::SvCombatEvent:        return "CombatEvent";
        case PacketType::SvSkillResult:        return "SkillResult";
        case PacketType::SvSkillResultBatch:   return "SkillResultBatch";
        case PacketType::SvLootPickup:         return "LootPickup";
        case PacketType::SvInventorySync:      return "InventorySync";
        case PacketType::SvPlayerState:        return "PlayerState";
        case PacketType::SvBuffSync:           return "BuffSync";
        case PacketType::SvBagContents:        return "BagContents";
        case PacketType::SvChatMessage:        return "ChatMessage";
        case PacketType::SvDeathNotify:        return "DeathNotify";
        case PacketType::SvRespawn:            return "Respawn";
        case PacketType::SvScenePopulated:     return "ScenePopulated";
        case PacketType::SvZoneTransition:     return "ZoneTransition";
        case PacketType::Heartbeat:            return "Heartbeat";
        // S143 audit — inbound opcodes worth surfacing in the rx: section so
        // we can see whether the ACK pipeline is alive during a retx storm.
        case PacketType::CmdAckExtended:       return "AckExt";
        case PacketType::CmdMove:              return "Move";
        case PacketType::CmdUseSkill:          return "UseSkill";
        case PacketType::CmdAuthProof:         return "AuthProof";
        default:                               return nullptr;
    }
}
} // namespace

std::string NetServer::formatOpcodeStats() const {
    // Aggregate totals + collect non-zero opcodes.
    // S143 audit: fresh (sentPackets/sentBytes) and retx (retransmits/
    // retransmitBytes) tracked separately. Total wire load = fresh + retx.
    uint64_t totalFreshPackets = 0;
    uint64_t totalFreshBytes = 0;
    uint64_t totalRetxBytes = 0;
    uint64_t totalDrops = 0;
    uint64_t totalEvict = 0;
    uint64_t totalRetx  = 0;
    struct Row {
        uint8_t opcode;
        uint64_t freshBytes;
        uint64_t freshPkts;
        uint64_t retxBytes;
        uint64_t retxCount;
        uint64_t drops;
        uint64_t evict;
    };
    std::vector<Row> rows;
    rows.reserve(64);
    for (size_t i = 0; i < 256; ++i) {
        uint64_t pkts      = opcodeStats_.sentPackets[i];
        uint64_t bytes     = opcodeStats_.sentBytes[i];
        uint64_t drops     = opcodeStats_.backpressureDrops[i];
        uint64_t evict     = opcodeStats_.hardEvictions[i];
        uint64_t retx      = opcodeStats_.retransmits[i];
        uint64_t retxBytes = opcodeStats_.retransmitBytes[i];
        totalFreshPackets += pkts;
        totalFreshBytes   += bytes;
        totalRetxBytes    += retxBytes;
        totalDrops        += drops;
        totalEvict        += evict;
        totalRetx         += retx;
        if (pkts || drops || evict || retx) {
            rows.push_back({static_cast<uint8_t>(i), bytes, pkts, retxBytes, retx, drops, evict});
        }
    }

    size_t maxQ = opcodeStats_.maxPendingDepth;

    if (totalFreshPackets == 0 && totalDrops == 0 && totalEvict == 0 && totalRetx == 0 && maxQ == 0) {
        return {};
    }

    // Top 6 opcodes by total wire bytes (fresh + retx) — the actual bandwidth hogs.
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) {
                  return (a.freshBytes + a.retxBytes) > (b.freshBytes + b.retxBytes);
              });
    const size_t kTop = (std::min)(static_cast<size_t>(6), rows.size());

    // Clamping append: snprintf returns the count it *would have* written, not
    // bytes actually written. Naively adding that to `n` lets it exceed
    // sizeof(buf), and the next call's `sizeof(buf) - n` size argument
    // underflows to a huge size_t — at that point snprintf scribbles past the
    // stack buffer. Clamp after every call so once the buffer fills we
    // silently truncate instead of corrupting the stack.
    // Buffer bumped 1024→2048 in S143 audit since rx: + Q: sections add
    // ~6 opcodes worth of breakdown.
    char buf[2048];
    size_t n = 0;
    auto append = [&](const char* fmt, auto... args) {
        if (n >= sizeof(buf) - 1) return;
        int w = std::snprintf(buf + n, sizeof(buf) - n, fmt, args...);
        if (w < 0) return;
        n += static_cast<size_t>(w);
        if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
    };

    // Header now shows fresh and retx wire bytes separately — under loss the
    // retx fraction can dominate the wire and that ratio is the signal we
    // want, not the conflated number.
    append("Net stats (5s): fresh=%llu pkts/%llu B | retxB=%llu B | drops=%llu | evict=%llu | retx=%llu | maxQ=%zu/%zu",
           static_cast<unsigned long long>(totalFreshPackets),
           static_cast<unsigned long long>(totalFreshBytes),
           static_cast<unsigned long long>(totalRetxBytes),
           static_cast<unsigned long long>(totalDrops),
           static_cast<unsigned long long>(totalEvict),
           static_cast<unsigned long long>(totalRetx),
           maxQ, ReliabilityLayer::MAX_PENDING_PACKETS);

    if (kTop > 0) {
        append("%s", " | top:");
        for (size_t i = 0; i < kTop; ++i) {
            const Row& r = rows[i];
            const char* nm = opcodeName(r.opcode);
            // Each entry: opcode(name)=freshBytes/freshPkts +retxBytes/retxCount
            if (nm) {
                append(" 0x%02X(%s)=%lluB/%llup +%lluB/%llur",
                       r.opcode, nm,
                       static_cast<unsigned long long>(r.freshBytes),
                       static_cast<unsigned long long>(r.freshPkts),
                       static_cast<unsigned long long>(r.retxBytes),
                       static_cast<unsigned long long>(r.retxCount));
            } else {
                append(" 0x%02X=%lluB/%llup +%lluB/%llur",
                       r.opcode,
                       static_cast<unsigned long long>(r.freshBytes),
                       static_cast<unsigned long long>(r.freshPkts),
                       static_cast<unsigned long long>(r.retxBytes),
                       static_cast<unsigned long long>(r.retxCount));
            }
        }
    }
    // Always surface drops/evictions even if they're not in the top-bytes set.
    if (totalDrops || totalEvict || totalRetx) {
        append("%s", " | hot:");
        for (const Row& r : rows) {
            if (r.drops == 0 && r.evict == 0 && r.retxCount == 0) continue;
            const char* nm = opcodeName(r.opcode);
            append(" 0x%02X%s%s%s",
                   r.opcode, nm ? "(" : "", nm ? nm : "", nm ? ")" : "");
            if (r.drops)     append(" d=%llu", (unsigned long long)r.drops);
            if (r.evict)     append(" e=%llu", (unsigned long long)r.evict);
            if (r.retxCount) append(" r=%llu", (unsigned long long)r.retxCount);
        }
    }

    // S143 audit — rx: section. Surfaces inbound packet counts for the
    // opcodes that determine whether the ACK pipeline is alive. Most
    // important: 0xE1 CmdAckExtended — if this is 0 during a retx storm
    // the client isn't sending the stranded-seq ACKs and the server's
    // pending queue cannot drain. CmdMove + Heartbeat tell us whether the
    // client is broadly responsive at all.
    bool anyRx = false;
    for (size_t i = 0; i < 256; ++i) {
        if (opcodeStats_.receivedPackets[i] > 0) { anyRx = true; break; }
    }
    if (anyRx) {
        append("%s", " | rx:");
        // Always print a fixed set of audit-relevant opcodes (even if zero)
        // plus anything else with non-zero rx.
        const uint8_t kAlwaysShow[] = {
            PacketType::CmdAckExtended,
            PacketType::CmdMove,
            PacketType::Heartbeat,
        };
        bool printed[256] = {};
        for (uint8_t op : kAlwaysShow) {
            uint64_t cnt = opcodeStats_.receivedPackets[op];
            const char* nm = opcodeName(op);
            if (nm) append(" 0x%02X(%s)=%llu", op, nm, (unsigned long long)cnt);
            else    append(" 0x%02X=%llu", op, (unsigned long long)cnt);
            printed[op] = true;
        }
        for (size_t i = 0; i < 256; ++i) {
            if (printed[i]) continue;
            uint64_t cnt = opcodeStats_.receivedPackets[i];
            if (cnt == 0) continue;
            const char* nm = opcodeName(static_cast<uint8_t>(i));
            if (nm) append(" 0x%02zX(%s)=%llu", i, nm, (unsigned long long)cnt);
            else    append(" 0x%02zX=%llu", i, (unsigned long long)cnt);
        }
    }

    // S143 audit — Q: section. Worst per-opcode pending depth observed in
    // the window. With this we can prove "EntityEnterBatch is hogging 200
    // slots" vs "the queue cycles fast" — the former points to ACK drain
    // failure, the latter to producer rate.
    bool anyQ = false;
    for (size_t i = 0; i < 256; ++i) {
        if (opcodeStats_.pendingByOpcodeMax[i] > 0) { anyQ = true; break; }
    }
    if (anyQ) {
        append("%s", " | Q:");
        // Top 6 by depth.
        struct QRow { uint8_t op; uint64_t depth; };
        std::vector<QRow> qrows;
        qrows.reserve(32);
        for (size_t i = 0; i < 256; ++i) {
            uint64_t d = opcodeStats_.pendingByOpcodeMax[i];
            if (d == 0) continue;
            qrows.push_back({static_cast<uint8_t>(i), d});
        }
        std::sort(qrows.begin(), qrows.end(),
                  [](const QRow& a, const QRow& b) { return a.depth > b.depth; });
        const size_t kQTop = (std::min)(static_cast<size_t>(6), qrows.size());
        for (size_t i = 0; i < kQTop; ++i) {
            const QRow& q = qrows[i];
            const char* nm = opcodeName(q.op);
            if (nm) append(" 0x%02X(%s)=%llu", q.op, nm, (unsigned long long)q.depth);
            else    append(" 0x%02X=%llu", q.op, (unsigned long long)q.depth);
        }
    }

    // S143 audit — age: section. Worst observed pending age (ms) per opcode
    // that had any pending depth in the window. Direct evidence of
    // queue-stuck packets: 0xE0(EntityEnterBatch) age=8400ms means the
    // oldest enter has been parked unacked for 8.4 seconds — confirms the
    // ACK pipeline isn't draining it.
    bool anyAge = false;
    for (size_t i = 0; i < 256; ++i) {
        if (opcodeStats_.oldestPendingAgeMsMax[i] > 0) { anyAge = true; break; }
    }
    if (anyAge) {
        append("%s", " | age:");
        struct AgeRow { uint8_t op; uint32_t ageMs; };
        std::vector<AgeRow> arows;
        arows.reserve(32);
        for (size_t i = 0; i < 256; ++i) {
            uint32_t a = opcodeStats_.oldestPendingAgeMsMax[i];
            if (a == 0) continue;
            arows.push_back({static_cast<uint8_t>(i), a});
        }
        std::sort(arows.begin(), arows.end(),
                  [](const AgeRow& a, const AgeRow& b) { return a.ageMs > b.ageMs; });
        const size_t kATop = (std::min)(static_cast<size_t>(6), arows.size());
        for (size_t i = 0; i < kATop; ++i) {
            const AgeRow& a = arows[i];
            const char* nm = opcodeName(a.op);
            if (nm) append(" 0x%02X(%s)=%ums", a.op, nm, a.ageMs);
            else    append(" 0x%02X=%ums", a.op, a.ageMs);
        }
    }

    return std::string(buf, n);
}

} // namespace fate
