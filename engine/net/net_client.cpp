#include "engine/net/net_client.h"
#include "engine/net/game_messages.h"
#include "engine/core/logger.h"

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

    // Parse host to IP (support "127.0.0.1" dotted-quad format)
    uint32_t ip = 0;
    {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (sscanf(host.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            ip = (a << 24) | (b << 16) | (c << 8) | d;
        } else {
            LOG_ERROR("NetClient", "Invalid host: %s (only dotted-quad IPs supported)", host.c_str());
            return false;
        }
    }

    serverAddress_.ip = ip;
    serverAddress_.port = port;

    // Send Connect packet (reliable)
    sendPacket(Channel::ReliableOrdered, PacketType::Connect);
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

    // Parse host to IP (support "127.0.0.1" dotted-quad format)
    uint32_t ip = 0;
    {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (sscanf(host.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            ip = (a << 24) | (b << 16) | (c << 8) | d;
        } else {
            LOG_ERROR("NetClient", "Invalid host: %s", host.c_str());
            return false;
        }
    }

    serverAddress_.ip = ip;
    serverAddress_.port = port;
    authToken_ = token;

    // Send Connect packet with auth token as payload
    sendPacket(Channel::ReliableOrdered, PacketType::Connect, token.data(), 16);
    waitingForAccept_ = true;
    connectStartTime_ = 0.0f;

    LOG_INFO("NetClient", "Connecting to %s:%d with auth token...", host.c_str(), port);
    return true;
}

void NetClient::disconnect() {
    if (connected_) {
        sendPacket(Channel::Unreliable, PacketType::Disconnect);
    }
    socket_.close();
    connected_ = false;
    waitingForAccept_ = false;
    clientId_ = 0;
    sessionToken_ = 0;

    if (onDisconnected) onDisconnected();

    LOG_INFO("NetClient", "Disconnected");
}

void NetClient::poll(float currentTime) {
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
            handlePacket(buf, received);
        }
    }

    // Send heartbeat every second if connected
    if (connected_ && currentTime - lastHeartbeatSent_ > 1.0f) {
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

    switch (hdr.packetType) {
        case PacketType::ConnectAccept: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            clientId_ = payload.readU16();
            sessionToken_ = payload.readU32();
            connected_ = true;
            waitingForAccept_ = false;
            LOG_INFO("NetClient", "Connected as client %d", clientId_);
            if (onConnected) onConnected();
            break;
        }
        case PacketType::ConnectReject: {
            waitingForAccept_ = false;
            socket_.close();
            ByteReader payload(data + r.position(), hdr.payloadSize);
            std::string reason = payload.readString();
            LOG_WARN("NetClient", "Connection rejected: %s", reason.c_str());
            if (onConnectRejected) onConnectRejected(reason);
            break;
        }
        case PacketType::SvEntityEnter: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvEntityEnterMsg::read(payload);
            if (onEntityEnter) onEntityEnter(msg);
            break;
        }
        case PacketType::SvEntityLeave: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvEntityLeaveMsg::read(payload);
            if (onEntityLeave) onEntityLeave(msg);
            break;
        }
        case PacketType::SvEntityUpdate: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvEntityUpdateMsg::read(payload);
            if (onEntityUpdate) onEntityUpdate(msg);
            break;
        }
        case PacketType::SvCombatEvent: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvCombatEventMsg::read(payload);
            if (onCombatEvent) onCombatEvent(msg);
            break;
        }
        case PacketType::SvChatMessage: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvChatMessageMsg::read(payload);
            if (onChatMessage) onChatMessage(msg);
            break;
        }
        case PacketType::SvPlayerState: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvPlayerStateMsg::read(payload);
            if (onPlayerState) onPlayerState(msg);
            break;
        }
        case PacketType::SvMovementCorrection: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvMovementCorrectionMsg::read(payload);
            if (onMovementCorrection) onMovementCorrection(msg);
            break;
        }
        case PacketType::SvLootPickup: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvLootPickupMsg::read(payload);
            if (onLootPickup) onLootPickup(msg);
            break;
        }
        case PacketType::SvTradeUpdate: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvTradeUpdateMsg::read(payload);
            if (onTradeUpdate) onTradeUpdate(msg);
            break;
        }
        case PacketType::SvMarketResult: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvMarketResultMsg::read(payload);
            if (onMarketResult) onMarketResult(msg);
            break;
        }
        case PacketType::SvBountyUpdate: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvBountyUpdateMsg::read(payload);
            if (onBountyUpdate) onBountyUpdate(msg);
            break;
        }
        case PacketType::SvGauntletUpdate: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvGauntletUpdateMsg::read(payload);
            if (onGauntletUpdate) onGauntletUpdate(msg);
            break;
        }
        case PacketType::SvGuildUpdate: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvGuildUpdateMsg::read(payload);
            if (onGuildUpdate) onGuildUpdate(msg);
            break;
        }
        case PacketType::SvSocialUpdate: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvSocialUpdateMsg::read(payload);
            if (onSocialUpdate) onSocialUpdate(msg);
            break;
        }
        case PacketType::SvQuestUpdate: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvQuestUpdateMsg::read(payload);
            if (onQuestUpdate) onQuestUpdate(msg);
            break;
        }
        case PacketType::SvZoneTransition: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvZoneTransitionMsg::read(payload);
            if (onZoneTransition) onZoneTransition(msg);
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
    hdr.payloadSize = static_cast<uint16_t>(payloadSize);
    hdr.write(w);

    if (payload && payloadSize > 0) {
        w.writeBytes(payload, payloadSize);
    }

    socket_.sendTo(buf, w.size(), serverAddress_);

    if (channel != Channel::Unreliable) {
        reliability_.trackReliable(hdr.sequence, buf, w.size());
    }
}

} // namespace fate
