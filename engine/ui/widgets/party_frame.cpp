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

    for (int i = 0; i < count; ++i) {
        const PartyMemberInfo& m = members[static_cast<size_t>(i)];

        float cardX = rect.x;
        float cardY = rect.y + static_cast<float>(i) * (cardHeight + cardSpacing);

        // ---- Card background ----
        Color cardBg = {0.08f, 0.08f, 0.12f, 0.85f};
        batch.drawRect(
            {cardX + cardWidth * 0.5f, cardY + cardHeight * 0.5f},
            {cardWidth, cardHeight},
            cardBg, d);

        // ---- Thin border ----
        Color bdr = {0.25f, 0.25f, 0.35f, 0.70f};
        float bw = 1.0f;
        float ibH = cardHeight - bw * 2.0f;
        batch.drawRect({cardX + cardWidth * 0.5f, cardY + bw * 0.5f},           {cardWidth, bw},  bdr, d + 0.05f);
        batch.drawRect({cardX + cardWidth * 0.5f, cardY + cardHeight - bw * 0.5f}, {cardWidth, bw}, bdr, d + 0.05f);
        batch.drawRect({cardX + bw * 0.5f,        cardY + cardHeight * 0.5f},    {bw, ibH},        bdr, d + 0.05f);
        batch.drawRect({cardX + cardWidth - bw * 0.5f, cardY + cardHeight * 0.5f}, {bw, ibH},      bdr, d + 0.05f);

        // ---- Portrait circle ----
        float portR  = 10.0f;
        float portCX = cardX + 6.0f + portR;
        float portCY = cardY + cardHeight * 0.5f;

        Color portBg  = {0.20f, 0.22f, 0.30f, 0.95f};
        Color portRim = {0.45f, 0.45f, 0.60f, 0.80f};
        batch.drawCircle({portCX, portCY}, portR, portBg, d + 0.1f, 16);
        batch.drawRing  ({portCX, portCY}, portR, 1.5f, portRim, d + 0.2f, 16);

        // ---- Leader crown (diamond) ----
        if (m.isLeader) {
            // Small yellow diamond above portrait top-right
            float cx = portCX + portR * 0.6f;
            float cy = portCY - portR * 0.6f;
            float sz = 5.0f;
            Color crown = {1.0f, 0.82f, 0.0f, 1.0f};
            // Draw diamond as two triangles via thin rects at angle — approximate with a small rect
            batch.drawRect({cx, cy}, {sz, sz}, crown, d + 0.3f);
        }

        // ---- Text layout ----
        float textX    = portCX + portR + 6.0f;
        float textMaxW = cardWidth - (textX - cardX) - 4.0f;

        // Name (white, 11px)
        float nameY = cardY + 6.0f;
        Color nameColor = {1.0f, 1.0f, 1.0f, 0.95f};
        sdf.drawScreen(batch, m.name.c_str(), {textX, nameY}, 11.0f, nameColor, d + 0.2f);

        // "Lv N" right-aligned (grey, 10px)
        char lvBuf[16];
        snprintf(lvBuf, sizeof(lvBuf), "Lv %d", m.level);
        Color lvColor = {0.65f, 0.65f, 0.70f, 0.85f};
        Vec2 lvSize = sdf.measure(lvBuf, 10.0f);
        float lvX = cardX + cardWidth - lvSize.x - 5.0f;
        sdf.drawScreen(batch, lvBuf, {lvX, nameY}, 10.0f, lvColor, d + 0.2f);

        // ---- HP bar (red, 8px tall) ----
        float barX  = textX;
        float barW  = textMaxW;
        float hpBarY = nameY + 13.0f;
        float hpBarH = 8.0f;

        Color hpBg   = {0.15f, 0.08f, 0.08f, 0.85f};
        Color hpFill = {0.80f, 0.18f, 0.18f, 1.0f};
        float hpFrac = (m.maxHp > 0.0f) ? std::max(0.0f, std::min(1.0f, m.hp / m.maxHp)) : 0.0f;

        batch.drawRect({barX + barW * 0.5f, hpBarY + hpBarH * 0.5f},
                       {barW, hpBarH}, hpBg, d + 0.1f);
        if (hpFrac > 0.0f) {
            float fillW = barW * hpFrac;
            batch.drawRect({barX + fillW * 0.5f, hpBarY + hpBarH * 0.5f},
                           {fillW, hpBarH}, hpFill, d + 0.2f);
        }

        // ---- MP bar (blue, 6px tall) ----
        float mpBarY = hpBarY + hpBarH + 2.0f;
        float mpBarH = 6.0f;

        Color mpBg   = {0.08f, 0.08f, 0.18f, 0.85f};
        Color mpFill = {0.18f, 0.40f, 0.85f, 1.0f};
        float mpFrac = (m.maxMp > 0.0f) ? std::max(0.0f, std::min(1.0f, m.mp / m.maxMp)) : 0.0f;

        batch.drawRect({barX + barW * 0.5f, mpBarY + mpBarH * 0.5f},
                       {barW, mpBarH}, mpBg, d + 0.1f);
        if (mpFrac > 0.0f) {
            float fillW = barW * mpFrac;
            batch.drawRect({barX + fillW * 0.5f, mpBarY + mpBarH * 0.5f},
                           {fillW, mpBarH}, mpFill, d + 0.2f);
        }
    }

    renderChildren(batch, sdf);
}

} // namespace fate
