#include "engine/ui/widgets/guild_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <algorithm>

namespace fate {

GuildPanel::GuildPanel(const std::string& id)
    : UINode(id, "guild_panel") {}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void GuildPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // ---- Parchment background ----
    Color bg  = {0.87f, 0.80f, 0.67f, 0.97f};
    Color bdr = {0.40f, 0.30f, 0.20f, 1.0f};
    float bw  = 3.0f;

    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);
    float ibH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},              {rect.w, bw},  bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},     {rect.w, bw},  bdr, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},          {bw, ibH},     bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},     {bw, ibH},     bdr, d + 0.1f);

    // ---- Title: "GUILD" ----
    Color titleColor = {0.28f, 0.18f, 0.08f, 1.0f};
    Vec2 titleSize = sdf.measure("GUILD", scaledFont(16.0f));
    sdf.drawScreen(batch, "GUILD",
        {rect.x + (rect.w - titleSize.x) * 0.5f, rect.y + 7.0f * layoutScale_},
        scaledFont(16.0f), titleColor, d + 0.2f);

    // ---- Close button (X circle, top-right) ----
    float closeR  = 11.0f * layoutScale_;
    float closeCX = rect.x + rect.w - closeR - 6.0f * layoutScale_;
    float closeCY = rect.y + closeR + 5.0f * layoutScale_;
    Color closeBg  = {0.55f, 0.42f, 0.28f, 1.0f};
    Color closeBdr = {0.30f, 0.20f, 0.10f, 1.0f};
    Color closeXC  = {1.0f, 0.95f, 0.88f, 1.0f};
    batch.drawCircle({closeCX, closeCY}, closeR, closeBg,  d + 0.2f, 16);
    batch.drawRing  ({closeCX, closeCY}, closeR, 1.5f * layoutScale_, closeBdr, d + 0.3f, 16);
    Vec2 xts = sdf.measure("X", scaledFont(10.0f));
    sdf.drawScreen(batch, "X",
        {closeCX - xts.x * 0.5f, closeCY - xts.y * 0.5f},
        scaledFont(10.0f), closeXC, d + 0.4f);

    // ---- Horizontal divider below title ----
    float headerH = 32.0f * layoutScale_;
    Color divColor = {0.38f, 0.28f, 0.18f, 0.5f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + headerH},
                   {rect.w - bw * 2.0f, 1.5f * layoutScale_}, divColor, d + 0.1f);

    // ---- Top section: guild info + emblem ----
    float topSectionY  = rect.y + headerH + 6.0f * layoutScale_;
    float emblemSize   = 64.0f * layoutScale_;
    float emblemX      = rect.x + 10.0f * layoutScale_;
    float emblemY      = topSectionY;
    float emblemCX     = emblemX + emblemSize * 0.5f;
    float emblemCY     = emblemY + emblemSize * 0.5f;

    // Emblem placeholder: inset parchment rect with "?"
    Color emblemBg  = {0.78f, 0.70f, 0.55f, 1.0f};
    Color emblemBdr = {0.42f, 0.32f, 0.20f, 0.80f};
    float ebw = 1.5f * layoutScale_;
    float eibH = emblemSize - ebw * 2.0f;
    batch.drawRect({emblemCX, emblemCY}, {emblemSize, emblemSize}, emblemBg, d + 0.1f);
    batch.drawRect({emblemCX, emblemY + ebw * 0.5f},                {emblemSize, ebw}, emblemBdr, d + 0.2f);
    batch.drawRect({emblemCX, emblemY + emblemSize - ebw * 0.5f},   {emblemSize, ebw}, emblemBdr, d + 0.2f);
    batch.drawRect({emblemX + ebw * 0.5f, emblemCY},                {ebw, eibH},       emblemBdr, d + 0.2f);
    batch.drawRect({emblemX + emblemSize - ebw * 0.5f, emblemCY},   {ebw, eibH},       emblemBdr, d + 0.2f);

    Color emblemQ = {0.45f, 0.35f, 0.22f, 0.70f};
    Vec2  qts = sdf.measure("?", scaledFont(22.0f));
    sdf.drawScreen(batch, "?",
        {emblemCX - qts.x * 0.5f, emblemCY - qts.y * 0.5f},
        scaledFont(22.0f), emblemQ, d + 0.3f);

    // Guild name, level, member count (right of emblem)
    float infoX = emblemX + emblemSize + 10.0f * layoutScale_;
    float infoY = topSectionY + 4.0f * layoutScale_;

    std::string displayName = guildName.empty() ? "[No Guild]" : guildName;
    Color gnColor = {0.25f, 0.15f, 0.05f, 1.0f};
    sdf.drawScreen(batch, displayName.c_str(), {infoX, infoY}, scaledFont(14.0f), gnColor, d + 0.2f);

    char lvBuf[32];
    snprintf(lvBuf, sizeof(lvBuf), "Level %d", guildLevel);
    Color metaColor = {0.42f, 0.32f, 0.18f, 1.0f};
    sdf.drawScreen(batch, lvBuf, {infoX, infoY + 18.0f * layoutScale_}, scaledFont(11.0f), metaColor, d + 0.2f);

    char mbBuf[32];
    snprintf(mbBuf, sizeof(mbBuf), "Members: %d", memberCount > 0 ? memberCount
                                                                    : static_cast<int>(members.size()));
    sdf.drawScreen(batch, mbBuf, {infoX, infoY + 33.0f * layoutScale_}, scaledFont(11.0f), metaColor, d + 0.2f);

    // ---- Divider above roster ----
    float rosterTopY = topSectionY + emblemSize + 8.0f * layoutScale_;
    batch.drawRect({rect.x + rect.w * 0.5f, rosterTopY},
                   {rect.w - bw * 2.0f, 1.5f * layoutScale_}, divColor, d + 0.1f);

    // ---- Roster header ----
    float rosterHeaderY = rosterTopY + 4.0f * layoutScale_;
    float rosterHeaderH = 18.0f * layoutScale_;
    Color hdrBg = {0.72f, 0.64f, 0.50f, 0.80f};
    batch.drawRect({rect.x + rect.w * 0.5f, rosterHeaderY + rosterHeaderH * 0.5f},
                   {rect.w - bw * 2.0f, rosterHeaderH}, hdrBg, d + 0.1f);

    float col1X = rect.x + 8.0f * layoutScale_;
    float col2X = rect.x + rect.w * 0.48f;
    float col3X = rect.x + rect.w * 0.65f;
    float col4X = rect.x + rect.w * 0.84f;

    Color hdrColor = {0.25f, 0.15f, 0.05f, 1.0f};
    float hdrFS = scaledFont(9.5f);
    sdf.drawScreen(batch, "Name",   {col1X, rosterHeaderY + 3.0f * layoutScale_}, hdrFS, hdrColor, d + 0.2f);
    sdf.drawScreen(batch, "Lv",     {col2X, rosterHeaderY + 3.0f * layoutScale_}, hdrFS, hdrColor, d + 0.2f);
    sdf.drawScreen(batch, "Rank",   {col3X, rosterHeaderY + 3.0f * layoutScale_}, hdrFS, hdrColor, d + 0.2f);
    sdf.drawScreen(batch, "Status", {col4X, rosterHeaderY + 3.0f * layoutScale_}, hdrFS, hdrColor, d + 0.2f);

    // ---- Roster rows ----
    float rowH        = 22.0f * layoutScale_;
    float rowAreaY    = rosterHeaderY + rosterHeaderH + 2.0f * layoutScale_;
    float rowAreaH    = rect.y + rect.h - rowAreaY - 6.0f * layoutScale_;
    int   maxRows     = static_cast<int>(rowAreaH / rowH);

    // Apply scroll: determine start index
    int startRow = static_cast<int>(scrollOffset / rowH);
    if (startRow < 0) startRow = 0;
    if (startRow >= static_cast<int>(members.size())) startRow = 0;

    for (int i = 0; i < maxRows; ++i) {
        int memberIdx = startRow + i;
        if (memberIdx >= static_cast<int>(members.size())) break;

        const GuildPanelMemberInfo& gm = members[static_cast<size_t>(memberIdx)];
        float rowY   = rowAreaY + static_cast<float>(i) * rowH;
        float rowCY  = rowY + rowH * 0.5f;

        // Alternate row shading
        if (i % 2 == 1) {
            Color altBg = {0.0f, 0.0f, 0.0f, 0.06f};
            batch.drawRect({rect.x + rect.w * 0.5f, rowCY},
                           {rect.w - bw * 2.0f, rowH}, altBg, d + 0.05f);
        }

        Color rowColor = {0.22f, 0.14f, 0.06f, 1.0f};
        float rowFS = scaledFont(10.0f);

        // Name
        sdf.drawScreen(batch, gm.name.c_str(), {col1X, rowY + 5.0f * layoutScale_}, rowFS, rowColor, d + 0.2f);

        // Level
        char lvbuf[8];
        snprintf(lvbuf, sizeof(lvbuf), "%d", gm.level);
        sdf.drawScreen(batch, lvbuf, {col2X, rowY + 5.0f * layoutScale_}, rowFS, rowColor, d + 0.2f);

        // Rank badge
        Color rankColor = {0.28f, 0.18f, 0.08f, 1.0f};
        if (gm.rank == "Leader") rankColor = {0.75f, 0.55f, 0.0f, 1.0f};
        else if (gm.rank == "Officer") rankColor = {0.35f, 0.52f, 0.75f, 1.0f};
        sdf.drawScreen(batch, gm.rank.c_str(), {col3X, rowY + 5.0f * layoutScale_}, scaledFont(9.0f), rankColor, d + 0.2f);

        // Online dot
        Color dotColor = gm.online
            ? Color{0.2f, 0.85f, 0.3f, 1.0f}
            : Color{0.45f, 0.45f, 0.45f, 0.8f};
        batch.drawCircle({col4X + 4.0f * layoutScale_, rowCY}, 4.5f * layoutScale_, dotColor, d + 0.2f, 12);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool GuildPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // Close button hit test
    float closeR  = 11.0f;
    float closeCX = computedRect_.w - closeR - 6.0f;
    float closeCY = closeR + 5.0f;
    float dx = localPos.x - closeCX;
    float dy = localPos.y - closeCY;
    if (dx * dx + dy * dy <= closeR * closeR) {
        if (onClose) onClose(id_);
        return true;
    }

    return true;  // consume all clicks on panel
}

} // namespace fate
