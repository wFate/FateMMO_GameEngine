#include "game/ui/npc_dialogue_ui.h"
#include "game/ui/game_viewport.h"
#include "game/components/game_components.h"
#include "game/components/player_controller.h"
#include "game/systems/npc_interaction_system.h"
#include "game/systems/quest_system.h"
#include "game/shared/quest_data.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include <cstdio>

namespace fate {

// ============================================================================
// Main Render
// ============================================================================

void NPCDialogueUI::render(Entity* npc, Entity* player,
                            NPCInteractionSystem* npcSystem,
                            QuestSystem* questSystem) {
    if (!npc || !player || !npcSystem) return;

    auto* npcComp = npc->getComponent<NPCComponent>();
    if (!npcComp) return;

    // Window setup — centered, styled to match existing UI
    float panelW = 400.0f;
    float panelH = 350.0f;
    ImGui::SetNextWindowPos(ImVec2(GameViewport::centerX() - panelW * 0.5f,
                                    GameViewport::centerY() - panelH * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));

    char title[128];
    snprintf(title, sizeof(title), "%s###NPCDialogue", npcComp->displayName.c_str());

    bool open = true;
    if (!ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (!open) npcSystem->closeDialogue();
        return;
    }

    // Close on X button
    if (!open) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        npcSystem->closeDialogue();
        return;
    }

    // Story dialogue mode takes over the entire window
    if (inStoryDialogue_) {
        renderStoryDialogue(npc, player, npcSystem);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }

    // Greeting text
    if (!npcComp->dialogueGreeting.empty()) {
        ImGui::TextWrapped("%s", npcComp->dialogueGreeting.c_str());
    } else {
        ImGui::TextWrapped("...");
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Role-specific UI sections
    if (npc->getComponent<QuestGiverComponent>()) {
        renderQuestGiverOptions(npc, player, npcSystem, questSystem);
    }
    if (npc->getComponent<ShopComponent>()) {
        renderShopButton(npc, npcSystem);
    }
    if (npc->getComponent<SkillTrainerComponent>()) {
        renderSkillTrainerButton(npc, npcSystem);
    }
    if (npc->getComponent<BankerComponent>()) {
        renderBankerButton(npc, npcSystem);
    }
    if (npc->getComponent<GuildNPCComponent>()) {
        renderGuildNPCButton(npc, player, npcSystem);
    }
    if (npc->getComponent<TeleporterComponent>()) {
        renderTeleporterOptions(npc, player, npcSystem);
    }
    if (npc->getComponent<StoryNPCComponent>()) {
        ImGui::Spacing();
        if (ImGui::Button("Talk", ImVec2(-1, 0))) {
            auto* story = npc->getComponent<StoryNPCComponent>();
            currentDialogueNodeId_ = story->rootNodeId;
            inStoryDialogue_ = true;
        }
    }

    // Close button
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Close", ImVec2(-1, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        npcSystem->closeDialogue();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================
// Quest Giver Options
// ============================================================================

void NPCDialogueUI::renderQuestGiverOptions(Entity* npc, Entity* player,
                                              NPCInteractionSystem* npcSystem,
                                              QuestSystem* questSystem) {
    auto* qgComp = npc->getComponent<QuestGiverComponent>();
    auto* questComp = player->getComponent<QuestComponent>();
    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    if (!qgComp || !questComp) return;

    int playerLevel = statsComp ? statsComp->stats.level : 1;
    auto& quests = questComp->quests;
    auto* npcComp = npc->getComponent<NPCComponent>();
    std::string npcIdStr = npcComp ? std::to_string(npcComp->npcId) : "";

    for (uint32_t questId : qgComp->questIds) {
        const auto* def = QuestData::getQuest(questId);
        if (!def) continue;

        ImGui::PushID((int)questId);

        const auto* activeQuest = quests.getActiveQuest(questId);

        if (activeQuest && activeQuest->isReadyToTurnIn(*def) &&
            def->turnInNpcId == npcIdStr) {
            // Ready to turn in
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[Complete] %s",
                               def->questName.c_str());
            ImGui::TextWrapped("%s", def->turnInDialogue.c_str());

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.15f, 1.0f));
            if (ImGui::Button("Complete Quest", ImVec2(-1, 0))) {
                auto* inv = player->getComponent<InventoryComponent>();
                if (statsComp && inv) {
                    quests.turnInQuest(questId, statsComp->stats, inv->inventory);
                    if (questSystem) questSystem->refreshQuestMarkers();
                    LOG_INFO("DialogueUI", "Turned in quest: %s", def->questName.c_str());
                }
            }
            ImGui::PopStyleColor();

        } else if (activeQuest) {
            // In progress
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[In Progress] %s",
                               def->questName.c_str());
            ImGui::TextWrapped("%s", def->inProgressDialogue.c_str());

            // Show objective progress
            for (size_t i = 0; i < def->objectives.size(); ++i) {
                uint16_t current = (i < activeQuest->objectiveProgress.size())
                    ? activeQuest->objectiveProgress[i] : 0;
                uint16_t required = def->objectives[i].requiredCount;
                bool done = current >= required;

                ImVec4 color = done ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                    : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                ImGui::TextColored(color, "  %s: %d/%d",
                                   def->objectives[i].description.c_str(),
                                   current, required);
            }

        } else if (quests.canAcceptQuest(questId, playerLevel)) {
            // Available to accept
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "[Available] %s",
                               def->questName.c_str());
            ImGui::TextWrapped("%s", def->offerDialogue.c_str());
            ImGui::Spacing();

            // Quest info
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                               def->description.c_str());

            // Objectives preview
            for (auto& obj : def->objectives) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  - %s",
                                   obj.description.c_str());
            }

            // Rewards preview
            if (def->rewards.xp > 0 || def->rewards.gold > 0) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Rewards:");
                if (def->rewards.xp > 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f),
                                       "%u XP", def->rewards.xp);
                }
                if (def->rewards.gold > 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                                       "%lld Gold", (long long)def->rewards.gold);
                }
            }

            ImGui::Spacing();

            // Accept / Decline buttons
            float buttonW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.15f, 1.0f));
            if (ImGui::Button("Accept", ImVec2(buttonW, 0))) {
                quests.acceptQuest(questId, playerLevel);
                if (questSystem) questSystem->refreshQuestMarkers();
                LOG_INFO("DialogueUI", "Accepted quest: %s", def->questName.c_str());
            }
            ImGui::PopStyleColor();

            ImGui::SameLine(0, 8.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("Decline", ImVec2(buttonW, 0))) {
                // Just skip — quest remains available
                LOG_INFO("DialogueUI", "Declined quest: %s", def->questName.c_str());
            }
            ImGui::PopStyleColor();

        } else if (quests.hasCompletedQuest(questId)) {
            // Already completed — skip display
            ImGui::PopID();
            continue;
        }

        ImGui::Spacing();
        ImGui::PopID();
    }
}

// ============================================================================
// Shop Button (placeholder)
// ============================================================================

void NPCDialogueUI::renderShopButton(Entity* npc, NPCInteractionSystem* /*npcSystem*/) {
    auto* shop = npc->getComponent<ShopComponent>();
    if (!shop) return;

    ImGui::Spacing();
    char label[64];
    snprintf(label, sizeof(label), "Shop — %s", shop->shopName.c_str());
    if (ImGui::Button(label, ImVec2(-1, 0))) {
        if (onOpenShop) onOpenShop(npc);
    }
}

// ============================================================================
// Skill Trainer Button (placeholder)
// ============================================================================

void NPCDialogueUI::renderSkillTrainerButton(Entity* npc, NPCInteractionSystem* /*npcSystem*/) {
    auto* trainer = npc->getComponent<SkillTrainerComponent>();
    if (!trainer) return;

    ImGui::Spacing();
    if (ImGui::Button("Learn Skills", ImVec2(-1, 0))) {
        if (onOpenSkillTrainer) onOpenSkillTrainer(npc);
    }
}

// ============================================================================
// Banker Button (placeholder)
// ============================================================================

void NPCDialogueUI::renderBankerButton(Entity* npc, NPCInteractionSystem* /*npcSystem*/) {
    auto* banker = npc->getComponent<BankerComponent>();
    if (!banker) return;

    ImGui::Spacing();
    if (ImGui::Button("Open Storage", ImVec2(-1, 0))) {
        if (onOpenBank) onOpenBank(npc);
    }
}

// ============================================================================
// Guild NPC Button
// ============================================================================

void NPCDialogueUI::renderGuildNPCButton(Entity* npc, Entity* player,
                                           NPCInteractionSystem* /*npcSystem*/) {
    auto* guildNPC = npc->getComponent<GuildNPCComponent>();
    auto* guildComp = player->getComponent<GuildComponent>();
    if (!guildNPC || !guildComp) return;

    ImGui::Spacing();

    if (!guildComp->guild.isInGuild()) {
        // Not in a guild — show create button
        char label[64];
        snprintf(label, sizeof(label), "Create Guild (%lld Gold)",
                 (long long)guildNPC->creationCost);
        if (ImGui::Button(label, ImVec2(-1, 0))) {
            if (onOpenGuildCreation) onOpenGuildCreation(npc);
        }
        if (guildNPC->requiredLevel > 0) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "Requires level %d", guildNPC->requiredLevel);
        }
    } else {
        // In a guild — show info and leave button
        if (ImGui::Button("Guild Info", ImVec2(-1, 0))) {
            LOG_INFO("DialogueUI", "Guild info clicked");
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Leave Guild", ImVec2(-1, 0))) {
            guildComp->guild.leaveGuild();
            LOG_INFO("DialogueUI", "Left guild");
        }
        ImGui::PopStyleColor();
    }
}

// ============================================================================
// Teleporter Options
// ============================================================================

void NPCDialogueUI::renderTeleporterOptions(Entity* npc, Entity* player,
                                              NPCInteractionSystem* /*npcSystem*/) {
    auto* teleporter = npc->getComponent<TeleporterComponent>();
    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    if (!teleporter) return;

    int playerLevel = statsComp ? statsComp->stats.level : 1;

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Destinations:");
    ImGui::Spacing();

    for (size_t i = 0; i < teleporter->destinations.size(); ++i) {
        auto& dest = teleporter->destinations[i];
        ImGui::PushID((int)i);

        bool meetsLevel = playerLevel >= (int)dest.requiredLevel;

        if (!meetsLevel) {
            // Greyed out — requirements not met
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }

        char label[128];
        if (dest.cost > 0) {
            snprintf(label, sizeof(label), "%s (%lld Gold)",
                     dest.destinationName.c_str(), (long long)dest.cost);
        } else {
            snprintf(label, sizeof(label), "%s (Free)",
                     dest.destinationName.c_str());
        }

        if (ImGui::Button(label, ImVec2(-1, 0)) && meetsLevel) {
            if (onTeleport) onTeleport(dest);
        }

        if (!meetsLevel) {
            ImGui::PopStyleColor(2);
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "  Requires level %d", dest.requiredLevel);
        }

        ImGui::PopID();
    }
}

// ============================================================================
// Story Dialogue (walk through DialogueNode tree)
// ============================================================================

void NPCDialogueUI::renderStoryDialogue(Entity* npc, Entity* /*player*/,
                                          NPCInteractionSystem* npcSystem) {
    auto* story = npc->getComponent<StoryNPCComponent>();
    if (!story) {
        inStoryDialogue_ = false;
        return;
    }

    // Find the current node
    const DialogueNode* currentNode = nullptr;
    for (auto& node : story->dialogueTree) {
        if (node.nodeId == currentDialogueNodeId_) {
            currentNode = &node;
            break;
        }
    }

    if (!currentNode) {
        // Node not found — exit story dialogue
        inStoryDialogue_ = false;
        return;
    }

    // NPC text
    auto* npcComp = npc->getComponent<NPCComponent>();
    if (npcComp) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s:",
                           npcComp->displayName.c_str());
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", currentNode->npcText.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (currentNode->choices.empty()) {
        // Terminal node — show "End Conversation" button
        if (ImGui::Button("End Conversation", ImVec2(-1, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            inStoryDialogue_ = false;
        }
    } else {
        // Show choice buttons
        for (size_t i = 0; i < currentNode->choices.size(); ++i) {
            auto& choice = currentNode->choices[i];

            if (ImGui::Button(choice.buttonText.c_str(), ImVec2(-1, 0))) {
                if (choice.nextNodeId == 0) {
                    // nextNodeId 0 means end of conversation
                    inStoryDialogue_ = false;
                } else {
                    currentDialogueNodeId_ = choice.nextNodeId;
                }
                LOG_DEBUG("DialogueUI", "Choice selected: %s -> node %u",
                          choice.buttonText.c_str(), choice.nextNodeId);
            }
        }

        // Also allow ESC to back out
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            inStoryDialogue_ = false;
            npcSystem->closeDialogue();
        }
    }
}

} // namespace fate
