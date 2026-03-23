#include "engine/ui/widgets/teleporter_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>
#include <SDL.h>

namespace fate {

TeleporterPanel::TeleporterPanel(const std::string& id)
    : UINode(id, "teleporter_panel") {}

// ---------------------------------------------------------------------------
// open / close / rebuild
// ---------------------------------------------------------------------------

void TeleporterPanel::open(uint32_t npc, const std::vector<TeleportDestination>& dests,
                           int64_t gold, uint16_t level) {
    npcId = npc;
    playerGold = gold;
    playerLevel = level;

    destinations.clear();
    destinations.reserve(dests.size());
    for (const auto& d : dests) {
        Destination dest;
        dest.name = d.destinationName;
        dest.sceneId = d.sceneId;
        dest.position = d.targetPosition;
        dest.cost = d.cost;
        dest.requiredLevel = d.requiredLevel;
        destinations.push_back(std::move(dest));
    }

    rebuild();
    setVisible(true);
}

void TeleporterPanel::close() {
    setVisible(false);
    if (onClose) onClose(id_);
}

void TeleporterPanel::rebuild() {
    scrollOffset_ = 0.0f;
    errorMessage.clear();
    errorTimer = 0.0f;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

void TeleporterPanel::update(float dt) {
    if (errorTimer > 0.0f) {
        errorTimer -= dt;
        if (errorTimer < 0.0f) {
            errorTimer = 0.0f;
            errorMessage.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void TeleporterPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // ---- Background (dark panel) ----
    Color bg  = {0.08f, 0.08f, 0.12f, 0.95f};
    Color bdr = {0.25f, 0.25f, 0.35f, 1.0f};
    float bw  = 2.0f;

    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);
    // Border edges
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},           {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},  {rect.w, bw}, bdr, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},       {bw, innerH}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},  {bw, innerH}, bdr, d + 0.1f);

    // ---- Title bar ----
    float headerH = 28.0f;
    Color titleBarBg = {0.12f, 0.12f, 0.18f, 1.0f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + headerH * 0.5f},
                   {rect.w - bw * 2.0f, headerH}, titleBarBg, d + 0.05f);

    Color titleColor = {0.9f, 0.9f, 0.85f, 1.0f};
    sdf.drawScreen(batch, title,
        {rect.x + 10.0f, rect.y + 7.0f},
        14.0f, titleColor, d + 0.2f);

    // ---- Close button (X at top-right, 20x20) ----
    float closeSize = 20.0f;
    float closeCX = rect.x + rect.w - closeSize * 0.5f - 6.0f;
    float closeCY = rect.y + headerH * 0.5f;
    Color closeBg  = {0.3f, 0.15f, 0.15f, 0.9f};
    Color closeXC  = {0.9f, 0.9f, 0.85f, 1.0f};
    batch.drawRect({closeCX, closeCY}, {closeSize, closeSize}, closeBg, d + 0.2f);
    Vec2 xts = sdf.measure("X", 12.0f);
    sdf.drawScreen(batch, "X",
        {closeCX - xts.x * 0.5f, closeCY - xts.y * 0.5f},
        12.0f, closeXC, d + 0.3f);

    // ---- Divider below title ----
    Color divColor = {0.25f, 0.25f, 0.35f, 0.6f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + headerH},
                   {rect.w - bw * 2.0f, 1.0f}, divColor, d + 0.1f);

    // ---- "Select a destination:" label ----
    float curY = rect.y + headerH + 6.0f;
    Color labelColor = {0.7f, 0.7f, 0.65f, 1.0f};
    sdf.drawScreen(batch, "Select a destination:",
        {rect.x + 10.0f, curY},
        11.0f, labelColor, d + 0.2f);
    curY += 16.0f;

    // ---- Destination rows ----
    float rowH = 40.0f;
    float rowAreaTop = curY;
    float goldBarH = 26.0f;
    float errorAreaH = (errorTimer > 0.0f) ? 18.0f : 0.0f;
    float rowAreaBottom = rect.y + rect.h - goldBarH - errorAreaH - 4.0f;
    int maxVisibleRows = static_cast<int>((rowAreaBottom - rowAreaTop) / rowH);
    if (maxVisibleRows < 1) maxVisibleRows = 1;

    int startRow = static_cast<int>(scrollOffset_ / rowH);
    if (startRow < 0) startRow = 0;
    if (startRow >= static_cast<int>(destinations.size())) startRow = 0;

    Color textColor     = {0.9f, 0.9f, 0.85f, 1.0f};
    Color goldColor     = {1.0f, 0.84f, 0.0f, 1.0f};
    Color disabledColor = {0.4f, 0.4f, 0.4f, 0.5f};
    Color errorColor    = {0.8f, 0.2f, 0.2f, 1.0f};
    Color hoverBg       = {0.15f, 0.15f, 0.22f, 0.8f};
    Color rowBorderC    = {0.2f, 0.2f, 0.3f, 0.5f};

    float rowContentW = rect.w - 20.0f;

    for (int i = 0; i < maxVisibleRows; ++i) {
        int destIdx = startRow + i;
        if (destIdx >= static_cast<int>(destinations.size())) break;

        const Destination& dest = destinations[static_cast<size_t>(destIdx)];
        float rowY = rowAreaTop + static_cast<float>(i) * rowH;
        float rowCY = rowY + rowH * 0.5f;

        bool canAfford = playerGold >= dest.cost;
        bool meetsLevel = playerLevel >= dest.requiredLevel || dest.requiredLevel == 0;
        bool eligible = canAfford && meetsLevel;

        // Row background (alternate shading)
        if (i % 2 == 1) {
            Color altBg = {0.1f, 0.1f, 0.15f, 0.5f};
            batch.drawRect({rect.x + rect.w * 0.5f, rowCY},
                           {rowContentW, rowH}, altBg, d + 0.05f);
        }

        // Row bottom border
        batch.drawRect({rect.x + rect.w * 0.5f, rowY + rowH - 0.5f},
                       {rowContentW, 1.0f}, rowBorderC, d + 0.1f);

        // Destination name
        float nameFontSize = 12.0f;
        Color nameColor = eligible ? textColor : disabledColor;
        sdf.drawScreen(batch, dest.name,
            {rect.x + 14.0f, rowY + 6.0f},
            nameFontSize, nameColor, d + 0.2f);

        // Cost text (right-aligned area)
        char costBuf[32];
        if (dest.cost > 0) {
            snprintf(costBuf, sizeof(costBuf), "%lld Gold",
                     static_cast<long long>(dest.cost));
        } else {
            snprintf(costBuf, sizeof(costBuf), "Free");
        }
        float costFontSize = 10.0f;
        Color costColor = eligible ? goldColor : disabledColor;
        Vec2 costSize = sdf.measure(costBuf, costFontSize);
        sdf.drawScreen(batch, costBuf,
            {rect.x + rect.w - costSize.x - 14.0f, rowY + 8.0f},
            costFontSize, costColor, d + 0.2f);

        // Requirement text below name (if not eligible)
        if (!meetsLevel) {
            char reqBuf[48];
            snprintf(reqBuf, sizeof(reqBuf), "Requires level %d", dest.requiredLevel);
            sdf.drawScreen(batch, reqBuf,
                {rect.x + 14.0f, rowY + 22.0f},
                9.0f, errorColor, d + 0.2f);
        } else if (!canAfford) {
            sdf.drawScreen(batch, "Not enough gold",
                {rect.x + 14.0f, rowY + 22.0f},
                9.0f, errorColor, d + 0.2f);
        }
    }

    // ---- Gold display at bottom ----
    float goldY = rect.y + rect.h - goldBarH - errorAreaH - 2.0f;
    // Divider above gold
    batch.drawRect({rect.x + rect.w * 0.5f, goldY},
                   {rect.w - bw * 2.0f, 1.0f}, divColor, d + 0.1f);

    char goldBuf[48];
    snprintf(goldBuf, sizeof(goldBuf), "Gold: %lld",
             static_cast<long long>(playerGold));
    sdf.drawScreen(batch, goldBuf,
        {rect.x + 10.0f, goldY + 5.0f},
        12.0f, goldColor, d + 0.2f);

    // ---- Error message (if active) ----
    if (errorTimer > 0.0f && !errorMessage.empty()) {
        float errAlpha = (errorTimer < 1.0f) ? errorTimer : 1.0f;
        Color errColor = {0.8f, 0.2f, 0.2f, errAlpha};
        float errY = rect.y + rect.h - errorAreaH - 2.0f;
        Vec2 errSize = sdf.measure(errorMessage, 10.0f);
        sdf.drawScreen(batch, errorMessage,
            {rect.x + rect.w * 0.5f - errSize.x * 0.5f, errY + 2.0f},
            10.0f, errColor, d + 0.3f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool TeleporterPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // Close button hit test (20x20 rect at top-right)
    float closeSize = 20.0f;
    float headerH = 28.0f;
    float closeCX = computedRect_.w - closeSize * 0.5f - 6.0f;
    float closeCY = headerH * 0.5f;
    if (localPos.x >= closeCX - closeSize * 0.5f &&
        localPos.x <= closeCX + closeSize * 0.5f &&
        localPos.y >= closeCY - closeSize * 0.5f &&
        localPos.y <= closeCY + closeSize * 0.5f) {
        close();
        return true;
    }

    // Hit test destination rows
    float rowH = 40.0f;
    float rowAreaTop = headerH + 6.0f + 16.0f; // header + label gap + label height

    int startRow = static_cast<int>(scrollOffset_ / rowH);
    if (startRow < 0) startRow = 0;

    for (size_t i = 0; i < destinations.size(); ++i) {
        int visIdx = static_cast<int>(i) - startRow;
        if (visIdx < 0) continue;

        float rowY = rowAreaTop + static_cast<float>(visIdx) * rowH;
        if (localPos.y >= rowY && localPos.y < rowY + rowH &&
            localPos.x >= 10.0f && localPos.x <= computedRect_.w - 10.0f) {

            const Destination& dest = destinations[i];
            bool canAfford = playerGold >= dest.cost;
            bool meetsLevel = playerLevel >= dest.requiredLevel || dest.requiredLevel == 0;

            if (canAfford && meetsLevel) {
                if (onTeleport) {
                    onTeleport(npcId, static_cast<uint8_t>(i));
                }
            } else if (!meetsLevel) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Requires level %d", dest.requiredLevel);
                errorMessage = buf;
                errorTimer = 3.0f;
            } else {
                errorMessage = "Not enough gold";
                errorTimer = 3.0f;
            }
            return true;
        }
    }

    return true; // consume all clicks on panel
}

bool TeleporterPanel::onKeyInput(int scancode, bool pressed) {
    if (!pressed) return false;
    if (scancode == SDL_SCANCODE_ESCAPE) {
        close();
        return true;
    }
    return false;
}

} // namespace fate
