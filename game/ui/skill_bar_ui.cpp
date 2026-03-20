#include "game/ui/skill_bar_ui.h"
#include "game/ui/game_viewport.h"
#include "engine/core/logger.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/game_components.h"
#include <cstdio>

namespace fate {

// ============================================================================
// Main Draw
// ============================================================================

void SkillBarUI::draw(World* world) {
    if (!visible_ || !world) return;

    SkillManager* skills = findPlayerSkills(world);
    if (!skills) return;

    // Find local player's death state
    bool playerDead = false;
    world->forEach<PlayerController, CharacterStatsComponent>(
        [&](Entity*, PlayerController* ctrl, CharacterStatsComponent* sc) {
            if (ctrl->isLocalPlayer) playerDead = sc->stats.isDead;
        }
    );

    float slotSize = 40.0f;
    float spacing = 4.0f;
    float panelW = slotSize + 16.0f;  // slot + padding
    float slotsH = SLOTS_PER_PAGE * (slotSize + spacing);
    float navH = 20.0f;    // page up button
    float pageH = 16.0f;   // page indicator text
    float panelH = navH + spacing + slotsH + spacing + navH + spacing + pageH + 12.0f;

    // Position: right side of viewport, vertically centered
    float posX = GameViewport::right() - panelW - 10.0f;
    float posY = GameViewport::centerY() - panelH * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.1f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.4f, 0.6f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("##SkillBar", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return;
    }

    if (playerDead) ImGui::BeginDisabled();

    // Page up arrow
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.22f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.5f, 1.0f));
    if (ImGui::Button("^##pgup", ImVec2(slotSize, navH))) {
        prevPage();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Previous page ([)");
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();

    // 5 skill slots for the current page
    for (int i = 0; i < SLOTS_PER_PAGE; i++) {
        drawSkillSlot(skills, i, slotSize);
        if (i < SLOTS_PER_PAGE - 1) {
            ImGui::Dummy(ImVec2(0, spacing));
        }
    }

    ImGui::Spacing();

    // Page down arrow
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.22f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.5f, 1.0f));
    if (ImGui::Button("v##pgdn", ImVec2(slotSize, navH))) {
        nextPage();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Next page (])");
    }
    ImGui::PopStyleColor(3);

    // Page indicator centered
    char pageBuf[8];
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", currentPage_ + 1, TOTAL_PAGES);
    float textW = ImGui::CalcTextSize(pageBuf).x;
    ImGui::SetCursorPosX((panelW - textW) * 0.5f);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.65f, 1.0f), "%s", pageBuf);

    if (playerDead) ImGui::EndDisabled();

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// ============================================================================
// Find Player
// ============================================================================

Entity* SkillBarUI::findPlayer(World* world) {
    Entity* result = nullptr;
    world->forEach<Transform, PlayerController>(
        [&](Entity* e, Transform*, PlayerController* ctrl) {
            if (ctrl->isLocalPlayer) result = e;
        }
    );
    return result;
}

SkillManager* SkillBarUI::findPlayerSkills(World* world) {
    SkillManager* result = nullptr;
    world->forEach<PlayerController, SkillManagerComponent>(
        [&](Entity*, PlayerController* ctrl, SkillManagerComponent* sc) {
            if (ctrl->isLocalPlayer) result = &sc->skills;
        }
    );
    return result;
}

// ============================================================================
// Skill Slot
// ============================================================================

void SkillBarUI::drawSkillSlot(SkillManager* skills, int pageSlotIndex, float size) {
    int globalIndex = currentPage_ * SLOTS_PER_PAGE + pageSlotIndex;
    std::string skillId = skills->getSkillInSlot(globalIndex);
    bool hasSkill = !skillId.empty();

    ImGui::PushID(globalIndex);

    // Cooldown state
    bool onCooldown = false;
    float cdRemaining = 0.0f;
    float cdPct = 0.0f;
    if (hasSkill) {
        onCooldown = skills->isOnCooldown(skillId);
        if (onCooldown) {
            cdRemaining = skills->getRemainingCooldown(skillId);
            cdPct = (std::min)(1.0f, cdRemaining / 10.0f); // normalize to 10s max display
        }
    }

    // Slot colors
    ImVec4 bgColor, borderColor;
    if (hasSkill && onCooldown) {
        bgColor = ImVec4(0.18f, 0.08f, 0.08f, 1.0f);     // dark red when on CD
        borderColor = ImVec4(0.5f, 0.2f, 0.2f, 1.0f);
    } else if (hasSkill) {
        bgColor = ImVec4(0.12f, 0.12f, 0.22f, 1.0f);      // dark blue when assigned
        borderColor = ImVec4(0.4f, 0.4f, 0.65f, 1.0f);
    } else {
        bgColor = ImVec4(0.08f, 0.08f, 0.1f, 1.0f);       // very dark when empty
        borderColor = ImVec4(0.2f, 0.2f, 0.25f, 1.0f);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(bgColor.x + 0.08f, bgColor.y + 0.08f, bgColor.z + 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(bgColor.x + 0.15f, bgColor.y + 0.15f, bgColor.z + 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, hasSkill ? 2.0f : 1.0f);

    // Button label: skill abbreviation or slot number
    char label[32];
    if (hasSkill) {
        // Show up to 4 chars of skill ID
        snprintf(label, sizeof(label), "%.4s##s%d", skillId.c_str(), globalIndex);
    } else {
        snprintf(label, sizeof(label), "%d##s%d", pageSlotIndex + 1, globalIndex);
    }

    ImVec2 slotScreenPos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::Button(label, ImVec2(size, size));

    // Left-click activates the skill (if assigned, not on cooldown)
    if (clicked && hasSkill && !onCooldown && onSkillActivated) {
        const LearnedSkill* ls = skills->getLearnedSkill(skillId);
        int rank = ls ? ls->effectiveRank() : 1;
        if (rank > 0) {
            onSkillActivated(skillId, rank);
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    // Cooldown overlay drawn on top of the button
    if (hasSkill && onCooldown) {
        drawCooldownOverlay(slotScreenPos, ImVec2(size, size), cdPct);

        // Cooldown number centered on slot
        char cdBuf[8];
        snprintf(cdBuf, sizeof(cdBuf), "%.1f", cdRemaining);
        ImVec2 textSize = ImGui::CalcTextSize(cdBuf);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(
            ImVec2(slotScreenPos.x + (size - textSize.x) * 0.5f,
                   slotScreenPos.y + (size - textSize.y) * 0.5f),
            IM_COL32(255, 180, 180, 240), cdBuf);
    }

    // Tooltip
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (hasSkill) {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s", skillId.c_str());
            const LearnedSkill* ls = skills->getLearnedSkill(skillId);
            if (ls) {
                ImGui::Text("Rank: %d / %d", ls->activatedRank, ls->unlockedRank);
            }
            if (onCooldown) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "CD: %.1fs", cdRemaining);
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Right-click to clear");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Empty slot %d", pageSlotIndex + 1);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Drag a skill here");
        }
        ImGui::EndTooltip();
    }

    // Drop target for skill assignment (from inventory Skills tab)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKILL_ASSIGN")) {
            std::string draggedId((const char*)payload->Data, payload->DataSize);
            // Strip null terminator if present
            if (!draggedId.empty() && draggedId.back() == '\0') draggedId.pop_back();
            if (skills->assignSkillToSlot(draggedId, globalIndex)) {
                LOG_INFO("SkillBar", "Assigned %s to slot %d (page %d)",
                         draggedId.c_str(), pageSlotIndex + 1, currentPage_ + 1);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click to clear slot
    if (hasSkill && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        skills->assignSkillToSlot("", globalIndex);
        LOG_INFO("SkillBar", "Cleared slot %d (page %d)", pageSlotIndex + 1, currentPage_ + 1);
    }

    ImGui::PopID();
}

// ============================================================================
// Cooldown Overlay
// ============================================================================

void SkillBarUI::drawCooldownOverlay(ImVec2 pos, ImVec2 size, float cooldownPct) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Dark sweep from bottom proportional to remaining cooldown
    float overlayH = size.y * std::clamp(cooldownPct, 0.0f, 1.0f);
    drawList->AddRectFilled(
        ImVec2(pos.x, pos.y + size.y - overlayH),
        ImVec2(pos.x + size.x, pos.y + size.y),
        IM_COL32(0, 0, 0, 150),
        2.0f);
}

} // namespace fate
