#pragma once
#include "engine/app.h"
#include "engine/render/sdf_text.h"
#include "engine/net/net_client.h"
#include "engine/net/interpolation.h"
#include "engine/net/protocol.h"
#include "engine/ecs/entity_handle.h"
#include "game/systems/render_system.h"
#include "engine/net/auth_client.h"
#include "engine/ui/widgets/login_screen.h"
#include "engine/ui/widgets/character_creation_screen.h"
#include "engine/ui/widgets/death_overlay.h"
#include "engine/ui/widgets/chat_panel.h"
#include "engine/ui/widgets/skill_arc.h"
#include "engine/ui/widgets/inventory_panel.h"
#include "engine/ui/widgets/costume_panel.h"
#include "game/ui/game_viewport.h"
#include "game/shared/faction.h"
#include "game/combat_prediction.h"
#include "engine/audio/audio_manager.h"
#include "engine/spatial/collision_grid.h"
#include "engine/scene/async_scene_loader.h"
#include "engine/ui/widgets/loading_panel.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace fate {

enum class ConnectionState {
    LoginScreen,
    Authenticating,
    CharacterSelect,
    CharacterCreation,
    UDPConnecting,
    LoadingScene,
    InGame,
};

class GameplaySystem;
class MovementSystem;
class MobAISystem;
class CombatActionSystem;
class ZoneSystem;
class NPCInteractionSystem;
class QuestSystem;
class NpcDialoguePanel;
class ShopPanel;
class BankPanel;
class TeleporterPanel;
class ArenaPanel;
class BattlefieldPanel;
class LeaderboardPanel;
class PetPanel;
class CraftingPanel;
class CollectionPanel;
class CharacterSelectScreen;
class ConfirmDialog;
class PlayerContextMenu;

class GameApp : public App {
public:
    void onInit() override;
    void onUpdate(float deltaTime) override;
    void onLoadingUpdate(float deltaTime) override;
    void onRender(SpriteBatch& batch, Camera& camera) override;
    void onShutdown() override;

private:
    SpriteRenderSystem* renderSystem_ = nullptr;
    GameplaySystem* gameplaySystem_ = nullptr;
    MovementSystem* movementSystem_ = nullptr;
    MobAISystem* mobAISystem_ = nullptr;
    CombatActionSystem* combatSystem_ = nullptr;
    ZoneSystem* zoneSystem_ = nullptr;
    NPCInteractionSystem* npcInteractionSystem_ = nullptr;
    QuestSystem* questSystem_ = nullptr;
    NetClient netClient_;

    // Retained-mode widget pointers (owned by UI screens, found by ID)
    DeathOverlay* deathOverlay_ = nullptr;
    ChatPanel* chatPanel_ = nullptr;
    SkillArc* skillArc_ = nullptr;
    InventoryPanel* inventoryPanel_ = nullptr;
    PetPanel* petPanel_ = nullptr;
    CraftingPanel* craftingPanel_ = nullptr;
    CollectionPanel* collectionPanel_ = nullptr;
    CostumePanel* costumePanel_ = nullptr;

    // Retained-mode NPC panels (owned by npc_panels screen, found by ID)
    NpcDialoguePanel* npcDialoguePanel_ = nullptr;
    ShopPanel* shopPanel_ = nullptr;
    BankPanel* bankPanel_ = nullptr;
    TeleporterPanel* teleporterPanel_ = nullptr;
    ArenaPanel* arenaPanel_ = nullptr;
    BattlefieldPanel* battlefieldPanel_ = nullptr;
    LeaderboardPanel* leaderboardPanel_ = nullptr;
    ConfirmDialog* dungeonInviteDialog_ = nullptr;  // owned by UI screen, found by ID
    PlayerContextMenu* playerContextMenu_ = nullptr;
    EntityId lastContextMenuTargetId_ = INVALID_ENTITY; // track target changes for context menu popup
    ConfirmDialog* destroyItemDialog_ = nullptr;
    int32_t destroyItemSlot_ = -1;                  // slot pending destruction confirmation
    std::string destroyItemId_;                     // instance ID for race-condition safety
    bool inDungeon_ = false;                         // true while inside a dungeon instance
    AsyncSceneLoader asyncLoader_;
    LoadingPanel* loadingPanel_ = nullptr;
    AudioManager audioManager_;
    CollisionGrid collisionGrid_;
    InterpolationManager ghostInterpolation_;
    std::unordered_map<uint64_t, EntityHandle> ghostEntities_; // PersistentId -> local ghost
    std::unordered_set<uint64_t> droppedItemPids_;  // PIDs of dropped item ghosts (for pickup)
    std::unordered_map<uint64_t, uint8_t> ghostUpdateSeqs_; // PersistentId -> last applied seq
    std::unordered_map<uint64_t, float> ghostDeathTimers_; // PersistentId -> time of death (for corpse fade)
    float lastMoveSendTime_ = 0.0f;
    Vec2 lastSentPos_ = {-99999.0f, -99999.0f}; // last position sent to server (skip unchanged)
    float netTime_ = 0.0f; // accumulated time for network polling

    // Login state machine
    ConnectionState connState_ = ConnectionState::LoginScreen;
    AuthClient authClient_;
    LoginScreen* loginScreenWidget_ = nullptr; // retained-mode, in uiManager_ tree
    AuthToken pendingAuthToken_ = {};
    // Pending registration state (for CharacterCreation flow)
    std::string pendingRegUser_, pendingRegPass_, pendingRegEmail_, pendingRegServer_;
    int pendingRegPort_ = 0;
    int authPort_ = 7778;
    std::string pendingCharName_;
    std::string pendingClassName_;
    Vec2 pendingSpawnPos_ = {0.0f, 0.0f};
    std::string pendingSceneName_;
    Faction pendingFaction_ = Faction::Xyros;
    int32_t pendingLevel_ = 1;
    uint8_t pendingGender_ = 0;
    uint8_t pendingHairstyle_ = 0;
    AuthClientResult pendingAuthResponse_;
    std::vector<CharacterPreview> pendingCharacterList_;
    std::string selectedCharacterId_;
    uint64_t localPlayerPid_ = 0; // Our PersistentId, learned from first SvCombatEvent we send
    bool localPlayerCreated_ = false;
    bool lastEditorPaused_ = false; // track pause transitions for server notification
    bool retainedUILoaded_ = false; // retained-mode UI screens loaded after first InGame frame
    bool hasPendingPlayerState_ = false;
    SvPlayerStateMsg pendingPlayerState_;
    bool hasPendingDeathNotify_ = false;
    bool hasPendingInventorySync_ = false;
    SvInventorySyncMsg pendingInventorySync_;
    bool hasPendingSkillSync_ = false;
    SvSkillSyncMsg pendingSkillSync_;
    bool hasPendingSkillDefs_ = false;
    SvSkillDefsMsg pendingSkillDefs_;
    bool hasPendingQuestSync_ = false;
    SvQuestSyncMsg pendingQuestSync_;
    std::vector<SvQuestUpdateMsg> pendingQuestUpdates_;
    std::unordered_map<std::string, CostumeDefEntry> costumeDefCache_;
    struct PendingChat { uint8_t channel; std::string sender; std::string text; uint8_t faction; };
    std::vector<PendingChat> pendingChatMessages_;

    // Optimistic combat prediction tracking — records attacks sent to server so
    // we can reconcile when SvCombatEvent / SvSkillResult responses arrive.
    CombatPredictionBuffer combatPredictions_;

    // Deferred zone transition — set by onZoneTransition callback, processed
    // after poll() completes to avoid destroying the world mid-frame.
    // SvEntityEnter messages that arrive during the transition are buffered
    // and replayed after the new scene loads (prevents ghost entity race condition).
    bool pendingZoneTransition_ = false;
    std::string pendingZoneScene_;
    Vec2 pendingZoneSpawn_ = {0.0f, 0.0f};
    std::vector<SvEntityEnterMsg> pendingEntityEnters_;
    float loadingMinTimer_ = 0.0f;          // minimum display time remaining
    bool loadingDataReady_ = false;         // async load finished, waiting for timer
    float logoutTimer_ = 0.0f;             // countdown before disconnect on logout

    // Network config UI
    char serverHost_[64] = "127.0.0.1";
    int serverPort_ = 7777;
    bool showNetPanel_ = true;
    void drawNetworkPanel();

    void populateCharacterSlots(CharacterSelectScreen* screen,
                                const std::vector<CharacterPreview>& chars);
    void wireCharacterSelectCallbacks(CharacterSelectScreen* charSelect);
    void renderCollisionDebug(SpriteBatch& batch, Camera& camera);
    void renderAggroRadius(SpriteBatch& batch, Camera& camera);
    void renderAttackRange(SpriteBatch& batch, Camera& camera);
    void closeAllNpcPanels();
    Entity* findNpcById(uint32_t npcId);
    void applySkillDefs(const SvSkillDefsMsg& msg);
    void enrichCostumeEntry(CostumeEntry& entry);
    void captureLocalPlayerState();
};

} // namespace fate
