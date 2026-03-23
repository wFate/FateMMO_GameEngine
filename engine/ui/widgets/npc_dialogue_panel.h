#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <functional>
#include <vector>
#include <string>

namespace fate {

struct DialogueNode;

class NpcDialoguePanel : public UINode {
public:
    NpcDialoguePanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    bool onKeyInput(int scancode, bool pressed) override;

    // NPC identity
    uint32_t npcId = 0;
    std::string npcName;
    std::string greeting;

    // Available roles (set by GameApp based on NPC components)
    bool hasShop = false;
    bool hasBank = false;
    bool hasTeleporter = false;
    bool hasGuild = false;
    bool hasDungeon = false;

    // Quest data
    struct QuestEntry {
        uint32_t questId = 0;
        std::string questName;
        std::string description;
        bool isCompletable = false;  // ready to turn in
        bool isAccepted = false;     // already accepted
    };
    std::vector<QuestEntry> quests;

    // Story dialogue mode
    bool isStoryMode = false;
    std::string storyText;
    struct StoryChoice {
        std::string text;
        uint32_t nextNodeId = 0;
    };
    std::vector<StoryChoice> storyChoices;

    // Callbacks
    std::function<void(uint32_t npcId)> onOpenShop;
    std::function<void(uint32_t npcId)> onOpenBank;
    std::function<void(uint32_t npcId)> onOpenTeleporter;
    std::function<void(uint32_t npcId)> onOpenGuildCreation;
    std::function<void(uint32_t npcId)> onOpenDungeon;
    std::function<void(uint32_t questId)> onQuestAccept;
    std::function<void(uint32_t questId)> onQuestComplete;
    std::function<void(uint32_t nodeId)> onStoryChoice;
    UIClickCallback onClose;

    void open();
    void close();
    bool isOpen() const { return visible(); }
    void rebuild();

private:
    int expandedQuestIndex_ = -1;
};

} // namespace fate
