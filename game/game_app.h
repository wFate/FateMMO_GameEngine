#pragma once
#include "engine/app.h"
#include "engine/tilemap/tilemap.h"
#include "engine/render/text_renderer.h"
#include "game/systems/render_system.h"
#include "game/ui/npc_dialogue_ui.h"
#include "game/ui/quest_log_ui.h"
#include <memory>

namespace fate {

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
    Font* hudFont_ = nullptr;

    NPCDialogueUI npcDialogueUI_;
    QuestLogUI questLogUI_;

    void createPlayer(World& world);
    void createTestEntities(World& world);
    void spawnTestMobs(World& world);
    void spawnTestNPCs(World& world);
    void renderCollisionDebug(SpriteBatch& batch, Camera& camera);
    void renderAggroRadius(SpriteBatch& batch, Camera& camera);
};

} // namespace fate
