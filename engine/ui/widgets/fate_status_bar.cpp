// engine/ui/widgets/fate_status_bar.cpp
#include "engine/ui/widgets/fate_status_bar.h"
#include "engine/ui/widgets/metallic_draw.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace fate {

// ---- Constants (all in reference pixels, scaled by layoutScale_) -----------
static constexpr float kTopBarHeight   = 40.0f;  // dark strip height (TWOM-sized, text overflows slightly)
static constexpr float kPortraitRadius = 20.0f;  // portrait circle
static constexpr float kBarHeight      = 18.0f;  // HP / MP bar height (TWOM bold)
static constexpr float kMenuBtnR       = 21.0f;  // Menu button radius
static constexpr float kChatBtnR       = 21.0f;  // Chat button radius
static constexpr float kMenuItemH      = 36.0f;  // menu overlay row height
static constexpr float kMenuOverlayW   = 140.0f; // menu overlay width

static constexpr float kPi = 3.14159265358979f;

// --------------------------------------------------------------------------

FateStatusBar::FateStatusBar(const std::string& id)
    : UINode(id, "fate_status_bar")
{
    menuItems = {"Event", "Inventory", "Skill", "Guild", "Party", "Status", "Settings"};
}

// --------------------------------------------------------------------------
// Main render
// --------------------------------------------------------------------------
void FateStatusBar::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    float d = static_cast<float>(zOrder_);
    float s = layoutScale_;

    renderTopBar(batch, sdf, d, s);
    renderMenuButton(batch, sdf, d, s);
    renderChatButton(batch, sdf, d, s);

    if (menuOpen)
        renderMenuOverlay(batch, sdf, d, s);

    renderChildren(batch, sdf);
}

// --------------------------------------------------------------------------
// Top bar: dark strip, portrait, level, HP/MP bars, coordinates
// --------------------------------------------------------------------------
void FateStatusBar::renderTopBar(SpriteBatch& batch, SDFText& sdf,
                                  float d, float s) {
    const auto& rect = computedRect_;

    // Semi-transparent dark strip across top
    float barH = kTopBarHeight * s;
    Color stripBg{0.0f, 0.0f, 0.0f, 0.45f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + barH * 0.5f},
                   {rect.w, barH}, stripBg, d - 0.2f);

    Color white{1.0f, 1.0f, 1.0f, 1.0f};
    Color shadow{0.0f, 0.0f, 0.0f, 0.85f};
    Color yellow{1.0f, 0.9f, 0.2f, 1.0f};

    // Portrait circle (40px diameter)
    float pr = kPortraitRadius * s;
    Vec2 pc = {rect.x + 4.0f * s + pr, rect.y + 4.0f * s + pr};
    batch.drawCircle(pc, pr, {0.15f, 0.15f, 0.2f, 0.9f}, d, 24);
    batch.drawRing(pc, pr, 2.0f * s, {0.55f, 0.55f, 0.75f, 1.0f}, d + 0.05f, 24);

    // --- TWOM horizontal row: [LV 20] [HP] [████] [149/149] [MP] [████] [419/505] ---
    // Bars stretch to fill available space (like TWOM — fills portrait-to-edge)
    float rowCenterY = rect.y + barH * 0.5f;
    float bh = 22.0f * s;
    float gap = 4.0f * s;  // small gaps between elements

    // Pre-measure all fixed-width text elements
    char lvBuf[16];
    snprintf(lvBuf, sizeof(lvBuf), "LV %d", level);
    float lvFont = scaledFont(26.0f);
    Vec2 lvSz = sdf.measure(std::string(lvBuf), lvFont);

    float labelFont = scaledFont(22.0f);
    Vec2 hpLabelSz = sdf.measure("HP", labelFont);
    Vec2 mpLabelSz = sdf.measure("MP", labelFont);

    char hpBuf[32], mpBuf[32];
    snprintf(hpBuf, sizeof(hpBuf), "%.0f/%.0f", hp, maxHp);
    snprintf(mpBuf, sizeof(mpBuf), "%.0f/%.0f", mp, maxMp);
    float numFont = scaledFont(28.0f);
    Vec2 hpNumSz = sdf.measure(std::string(hpBuf), numFont);
    Vec2 mpNumSz = sdf.measure(std::string(mpBuf), numFont);

    // Calculate bar width: fill remaining space equally between HP and MP bars
    float leftEdge = pc.x + pr + 8.0f * s;     // right of portrait
    float rightEdge = rect.x + rect.w - 60.0f * s; // leave room for Chat button
    float fixedWidth = lvSz.x + gap*2 + hpLabelSz.x + gap + hpNumSz.x + gap*2
                     + mpLabelSz.x + gap + mpNumSz.x;
    float availForBars = rightEdge - leftEdge - fixedWidth;
    float bw = std::max(availForBars * 0.5f, 80.0f * s); // each bar gets half, min 80px

    // Now draw left-to-right
    float curX = leftEdge;

    // LV text
    float lvY = rowCenterY - lvSz.y * 0.5f;
    sdf.drawScreen(batch, std::string(lvBuf), Vec2{curX + 1.5f, lvY + 1.5f}, lvFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, std::string(lvBuf), Vec2{curX, lvY}, lvFont, white, d + 0.2f);
    curX += lvSz.x + gap * 2;

    // HP label
    float labelY = rowCenterY - hpLabelSz.y * 0.5f;
    sdf.drawScreen(batch, "HP", Vec2{curX + 1.0f, labelY + 1.0f}, labelFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, "HP", Vec2{curX, labelY}, labelFont, white, d + 0.2f);
    curX += hpLabelSz.x + gap;

    // HP bar (stretches to fill)
    batch.drawRect({curX + bw * 0.5f, rowCenterY}, {bw, bh}, {0.1f, 0.1f, 0.1f, 0.85f}, d);
    float hpRatio = (maxHp > 0.0f) ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 0.0f;
    if (hpRatio > 0.0f) {
        float fw = bw * hpRatio;
        batch.drawRect({curX + fw * 0.5f, rowCenterY}, {fw, bh}, {0.9f, 0.55f, 0.1f, 1.0f}, d + 0.01f);
    }
    curX += bw + gap;

    // HP numbers
    float numY = rowCenterY - hpNumSz.y * 0.5f;
    sdf.drawScreen(batch, std::string(hpBuf), Vec2{curX + 1.5f, numY + 1.5f}, numFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, std::string(hpBuf), Vec2{curX, numY}, numFont, yellow, d + 0.2f);
    curX += hpNumSz.x + gap * 2;

    // MP label
    sdf.drawScreen(batch, "MP", Vec2{curX + 1.0f, labelY + 1.0f}, labelFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, "MP", Vec2{curX, labelY}, labelFont, white, d + 0.2f);
    curX += mpLabelSz.x + gap;

    // MP bar (stretches to fill, same width as HP)
    batch.drawRect({curX + bw * 0.5f, rowCenterY}, {bw, bh}, {0.1f, 0.1f, 0.1f, 0.85f}, d);
    float mpRatio = (maxMp > 0.0f) ? std::clamp(mp / maxMp, 0.0f, 1.0f) : 0.0f;
    if (mpRatio > 0.0f) {
        float fw = bw * mpRatio;
        batch.drawRect({curX + fw * 0.5f, rowCenterY}, {fw, bh}, {0.2f, 0.5f, 0.9f, 1.0f}, d + 0.01f);
    }
    curX += bw + gap;

    // MP numbers
    sdf.drawScreen(batch, std::string(mpBuf), Vec2{curX + 1.5f, numY + 1.5f}, numFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, std::string(mpBuf), Vec2{curX, numY}, numFont, yellow, d + 0.2f);
    curX += mpNumSz.x;

    // Coordinates centered below the bar strip (like TWOM's "777,2331")
    char coordBuf[32];
    snprintf(coordBuf, sizeof(coordBuf), "%d,%d", playerTileX, playerTileY);
    float coordFont = scaledFont(11.0f);
    Vec2 coordSz = sdf.measure(std::string(coordBuf), coordFont);
    float rowCenterX = (leftEdge + curX) * 0.5f;
    sdf.drawScreen(batch, std::string(coordBuf), Vec2{rowCenterX - coordSz.x * 0.5f, rect.y + barH + 3.0f * s}, coordFont, {1.0f, 1.0f, 1.0f, 0.8f}, d + 0.2f);
}

// --------------------------------------------------------------------------
// Menu button: EXP progress arc + gold metallic "Menu" button below portrait
// --------------------------------------------------------------------------
void FateStatusBar::renderMenuButton(SpriteBatch& batch, SDFText& sdf,
                                      float d, float s) {
    const auto& rect = computedRect_;
    float barH = kTopBarHeight * s;
    float pr = kPortraitRadius * s;
    Vec2 pc = {rect.x + 6.0f * s + pr, rect.y + barH * 0.5f};

    // EXP progress arc around portrait
    float expR = (kPortraitRadius + 4.0f) * s;
    float xpRatio = (xpToLevel > 0.0f) ? std::clamp(xp / xpToLevel, 0.0f, 1.0f) : 0.0f;
    if (xpRatio > 0.0f) {
        Color expColor{0.8f, 0.65f, 0.1f, 0.9f};
        // Arc from top (-90 deg) clockwise by xpRatio * 360
        float startAngle = -kPi * 0.5f;
        float endAngle   = startAngle + xpRatio * 2.0f * kPi;
        batch.drawArc(pc, expR, startAngle, endAngle, expColor, d + 0.25f, 32);
    }
    // EXP arc background ring (thin, dark)
    Color expBgRing{0.3f, 0.3f, 0.3f, 0.5f};
    batch.drawRing(pc, expR, 2.0f * s, expBgRing, d + 0.05f, 32);

    // Gold metallic "Menu" button below EXP circle
    float mbR = kMenuBtnR * s;
    Vec2 mbCenter = {pc.x, pc.y + expR + mbR + 6.0f * s};
    drawMetallicCircle(batch, mbCenter, mbR, d + 0.3f, resolvedStyle_.opacity);

    // "Menu" label centered on button
    float menuFont = scaledFont(9.0f);
    std::string menuLabel("Menu");
    Vec2 menuSize = sdf.measure(menuLabel, menuFont);
    Color darkText{0.15f, 0.10f, 0.0f, 1.0f};
    sdf.drawScreen(batch, menuLabel,
        Vec2{mbCenter.x - menuSize.x * 0.5f, mbCenter.y - menuSize.y * 0.5f},
        menuFont, darkText, d + 0.35f);

    // Cache hit-test region
    menuBtnCenter_ = mbCenter;
    menuBtnRadius_ = mbR;
}

// --------------------------------------------------------------------------
// Chat button: gold metallic circle at top-right
// --------------------------------------------------------------------------
void FateStatusBar::renderChatButton(SpriteBatch& batch, SDFText& sdf,
                                      float d, float s) {
    const auto& rect = computedRect_;
    float barH = kTopBarHeight * s;
    float cbR = kChatBtnR * s;
    Vec2 cbCenter = {rect.x + rect.w - 8.0f * s - cbR, rect.y + barH * 0.5f};

    drawMetallicCircle(batch, cbCenter, cbR, d + 0.3f, resolvedStyle_.opacity);

    // "Chat" label
    float chatFont = scaledFont(9.0f);
    std::string chatLabel("Chat");
    Vec2 chatSize = sdf.measure(chatLabel, chatFont);
    Color darkText{0.15f, 0.10f, 0.0f, 1.0f};
    sdf.drawScreen(batch, chatLabel,
        Vec2{cbCenter.x - chatSize.x * 0.5f, cbCenter.y - chatSize.y * 0.5f},
        chatFont, darkText, d + 0.35f);

    // Cache hit-test region
    chatBtnCenter_ = cbCenter;
    chatBtnRadius_ = cbR;
}

// --------------------------------------------------------------------------
// Menu overlay: parchment popup with 7 menu items
// --------------------------------------------------------------------------
void FateStatusBar::renderMenuOverlay(SpriteBatch& batch, SDFText& sdf,
                                       float d, float s) {
    const auto& rect = computedRect_;
    float mw = kMenuOverlayW * s;
    float mih = kMenuItemH * s;
    float mh = mih * static_cast<float>(menuItems.size());

    // Position below the menu button (left side)
    float mx = rect.x + 4.0f * s;
    float my = menuBtnCenter_.y + menuBtnRadius_ + 4.0f * s;

    // Parchment background
    Color parchment{0.92f, 0.87f, 0.72f, 0.95f};
    Color border{0.55f, 0.45f, 0.25f, 1.0f};
    batch.drawRect({mx + mw * 0.5f, my + mh * 0.5f},
                   {mw + 3.0f, mh + 3.0f}, border, d + 0.4f);
    batch.drawRect({mx + mw * 0.5f, my + mh * 0.5f},
                   {mw, mh}, parchment, d + 0.45f);

    // Menu items
    float fontSize = scaledFont(12.0f);
    Color itemText{0.20f, 0.15f, 0.05f, 1.0f};
    Color divider{0.70f, 0.60f, 0.40f, 0.6f};

    for (size_t i = 0; i < menuItems.size(); ++i) {
        float iy = my + mih * static_cast<float>(i);

        // Divider line between items (skip first)
        if (i > 0) {
            batch.drawRect({mx + mw * 0.5f, iy + 0.5f},
                           {mw - 8.0f * s, 1.0f}, divider, d + 0.5f);
        }

        // Centered text
        Vec2 ts = sdf.measure(menuItems[i], fontSize);
        float tx = mx + (mw - ts.x) * 0.5f;
        float ty = iy + (mih - ts.y) * 0.5f;
        sdf.drawScreen(batch, menuItems[i],
            Vec2{tx, ty}, fontSize, itemText, d + 0.55f);
    }

    // Cache hit-test region for onPress
    menuOverlayRect_ = {mx, my, mw, mh};
    menuItemHeight_ = mih;
}

// --------------------------------------------------------------------------
// onPress: hit-test menu button, chat button, menu overlay items
// --------------------------------------------------------------------------
bool FateStatusBar::onPress(const Vec2& localPos) {
    // Menu overlay items (highest priority when open)
    if (menuOpen && menuOverlayRect_.w > 0.0f) {
        float rx = localPos.x - menuOverlayRect_.x;
        float ry = localPos.y - menuOverlayRect_.y;
        if (rx >= 0.0f && rx <= menuOverlayRect_.w &&
            ry >= 0.0f && ry <= menuOverlayRect_.h) {
            int idx = static_cast<int>(ry / menuItemHeight_);
            if (idx >= 0 && idx < static_cast<int>(menuItems.size())) {
                menuOpen = false;
                if (onMenuItemSelected)
                    onMenuItemSelected(menuItems[idx]);
                return true;
            }
        }
        // Clicked outside overlay — close it
        menuOpen = false;
        return true;
    }

    // Menu button (circle hit-test)
    if (menuBtnRadius_ > 0.0f) {
        float dx = localPos.x - menuBtnCenter_.x;
        float dy = localPos.y - menuBtnCenter_.y;
        if (dx * dx + dy * dy <= menuBtnRadius_ * menuBtnRadius_) {
            menuOpen = !menuOpen;
            return true;
        }
    }

    // Chat button (circle hit-test)
    if (chatBtnRadius_ > 0.0f) {
        float dx = localPos.x - chatBtnCenter_.x;
        float dy = localPos.y - chatBtnCenter_.y;
        if (dx * dx + dy * dy <= chatBtnRadius_ * chatBtnRadius_) {
            if (onChatButtonPressed)
                onChatButtonPressed(id_);
            return true;
        }
    }

    return false;
}

} // namespace fate
