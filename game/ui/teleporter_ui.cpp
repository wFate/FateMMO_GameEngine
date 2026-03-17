#include "game/ui/teleporter_ui.h"
#include "game/components/game_components.h"
#include "engine/core/logger.h"
#include "imgui.h"
#include <cstdio>

namespace fate {

// ============================================================================
// Open / Close
// ============================================================================

void TeleporterUI::open(Entity* npc) {
    if (!npc) return;
    teleporterNPC = npc;
    isOpen = true;
    LOG_INFO("TeleporterUI", "Opened teleporter");
}

void TeleporterUI::close() {
    isOpen = false;
    teleporterNPC = nullptr;
}

// ============================================================================
// Render
// ============================================================================

void TeleporterUI::render(Entity* player) {
    if (!isOpen || !teleporterNPC || !player) return;

    auto* teleComp = teleporterNPC->getComponent<TeleporterComponent>();
    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    auto* invComp = player->getComponent<InventoryComponent>();
    if (!teleComp || !statsComp || !invComp) return;

    int playerLevel = statsComp->stats.level;
    auto& inv = invComp->inventory;

    // Window setup
    ImGuiIO& io = ImGui::GetIO();
    float panelW = 360.0f;
    float panelH = 350.0f;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - panelW) * 0.5f,
                                    (io.DisplaySize.y - panelH) * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.95f));

    bool open = isOpen;
    if (!ImGui::Begin("Teleporter###TeleporterUI", &open, ImGuiWindowFlags_NoCollapse)) {
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

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Select a destination:");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("DestList", ImVec2(0, -40), true);

    for (int i = 0; i < (int)teleComp->destinations.size(); ++i) {
        auto& dest = teleComp->destinations[i];
        ImGui::PushID(i);

        bool meetsLevel = playerLevel >= (int)dest.requiredLevel;
        bool canAfford = inv.getGold() >= dest.cost;
        bool eligible = meetsLevel && canAfford;

        // Grey out if not eligible
        if (!eligible) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }

        // Destination button
        char label[128];
        if (dest.cost > 0) {
            snprintf(label, sizeof(label), "%s  —  %s gold",
                     dest.destinationName.c_str(), formatGold(dest.cost).c_str());
        } else {
            snprintf(label, sizeof(label), "%s  —  Free",
                     dest.destinationName.c_str());
        }

        if (ImGui::Button(label, ImVec2(-1, 0)) && eligible) {
            // Deduct gold and teleport
            if (dest.cost > 0) {
                inv.removeGold(dest.cost);
            }
            // Placeholder: scene transition to dest.sceneId at dest.targetPosition
            LOG_INFO("TeleporterUI", "Teleporting to %s (scene: %s, cost: %lld)",
                     dest.destinationName.c_str(), dest.sceneId.c_str(),
                     (long long)dest.cost);
            close();
            ImGui::PopID();

            ImGui::EndChild();
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            return;
        }

        if (!eligible) {
            ImGui::PopStyleColor(2);

            // Show why not eligible
            if (!meetsLevel) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                   "  Requires level %d", dest.requiredLevel);
            }
            if (!canAfford) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                   "  Not enough gold");
            }
        }

        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::EndChild();

    // Gold display at bottom
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Gold: %s",
                       formatGold(inv.getGold()).c_str());

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================
// Helpers
// ============================================================================

std::string TeleporterUI::formatGold(int64_t gold) {
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
