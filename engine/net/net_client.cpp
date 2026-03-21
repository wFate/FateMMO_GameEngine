#include "engine/net/net_client.h"
#include "engine/net/game_messages.h"
#include "engine/net/packet_crypto.h"
#include "engine/core/logger.h"
#include <cstring>

namespace fate {

bool NetClient::connect(const std::string& host, uint16_t port) {
    if (connected_ || waitingForAccept_) return false;

    if (!socket_.isOpen()) {
        NetSocket::initPlatform();
        if (!socket_.open(0)) { // bind to any available port
            LOG_ERROR("NetClient", "Failed to open socket");
            return false;
        }
    }

    NetAddress addr;
    if (!NetAddress::resolve(host.c_str(), port, addr)) {
        LOG_ERROR("NetClient", "Failed to resolve host: %s", host.c_str());
        return false;
    }
    serverAddress_ = addr;
    lastHost_ = host;
    lastPort_ = port;

    // Send Connect packet (reliable)
    // Send protocol version even without token
    uint8_t versionByte = PROTOCOL_VERSION;
    sendPacket(Channel::ReliableOrdered, PacketType::Connect, &versionByte, 1);
    waitingForAccept_ = true;
    connectStartTime_ = 0.0f; // will be set on first poll

    LOG_INFO("NetClient", "Connecting to %s:%d...", host.c_str(), port);
    return true;
}

bool NetClient::connectWithToken(const std::string& host, uint16_t port, const AuthToken& token) {
    if (connected_ || waitingForAccept_) return false;

    if (!socket_.isOpen()) {
        NetSocket::initPlatform();
        if (!socket_.open(0)) {
            LOG_ERROR("NetClient", "Failed to open socket");
            return false;
        }
    }

    NetAddress addr;
    if (!NetAddress::resolve(host.c_str(), port, addr)) {
        LOG_ERROR("NetClient", "Failed to resolve host: %s", host.c_str());
        return false;
    }
    serverAddress_ = addr;
    authToken_ = token;
    lastHost_ = host;
    lastPort_ = port;

    // Prepend protocol version byte before auth token
    uint8_t connectPayload[17]; // 1 byte version + 16 byte token
    connectPayload[0] = PROTOCOL_VERSION;
    std::memcpy(connectPayload + 1, token.data(), 16);
    sendPacket(Channel::ReliableOrdered, PacketType::Connect, connectPayload, 17);
    waitingForAccept_ = true;
    connectStartTime_ = 0.0f;

    LOG_INFO("NetClient", "Connecting to %s:%d with auth token...", host.c_str(), port);
    return true;
}

void NetClient::disconnect() {
    if (connected_) {
        // Send Disconnect multiple times (unreliable) to maximize delivery chance
        // before we close the socket — can't use Reliable since socket closes immediately
        sendPacket(Channel::Unreliable, PacketType::Disconnect);
        sendPacket(Channel::Unreliable, PacketType::Disconnect);
        sendPacket(Channel::Unreliable, PacketType::Disconnect);
    }
    socket_.close();
    connected_ = false;
    waitingForAccept_ = false;
    clientId_ = 0;
    sessionToken_ = 0;
    reliability_.reset();
    crypto_.clearKeys();
    lastHeartbeatSent_ = 0.0f;
    lastPacketReceived_ = 0.0f;
    reconnectPhase_ = ReconnectPhase::None;
    reconnectAttempts_ = 0;

    if (onDisconnected) onDisconnected();

    LOG_INFO("NetClient", "Disconnected");
}

void NetClient::poll(float currentTime) {
    // --- Reconnect state machine ---
    if (reconnectPhase_ == ReconnectPhase::Reconnecting) {
        // Use currentTime for elapsed tracking; on first tick set baseline
        if (reconnectElapsed_ == 0.0f && reconnectAttempts_ == 0) {
            reconnectElapsed_ = currentTime;
        }

        float elapsed = currentTime - reconnectElapsed_;
        if (elapsed > RECONNECT_TIMEOUT) {
            LOG_WARN("NetClient", "Reconnect timed out after %d attempts", reconnectAttempts_);
            reconnectPhase_ = ReconnectPhase::Failed;
            if (onDisconnected) onDisconnected();
            return;
        }

        // If not currently waiting for a connect accept, try reconnecting
        if (!waitingForAccept_) {
            reconnectTimer_ -= (currentTime - (lastHeartbeatSent_ > 0.0f ? lastHeartbeatSent_ : currentTime));
            if (reconnectTimer_ <= 0.0f) {
                LOG_INFO("NetClient", "Reconnect attempt %d (delay %.1fs)",
                         reconnectAttempts_ + 1, reconnectDelay_);
                // Reset socket for fresh connection
                socket_.close();
                reliability_.reset();
                connectWithToken(lastHost_, lastPort_, authToken_);
                reconnectAttempts_++;
                reconnectTimer_ = reconnectDelay_;
                reconnectDelay_ = (reconnectDelay_ * 2.0f > MAX_RECONNECT_DELAY)
                                  ? MAX_RECONNECT_DELAY : reconnectDelay_ * 2.0f;
            }
        }

        lastHeartbeatSent_ = currentTime;
    }

    // Track connect timeout
    if (waitingForAccept_) {
        if (connectStartTime_ == 0.0f) {
            connectStartTime_ = currentTime;
        } else if (currentTime - connectStartTime_ > connectTimeout_) {
            LOG_WARN("NetClient", "Connection timed out");
            waitingForAccept_ = false;
            socket_.close();
            return;
        }
    }

    // Drain incoming packets
    uint8_t buf[MAX_PACKET_SIZE];
    NetAddress from;
    int received;
    while ((received = socket_.recvFrom(buf, sizeof(buf), from)) > 0) {
        // Only accept packets from the server address
        if (from == serverAddress_) {
            lastPacketReceived_ = currentTime;
            handlePacket(buf, received);
        }
    }

    // Heartbeat timeout detection — start reconnect if no packets for 5+ seconds
    // Only auto-reconnect if we have a stored auth token (non-zero)
    static constexpr AuthToken EMPTY_TOKEN = {};
    if (connected_ && lastPacketReceived_ > 0.0f &&
        currentTime - lastPacketReceived_ > HEARTBEAT_TIMEOUT &&
        reconnectPhase_ == ReconnectPhase::None &&
        authToken_ != EMPTY_TOKEN) {
        LOG_WARN("NetClient", "Heartbeat timeout (%.1fs without server packets), starting reconnect",
                 currentTime - lastPacketReceived_);
        startReconnect();
    }

    // Send heartbeat every second if connected
    if (connected_ && reconnectPhase_ == ReconnectPhase::None &&
        currentTime - lastHeartbeatSent_ > 1.0f) {
        sendPacket(Channel::Unreliable, PacketType::Heartbeat);
        lastHeartbeatSent_ = currentTime;
    }

    // Process retransmits
    if (connected_ || waitingForAccept_) {
        auto retransmits = reliability_.getRetransmits(currentTime);
        for (auto& pkt : retransmits) {
            socket_.sendTo(pkt.data.data(), pkt.data.size(), serverAddress_);
            reliability_.markRetransmitted(pkt.sequence, currentTime);
        }
    }
}

void NetClient::sendRespawn(uint8_t respawnType) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdRespawnMsg msg;
    msg.respawnType = respawnType;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdRespawn, w.data(), w.size());
}

void NetClient::sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetPersistentId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdUseSkillMsg msg;
    msg.skillId = skillId;
    msg.rank = rank;
    msg.targetId = targetPersistentId;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdUseSkill, w.data(), w.size());
}

void NetClient::handlePacket(const uint8_t* data, int size) {
    if (size < static_cast<int>(PACKET_HEADER_SIZE)) return;

    ByteReader r(data, static_cast<size_t>(size));
    PacketHeader hdr = PacketHeader::read(r);

    if (hdr.protocolId != PROTOCOL_ID) return;

    // Process acks on reliability layer
    reliability_.processAck(hdr.ack, hdr.ackBits);
    bool isNewPacket = reliability_.onReceive(hdr.sequence);

    // Skip duplicate reliable packets (retransmits we already processed)
    if (!isNewPacket && hdr.channel != Channel::Unreliable) {
        return;
    }

    // Decrypt incoming payload for non-system packets
    uint8_t decryptedBuf[MAX_PACKET_SIZE];
    const uint8_t* payloadData = data + r.position();
    size_t payloadLen = hdr.payloadSize;

    if (crypto_.hasKeys() && !isSystemPacket(hdr.packetType) && payloadLen > 0) {
        if (payloadLen <= PacketCrypto::TAG_SIZE) {
            return; // too short to contain tag — drop
        }
        size_t decryptedSize = payloadLen - PacketCrypto::TAG_SIZE;
        if (!crypto_.decrypt(payloadData, payloadLen, hdr.sequence, decryptedBuf, sizeof(decryptedBuf))) {
            return; // tampered or wrong key — silently drop
        }
        payloadData = decryptedBuf;
        payloadLen = decryptedSize;
    }

    switch (hdr.packetType) {
        case PacketType::ConnectAccept: {
            ByteReader payload(payloadData, payloadLen);
            clientId_ = payload.readU16();
            sessionToken_ = payload.readU32();
            connected_ = true;
            waitingForAccept_ = false;
            crypto_.clearKeys(); // clear stale keys on reconnect
            if (reconnectPhase_ == ReconnectPhase::Reconnecting) {
                LOG_INFO("NetClient", "Reconnected as client %d after %d attempts",
                         clientId_, reconnectAttempts_);
                reconnectPhase_ = ReconnectPhase::None;
                reconnectAttempts_ = 0;
                reconnectDelay_ = 1.0f;
            } else {
                LOG_INFO("NetClient", "Connected as client %d", clientId_);
            }
            if (onConnected) onConnected();
            break;
        }
        case PacketType::KeyExchange: {
            ByteReader payload(payloadData, payloadLen);
            PacketCrypto::Key txKey{}, rxKey{};
            payload.readBytes(txKey.data(), 32);
            payload.readBytes(rxKey.data(), 32);
            if (payload.ok()) {
                crypto_.setKeys(txKey, rxKey);
                LOG_INFO("NetClient", "AEAD session keys received — encryption active");
            }
            break;
        }
        case PacketType::ConnectReject: {
            waitingForAccept_ = false;
            socket_.close();
            ByteReader payload(payloadData, payloadLen);
            std::string reason = payload.readString();
            LOG_WARN("NetClient", "Connection rejected: %s", reason.c_str());
            if (onConnectRejected) onConnectRejected(reason);
            break;
        }
        case PacketType::SvEntityEnter: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvEntityEnterMsg::read(payload);
            if (onEntityEnter) onEntityEnter(msg);
            break;
        }
        case PacketType::SvEntityLeave: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvEntityLeaveMsg::read(payload);
            if (onEntityLeave) onEntityLeave(msg);
            break;
        }
        case PacketType::SvEntityUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvEntityUpdateMsg::read(payload);
            if (onEntityUpdate) onEntityUpdate(msg);
            break;
        }
        case PacketType::SvCombatEvent: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvCombatEventMsg::read(payload);
            if (onCombatEvent) onCombatEvent(msg);
            break;
        }
        case PacketType::SvChatMessage: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvChatMessageMsg::read(payload);
            if (onChatMessage) onChatMessage(msg);
            break;
        }
        case PacketType::SvPlayerState: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvPlayerStateMsg::read(payload);
            if (onPlayerState) onPlayerState(msg);
            break;
        }
        case PacketType::SvMovementCorrection: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvMovementCorrectionMsg::read(payload);
            if (onMovementCorrection) onMovementCorrection(msg);
            break;
        }
        case PacketType::SvLootPickup: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvLootPickupMsg::read(payload);
            if (onLootPickup) onLootPickup(msg);
            break;
        }
        case PacketType::SvTradeUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvTradeUpdateMsg::read(payload);
            if (onTradeUpdate) onTradeUpdate(msg);
            break;
        }
        case PacketType::SvMarketResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvMarketResultMsg::read(payload);
            if (onMarketResult) onMarketResult(msg);
            break;
        }
        case PacketType::SvBountyUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvBountyUpdateMsg::read(payload);
            if (onBountyUpdate) onBountyUpdate(msg);
            break;
        }
        case PacketType::SvGauntletUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvGauntletUpdateMsg::read(payload);
            if (onGauntletUpdate) onGauntletUpdate(msg);
            break;
        }
        case PacketType::SvGuildUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvGuildUpdateMsg::read(payload);
            if (onGuildUpdate) onGuildUpdate(msg);
            break;
        }
        case PacketType::SvSocialUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvSocialUpdateMsg::read(payload);
            if (onSocialUpdate) onSocialUpdate(msg);
            break;
        }
        case PacketType::SvQuestUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvQuestUpdateMsg::read(payload);
            if (onQuestUpdate) onQuestUpdate(msg);
            break;
        }
        case PacketType::SvZoneTransition: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvZoneTransitionMsg::read(payload);
            if (onZoneTransition) onZoneTransition(msg);
            break;
        }
        case PacketType::SvDeathNotify: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvDeathNotifyMsg::read(payload);
            if (onDeathNotify) onDeathNotify(msg);
            break;
        }
        case PacketType::SvRespawn: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvRespawnMsg::read(payload);
            if (onRespawn) onRespawn(msg);
            break;
        }
        case PacketType::SvSkillResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvSkillResultMsg::read(payload);
            if (onSkillResult) onSkillResult(msg);
            break;
        }
        case PacketType::SvLevelUp: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvLevelUpMsg::read(payload);
            if (onLevelUp) onLevelUp(msg);
            break;
        }
        case PacketType::SvSkillSync: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvSkillSyncMsg::read(payload);
            if (onSkillSync) onSkillSync(msg);
            break;
        }
        case PacketType::SvQuestSync: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvQuestSyncMsg::read(payload);
            if (onQuestSync) onQuestSync(msg);
            break;
        }
        case PacketType::SvInventorySync: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvInventorySyncMsg::read(payload);
            if (onInventorySync) onInventorySync(msg);
            break;
        }
        case PacketType::SvBossLootOwner: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvBossLootOwnerMsg::read(payload);
            if (onBossLootOwner) onBossLootOwner(msg);
            break;
        }
        case PacketType::SvEnchantResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvEnchantResultMsg::read(payload);
            if (onEnchantResult) onEnchantResult(msg);
            break;
        }
        case PacketType::SvRepairResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvRepairResultMsg::read(payload);
            if (onRepairResult) onRepairResult(msg);
            break;
        }
        case PacketType::SvExtractResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvExtractResultMsg::read(payload);
            if (onExtractResult) onExtractResult(msg);
            break;
        }
        case PacketType::SvCraftResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvCraftResultMsg::read(payload);
            if (onCraftResult) onCraftResult(msg);
            break;
        }
        case PacketType::SvBattlefieldUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvBattlefieldUpdateMsg::read(payload);
            if (onBattlefieldUpdate) onBattlefieldUpdate(msg);
            break;
        }
        case PacketType::SvArenaUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvArenaUpdateMsg::read(payload);
            if (onArenaUpdate) onArenaUpdate(msg);
            break;
        }
        case PacketType::SvPetUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvPetUpdateMsg::read(payload);
            if (onPetUpdate) onPetUpdate(msg);
            break;
        }
        case PacketType::SvBankResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvBankResultMsg::read(payload);
            if (onBankResult) onBankResult(msg);
            break;
        }
        default:
            break;
    }
}

void NetClient::sendMove(const Vec2& position, const Vec2& velocity, float timestamp) {
    CmdMove move;
    move.position = position;
    move.velocity = velocity;
    move.timestamp = timestamp;

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    move.write(w);

    sendPacket(Channel::Unreliable, PacketType::CmdMove, w.data(), w.size());
}

void NetClient::sendAction(uint8_t actionType, uint64_t targetId, uint16_t skillId) {
    CmdAction action;
    action.actionType = actionType;
    action.targetId = targetId;
    action.skillId = skillId;

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    action.write(w);

    sendPacket(Channel::ReliableOrdered, PacketType::CmdAction, w.data(), w.size());
}

void NetClient::sendChat(uint8_t channel, const std::string& message, const std::string& target) {
    CmdChat chat;
    chat.channel = channel;
    chat.message = message;
    chat.targetName = target;

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    chat.write(w);

    sendPacket(Channel::ReliableOrdered, PacketType::CmdChat, w.data(), w.size());
}

void NetClient::sendZoneTransition(const std::string& targetScene) {
    CmdZoneTransition cmd;
    cmd.targetScene = targetScene;

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    cmd.write(w);

    sendPacket(Channel::ReliableOrdered, PacketType::CmdZoneTransition, w.data(), w.size());
}

void NetClient::startReconnect() {
    if (reconnectPhase_ != ReconnectPhase::None) return;
    connected_ = false;
    waitingForAccept_ = false;
    reconnectPhase_ = ReconnectPhase::Reconnecting;
    reconnectAttempts_ = 0;
    reconnectTimer_ = 0.0f;  // attempt immediately on next poll
    reconnectDelay_ = 1.0f;
    reconnectElapsed_ = 0.0f;
    LOG_INFO("NetClient", "Starting auto-reconnect to %s:%d", lastHost_.c_str(), lastPort_);
}

bool NetClient::isReconnecting() const {
    return reconnectPhase_ == ReconnectPhase::Reconnecting;
}

bool NetClient::reconnectFailed() const {
    return reconnectPhase_ == ReconnectPhase::Failed;
}

int NetClient::reconnectAttempts() const {
    return reconnectAttempts_;
}

void NetClient::sendPacket(Channel channel, uint8_t packetType,
                           const uint8_t* payload, size_t payloadSize) {
    uint8_t buf[MAX_PACKET_SIZE];
    ByteWriter w(buf, sizeof(buf));

    uint16_t ack, ackBits;
    reliability_.buildAckFields(ack, ackBits);

    PacketHeader hdr;
    hdr.sessionToken = sessionToken_;
    hdr.sequence = reliability_.nextLocalSequence();
    hdr.ack = ack;
    hdr.ackBits = ackBits;
    hdr.channel = channel;
    hdr.packetType = packetType;

    // Encrypt payload for non-system packets when keys are available
    uint8_t encryptedBuf[MAX_PACKET_SIZE];
    const uint8_t* sendPayload = payload;
    size_t sendPayloadSize = payloadSize;

    if (crypto_.hasKeys() && !isSystemPacket(packetType) && payload && payloadSize > 0) {
        size_t encSize = payloadSize + PacketCrypto::TAG_SIZE;
        if (encSize <= sizeof(encryptedBuf) &&
            crypto_.encrypt(payload, payloadSize, hdr.sequence, encryptedBuf, sizeof(encryptedBuf))) {
            sendPayload = encryptedBuf;
            sendPayloadSize = encSize;
        }
    }

    hdr.payloadSize = static_cast<uint16_t>(sendPayloadSize);
    hdr.write(w);

    if (sendPayload && sendPayloadSize > 0) {
        w.writeBytes(sendPayload, sendPayloadSize);
    }

    socket_.sendTo(buf, w.size(), serverAddress_);

    if (channel != Channel::Unreliable) {
        reliability_.trackReliable(hdr.sequence, buf, w.size());
    }
}

} // namespace fate
