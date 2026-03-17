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
    // Normalized positions (0-1 range relative to viewport)
    // These get mapped to the actual viewport rect in drawBarAt()
    hpBar = { 0.01f, 0.01f, 0.30f, 0.04f };
    mpBar = { 0.69f, 0.01f, 0.30f, 0.04f };
    xpBar = { 0.35f, 0.96f, 0.30f, 0.03f };

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
    if (!ImGui::Begin("HUD Layout")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reset to Defaults")) {
        layoutInitialized_ = false;
    }

    ImGui::Separator();

    ImGui::Text("Viewport: %.0fx%.0f", vpW_, vpH_);
    ImGui::Separator();

    auto barEditor = [](const char* label, BarConfig& bar) {
        if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
            char id[32];
            snprintf(id, sizeof(id), "%s X", label);
            ImGui::DragFloat(id, &bar.x, 0.005f, 0.0f, 1.0f, "%.3f");
            snprintf(id, sizeof(id), "%s Y", label);
            ImGui::DragFloat(id, &bar.y, 0.005f, 0.0f, 1.0f, "%.3f");
            snprintf(id, sizeof(id), "%s W", label);
            ImGui::DragFloat(id, &bar.width, 0.005f, 0.01f, 1.0f, "%.3f");
            snprintf(id, sizeof(id), "%s H", label);
            ImGui::DragFloat(id, &bar.height, 0.002f, 0.005f, 0.2f, "%.3f");
        }
    };
    barEditor("HP Bar", hpBar);
    barEditor("MP Bar", mpBar);
    barEditor("XP Bar", xpBar);

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
    // Map normalized coords (0-1) to viewport screen pixels
    float px = vpX_ + cfg.x * vpW_;
    float py = vpY_ + cfg.y * vpH_;
    float pw = cfg.width * vpW_;
    float ph = cfg.height * vpH_;

    ImVec2 pos(px, py);
    ImVec2 br(px + pw, py + ph);
    float rounding = 2.0f;

    // Background
    dl->AddRectFilled(pos, br, bgColor, rounding);

    // Fill
    if (fillPct > 0.0f) {
        dl->AddRectFilled(pos,
            ImVec2(pos.x + pw * fillPct, br.y),
            fillColor, rounding);
    }

    // Border
    dl->AddRect(pos, br, IM_COL32(0, 0, 0, 160), rounding);

    // Centered text with shadow
    ImVec2 textSize = ImGui::CalcTextSize(text);
    float tx = pos.x + (pw - textSize.x) * 0.5f;
    float ty = pos.y + (ph - textSize.y) * 0.5f;
    dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 240), text);
}

} // namespace fate
