#pragma once
#include "engine/net/socket.h"
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"
#include "engine/net/reliability.h"
#include "engine/net/protocol.h"
#include "engine/net/auth_protocol.h"
#include "engine/net/game_messages.h"
#include <functional>
#include <string>

namespace fate {

class NetClient {
public:
    bool connect(const std::string& host, uint16_t port);
    bool connectWithToken(const std::string& host, uint16_t port, const AuthToken& token);
    void disconnect();
    void poll(float currentTime);

    void sendMove(const Vec2& position, const Vec2& velocity, float timestamp);
    void sendAction(uint8_t actionType, uint64_t targetId, uint16_t skillId);
    void sendChat(uint8_t channel, const std::string& message, const std::string& target);
    void sendZoneTransition(const std::string& targetScene);
    void sendRespawn(uint8_t respawnType);
    void sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetPersistentId);

    bool isConnected() const { return connected_; }
    uint16_t clientId() const { return clientId_; }

    // Reconnect state queries
    bool isReconnecting() const;
    bool reconnectFailed() const;
    int reconnectAttempts() const;

    // Callbacks
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const SvEntityEnterMsg&)> onEntityEnter;
    std::function<void(const SvEntityLeaveMsg&)> onEntityLeave;
    std::function<void(const SvEntityUpdateMsg&)> onEntityUpdate;
    std::function<void(const SvCombatEventMsg&)> onCombatEvent;
    std::function<void(const SvChatMessageMsg&)> onChatMessage;
    std::function<void(const SvPlayerStateMsg&)> onPlayerState;
    std::function<void(const SvMovementCorrectionMsg&)> onMovementCorrection;
    std::function<void(const SvLootPickupMsg&)> onLootPickup;
    std::function<void(const SvTradeUpdateMsg&)> onTradeUpdate;
    std::function<void(const SvMarketResultMsg&)> onMarketResult;
    std::function<void(const SvBountyUpdateMsg&)> onBountyUpdate;
    std::function<void(const SvGauntletUpdateMsg&)> onGauntletUpdate;
    std::function<void(const SvGuildUpdateMsg&)> onGuildUpdate;
    std::function<void(const SvSocialUpdateMsg&)> onSocialUpdate;
    std::function<void(const SvQuestUpdateMsg&)> onQuestUpdate;
    std::function<void(const SvZoneTransitionMsg&)> onZoneTransition;
    std::function<void(const SvDeathNotifyMsg&)> onDeathNotify;
    std::function<void(const SvRespawnMsg&)> onRespawn;
    std::function<void(const SvSkillResultMsg&)> onSkillResult;
    std::function<void(const SvLevelUpMsg&)> onLevelUp;
    std::function<void(const SvSkillSyncMsg&)> onSkillSync;
    std::function<void(const SvQuestSyncMsg&)> onQuestSync;
    std::function<void(const SvInventorySyncMsg&)> onInventorySync;
    std::function<void(const SvBossLootOwnerMsg&)> onBossLootOwner;
    std::function<void(const std::string& reason)> onConnectRejected;

private:
    NetSocket socket_;
    ReliabilityLayer reliability_;
    NetAddress serverAddress_;
    uint16_t clientId_ = 0;
    uint32_t sessionToken_ = 0;
    bool connected_ = false;
    float connectTimeout_ = 15.0f;  // remote DB can take 10s+ to load character
    float connectStartTime_ = 0.0f;
    bool waitingForAccept_ = false;
    float lastHeartbeatSent_ = 0.0f;
    float lastPacketReceived_ = 0.0f;
    AuthToken authToken_ = {};

    // Reconnect state machine
    enum class ReconnectPhase : uint8_t { None, Reconnecting, Failed };
    ReconnectPhase reconnectPhase_ = ReconnectPhase::None;
    int reconnectAttempts_ = 0;
    float reconnectTimer_ = 0.0f;
    float reconnectDelay_ = 1.0f;
    float reconnectElapsed_ = 0.0f;
    static constexpr float RECONNECT_TIMEOUT = 60.0f;
    static constexpr float MAX_RECONNECT_DELAY = 30.0f;
    static constexpr float HEARTBEAT_TIMEOUT = 5.0f;
    std::string lastHost_;
    uint16_t lastPort_ = 0;

    void startReconnect();

    void sendPacket(Channel channel, uint8_t packetType,
                    const uint8_t* payload = nullptr, size_t payloadSize = 0);
    void handlePacket(const uint8_t* data, int size);
};

} // namespace fate
