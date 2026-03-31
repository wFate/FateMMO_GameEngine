#include "engine/ui/widgets/party_frame.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <algorithm>

namespace fate {

PartyFrame::PartyFrame(const std::string& id)
    : UINode(id, "party_frame") {}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void PartyFrame::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;
    if (members.empty()) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    int count = (std::min)(static_cast<int>(members.size()), 2);

    float s = layoutScale_;
    float cW = cardWidth * s;
    float cH = cardHeight * s;
    float cS = cardSpacing * s;

    for (int i = 0; i < count; ++i) {
        const PartyFrameMemberInfo& m = members[static_cast<size_t>(i)];

        float cardX = rect.x;
        float cardY = rect.y + static_cast<float>(i) * (cH + cS);

        // ---- Card background ----
        batch.drawRect(
            {cardX + cW * 0.5f, cardY + cH * 0.5f},
            {cW, cH},
            cardBgColor, d);

        // ---- Thin border ----
        float bw = borderWidth * s;
        float ibH = cH - bw * 2.0f;
        batch.drawRect({cardX + cW * 0.5f, cardY + bw * 0.5f},           {cW, bw},  cardBorderColor, d + 0.05f);
        batch.drawRect({cardX + cW * 0.5f, cardY + cH - bw * 0.5f}, {cW, bw}, cardBorderColor, d + 0.05f);
        batch.drawRect({cardX + bw * 0.5f,        cardY + cH * 0.5f},    {bw, ibH},        cardBorderColor, d + 0.05f);
        batch.drawRect({cardX + cW - bw * 0.5f, cardY + cH * 0.5f}, {bw, ibH},      cardBorderColor, d + 0.05f);

        // ---- Portrait circle ----
        float portR  = portraitRadius * s;
        float portCX = cardX + portraitPadLeft * s + portR + portraitOffset.x * s;
        float portCY = cardY + cH * 0.5f + portraitOffset.y * s;

        batch.drawCircle({portCX, portCY}, portR, portraitFillColor, d + 0.1f, 16);
        batch.drawRing  ({portCX, portCY}, portR, portraitRimWidth * s, portraitRimColor, d + 0.2f, 16);

        // ---- Leader crown (diamond) ----
        if (m.isLeader) {
            // Small yellow diamond above portrait top-right
            float cx = portCX + portR * 0.6f;
            float cy = portCY - portR * 0.6f;
            float sz = crownSize * s;
            // Draw diamond as two triangles via thin rects at angle — approximate with a small rect
            batch.drawRect({cx, cy}, {sz, sz}, crownColor, d + 0.3f);
        }

        // ---- Text layout ----
        float textX    = portCX + portR + textGapAfterPortrait * s;
        float textMaxW = cW - (textX - cardX) - textPadRight * s;

        // Name
        float nameY = cardY + namePadTop * s;
        sdf.drawScreen(batch, m.name.c_str(), {textX + nameOffset.x * s, nameY + nameOffset.y * s}, scaledFont(nameFontSize), nameColor, d + 0.2f);

        // "Lv N" right-aligned
        char lvBuf[16];
        snprintf(lvBuf, sizeof(lvBuf), "Lv %d", m.level);
        Vec2 lvSize = sdf.measure(lvBuf, scaledFont(levelFontSize));
        float lvX = cardX + cW - lvSize.x - levelPadRight * s;
        sdf.drawScreen(batch, lvBuf, {lvX + levelOffset.x * s, nameY + levelOffset.y * s}, scaledFont(levelFontSize), levelColor, d + 0.2f);

        // ---- HP bar ----
        float barX  = textX + barOffset.x * s;
        float barW  = textMaxW;
        float hpBarY = nameY + barOffsetY * s + barOffset.y * s;
        float hpBarH = hpBarHeight * s;

        float hpFrac = (m.maxHp > 0.0f) ? std::max(0.0f, std::min(1.0f, m.hp / m.maxHp)) : 0.0f;

        batch.drawRect({barX + barW * 0.5f, hpBarY + hpBarH * 0.5f},
                       {barW, hpBarH}, hpBarBgColor, d + 0.1f);
        if (hpFrac > 0.0f) {
            float fillW = barW * hpFrac;
            batch.drawRect({barX + fillW * 0.5f, hpBarY + hpBarH * 0.5f},
                           {fillW, hpBarH}, hpFillColor, d + 0.2f);
        }

        // ---- MP bar ----
        float mpBarY = hpBarY + hpBarH + barGap * s;
        float mpBarH = mpBarHeight * s;

        float mpFrac = (m.maxMp > 0.0f) ? std::max(0.0f, std::min(1.0f, m.mp / m.maxMp)) : 0.0f;

        batch.drawRect({barX + barW * 0.5f, mpBarY + mpBarH * 0.5f},
                       {barW, mpBarH}, mpBarBgColor, d + 0.1f);
        if (mpFrac > 0.0f) {
            float fillW = barW * mpFrac;
            batch.drawRect({barX + fillW * 0.5f, mpBarY + mpBarH * 0.5f},
                           {fillW, mpBarH}, mpFillColor, d + 0.2f);
        }
    }

    renderChildren(batch, sdf);
}

} // namespace fate
