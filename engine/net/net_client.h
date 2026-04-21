#pragma once
#include "engine/net/socket.h"
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"
#include "engine/net/reliability.h"
#include "engine/net/protocol.h"
#include "engine/net/auth_protocol.h"
#include "engine/net/game_messages.h"
#include "engine/net/admin_messages.h"
#include "engine/net/packet_crypto.h"
#include <functional>
#include <string>

namespace fate {

class NetClient {
public:
    // Set the server's static identity public key for Noise_NK handshake.
    // Must be called before connect. Without this, falls back to legacy DH.
    void setServerStaticKey(const PacketCrypto::PublicKey& pk);

    bool connect(const std::string& host, uint16_t port);
    bool connectWithToken(const std::string& host, uint16_t port, const AuthToken& token);
    void disconnect();
    void poll(float currentTime);

    void sendMove(const Vec2& position, const Vec2& velocity, float timestamp);
    void sendAction(uint8_t actionType, uint64_t targetId, uint16_t skillId);
    void sendChat(uint8_t channel, const std::string& message, const std::string& target);
    void sendEmoticon(uint8_t emoticonId);
    void sendZoneTransition(const std::string& targetScene);
    void sendRespawn(uint8_t respawnType);
    void sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetPersistentId);
    void sendUseConsumable(uint8_t inventorySlot);
    void sendUseConsumableWithTarget(uint8_t slot, uint32_t targetEntityId);
    void sendStatEnchant(uint8_t targetSlot, const std::string& scrollItemId);
    void sendShopBuy(uint32_t npcId, const std::string& itemId, uint16_t quantity);
    void sendShopSell(uint32_t npcId, uint8_t inventorySlot, uint16_t quantity);
    void sendBankDepositItem(uint32_t npcId, uint8_t inventorySlot);
    void sendBankWithdrawItem(uint32_t npcId, uint16_t itemIndex);
    void sendBankDepositGold(uint32_t npcId, int64_t amount);
    void sendBankWithdrawGold(uint32_t npcId, int64_t amount);
    void sendTeleport(uint32_t npcId, uint8_t destinationIndex);
    void sendStartDungeon(const std::string& sceneId);
    void sendDungeonResponse(uint8_t accept);
    void sendMoveItem(int32_t sourceSlot, int32_t destSlot, int32_t quantity = 0);
    void sendDestroyItem(int32_t slot, const std::string& expectedItemId);
    void sendEquip(uint8_t action, int32_t inventorySlot, uint8_t equipSlot);
    void sendUnequipToBag(uint8_t equipSlot, int32_t bagInventorySlot, int32_t bagSlotIndex);
    void sendEquipFromBag(int32_t bagInventorySlot, int32_t bagSlotIndex, uint8_t targetEquipSlot);
    void sendActivateSkillRank(const std::string& skillId);
    void sendAssignSkillSlot(uint8_t action, const std::string& skillId, uint8_t slotA, uint8_t slotB = 0);
    void sendAllocateStat(uint8_t statType, int16_t amount);
    void sendEnchant(uint8_t inventorySlot, uint8_t useProtectionStone);
    void sendBagEnchant(uint8_t bagSlot, uint8_t bagSubSlot, uint8_t useProtectionStone);
    void sendRepair(uint8_t inventorySlot);
    void sendBagRepair(uint8_t bagSlot, uint8_t bagSubSlot);
    void sendExtractCore(uint8_t itemSlot, uint8_t scrollSlot);
    void sendBagExtractCore(uint8_t bagSlot, uint8_t bagSubSlot, uint8_t scrollSlot);
    void sendCraft(const std::string& recipeId, uint32_t npcId);
    void sendOpenCrafting(uint32_t npcId);
    void sendOpenBag(uint8_t inventorySlot);
    void sendBagStore(uint8_t srcSlot, uint8_t bagSlot, uint8_t bagSubSlot);
    void sendBagRetrieve(uint8_t bagSlot, uint8_t bagSubSlot);
    void sendBagUseItem(uint8_t bagSlot, uint8_t bagSubSlot);
    void sendBagDestroyItem(uint8_t bagSlot, uint8_t bagSubSlot);
    void sendBagMoveItem(uint8_t bagSlot, uint8_t fromSubSlot, uint8_t toSubSlot);
    void sendClaimAdReward();
    void sendSetRecall(uint32_t npcId);
    void sendSocketItem(uint8_t equipSlot, const std::string& scrollItemId);
    void sendArena(uint8_t action, uint8_t mode);
    void sendBattlefield(uint8_t action);
    void sendPetCommand(uint8_t action, int32_t petDbId);
    void sendRankingQuery(const CmdRankingQueryMsg& msg);
    void sendEquipCostume(const std::string& costumeDefId);
    void sendUnequipCostume(uint8_t slotType);
    void sendToggleCostumes(bool show);
    void sendEditorPause(bool paused);
    void sendSpectateScene(const std::string& targetScene, bool active);

    // Admin content pipeline
    void sendAdminSaveContent(uint8_t contentType, bool isNew, const std::string& json);
    void sendAdminDeleteContent(uint8_t contentType, const std::string& id);
    void sendAdminReloadCache(uint8_t cacheType);
    void sendAdminValidate();
    void sendAdminRequestContentList(uint8_t contentType);

    // Guild actions
    void sendGuildAction(uint8_t action, const std::string& data);
    void sendGuildAction(uint8_t action);

    // Social actions (friend requests, block, etc.)
    void sendSocialAction(uint8_t action, const std::string& targetCharId);

    // Party actions
    void sendPartyAction(uint8_t action, const std::string& targetCharId);
    void sendPartyAction(uint8_t action);
    void sendPartySetLootMode(uint8_t mode);

    void sendTradeAction(uint8_t action);
    void sendTradeAction(uint8_t action, const std::string& data);
    void sendTradeConfirm();
    void sendTradeAddItem(uint8_t slotIdx, int32_t sourceSlot, const std::string& instanceId, int32_t quantity);
    void sendTradeSetGold(int64_t gold);
    void sendMarketBuy(int32_t listingId);
    void sendMarketList(const std::string& instanceId, int64_t priceGold);
    void sendMarketCancel(int32_t listingId);
    void sendMarketGetListings(int32_t page, const std::string& filterJson);
    void sendMarketGetMyListings();
    void sendMarketClaim(int32_t listingId);

    bool isConnected() const { return connected_; }
    bool isEncrypted() const { return connected_ && crypto_.hasKeys(); }
    bool isWaitingForConnection() const { return waitingForAccept_; }
    uint16_t clientId() const { return clientId_; }

    // Reconnect state queries
    bool isReconnecting() const;
    bool reconnectFailed() const;
    int reconnectAttempts() const;

    // Callbacks
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void()> onReconnectStart;  // fired when auto-reconnect begins (clean up stale state)
    std::function<void(const SvEntityEnterMsg&)> onEntityEnter;
    std::function<void(const SvEntityLeaveMsg&)> onEntityLeave;
    std::function<void(const SvEntityUpdateMsg&)> onEntityUpdate;
    std::function<void(const SvCombatEventMsg&)> onCombatEvent;
    std::function<void(const SvChatMessageMsg&)> onChatMessage;
    std::function<void(const SvEmoticonMsg&)> onEmoticon;
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
    std::function<void(const SvEnchantResultMsg&)> onEnchantResult;
    std::function<void(const SvRepairResultMsg&)> onRepairResult;
    std::function<void(const SvExtractResultMsg&)> onExtractResult;
    std::function<void(const SvCraftResultMsg&)> onCraftResult;
    std::function<void(const SvCraftingRecipeListMsg&)> onCraftingRecipeList;
    std::function<void(const SvBattlefieldUpdateMsg&)> onBattlefieldUpdate;
    std::function<void(const SvArenaUpdateMsg&)> onArenaUpdate;
    std::function<void(const SvPetUpdateMsg&)> onPetUpdate;
    std::function<void(const SvBankResultMsg&)> onBankResult;
    std::function<void(const SvSocketResultMsg&)> onSocketResult;
    std::function<void(const SvStatEnchantResultMsg&)> onStatEnchantResult;
    std::function<void(const SvShopResultMsg&)> onShopResult;
    std::function<void(const SvTeleportResultMsg&)> onTeleportResult;
    std::function<void(const SvAuroraStatusMsg&)> onAuroraStatus;
    std::function<void(const SvConsumeResultMsg&)> onConsumeResult;
    std::function<void(const SvRankingResultMsg&)> onRankingResult;
    std::function<void(const SvDungeonInviteMsg&)> onDungeonInvite;
    std::function<void(const SvDungeonStartMsg&)> onDungeonStart;
    std::function<void(const SvDungeonEndMsg&)> onDungeonEnd;
    std::function<void(const SvSkillDefsMsg&)> onSkillDefs;
    std::function<void(const SvCollectionSyncMsg&)> onCollectionSync;
    std::function<void(const SvCollectionDefsMsg&)> onCollectionDefs;
    std::function<void(const SvCostumeSyncMsg&)> onCostumeSync;
    std::function<void(const SvCostumeUpdateMsg&)> onCostumeUpdate;
    std::function<void(const SvCostumeDefsMsg&)> onCostumeDefs;
    std::function<void(const SvBuffSyncMsg&)> onBuffSync;
    std::function<void(const SvPartyUpdateMsg&)> onPartyUpdate;
    std::function<void(const SvGuildRosterMsg&)> onGuildRoster;
    std::function<void(const SvMarketListingsMsg&)> onMarketListings;
    std::function<void(const SvBagContentsMsg&)> onBagContents;
    std::function<void(uint8_t success, float remaining)> onAdRewardResult;
    std::function<void(const std::string& reason)> onConnectRejected;
    std::function<void(uint8_t kickCode, const std::string& reason)> onKicked;
    std::function<void(const SvRecallResultMsg&)> onRecallResult;
    std::function<void()> onScenePopulated;
    std::function<void(MoveRejectReason reason)> onMoveReject; // Phase C Batch 3 WU14c

    // Admin content pipeline callbacks
    std::function<void(const SvAdminResultMsg&)>      onAdminResult;
    std::function<void(const SvAdminContentListMsg&)>  onAdminContentList;
    std::function<void(const SvValidationReportMsg&)>  onValidationReport;

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
    float lastPollTime_ = 0.0f;  // cached from poll() for RTT tracking
    // Diagnostic for "all mobs hiccup" — tracks the last time a mob-position
    // batch arrived. Gaps >150ms get logged so we can tell whether the stutter
    // is network loss/delay vs server-side pause.
    float lastBatchReceivedTime_ = 0.0f;
    AuthToken authToken_ = {};
    PacketCrypto crypto_;
    PacketCrypto::Keypair clientKeypair_ = {};
    bool keypairGenerated_ = false;

    // Server's static identity public key for Noise_NK (MITM prevention).
    // Loaded from server_identity.key.pub or compiled-in.
    PacketCrypto::PublicKey serverStaticPk_{};
    bool hasServerStaticPk_ = false;

    // Reconnect state machine
    enum class ReconnectPhase : uint8_t { None, Reconnecting, Failed };
    ReconnectPhase reconnectPhase_ = ReconnectPhase::None;
    int reconnectAttempts_ = 0;
    float reconnectTimer_ = 0.0f;
    float reconnectDelay_ = 1.0f;
    float reconnectStartTime_ = 0.0f;  // when reconnect began (for total timeout)
    float reconnectLastTick_ = 0.0f;   // last poll time (for delta-based timer)
    static constexpr float RECONNECT_TIMEOUT = 5.0f;
    static constexpr float MAX_RECONNECT_DELAY = 2.0f;
    static constexpr float HEARTBEAT_TIMEOUT = 15.0f;
    int heartbeatCounter_ = 0;
    std::string lastHost_;
    uint16_t lastPort_ = 0;

    uint64_t tradeNonce_ = 0;
    uint64_t marketNonce_ = 0;

    void startReconnect();

    void sendPacket(Channel channel, uint8_t packetType,
                    const uint8_t* payload = nullptr, size_t payloadSize = 0);
    void handlePacket(const uint8_t* data, int size);
};

} // namespace fate
