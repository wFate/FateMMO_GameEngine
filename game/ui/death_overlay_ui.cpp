#include "game/ui/death_overlay_ui.h"
#include "game/ui/game_viewport.h"
#include "game/components/game_components.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>

namespace fate {

void DeathOverlayUI::onDeath(int32_t xpLost, int32_t honorLost, float respawnTimer) {
    active_ = true;
    countdown_ = respawnTimer;
    xpLost_ = xpLost;
    honorLost_ = honorLost;
}

// Helper: draw centered text on ForegroundDrawList, returns Y advance
static float drawCenteredText(ImDrawList* dl, const char* text, float cx, float y,
                              ImU32 col, ImFont* font, float fontSize) {
    ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    dl->AddText(font, fontSize, ImVec2(cx - sz.x * 0.5f, y), col, text);
    return sz.y + 4.0f;
}

// Helper: draw a button on ForegroundDrawList, returns true if clicked
static bool drawFgButton(ImDrawList* dl, const char* label, ImVec2 pos, ImVec2 size,
                          ImU32 bgCol, ImU32 hoverCol, ImU32 textCol,
                          ImFont* font, float fontSize, bool enabled) {
    ImVec2 min = pos;
    ImVec2 max = ImVec2(pos.x + size.x, pos.y + size.y);

    bool hovered = enabled && ImGui::IsMouseHoveringRect(min, max);
    bool clicked = hovered && ImGui::IsMouseClicked(0);

    ImU32 bg = (hovered && enabled) ? hoverCol : bgCol;
    if (!enabled) bg = IM_COL32(40, 40, 50, 180);

    dl->AddRectFilled(min, max, bg, 4.0f);
    dl->AddRect(min, max, IM_COL32(80, 80, 100, 200), 4.0f);

    ImVec2 textSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    dl->AddText(font, fontSize,
        ImVec2(pos.x + (size.x - textSz.x) * 0.5f, pos.y + (size.y - textSz.y) * 0.5f),
        enabled ? textCol : IM_COL32(120, 120, 120, 180), label);

    return clicked && enabled;
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

    // Render entirely on ForegroundDrawList so it layers above nameplates
    auto* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    float fontSize = font->FontSize;

    float panelW = 300.0f;
    float panelH = 220.0f;
    float cx = GameViewport::centerX();
    float cy = GameViewport::centerY();
    ImVec2 panelMin(cx - panelW * 0.5f, cy - panelH * 0.5f);
    ImVec2 panelMax(cx + panelW * 0.5f, cy + panelH * 0.5f);

    // Panel background
    dl->AddRectFilled(panelMin, panelMax, IM_COL32(13, 5, 5, 217), 8.0f);
    dl->AddRect(panelMin, panelMax, IM_COL32(128, 25, 25, 204), 8.0f);

    float y = panelMin.y + 14.0f;

    // "You have died."
    y += drawCenteredText(dl, "You have died.", cx, y, IM_COL32(255, 77, 77, 255), font, fontSize * 1.1f);
    y += 4.0f;

    // Penalty display
    if (xpLost_ > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Lost %d XP", xpLost_);
        y += drawCenteredText(dl, buf, cx, y, IM_COL32(255, 153, 77, 255), font, fontSize);
    }
    if (honorLost_ > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Lost %d Honor", honorLost_);
        y += drawCenteredText(dl, buf, cx, y, IM_COL32(204, 102, 204, 255), font, fontSize);
    }

    // Countdown
    bool timerReady = countdown_ <= 0.0f;
    if (!timerReady) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Respawn available in %d...", (int)countdown_ + 1);
        y += drawCenteredText(dl, buf, cx, y, IM_COL32(179, 179, 179, 255), font, fontSize);
    }

    y += 8.0f;

    float buttonW = panelW - 40.0f;
    float buttonH = 28.0f;
    float buttonX = panelMin.x + 20.0f;

    // Respawn in Town
    if (drawFgButton(dl, "Respawn in Town", ImVec2(buttonX, y), ImVec2(buttonW, buttonH),
                     IM_COL32(50, 50, 70, 220), IM_COL32(70, 70, 100, 240), IM_COL32(255, 255, 255, 255),
                     font, fontSize, timerReady)) {
        if (onRespawnRequested) onRespawnRequested(0);
    }
    y += buttonH + 4.0f;

    // Respawn at Spawn Point
    if (drawFgButton(dl, "Respawn at Spawn Point", ImVec2(buttonX, y), ImVec2(buttonW, buttonH),
                     IM_COL32(50, 50, 70, 220), IM_COL32(70, 70, 100, 240), IM_COL32(255, 255, 255, 255),
                     font, fontSize, timerReady)) {
        if (onRespawnRequested) onRespawnRequested(1);
    }
    y += buttonH + 4.0f;

    // Phoenix Down
    auto* invComp = player->getComponent<InventoryComponent>();
    int phoenixCount = invComp ? invComp->inventory.countItem("phoenix_down") : 0;

    if (phoenixCount > 0) {
        char label[64];
        std::snprintf(label, sizeof(label), "Respawn Here (Phoenix Down x%d)", phoenixCount);
        if (drawFgButton(dl, label, ImVec2(buttonX, y), ImVec2(buttonW, buttonH),
                         IM_COL32(153, 102, 25, 255), IM_COL32(179, 128, 38, 255), IM_COL32(255, 255, 255, 255),
                         font, fontSize, true)) {
            if (onRespawnRequested) onRespawnRequested(2);
        }
    }
}

} // namespace fate
