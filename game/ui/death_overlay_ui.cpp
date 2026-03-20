#include "game/ui/death_overlay_ui.h"
#include "game/ui/game_viewport.h"
#include "game/components/game_components.h"
#include "imgui.h"
#include <cstdio>

namespace fate {

void DeathOverlayUI::onDeath(int32_t xpLost, int32_t honorLost, float respawnTimer) {
    active_ = true;
    countdown_ = respawnTimer;
    xpLost_ = xpLost;
    honorLost_ = honorLost;
}

void DeathOverlayUI::render(Entity* player) {
    if (!player) return;

    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    if (!statsComp || !statsComp->stats.isDead) {
        active_ = false;
        return;
    }

    // Auto-activate when player dies locally (before server sends SvDeathNotifyMsg)
    if (!active_) {
        onDeath(0, 0, 5.0f);
    }

    // Tick local countdown
    float dt = ImGui::GetIO().DeltaTime;
    if (countdown_ > 0.0f) {
        countdown_ -= dt;
        if (countdown_ < 0.0f) countdown_ = 0.0f;
    }

    // Panel sizing and positioning via GameViewport
    float panelW = 300.0f;
    float panelH = 220.0f;
    ImVec2 center(GameViewport::centerX(), GameViewport::centerY());

    ImGui::SetNextWindowPos(ImVec2(center.x - panelW * 0.5f, center.y - panelH * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.02f, 0.02f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.1f, 0.1f, 0.8f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav;

    if (!ImGui::Begin("##DeathOverlay", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        return;
    }

    // "You have died."
    ImVec2 titleSize = ImGui::CalcTextSize("You have died.");
    ImGui::SetCursorPosX((panelW - titleSize.x) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "You have died.");
    ImGui::Spacing();

    // Penalty display
    if (xpLost_ > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Lost %d XP", xpLost_);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((panelW - sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", buf);
    }
    if (honorLost_ > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Lost %d Honor", honorLost_);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((panelW - sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.8f, 1.0f), "%s", buf);
    }

    ImGui::Spacing();

    // Countdown
    bool timerReady = countdown_ <= 0.0f;
    if (!timerReady) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Respawn available in %d...", (int)countdown_ + 1);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((panelW - sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", buf);
    }

    ImGui::Spacing();
    float buttonW = panelW - 40.0f;

    // Respawn in Town
    ImGui::SetCursorPosX(20.0f);
    if (!timerReady) ImGui::BeginDisabled();
    if (ImGui::Button("Respawn in Town", ImVec2(buttonW, 28.0f))) {
        if (onRespawnRequested) onRespawnRequested(0);
    }
    if (!timerReady) ImGui::EndDisabled();

    // Respawn at Spawn Point
    ImGui::SetCursorPosX(20.0f);
    if (!timerReady) ImGui::BeginDisabled();
    if (ImGui::Button("Respawn at Spawn Point", ImVec2(buttonW, 28.0f))) {
        if (onRespawnRequested) onRespawnRequested(1);
    }
    if (!timerReady) ImGui::EndDisabled();

    // Phoenix Down — only if player has the item
    auto* invComp = player->getComponent<InventoryComponent>();
    int phoenixCount = 0;
    if (invComp) {
        phoenixCount = invComp->inventory.countItem("phoenix_down");
    }

    if (phoenixCount > 0) {
        ImGui::SetCursorPosX(20.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.5f, 0.15f, 1.0f));
        char label[64];
        std::snprintf(label, sizeof(label), "Respawn Here (Phoenix Down x%d)", phoenixCount);
        if (ImGui::Button(label, ImVec2(buttonW, 28.0f))) {
            if (onRespawnRequested) onRespawnRequested(2);
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

} // namespace fate
