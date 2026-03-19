#include "game/ui/skill_trainer_ui.h"
#include "game/ui/game_viewport.h"
#include "game/components/game_components.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include <cstdio>

namespace fate {

// ============================================================================
// Open / Close
// ============================================================================

void SkillTrainerUI::open(Entity* npc) {
    if (!npc) return;
    trainerNPC = npc;
    isOpen = true;
    selectedSkillIndex_ = -1;
    LOG_INFO("SkillTrainerUI", "Opened skill trainer");
}

void SkillTrainerUI::close() {
    isOpen = false;
    trainerNPC = nullptr;
    selectedSkillIndex_ = -1;
}

// ============================================================================
// Render
// ============================================================================

void SkillTrainerUI::render(Entity* player) {
    if (!isOpen || !trainerNPC || !player) return;

    auto* trainerComp = trainerNPC->getComponent<SkillTrainerComponent>();
    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    auto* invComp = player->getComponent<InventoryComponent>();
    auto* skillComp = player->getComponent<SkillManagerComponent>();
    if (!trainerComp || !statsComp || !invComp || !skillComp) return;

    auto& stats = statsComp->stats;
    auto& inv = invComp->inventory;
    auto& skills = skillComp->skills;
    int playerLevel = stats.level;

    // Window setup
    float panelW = 400.0f;
    float panelH = 420.0f;
    ImGui::SetNextWindowPos(ImVec2(GameViewport::centerX() - panelW * 0.5f,
                                    GameViewport::centerY() - panelH * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));

    char title[128];
    snprintf(title, sizeof(title), "Skill Trainer — %s###SkillTrainerUI",
             getClassName(trainerComp->trainerClass));

    bool open = isOpen;
    if (!ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (!open) close();
        return;
    }

    if (!open) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        close();
        return;
    }

    // Header
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Available Skills:");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "Skill Points: %d  |  Gold: %s",
                       skills.availablePoints(), formatGold(inv.getGold()).c_str());
    ImGui::Separator();
    ImGui::Spacing();

    // Skill list
    ImGui::BeginChild("SkillList", ImVec2(0, -4), true);

    auto& trainable = trainerComp->skills;
    if (trainable.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No skills available.");
    }

    for (int i = 0; i < (int)trainable.size(); ++i) {
        auto& ts = trainable[i];
        ImGui::PushID(i);

        bool meetsLevel = playerLevel >= (int)ts.requiredLevel;
        bool meetsClass = (ts.requiredClass == ClassType::Any) ||
                          (ts.requiredClass == stats.classDef.classType);
        bool hasGold = inv.getGold() >= ts.goldCost;
        bool hasPoints = skills.availablePoints() >= (int)ts.skillPointCost;
        bool alreadyLearned = skills.hasSkill(ts.skillId);
        bool canLearn = meetsLevel && meetsClass && hasGold && hasPoints && !alreadyLearned;

        // Grey out if requirements not met
        if (!canLearn) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }

        // Skill entry
        ImGui::Text("%s", ts.skillId.c_str());

        // Requirements line
        ImGui::SameLine(200);
        ImGui::Text("Lv%d", ts.requiredLevel);

        if (ts.goldCost > 0) {
            ImGui::SameLine(240);
            ImVec4 goldColor = hasGold ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f)
                                       : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(goldColor, "%s g", formatGold(ts.goldCost).c_str());
        }

        if (ts.skillPointCost > 0) {
            ImGui::SameLine(310);
            ImVec4 spColor = hasPoints ? ImVec4(0.3f, 0.8f, 1.0f, 1.0f)
                                       : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(spColor, "%d SP", ts.skillPointCost);
        }

        // Learn button
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
        if (alreadyLearned) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Learned");
        } else {
            if (!canLearn) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.15f, 1.0f));
            }

            if (ImGui::SmallButton("Learn") && canLearn) {
                // Deduct gold
                if (inv.removeGold(ts.goldCost)) {
                    // Route to SkillManager — learnSkill expects (skillId, rank)
                    skills.learnSkill(ts.skillId, 1);
                    LOG_INFO("SkillTrainerUI", "Learned skill: %s (cost: %lld gold, %d SP)",
                             ts.skillId.c_str(), (long long)ts.goldCost, ts.skillPointCost);
                }
            }

            ImGui::PopStyleColor();
        }

        // Requirements tooltip
        if (!canLearn && !alreadyLearned && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            if (!meetsLevel)
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Requires level %d", ts.requiredLevel);
            if (!meetsClass)
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Requires class: %s",
                                   getClassName(ts.requiredClass));
            if (!hasGold)
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Not enough gold");
            if (!hasPoints)
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Not enough skill points");
            ImGui::EndTooltip();
        }

        if (!canLearn) {
            ImGui::PopStyleColor();  // Text color
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================
// Helpers
// ============================================================================

const char* SkillTrainerUI::getClassName(ClassType type) {
    switch (type) {
        case ClassType::Warrior: return "Warrior";
        case ClassType::Mage:    return "Mage";
        case ClassType::Archer:  return "Archer";
        case ClassType::Any:     return "All Classes";
        default: return "Unknown";
    }
}

std::string SkillTrainerUI::formatGold(int64_t gold) {
    if (gold >= 1000000000LL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fB", gold / 1000000000.0);
        return buf;
    }
    if (gold >= 1000000LL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fM", gold / 1000000.0);
        return buf;
    }
    if (gold >= 10000LL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fK", gold / 1000.0);
        return buf;
    }
    return std::to_string(gold);
}

} // namespace fate
