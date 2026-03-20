#pragma once
#include "engine/ecs/entity.h"
#include "game/shared/npc_types.h"
#include <functional>

namespace fate {

class NPCInteractionSystem;
class QuestSystem;

class NPCDialogueUI {
public:
    void render(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem, QuestSystem* questSystem);

    // Callbacks wired by GameApp to open sub-UIs
    std::function<void(Entity* npc)> onOpenShop;
    std::function<void(Entity* npc)> onOpenSkillTrainer;
    std::function<void(Entity* npc)> onOpenBank;
    std::function<void(Entity* npc)> onOpenGuildCreation;
    std::function<void(const TeleportDestination& dest)> onTeleport;

private:
    uint32_t currentDialogueNodeId_ = 0;
    bool inStoryDialogue_ = false;

    void renderQuestGiverOptions(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem, QuestSystem* questSystem);
    void renderShopButton(Entity* npc, NPCInteractionSystem* npcSystem);
    void renderSkillTrainerButton(Entity* npc, NPCInteractionSystem* npcSystem);
    void renderBankerButton(Entity* npc, NPCInteractionSystem* npcSystem);
    void renderGuildNPCButton(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem);
    void renderTeleporterOptions(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem);
    void renderStoryDialogue(Entity* npc, Entity* player, NPCInteractionSystem* npcSystem);
};

} // namespace fate
