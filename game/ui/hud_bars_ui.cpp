#include "game/ui/hud_bars_ui.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/game_components.h"
#include <cstdio>
#include <algorithm>

namespace fate {

// ============================================================================
// Default Layout
// ============================================================================

void HudBarsUI::initDefaultLayout() {
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    float margin = 6.0f;

    hpBar = { margin, margin, sw * 0.30f, 18.0f };
    mpBar = { sw - sw * 0.30f - margin, margin, sw * 0.30f, 18.0f };
    xpBar = { (sw - sw * 0.30f) * 0.5f, sh - 14.0f - margin, sw * 0.30f, 14.0f };

    layoutInitialized_ = true;
}

// ============================================================================
// Main Draw
// ============================================================================

void HudBarsUI::draw(World* world) {
    if (!visible_ || !world) return;

    if (!layoutInitialized_) initDefaultLayout();

    Entity* player = findPlayer(world);
    if (!player) return;

    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    if (!statsComp) return;

    auto& s = statsComp->stats;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // HP bar
    {
        float pct = s.maxHP > 0 ? std::clamp((float)s.currentHP / s.maxHP, 0.0f, 1.0f) : 0.0f;
        char buf[32];
        snprintf(buf, sizeof(buf), "HP %d/%d", s.currentHP, s.maxHP);
        drawBarAt(dl, hpBar, pct,
                  IM_COL32(30, 190, 50, 255),
                  IM_COL32(10, 50, 15, 200), buf);
    }

    // MP bar
    {
        float pct = s.maxMP > 0 ? std::clamp((float)s.currentMP / s.maxMP, 0.0f, 1.0f) : 0.0f;
        char buf[32];
        snprintf(buf, sizeof(buf), "MP %d/%d", s.currentMP, s.maxMP);
        drawBarAt(dl, mpBar, pct,
                  IM_COL32(40, 80, 215, 255),
                  IM_COL32(12, 20, 65, 200), buf);
    }

    // XP bar
    {
        float pct = s.xpToNextLevel > 0
            ? std::clamp((float)((double)s.currentXP / s.xpToNextLevel), 0.0f, 1.0f) : 0.0f;
        char buf[48];
        snprintf(buf, sizeof(buf), "XP %lld/%lld",
                 (long long)s.currentXP, (long long)s.xpToNextLevel);
        drawBarAt(dl, xpBar, pct,
                  IM_COL32(190, 175, 40, 255),
                  IM_COL32(50, 45, 12, 200), buf);
    }
}

// ============================================================================
// Editor Settings Panel
// ============================================================================

void HudBarsUI::drawSettings() {
    if (!ImGui::Begin("HUD Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reset to Defaults")) {
        layoutInitialized_ = false;
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("HP Bar", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("HP X", &hpBar.x, 1.0f, 0, 2000);
        ImGui::DragFloat("HP Y", &hpBar.y, 1.0f, 0, 2000);
        ImGui::DragFloat("HP Width", &hpBar.width, 1.0f, 20, 1000);
        ImGui::DragFloat("HP Height", &hpBar.height, 0.5f, 6, 60);
    }

    if (ImGui::CollapsingHeader("MP Bar", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("MP X", &mpBar.x, 1.0f, 0, 2000);
        ImGui::DragFloat("MP Y", &mpBar.y, 1.0f, 0, 2000);
        ImGui::DragFloat("MP Width", &mpBar.width, 1.0f, 20, 1000);
        ImGui::DragFloat("MP Height", &mpBar.height, 0.5f, 6, 60);
    }

    if (ImGui::CollapsingHeader("XP Bar", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("XP X", &xpBar.x, 1.0f, 0, 2000);
        ImGui::DragFloat("XP Y", &xpBar.y, 1.0f, 0, 2000);
        ImGui::DragFloat("XP Width", &xpBar.width, 1.0f, 20, 1000);
        ImGui::DragFloat("XP Height", &xpBar.height, 0.5f, 6, 60);
    }

    ImGui::End();
}

// ============================================================================
// Find Player
// ============================================================================

Entity* HudBarsUI::findPlayer(World* world) {
    Entity* result = nullptr;
    world->forEach<Transform, PlayerController>(
        [&](Entity* e, Transform*, PlayerController* ctrl) {
            if (ctrl->isLocalPlayer) result = e;
        }
    );
    return result;
}

// ============================================================================
// Draw Bar
// ============================================================================

void HudBarsUI::drawBarAt(ImDrawList* dl, const BarConfig& cfg,
                           float fillPct, ImU32 fillColor, ImU32 bgColor,
                           const char* text) {
    ImVec2 pos(cfg.x, cfg.y);
    ImVec2 br(cfg.x + cfg.width, cfg.y + cfg.height);
    float rounding = 2.0f;

    // Background
    dl->AddRectFilled(pos, br, bgColor, rounding);

    // Fill
    if (fillPct > 0.0f) {
        dl->AddRectFilled(pos,
            ImVec2(pos.x + cfg.width * fillPct, br.y),
            fillColor, rounding);
    }

    // Border
    dl->AddRect(pos, br, IM_COL32(0, 0, 0, 160), rounding);

    // Centered text with shadow
    ImVec2 textSize = ImGui::CalcTextSize(text);
    float tx = pos.x + (cfg.width - textSize.x) * 0.5f;
    float ty = pos.y + (cfg.height - textSize.y) * 0.5f;
    dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 240), text);
}

} // namespace fate
