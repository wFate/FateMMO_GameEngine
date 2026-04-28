#pragma once
#include "engine/net/socket.h"
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"
#include "engine/net/reliability.h"
#include "engine/net/protocol.h"
#include "engine/net/auth_protocol.h"
#include "engine/net/game_messages.h"
#include "engine/net/dialogue_messages.h"
#include "engine/net/interact_site_messages.h"
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
    void sendUseFatesGrace();  // Phase 71: revive-in-place consumable
    void sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetPersistentId);
    void sendUseConsumable(uint8_t inventorySlot);
    void sendUseConsumableWithTarget(uint8_t slot, uint32_t targetEntityId);
    // v13: skill-bar slot (0..19) variant for consumable bindings.
    void sendUseLoadoutConsumable(uint8_t loadoutSlot, uint32_t targetEntityId = 0);
    void sendStatEnchant(uint8_t targetSlot, const std::string& scrollItemId);
    void sendShopBuy(uint32_t npcId, const std::string& itemId, uint16_t quantity);
    void sendShopSell(uint32_t npcId, uint8_t inventorySlot, uint16_t quantity);

    // Quest accept/abandon/complete (subAction per engine/net/game_messages.h QuestAction).
    void sendQuestAction(uint8_t subAction, uint32_t questId);

    // Dialogue-tree framework (Session 80 Option C).
    void sendDialogueGiveItem(uint64_t npcPid, const std::string& itemId, uint16_t qty);
    void sendDialogueGiveGold(uint64_t npcPid, int64_t amount);
    void sendDialogueSetFlag (uint64_t npcPid, const std::string& flagId);
    void sendDialogueHeal    (uint64_t npcPid, int32_t amount);

    // Interact-site framework (PROTOCOL 11). Player presses Action on a
    // targeted InteractSite entity; client fires CmdInteractSite with the
    // site's stable string id (sites are not replicated, so no PID is
    // available). Server replies via SvInteractSiteResult.
    void sendInteractSite(const std::string& siteStringId);
    void sendOpalsShopPurchase(const std::string& itemId, uint16_t quantity);  // Phase 71
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
    void sendAssignSlot(uint8_t action, uint8_t kind, const std::string& skillId,
                        const std::string& instanceId, uint8_t slotA, uint8_t slotB = 0);
    void sendAllocateStat(uint8_t statType, int16_t amount);
    // v14: stoneSlot is the source slot of the enhancement stone in the SAME
    // container as the equipment (per project_enchant_same_container_rule).
    // v15: useProtectionStone is deprecated (kept on wire for compat); server
    // derives protection from whether the stone is a `_protected` variant.
    void sendEnchant(uint8_t inventorySlot, uint8_t useProtectionStone, uint8_t stoneSlot);
    void sendBagEnchant(uint8_t bagSlot, uint8_t bagSubSlot,
                        uint8_t useProtectionStone, uint8_t stoneSubSlot);
    // v15: craft 1 `_protected` enhancement stone from 1 mat_protect_stone +
    // 1 base enhancement stone. Same-container slots; server checks space
    // BEFORE consuming.
    void sendCraftProtectStone(uint8_t isBagItem, uint8_t bagSlot,
                               uint8_t protectSlot, uint8_t stoneSlot);
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
    // Phase 71 Task 13: opals-shop pet commands (replaces sendPetCommand when wired).
    void sendEquipPet(uint32_t petInstanceId);
    void sendUnequipPet();
    void sendTogglePetAutoLoot(uint32_t petInstanceId, bool enabled);
    // Phase 71 Task 15: pet-initiated loot pickup. `lootPid` is the
    // PersistentId of the ground drop (same convention as sendAction's
    // actionType=3 click-pickup path).
    void sendPetPickupLoot(uint64_t lootPid);
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

    // Bounty actions
    void sendBountyGetBoard();
    void sendBountyPlace(const std::string& target, int64_t amount);
    void sendBountyCancel(const std::string& targetCharId);

    // Guild actions
    void sendGuildAction(uint8_t action, const std::string& data);
    void sendGuildAction(uint8_t action);

    // Social actions (friend requests, block, etc.)
    void sendSocialAction(uint8_t action);
    void sendSocialAction(uint8_t action, const std::string& targetCharId);

    // Party actions
    void sendPartyAction(uint8_t action, const std::string& targetCharId);
    void sendPartyAction(uint8_t action);
    void sendPartySetLootMode(uint8_t mode);
    void sendPickupPreference(uint8_t mode);

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

    // Network diagnostics (editor Network panel)
    float rttMs() const { return reliability_.rtt() * 1000.0f; }
    size_t reliableQueueDepth() const { return reliability_.pendingReliableCount(); }
    uint32_t droppedNonCritical() const { return reliability_.droppedNonCritical(); }
    const std::string& lastHost() const { return lastHost_; }
    uint16_t lastPort() const { return lastPort_; }

    // ---- Phase 104 Network panel instrumentation (Batches A + B) ----
    // GameApp pushes per-frame wall time so the gap classifier can tell apart
    // server stalls (gap large, frameMs small) from client hitches (gap large,
    // frameMs also large). Called every frame from GameApp::update.
    void noteFrameTime(float frameMs);

    // Time-series sample (1Hz, last NET_SAMPLE_COUNT seconds). Stored as a
    // ring; readers index modulo NET_SAMPLE_COUNT. Indices that are still
    // zero-initialized (sampleCount_ < NET_SAMPLE_COUNT) should be treated as
    // "no data yet".
    static constexpr int NET_SAMPLE_COUNT = 120; // 120s @ 1Hz
    struct NetSample {
        float    rttMs       = 0.0f;
        uint16_t queueDepth  = 0;
        uint16_t gapMs       = 0; // last batch gap observed during this 1s bucket (max)
        uint16_t frameMaxMs  = 0; // max client frame ms observed during this 1s bucket
        uint16_t kbpsIn      = 0;
        uint16_t kbpsOut     = 0;
    };
    const NetSample* netSamples() const { return netSamples_; }
    int  netSampleHead() const { return netSampleHead_; } // index just past the newest sample
    int  netSampleCount() const { return netSampleCount_; } // # valid samples (capped at NET_SAMPLE_COUNT)

    // Classified snapshot-gap events (last 32). The classifier fires whenever
    // SvEntityUpdateBatch arrives with gapMs > GAP_CLASSIFY_THRESHOLD_MS.
    enum class GapClass : uint8_t {
        Healthy = 0,
        ServerStallLikely,
        ClientFrameHitchLikely,
        PacketLossLikely,
        Unknown,
    };
    struct GapEvent {
        float    tSecondsAgo = 0.0f; // resolved at read time vs. clock
        float    timestamp   = 0.0f; // raw lastPollTime_ at fire
        uint16_t gapMs       = 0;
        uint16_t frameMaxMs  = 0; // max frame ms over last ~1s before the event
        uint16_t recvAnyAgeMs= 0; // time since any packet was received
        GapClass kind        = GapClass::Unknown;
    };
    static constexpr int GAP_EVENT_COUNT = 32;
    const GapEvent* gapEvents() const { return gapEvents_; }
    int  gapEventHead() const { return gapEventHead_; }
    int  gapEventCount() const { return gapEventCount_; }
    static const char* gapClassLabel(GapClass k);
    GapClass lastGapClass() const { return lastGapClass_; }
    float    lastBatchAgeMs() const; // time since most recent batch (live)

    // Replication / AOI health (last-1s sliding window, decayed every second).
    struct ReplicationStats {
        uint32_t entered1s        = 0;
        uint32_t left1s           = 0;
        uint32_t stayed1s         = 0;
        uint32_t lastBatchEntities= 0;
        uint32_t lastBatchBytes   = 0;
        float    batchRateHz      = 0.0f;
        bool     scenePopulated   = false;
        uint32_t bufferedReplay   = 0; // last replay count seen by GameApp; settable via setBufferedReplay
    };
    const ReplicationStats& replicationStats() const { return replicationStats_; }
    void noteScenePopulated(uint32_t bufferedEnters);

    // Bandwidth (live, smoothed).
    float kbpsIn()  const { return kbpsInLast_; }
    float kbpsOut() const { return kbpsOutLast_; }
    uint64_t bytesInTotal()  const { return bytesInTotal_; }
    uint64_t bytesOutTotal() const { return bytesOutTotal_; }

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
    std::function<void(const SvBountyBoardMsg&)>        onBountyBoard;
    std::function<void(const SvGauntletUpdateMsg&)> onGauntletUpdate;
    std::function<void(const SvGauntletScoreboardMsg&)> onGauntletScoreboard;
    std::function<void(const SvGuildUpdateMsg&)> onGuildUpdate;
    std::function<void(const SvSocialUpdateMsg&)> onSocialUpdate;
    std::function<void(const SvFriendsListMsg&)>        onFriendsList;
    std::function<void(const SvQuestUpdateMsg&)> onQuestUpdate;
    std::function<void(const SvDialogueActionResultMsg&)> onDialogueActionResult;
    std::function<void(const struct SvCharacterFlagsSnapshotMsg&)> onCharacterFlagsSnapshot;
    std::function<void(const struct SvCharacterFlagDeltaMsg&)>     onCharacterFlagDelta;
    std::function<void(const SvInteractSiteResultMsg&)> onInteractSiteResult;
    std::function<void(const SvZoneTransitionMsg&)> onZoneTransition;
    std::function<void(const SvDeathNotifyMsg&)> onDeathNotify;
    std::function<void(const SvRespawnMsg&)> onRespawn;
    std::function<void(const SvSkillResultMsg&)> onSkillResult;
    std::function<void(const SvLevelUpMsg&)> onLevelUp;
    std::function<void(const SvSkillSyncMsg&)> onSkillSync;
    std::function<void(const struct SvConsumableCooldownMsg&)> onConsumableCooldown;
    std::function<void(const SvQuestSyncMsg&)> onQuestSync;
    std::function<void(const SvInventorySyncMsg&)> onInventorySync;
    std::function<void(const SvBossLootOwnerMsg&)> onBossLootOwner;
    std::function<void(const SvEnchantResultMsg&)> onEnchantResult;
    std::function<void(const SvCraftProtectStoneResultMsg&)> onCraftProtectStoneResult;
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
    std::function<void(const SvPetStateMsg&)>   onPetState;
    std::function<void(const SvPetGrantedMsg&)> onPetGranted;
    std::function<void(const SvPetOwnedListMsg&)> onPetOwnedList;
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
    std::function<void(const SvSpectateAckMsg&)> onSpectateAck;

    // Admin content pipeline callbacks
    std::function<void(const SvAdminResultMsg&)>      onAdminResult;
    std::function<void(const SvAdminContentListMsg&)>  onAdminContentList;
    std::function<void(const SvValidationReportMsg&)>  onValidationReport;

private:
    NetSocket socket_;
    ReliabilityLayer reliability_;
    NetAddress serverAddress_;
    uint16_t clientId_ = 0;
    uint64_t sessionToken_ = 0;
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
    // C4: auth token is sent encrypted AFTER Noise_NK handshake, not in plaintext Connect.
    bool authProofSent_ = false;

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

    // ---- Phase 104 instrumentation state ----
    NetSample netSamples_[NET_SAMPLE_COUNT] = {};
    int       netSampleHead_  = 0;
    int       netSampleCount_ = 0;
    float     sampleBucketStart_ = 0.0f; // wall time of the current 1s sample bucket
    NetSample currentBucket_ = {};       // accumulator for the bucket-in-progress

    GapEvent gapEvents_[GAP_EVENT_COUNT] = {};
    int      gapEventHead_  = 0;
    int      gapEventCount_ = 0;
    GapClass lastGapClass_  = GapClass::Healthy;
    float    lastFrameMs_   = 0.0f;       // most recent frame from noteFrameTime
    float    frameMaxMs1s_  = 0.0f;       // rolling max frame ms over last ~1s
    float    frameMaxBucketStart_ = 0.0f;

    ReplicationStats replicationStats_ = {};
    float    replicationBucketStart_ = 0.0f;
    uint32_t pendingEntered_ = 0;
    uint32_t pendingLeft_    = 0;
    uint32_t pendingStayed_  = 0;
    float    lastBatchArrival_ = 0.0f;     // for batchRateHz EMA

    uint64_t bytesInTotal_  = 0;
    uint64_t bytesOutTotal_ = 0;
    uint64_t bytesInBucket_  = 0;
    uint64_t bytesOutBucket_ = 0;
    float    kbpsInLast_  = 0.0f;
    float    kbpsOutLast_ = 0.0f;

    static constexpr float GAP_CLASSIFY_THRESHOLD_MS = 150.0f;

    void   tickInstrumentation(float currentTime);
    void   noteBytesIn(size_t bytes)  { bytesInTotal_  += bytes; bytesInBucket_  += bytes; }
    void   noteBytesOut(size_t bytes) { bytesOutTotal_ += bytes; bytesOutBucket_ += bytes; }
    GapClass classifyGap(float gapMs) const;
    void     pushGapEvent(float gapMs, GapClass kind);

    void startReconnect();

    void sendPacket(Channel channel, uint8_t packetType,
                    const uint8_t* payload = nullptr, size_t payloadSize = 0);
    void handlePacket(const uint8_t* data, int size);
};

} // namespace fate
