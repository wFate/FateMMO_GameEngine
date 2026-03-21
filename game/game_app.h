#pragma once
#include "engine/app.h"
#include "engine/tilemap/tilemap.h"
#include "engine/render/sdf_text.h"
#include "engine/net/net_client.h"
#include "engine/net/interpolation.h"
#include "engine/net/protocol.h"
#include "engine/ecs/entity_handle.h"
#include "game/systems/render_system.h"
#include "game/ui/npc_dialogue_ui.h"
#include "game/ui/quest_log_ui.h"
#include "engine/net/auth_client.h"
#include "game/ui/login_screen.h"
#include "game/ui/chat_ui.h"
#include "game/ui/death_overlay_ui.h"
#include "game/ui/shop_ui.h"
#include "game/ui/skill_trainer_ui.h"
#include "game/ui/bank_storage_ui.h"
#include "game/ui/teleporter_ui.h"
#include "game/ui/game_viewport.h"
#include "game/shared/faction.h"
#include "game/combat_prediction.h"
#include "engine/audio/audio_manager.h"
#include <memory>
#include <unordered_map>

namespace fate {

enum class ConnectionState {
    LoginScreen,
    Authenticating,
    UDPConnecting,
    InGame,
};

class GameplaySystem;
class MobAISystem;
class CombatActionSystem;
class ZoneSystem;
class NPCInteractionSystem;
class QuestSystem;

class GameApp : public App {
public:
    void onInit() override;
    void onUpdate(float deltaTime) override;
    void onRender(SpriteBatch& batch, Camera& camera) override;
    void onShutdown() override;

private:
    SpriteRenderSystem* renderSystem_ = nullptr;
    GameplaySystem* gameplaySystem_ = nullptr;
    MobAISystem* mobAISystem_ = nullptr;
    CombatActionSystem* combatSystem_ = nullptr;
    ZoneSystem* zoneSystem_ = nullptr;
    NPCInteractionSystem* npcInteractionSystem_ = nullptr;
    QuestSystem* questSystem_ = nullptr;
    std::unique_ptr<Tilemap> tilemap_;
    NPCDialogueUI npcDialogueUI_;
    QuestLogUI questLogUI_;
    ChatUI chatUI_;
    DeathOverlayUI deathOverlayUI_;
    ShopUI shopUI_;
    SkillTrainerUI skillTrainerUI_;
    BankStorageUI bankStorageUI_;
    TeleporterUI teleporterUI_;
    NetClient netClient_;
    AudioManager audioManager_;
    InterpolationManager ghostInterpolation_;
    std::unordered_map<uint64_t, EntityHandle> ghostEntities_; // PersistentId -> local ghost
    std::unordered_map<uint64_t, uint8_t> ghostUpdateSeqs_; // PersistentId -> last applied seq
    float lastMoveSendTime_ = 0.0f;
    float netTime_ = 0.0f; // accumulated time for network polling

    // Login state machine
    ConnectionState connState_ = ConnectionState::LoginScreen;
    AuthClient authClient_;
    LoginScreen loginScreen_;
    AuthToken pendingAuthToken_ = {};
    int authPort_ = 7778;
    std::string pendingCharName_;
    std::string pendingClassName_;
    Vec2 pendingSpawnPos_ = {0.0f, 0.0f};
    std::string pendingSceneName_;
    Faction pendingFaction_ = Faction::Xyros;
    int32_t pendingLevel_ = 1;
    AuthResponse pendingAuthResponse_;
    bool localPlayerCreated_ = false;
    bool hasPendingPlayerState_ = false;
    SvPlayerStateMsg pendingPlayerState_;
    bool hasPendingDeathNotify_ = false;
    uint64_t localPlayerPid_ = 0; // Our PersistentId, learned from first SvCombatEvent we send

    // Optimistic combat prediction tracking — records attacks sent to server so
    // we can reconcile when SvCombatEvent / SvSkillResult responses arrive.
    CombatPredictionBuffer combatPredictions_;

    // Deferred zone transition — set by onZoneTransition callback, processed
    // after poll() completes to avoid destroying the world mid-frame.
    bool pendingZoneTransition_ = false;
    std::string pendingZoneScene_;
    Vec2 pendingZoneSpawn_ = {0.0f, 0.0f};

    // Network config UI
    char serverHost_[64] = "127.0.0.1";
    int serverPort_ = 7777;
    bool showNetPanel_ = true;
    void drawNetworkPanel();

    void createPlayer(World& world);
    void createTestEntities(World& world);
    void spawnTestMobs(World& world);
    void spawnTestNPCs(World& world);
    void renderCollisionDebug(SpriteBatch& batch, Camera& camera);
    void renderAggroRadius(SpriteBatch& batch, Camera& camera);
};

} // namespace fate
