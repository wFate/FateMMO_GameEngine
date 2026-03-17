#include "game/ui/quest_log_ui.h"
#include "game/components/game_components.h"
#include "game/shared/quest_data.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include <cstdio>

namespace fate {

// ============================================================================
// Render
// ============================================================================

void QuestLogUI::render(Entity* player) {
    if (!isOpen || !player) return;

    auto* questComp = player->getComponent<QuestComponent>();
    if (!questComp) return;

    auto& quests = questComp->quests;

    // Window setup — centered, styled to match existing UI
    ImGuiIO& io = ImGui::GetIO();
    float panelW = 380.0f;
    float panelH = 440.0f;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - panelW) * 0.5f,
                                    (io.DisplaySize.y - panelH) * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));

    if (!ImGui::Begin("Quest Log###QuestLog", &isOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }

    // ----------------------------------------------------------------
    // Active Quests
    // ----------------------------------------------------------------
    auto& activeQuests = quests.getActiveQuests();

    if (activeQuests.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No active quests.");
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Talk to NPCs to find quests.");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Active Quests (%d/%d)",
                           (int)activeQuests.size(), QuestManager::MAX_ACTIVE_QUESTS);
        ImGui::Separator();
        ImGui::Spacing();

        for (auto& aq : activeQuests) {
            const auto* def = QuestData::getQuest(aq.questId);
            if (!def) continue;

            ImGui::PushID((int)aq.questId);

            // Quest name — color based on completion status
            bool readyToTurnIn = aq.isReadyToTurnIn(*def);
            ImVec4 nameColor = readyToTurnIn ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f)
                                              : ImVec4(0.8f, 0.8f, 1.0f, 1.0f);

            if (ImGui::CollapsingHeader(def->questName.c_str(),
                                         ImGuiTreeNodeFlags_DefaultOpen)) {
                // Description
                ImGui::TextWrapped("%s", def->description.c_str());
                ImGui::Spacing();

                // Objective progress
                for (size_t i = 0; i < def->objectives.size(); ++i) {
                    uint16_t current = (i < aq.objectiveProgress.size())
                        ? aq.objectiveProgress[i] : 0;
                    uint16_t required = def->objectives[i].requiredCount;
                    bool done = current >= required;

                    ImVec4 color = done ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                        : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    ImGui::TextColored(color, "  %s: %d/%d",
                                       def->objectives[i].description.c_str(),
                                       current, required);
                }

                if (readyToTurnIn) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                                       "  Ready to turn in!");
                }

                // Abandon button
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.15f, 0.15f, 1.0f));
                char abandonLabel[64];
                snprintf(abandonLabel, sizeof(abandonLabel), "Abandon##%u", aq.questId);
                if (ImGui::SmallButton(abandonLabel)) {
                    quests.abandonQuest(aq.questId);
                    LOG_INFO("QuestLogUI", "Abandoned quest: %s", def->questName.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                    break;  // List modified, exit loop
                }
                ImGui::PopStyleColor();

                ImGui::Spacing();
            }

            ImGui::PopID();
        }
    }

    // ----------------------------------------------------------------
    // Completed Quests (collapsible)
    // ----------------------------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Completed Quests")) {
        // Iterate all quest definitions and show completed ones
        const auto& allQuests = QuestData::getAllQuests();
        bool hasCompleted = false;

        for (auto& [questId, def] : allQuests) {
            if (!quests.hasCompletedQuest(questId)) continue;
            hasCompleted = true;

            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "  %s",
                               def.questName.c_str());
        }

        if (!hasCompleted) {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
                               "  No completed quests yet.");
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace fate
