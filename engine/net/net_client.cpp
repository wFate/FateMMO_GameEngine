#include "engine/net/net_client.h"
#include "engine/net/game_messages.h"
#include "engine/net/admin_messages.h"
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

    // Reset timing state so a stale lastPacketReceived_ from a prior session
    // doesn't trigger an immediate heartbeat timeout on the fresh connection
    lastPacketReceived_ = 0.0f;
    lastHeartbeatSent_ = 0.0f;
    heartbeatCounter_ = 0;
    reconnectPhase_ = ReconnectPhase::None;
    reconnectAttempts_ = 0;

    // Generate DH keypair for secure key exchange (required)
    if (!PacketCrypto::isAvailable()) {
        LOG_ERROR("NetClient", "Cannot connect: packet encryption not available (libsodium required)");
        return false;
    }
    clientKeypair_ = PacketCrypto::generateKeypair();
    keypairGenerated_ = true;

    // Connect payload: version + auth token + DH public key
    uint8_t connectPayload[49]; // 1 byte version + 16 byte token + 32 byte pk
    connectPayload[0] = PROTOCOL_VERSION;
    std::memcpy(connectPayload + 1, token.data(), 16);
    std::memcpy(connectPayload + 17, clientKeypair_.pk.data(), 32);
    sendPacket(Channel::ReliableOrdered, PacketType::Connect, connectPayload, 49);
    waitingForAccept_ = true;
    connectStartTime_ = 0.0f;

    LOG_INFO("NetClient", "Connecting to %s:%d with auth token...", host.c_str(), port);
    return true;
}

void NetClient::disconnect() {
    if (connected_) {
        // Send Disconnect multiple times (unreliable) to maximize delivery chance
        // before we close the socket --can't use Reliable since socket closes immediately
        sendPacket(Channel::Unreliable, PacketType::Disconnect);
        sendPacket(Channel::Unreliable, PacketType::Disconnect);
        sendPacket(Channel::Unreliable, PacketType::Disconnect);
    }
    socket_.close();
    connected_ = false;
    waitingForAccept_ = false;
    clientId_ = 0;
    sessionToken_ = 0;
    authToken_ = {};
    reliability_.reset();
    crypto_.clearKeys();
    lastHeartbeatSent_ = 0.0f;
    lastPacketReceived_ = 0.0f;
    heartbeatCounter_ = 0;
    reconnectPhase_ = ReconnectPhase::None;
    reconnectAttempts_ = 0;

    if (onDisconnected) onDisconnected();

    LOG_INFO("NetClient", "Disconnected");
}

void NetClient::poll(float currentTime) {
    lastPollTime_ = currentTime;
    // --- Reconnect state machine ---
    if (reconnectPhase_ == ReconnectPhase::Reconnecting) {
        // Track total reconnect time from when reconnect started
        if (reconnectStartTime_ == 0.0f) {
            reconnectStartTime_ = currentTime;
            reconnectLastTick_ = currentTime;
        }

        float totalElapsed = currentTime - reconnectStartTime_;
        if (totalElapsed > RECONNECT_TIMEOUT) {
            LOG_WARN("NetClient", "Reconnect timed out after %d attempts", reconnectAttempts_);
            reconnectPhase_ = ReconnectPhase::Failed;
            connected_ = false;
            authToken_ = {};
            if (onDisconnected) onDisconnected();
            return;
        }

        // If not currently waiting for a connect accept, try reconnecting
        if (!waitingForAccept_) {
            float dt = currentTime - reconnectLastTick_;
            reconnectLastTick_ = currentTime;
            reconnectTimer_ -= dt;
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
            // If we were reconnecting, stop --the token is likely expired
            // and retrying will just spam the server with rejected connections
            if (reconnectPhase_ == ReconnectPhase::Reconnecting) {
                LOG_WARN("NetClient", "Reconnect connect-attempt timed out, giving up");
                reconnectPhase_ = ReconnectPhase::Failed;
                connected_ = false;
                authToken_ = {};
                if (onDisconnected) onDisconnected();
            }
            return;
        }
    }

    // Drain incoming packets (4K buffer for large payloads like inventory sync)
    constexpr size_t RECV_BUF_SIZE = 4096;
    uint8_t buf[RECV_BUF_SIZE];
    NetAddress from;
    int received;
    while ((received = socket_.recvFrom(buf, sizeof(buf), from)) > 0) {
        // Only accept packets from the server address
        if (from == serverAddress_) {
            lastPacketReceived_ = currentTime;
            handlePacket(buf, received);
        }
    }

    // Heartbeat timeout detection --start reconnect if no packets for 5+ seconds
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
        heartbeatCounter_++;
        if (heartbeatCounter_ % 3 == 0) {
            sendPacket(Channel::ReliableOrdered, PacketType::Heartbeat);
        }
    }

    // Process retransmits
    if (connected_ || waitingForAccept_) {
        float retransmitDelay = (std::max)(0.2f, reliability_.rtt() * 2.0f);
        auto retransmits = reliability_.getRetransmits(currentTime, retransmitDelay);
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
    uint8_t decryptedBuf[4096];
    const uint8_t* payloadData = data + r.position();
    size_t payloadLen = hdr.payloadSize;

    if (crypto_.hasKeys() && !isSystemPacket(hdr.packetType) && payloadLen > 0) {
        if (payloadLen <= PacketCrypto::TAG_SIZE) {
            return; // too short to contain tag --drop
        }
        size_t decryptedSize = payloadLen - PacketCrypto::TAG_SIZE;
        if (!crypto_.decrypt(payloadData, payloadLen, hdr.sequence, decryptedBuf, sizeof(decryptedBuf))) {
            return; // tampered or wrong key --silently drop
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
            if (payloadLen == PacketCrypto::PUBLIC_KEY_SIZE && keypairGenerated_) {
                // DH exchange: server sent its public key, derive shared session keys
                PacketCrypto::PublicKey serverPk{};
                payload.readBytes(serverPk.data(), PacketCrypto::PUBLIC_KEY_SIZE);
                if (payload.ok()) {
                    auto keys = PacketCrypto::deriveClientSessionKeys(
                        clientKeypair_.pk, clientKeypair_.sk, serverPk);
                    crypto_.setKeys(keys.txKey, keys.rxKey);
                    // Wipe secret key --no longer needed
                    PacketCrypto::secureWipe(clientKeypair_.sk.data(), clientKeypair_.sk.size());
                    keypairGenerated_ = false;
                    LOG_INFO("NetClient", "AEAD session keys derived via DH --encryption active");
                }
            } else {
                LOG_ERROR("NetClient", "Invalid KeyExchange payload (%d bytes), expected %d-byte DH public key",
                          payloadLen, PacketCrypto::PUBLIC_KEY_SIZE);
            }
            break;
        }
        case PacketType::ConnectReject: {
            waitingForAccept_ = false;
            connected_ = false;
            socket_.close();
            authToken_ = {};
            // Stop reconnect --token is invalid, must re-authenticate
            reconnectPhase_ = ReconnectPhase::Failed;
            reconnectAttempts_ = 0;
            ByteReader payload(payloadData, payloadLen);
            std::string reason = payload.readString();
            LOG_WARN("NetClient", "Connection rejected: %s (reconnect stopped)", reason.c_str());
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
        case PacketType::SvEntityUpdateBatch: {
            ByteReader payload(payloadData, payloadLen);
            uint8_t count = payload.readU8();
            for (uint8_t i = 0; i < count && payload.remaining() > 0; ++i) {
                auto msg = SvEntityUpdateMsg::read(payload);
                if (onEntityUpdate) onEntityUpdate(msg);
            }
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
        case PacketType::SvEmoticon: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvEmoticonMsg::read(payload);
            if (onEmoticon) onEmoticon(msg);
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
            // Store nonce from Lock response for Confirm
            if (msg.updateType == 3 && msg.resultCode == 0 && msg.nonce != 0) {
                tradeNonce_ = msg.nonce;
            }
            if (onTradeUpdate) onTradeUpdate(msg);
            break;
        }
        case PacketType::SvMarketResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvMarketResultMsg::read(payload);
            // Store nonce for next market operation
            if (msg.nonce != 0) {
                marketNonce_ = msg.nonce;
            }
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
        case PacketType::SvSocketResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvSocketResultMsg::read(payload);
            if (onSocketResult) onSocketResult(msg);
            break;
        }
        case PacketType::SvStatEnchantResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvStatEnchantResultMsg::read(payload);
            if (onStatEnchantResult) onStatEnchantResult(msg);
            break;
        }
        case PacketType::SvShopResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvShopResultMsg::read(payload);
            if (onShopResult) onShopResult(msg);
            break;
        }
        case PacketType::SvTeleportResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvTeleportResultMsg::read(payload);
            if (onTeleportResult) onTeleportResult(msg);
            break;
        }
        case PacketType::SvAuroraStatus: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvAuroraStatusMsg::read(payload);
            if (onAuroraStatus) onAuroraStatus(msg);
            break;
        }
        case PacketType::SvConsumeResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvConsumeResultMsg::read(payload);
            if (onConsumeResult) onConsumeResult(msg);
            break;
        }
        case PacketType::SvRankingResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvRankingResultMsg::read(payload);
            if (onRankingResult) onRankingResult(msg);
            break;
        }
        case PacketType::SvDungeonInvite: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvDungeonInviteMsg::read(payload);
            if (onDungeonInvite) onDungeonInvite(msg);
            break;
        }
        case PacketType::SvDungeonStart: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvDungeonStartMsg::read(payload);
            if (onDungeonStart) onDungeonStart(msg);
            break;
        }
        case PacketType::SvDungeonEnd: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvDungeonEndMsg::read(payload);
            if (onDungeonEnd) onDungeonEnd(msg);
            break;
        }
        case PacketType::SvSkillDefs: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvSkillDefsMsg::read(payload);
            if (onSkillDefs) onSkillDefs(msg);
            break;
        }
        case PacketType::SvCollectionSync: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvCollectionSyncMsg::read(payload);
            if (onCollectionSync) onCollectionSync(msg);
            break;
        }
        case PacketType::SvCollectionDefs: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvCollectionDefsMsg::read(payload);
            if (onCollectionDefs) onCollectionDefs(msg);
            break;
        }
        case PacketType::SvCostumeSync: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvCostumeSyncMsg::read(payload);
            if (onCostumeSync) onCostumeSync(msg);
            break;
        }
        case PacketType::SvCostumeUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvCostumeUpdateMsg::read(payload);
            if (onCostumeUpdate) onCostumeUpdate(msg);
            break;
        }
        case PacketType::SvCostumeDefs: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvCostumeDefsMsg::read(payload);
            if (onCostumeDefs) onCostumeDefs(msg);
            break;
        }
        case PacketType::SvBuffSync: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvBuffSyncMsg::read(payload);
            if (onBuffSync) onBuffSync(msg);
            break;
        }
        case PacketType::SvPartyUpdate: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvPartyUpdateMsg::read(payload);
            if (onPartyUpdate) onPartyUpdate(msg);
            break;
        }
        case PacketType::SvAdminResult: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvAdminResultMsg::read(payload);
            if (onAdminResult) onAdminResult(msg);
            break;
        }
        case PacketType::SvAdminContentList: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvAdminContentListMsg::read(payload);
            if (onAdminContentList) onAdminContentList(msg);
            break;
        }
        case PacketType::SvValidationReport: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvValidationReportMsg::read(payload);
            if (onValidationReport) onValidationReport(msg);
            break;
        }
        case PacketType::SvGuildRoster: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvGuildRosterMsg::read(payload);
            if (onGuildRoster) onGuildRoster(msg);
            break;
        }
        case PacketType::SvMarketListings: {
            ByteReader payload(payloadData, payloadLen);
            auto msg = SvMarketListingsMsg::read(payload);
            if (msg.nonce != 0) {
                marketNonce_ = msg.nonce;
            }
            if (onMarketListings) onMarketListings(msg);
            break;
        }
        default:
            break;
    }
}

void NetClient::sendUseConsumable(uint8_t inventorySlot) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdUseConsumableMsg msg;
    msg.inventorySlot = inventorySlot;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdUseConsumable, w.data(), w.size());
}

void NetClient::sendUseConsumableWithTarget(uint8_t slot, uint32_t targetEntityId) {
    CmdUseConsumableMsg msg;
    msg.inventorySlot = slot;
    msg.targetEntityId = targetEntityId;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdUseConsumable, w.data(), w.size());
}

void NetClient::sendStatEnchant(uint8_t targetSlot, const std::string& scrollItemId) {
    CmdStatEnchantMsg msg;
    msg.targetSlot    = targetSlot;
    msg.scrollStatType = 0; // server derives from item def
    msg.scrollItemId  = scrollItemId;

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdStatEnchant, w.data(), w.size());
}

void NetClient::sendShopBuy(uint32_t npcId, const std::string& itemId, uint16_t quantity) {
    CmdShopBuyMsg msg;
    msg.npcId = npcId;
    msg.itemId = itemId;
    msg.quantity = quantity;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdShopBuy, buf, w.size());
}

void NetClient::sendShopSell(uint32_t npcId, uint8_t inventorySlot, uint16_t quantity) {
    CmdShopSellMsg msg;
    msg.npcId = npcId;
    msg.inventorySlot = inventorySlot;
    msg.quantity = quantity;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdShopSell, buf, w.size());
}

void NetClient::sendBankDepositItem(uint32_t npcId, uint8_t inventorySlot) {
    CmdBankDepositItemMsg msg;
    msg.npcId = npcId;
    msg.inventorySlot = inventorySlot;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdBankDepositItem, buf, w.size());
}

void NetClient::sendBankWithdrawItem(uint32_t npcId, uint16_t itemIndex) {
    CmdBankWithdrawItemMsg msg;
    msg.npcId = npcId;
    msg.itemIndex = itemIndex;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdBankWithdrawItem, buf, w.size());
}

void NetClient::sendBankDepositGold(uint32_t npcId, int64_t amount) {
    CmdBankDepositGoldMsg msg;
    msg.npcId = npcId;
    msg.amount = amount;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdBankDepositGold, buf, w.size());
}

void NetClient::sendBankWithdrawGold(uint32_t npcId, int64_t amount) {
    CmdBankWithdrawGoldMsg msg;
    msg.npcId = npcId;
    msg.amount = amount;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdBankWithdrawGold, buf, w.size());
}

void NetClient::sendTeleport(uint32_t npcId, uint8_t destinationIndex) {
    CmdTeleportMsg msg;
    msg.npcId = npcId;
    msg.destinationIndex = destinationIndex;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdTeleport, buf, w.size());
}

void NetClient::sendStartDungeon(const std::string& sceneId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdStartDungeonMsg msg;
    msg.sceneId = sceneId;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdStartDungeon, w.data(), w.size());
}

void NetClient::sendDungeonResponse(uint8_t accept) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdDungeonResponseMsg msg;
    msg.accept = accept;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdDungeonResponse, w.data(), w.size());
}

void NetClient::sendMoveItem(int32_t sourceSlot, int32_t destSlot, int32_t quantity) {
    CmdMoveItemMsg msg;
    msg.sourceSlot = sourceSlot;
    msg.destSlot = destSlot;
    msg.quantity = quantity;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdMoveItem, buf, w.size());
}

void NetClient::sendDestroyItem(int32_t slot, const std::string& expectedItemId) {
    CmdDestroyItemMsg msg;
    msg.slot = slot;
    msg.expectedItemId = expectedItemId;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdDestroyItem, buf, w.size());
}

void NetClient::sendEquip(uint8_t action, int32_t inventorySlot, uint8_t equipSlot) {
    CmdEquipMsg msg;
    msg.action = action;
    msg.inventorySlot = inventorySlot;
    msg.equipSlot = equipSlot;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdEquip, buf, w.size());
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

void NetClient::sendEmoticon(uint8_t emoticonId) {
    CmdEmoticon cmd;
    cmd.emoticonId = emoticonId;

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    cmd.write(w);

    sendPacket(Channel::ReliableOrdered, PacketType::CmdEmoticon, w.data(), w.size());
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
    reconnectStartTime_ = 0.0f;
    reconnectLastTick_ = 0.0f;
    LOG_INFO("NetClient", "Starting auto-reconnect to %s:%d", lastHost_.c_str(), lastPort_);
    if (onReconnectStart) onReconnectStart();
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

    uint16_t ack;
    uint32_t ackBits;
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
        reliability_.trackReliable(hdr.sequence, buf, w.size(), lastPollTime_);
    }
}

void NetClient::sendTradeAction(uint8_t action) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(action);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdTrade, w.data(), w.size());
}

void NetClient::sendTradeAction(uint8_t action, const std::string& data) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(action);
    w.writeString(data);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdTrade, w.data(), w.size());
}

void NetClient::sendTradeConfirm() {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(TradeAction::Confirm);
    detail::writeU64(w, tradeNonce_);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdTrade, w.data(), w.size());
}

void NetClient::sendTradeAddItem(uint8_t slotIdx, int32_t sourceSlot, const std::string& instanceId, int32_t quantity) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(TradeAction::AddItem);
    w.writeU8(slotIdx);
    w.writeI32(sourceSlot);
    w.writeString(instanceId);
    w.writeI32(quantity);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdTrade, w.data(), w.size());
}

void NetClient::sendTradeSetGold(int64_t gold) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(TradeAction::SetGold);
    detail::writeI64(w, gold);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdTrade, w.data(), w.size());
}

void NetClient::sendMarketBuy(int32_t listingId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(MarketAction::BuyItem);
    w.writeI32(listingId);
    detail::writeU64(w, marketNonce_);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdMarket, w.data(), w.size());
}

void NetClient::sendMarketList(const std::string& instanceId, int64_t priceGold) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(MarketAction::ListItem);
    w.writeString(instanceId);
    detail::writeI64(w, priceGold);
    detail::writeU64(w, marketNonce_);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdMarket, w.data(), w.size());
}

void NetClient::sendMarketCancel(int32_t listingId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(MarketAction::CancelListing);
    w.writeI32(listingId);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdMarket, w.data(), w.size());
}

void NetClient::sendMarketGetListings(int32_t page, const std::string& filterJson) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(MarketAction::GetListings);
    w.writeI32(page);
    w.writeString(filterJson);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdMarket, w.data(), w.size());
}

void NetClient::sendMarketGetMyListings() {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(MarketAction::GetMyListings);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdMarket, w.data(), w.size());
}

void NetClient::sendGuildAction(uint8_t action, const std::string& data) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(action);
    w.writeString(data);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdGuild, w.data(), w.size());
}

void NetClient::sendGuildAction(uint8_t action) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(action);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdGuild, w.data(), w.size());
}

void NetClient::sendSocialAction(uint8_t action, const std::string& targetCharId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(action);
    w.writeString(targetCharId);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdSocial, w.data(), w.size());
}

void NetClient::sendPartyAction(uint8_t action, const std::string& targetCharId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(action);
    w.writeString(targetCharId);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdParty, w.data(), w.size());
}

void NetClient::sendPartyAction(uint8_t action) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(action);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdParty, w.data(), w.size());
}

void NetClient::sendPartySetLootMode(uint8_t mode) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(PartyAction::SetLootMode);
    w.writeU8(mode);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdParty, w.data(), w.size());
}

void NetClient::sendActivateSkillRank(const std::string& skillId) {
    CmdActivateSkillRankMsg msg;
    msg.skillId = skillId;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdActivateSkillRank, w.data(), w.size());
}

void NetClient::sendAssignSkillSlot(uint8_t action, const std::string& skillId, uint8_t slotA, uint8_t slotB) {
    CmdAssignSkillSlotMsg msg;
    msg.action = action;
    msg.skillId = skillId;
    msg.slotA = slotA;
    msg.slotB = slotB;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdAssignSkillSlot, w.data(), w.size());
}

void NetClient::sendAllocateStat(uint8_t statType, int16_t amount) {
    CmdAllocateStatMsg msg;
    msg.statType = statType;
    msg.amount = amount;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdAllocateStat, w.data(), w.size());
}

void NetClient::sendEnchant(uint8_t inventorySlot, uint8_t useProtectionStone) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdEnchantMsg msg;
    msg.inventorySlot = inventorySlot;
    msg.useProtectionStone = useProtectionStone;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdEnchant, w.data(), w.size());
}

void NetClient::sendRepair(uint8_t inventorySlot) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdRepairMsg msg;
    msg.inventorySlot = inventorySlot;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdRepair, w.data(), w.size());
}

void NetClient::sendExtractCore(uint8_t itemSlot, uint8_t scrollSlot) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdExtractCoreMsg msg;
    msg.itemSlot = itemSlot;
    msg.scrollSlot = scrollSlot;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdExtractCore, w.data(), w.size());
}

void NetClient::sendCraft(const std::string& recipeId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdCraftMsg msg;
    msg.recipeId = recipeId;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdCraft, w.data(), w.size());
}

void NetClient::sendSocketItem(uint8_t equipSlot, const std::string& scrollItemId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdSocketItemMsg msg;
    msg.equipSlot = equipSlot;
    msg.scrollItemId = scrollItemId;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdSocketItem, w.data(), w.size());
}

void NetClient::sendArena(uint8_t action, uint8_t mode) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdArenaMsg msg;
    msg.action = action;
    msg.mode = mode;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdArena, w.data(), w.size());
}

void NetClient::sendBattlefield(uint8_t action) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdBattlefieldMsg msg;
    msg.action = action;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdBattlefield, w.data(), w.size());
}

void NetClient::sendPetCommand(uint8_t action, int32_t petDbId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdPetMsg msg;
    msg.action = action;
    msg.petDbId = petDbId;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdPet, w.data(), w.size());
}

void NetClient::sendRankingQuery(const CmdRankingQueryMsg& msg) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdRankingQuery, w.data(), w.size());
}

void NetClient::sendEquipCostume(const std::string& costumeDefId) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdEquipCostumeMsg msg;
    msg.costumeDefId = costumeDefId;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdEquipCostume, w.data(), w.size());
}

void NetClient::sendUnequipCostume(uint8_t slotType) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdUnequipCostumeMsg msg;
    msg.slotType = slotType;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdUnequipCostume, w.data(), w.size());
}

void NetClient::sendToggleCostumes(bool show) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdToggleCostumesMsg msg;
    msg.show = show ? 1 : 0;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdToggleCostumes, w.data(), w.size());
}

void NetClient::sendEditorPause(bool paused) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    CmdEditorPauseMsg msg;
    msg.paused = paused ? 1 : 0;
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdEditorPause, w.data(), w.size());
}

void NetClient::sendAdminSaveContent(uint8_t contentType, bool isNew, const std::string& json) {
    CmdAdminSaveContentMsg msg;
    msg.contentType = contentType;
    msg.isNew = isNew ? 1 : 0;
    msg.jsonPayload = json;
    // Heap buffer for potentially large JSON payloads
    std::vector<uint8_t> buf(json.size() + 64);
    ByteWriter w(buf.data(), buf.size());
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdAdminSaveContent, w.data(), w.size());
}

void NetClient::sendAdminDeleteContent(uint8_t contentType, const std::string& id) {
    CmdAdminDeleteContentMsg msg;
    msg.contentType = contentType;
    msg.contentId = id;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdAdminDeleteContent, w.data(), w.size());
}

void NetClient::sendAdminReloadCache(uint8_t cacheType) {
    CmdAdminReloadCacheMsg msg;
    msg.cacheType = cacheType;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdAdminReloadCache, w.data(), w.size());
}

void NetClient::sendAdminValidate() {
    CmdAdminValidateMsg msg;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdAdminValidate, w.data(), w.size());
}

void NetClient::sendAdminRequestContentList(uint8_t contentType) {
    CmdAdminRequestContentListMsg msg;
    msg.contentType = contentType;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdAdminRequestContentList, w.data(), w.size());
}

} // namespace fate
