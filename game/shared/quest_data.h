#pragma once
#include "game/shared/quest_manager.h"
#include <unordered_map>
#include <optional>

namespace fate {

// ============================================================================
// QuestData — Static registry of all quest definitions
// ============================================================================
class QuestData {
public:
    [[nodiscard]] static const QuestDefinition* getQuest(uint32_t questId) {
        const auto& quests = getAllQuests();
        auto it = quests.find(questId);
        if (it == quests.end()) return nullptr;
        return &it->second;
    }

    [[nodiscard]] static const std::unordered_map<uint32_t, QuestDefinition>& getAllQuests() {
        static const std::unordered_map<uint32_t, QuestDefinition> quests = buildQuests();
        return quests;
    }

private:
    static std::unordered_map<uint32_t, QuestDefinition> buildQuests() {
        std::unordered_map<uint32_t, QuestDefinition> q;

        // Quest 1: Kill 10 leaf boars
        {
            QuestDefinition def;
            def.questId = 1;
            def.questName = "Boar Trouble";
            def.description = "The leaf boars are destroying the crops. Kill 10 of them.";
            def.offerDialogue = "Please help us with the boar problem!";
            def.inProgressDialogue = "Have you dealt with the boars yet?";
            def.turnInDialogue = "Thank you for handling those pests!";
            def.tier = QuestTier::Starter;
            def.requiredLevel = 0;
            def.turnInNpcId = "npc_farmer";
            def.objectives = {{ObjectiveType::Kill, "Kill 10 Leaf Boars", "leaf_boar", 10}};
            def.rewards = {100, 50, {}};
            q[def.questId] = std::move(def);
        }

        // Quest 2: Collect 5 clovers
        {
            QuestDefinition def;
            def.questId = 2;
            def.questName = "Lucky Clovers";
            def.description = "Collect 5 clovers from the meadow.";
            def.offerDialogue = "I need some clovers for a potion. Can you gather them?";
            def.inProgressDialogue = "Still looking for those clovers?";
            def.turnInDialogue = "Perfect! These will make a fine brew.";
            def.tier = QuestTier::Starter;
            def.requiredLevel = 0;
            def.turnInNpcId = "npc_herbalist";
            def.objectives = {{ObjectiveType::Collect, "Collect 5 Clovers", "clover", 5}};
            def.rewards = {75, 30, {}};
            q[def.questId] = std::move(def);
        }

        // Quest 3: Talk to two NPCs
        {
            QuestDefinition def;
            def.questId = 3;
            def.questName = "Meet the Villagers";
            def.description = "Introduce yourself to the village elder and the blacksmith.";
            def.offerDialogue = "You should meet the important people around here.";
            def.inProgressDialogue = "Have you met everyone yet?";
            def.turnInDialogue = "Great, now you know the locals!";
            def.tier = QuestTier::Starter;
            def.requiredLevel = 0;
            def.turnInNpcId = "npc_guide";
            def.objectives = {
                {ObjectiveType::TalkTo, "Talk to the Village Elder", "100", 1},
                {ObjectiveType::TalkTo, "Talk to the Blacksmith", "101", 1}
            };
            def.rewards = {50, 0, {}};
            q[def.questId] = std::move(def);
        }

        // Quest 4: PvP kills
        {
            QuestDefinition def;
            def.questId = 4;
            def.questName = "Prove Your Worth";
            def.description = "Defeat 2 players in PvP combat.";
            def.offerDialogue = "Think you can fight? Prove it in the arena.";
            def.inProgressDialogue = "You still need more victories.";
            def.turnInDialogue = "Impressive! You are a true warrior.";
            def.tier = QuestTier::Starter;
            def.requiredLevel = 10;
            def.turnInNpcId = "npc_arena_master";
            def.objectives = {{ObjectiveType::PvPKills, "Defeat 2 players in PvP", "", 2}};
            def.rewards = {200, 100, {}};
            q[def.questId] = std::move(def);
        }

        // Quest 5: Kill 5 forest guardians
        {
            QuestDefinition def;
            def.questId = 5;
            def.questName = "Guardian Menace";
            def.description = "The forest guardians have turned hostile. Defeat 5 of them.";
            def.offerDialogue = "Something has corrupted the forest guardians.";
            def.inProgressDialogue = "The guardians still roam the forest.";
            def.turnInDialogue = "You found a strange crystal on one of them...";
            def.tier = QuestTier::Novice;
            def.requiredLevel = 25;
            def.turnInNpcId = "npc_ranger";
            def.objectives = {{ObjectiveType::Kill, "Kill 5 Forest Guardians", "forest_guardian", 5}};
            def.rewards = {500, 200, {{"guardian_crystal", 1}}};
            q[def.questId] = std::move(def);
        }

        // Quest 6: Deliver guardian crystal (requires quest 5)
        {
            QuestDefinition def;
            def.questId = 6;
            def.questName = "Crystal Analysis";
            def.description = "Deliver the guardian crystal to the mage for analysis.";
            def.offerDialogue = "Take this crystal to the mage in town.";
            def.inProgressDialogue = "Have you delivered the crystal yet?";
            def.turnInDialogue = "Fascinating... this crystal holds ancient magic.";
            def.tier = QuestTier::Novice;
            def.requiredLevel = 25;
            def.turnInNpcId = "npc_mage";
            def.prerequisiteQuestIds = {5};
            def.objectives = {{ObjectiveType::Deliver, "Deliver the Guardian Crystal", "guardian_crystal", 1}};
            def.rewards = {750, 500, {}};
            q[def.questId] = std::move(def);
        }

        return q;
    }
};

} // namespace fate
