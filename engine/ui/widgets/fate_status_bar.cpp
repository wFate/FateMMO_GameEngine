// engine/ui/widgets/fate_status_bar.cpp
#include "engine/ui/widgets/fate_status_bar.h"
#include "engine/ui/widgets/metallic_draw.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace fate {

// ---- Constants for menu overlay (not yet exposed as fields) ----------------
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
// hitTest: expand hit area to include the menu button below the top bar
// --------------------------------------------------------------------------
bool FateStatusBar::hitTest(const Vec2& point) const {
    if (!visible_) return false;
    // Base rect (top bar)
    if (computedRect_.contains(point)) return true;
    // Expanded area below for menu button (circle below portrait)
    if (menuBtnRadius_ > 0.0f) {
        // menuBtnCenter_ is in LOCAL coords (set during render)
        float gx = computedRect_.x + menuBtnCenter_.x;
        float gy = computedRect_.y + menuBtnCenter_.y;
        float dx = point.x - gx;
        float dy = point.y - gy;
        if (dx * dx + dy * dy <= menuBtnRadius_ * menuBtnRadius_) return true;
    }
    // Menu overlay below the button
    if (menuOpen && menuOverlayRect_.w > 0.0f) {
        float ox = computedRect_.x + menuOverlayRect_.x;
        float oy = computedRect_.y + menuOverlayRect_.y;
        if (point.x >= ox && point.x <= ox + menuOverlayRect_.w &&
            point.y >= oy && point.y <= oy + menuOverlayRect_.h) return true;
    }
    return false;
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
    float barH = topBarHeight * s;
    Color stripBg{0.0f, 0.0f, 0.0f, 0.45f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + barH * 0.5f},
                   {rect.w, barH}, stripBg, d - 0.2f);

    Color white{1.0f, 1.0f, 1.0f, 1.0f};
    Color shadow{0.0f, 0.0f, 0.0f, 0.85f};
    Color yellow{1.0f, 0.9f, 0.2f, 1.0f};

    // Portrait circle
    float pr = portraitRadius * s;
    Vec2 pc = {rect.x + 4.0f * s + pr, rect.y + 4.0f * s + pr};
    batch.drawCircle(pc, pr, {0.15f, 0.15f, 0.2f, 0.9f}, d, 24);
    batch.drawRing(pc, pr, 2.0f * s, {0.55f, 0.55f, 0.75f, 1.0f}, d + 0.05f, 24);

    float rowCenterY = rect.y + barH * 0.5f;
    float bh = barHeight * s;
    float gap = 4.0f * s;

    char lvBuf[16];
    snprintf(lvBuf, sizeof(lvBuf), "LV %d", level);
    float lvFont = scaledFont(levelFontSize);
    Vec2 lvSz = sdf.measure(std::string(lvBuf), lvFont);

    float lFont = scaledFont(labelFontSize);
    Vec2 hpLabelSz = sdf.measure("HP", lFont);
    Vec2 mpLabelSz = sdf.measure("MP", lFont);

    char hpBuf[32], mpBuf[32];
    snprintf(hpBuf, sizeof(hpBuf), "%.0f/%.0f", hp, maxHp);
    snprintf(mpBuf, sizeof(mpBuf), "%.0f/%.0f", mp, maxMp);
    float nFont = scaledFont(numberFontSize);
    Vec2 hpNumSz = sdf.measure(std::string(hpBuf), nFont);
    Vec2 mpNumSz = sdf.measure(std::string(mpBuf), nFont);

    float leftEdge = pc.x + pr + 8.0f * s;
    float rightEdge = rect.x + rect.w - 60.0f * s;
    float fixedWidth = lvSz.x + gap*2 + hpLabelSz.x + gap + hpNumSz.x + gap*2
                     + mpLabelSz.x + gap + mpNumSz.x;
    float availForBars = rightEdge - leftEdge - fixedWidth;
    float bw = std::max(availForBars * 0.5f, 80.0f * s);

    float curX = leftEdge;

    // LV text
    float lvY = rowCenterY - lvSz.y * 0.5f;
    sdf.drawScreen(batch, std::string(lvBuf), Vec2{curX + 1.5f, lvY + 1.5f}, lvFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, std::string(lvBuf), Vec2{curX, lvY}, lvFont, white, d + 0.2f);
    curX += lvSz.x + gap * 2;

    // HP label
    float labelY = rowCenterY - hpLabelSz.y * 0.5f;
    sdf.drawScreen(batch, "HP", Vec2{curX + 1.0f, labelY + 1.0f}, lFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, "HP", Vec2{curX, labelY}, lFont, white, d + 0.2f);
    curX += hpLabelSz.x + gap;

    // HP bar
    batch.drawRect({curX + bw * 0.5f, rowCenterY}, {bw, bh}, {0.1f, 0.1f, 0.1f, 0.85f}, d);
    float hpRatio = (maxHp > 0.0f) ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 0.0f;
    if (hpRatio > 0.0f) {
        float fw = bw * hpRatio;
        batch.drawRect({curX + fw * 0.5f, rowCenterY}, {fw, bh}, hpBarColor, d + 0.01f);
    }
    curX += bw + gap;

    // HP numbers
    float numY = rowCenterY - hpNumSz.y * 0.5f;
    sdf.drawScreen(batch, std::string(hpBuf), Vec2{curX + 1.5f, numY + 1.5f}, nFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, std::string(hpBuf), Vec2{curX, numY}, nFont, yellow, d + 0.2f);
    curX += hpNumSz.x + gap * 2;

    // MP label
    sdf.drawScreen(batch, "MP", Vec2{curX + 1.0f, labelY + 1.0f}, lFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, "MP", Vec2{curX, labelY}, lFont, white, d + 0.2f);
    curX += mpLabelSz.x + gap;

    // MP bar
    batch.drawRect({curX + bw * 0.5f, rowCenterY}, {bw, bh}, {0.1f, 0.1f, 0.1f, 0.85f}, d);
    float mpRatio = (maxMp > 0.0f) ? std::clamp(mp / maxMp, 0.0f, 1.0f) : 0.0f;
    if (mpRatio > 0.0f) {
        float fw = bw * mpRatio;
        batch.drawRect({curX + fw * 0.5f, rowCenterY}, {fw, bh}, mpBarColor, d + 0.01f);
    }
    curX += bw + gap;

    // MP numbers
    sdf.drawScreen(batch, std::string(mpBuf), Vec2{curX + 1.5f, numY + 1.5f}, nFont, shadow, d + 0.15f);
    sdf.drawScreen(batch, std::string(mpBuf), Vec2{curX, numY}, nFont, yellow, d + 0.2f);
    curX += mpNumSz.x;

    // Coordinates below the bar strip
    if (showCoordinates) {
        char coordBuf[32];
        snprintf(coordBuf, sizeof(coordBuf), "%d,%d", playerTileX, playerTileY);
        float cFont = scaledFont(coordFontSize);
        Vec2 coordSz = sdf.measure(std::string(coordBuf), cFont);
        float rowCenterX = (leftEdge + curX) * 0.5f;
        sdf.drawScreen(batch, std::string(coordBuf),
            Vec2{rowCenterX - coordSz.x * 0.5f, rect.y + barH + coordOffsetY * s},
            cFont, coordColor, d + 0.2f);
    }
}

// --------------------------------------------------------------------------
// Menu button: EXP progress arc + gold metallic "Menu" button below portrait
// --------------------------------------------------------------------------
void FateStatusBar::renderMenuButton(SpriteBatch& batch, SDFText& sdf,
                                      float d, float s) {
    if (!showMenuButton) { menuBtnRadius_ = 0.0f; return; }

    const auto& rect = computedRect_;
    float barH = topBarHeight * s;
    float pr = portraitRadius * s;
    Vec2 pc = {rect.x + 6.0f * s + pr, rect.y + barH * 0.5f};

    // EXP progress arc around portrait
    float expR = (portraitRadius + 4.0f) * s;
    float xpRatio = (xpToLevel > 0.0f) ? std::clamp(xp / xpToLevel, 0.0f, 1.0f) : 0.0f;
    if (xpRatio > 0.0f) {
        Color expColor{0.8f, 0.65f, 0.1f, 0.9f};
        float startAngle = -kPi * 0.5f;
        float endAngle   = startAngle + xpRatio * 2.0f * kPi;
        batch.drawArc(pc, expR, startAngle, endAngle, expColor, d + 0.25f, 32);
    }
    Color expBgRing{0.3f, 0.3f, 0.3f, 0.5f};
    batch.drawRing(pc, expR, 2.0f * s, expBgRing, d + 0.05f, 32);

    // Gold metallic "Menu" button below EXP circle
    float mbR = menuBtnSize * s;
    Vec2 mbCenter = {pc.x, pc.y + expR + mbR + menuBtnGap * s};
    drawMetallicCircle(batch, mbCenter, mbR, d + 0.3f, resolvedStyle_.opacity);

    float mFont = scaledFont(buttonFontSize);
    std::string menuLabel("Menu");
    Vec2 menuSize = sdf.measure(menuLabel, mFont);
    Color darkText{0.15f, 0.10f, 0.0f, 1.0f};
    sdf.drawScreen(batch, menuLabel,
        Vec2{mbCenter.x - menuSize.x * 0.5f, mbCenter.y - menuSize.y * 0.5f},
        mFont, darkText, d + 0.35f);

    // Store in LOCAL coords (relative to computedRect_) for onPress
    menuBtnCenter_ = {mbCenter.x - computedRect_.x, mbCenter.y - computedRect_.y};
    menuBtnRadius_ = mbR;
}

// --------------------------------------------------------------------------
// Chat button: gold metallic circle at top-right
// --------------------------------------------------------------------------
void FateStatusBar::renderChatButton(SpriteBatch& batch, SDFText& sdf,
                                      float d, float s) {
    if (!showChatButton) { chatBtnRadius_ = 0.0f; return; }

    const auto& rect = computedRect_;
    float barH = topBarHeight * s;
    float cbR = chatBtnSize * s;
    Vec2 cbCenter = {rect.x + rect.w - chatBtnOffsetX * s - cbR, rect.y + barH * 0.5f};

    drawMetallicCircle(batch, cbCenter, cbR, d + 0.3f, resolvedStyle_.opacity);

    float cFont = scaledFont(buttonFontSize);
    std::string chatLabel("Chat");
    Vec2 chatSize = sdf.measure(chatLabel, cFont);
    Color darkText{0.15f, 0.10f, 0.0f, 1.0f};
    sdf.drawScreen(batch, chatLabel,
        Vec2{cbCenter.x - chatSize.x * 0.5f, cbCenter.y - chatSize.y * 0.5f},
        cFont, darkText, d + 0.35f);

    // Store in LOCAL coords (relative to computedRect_) for onPress
    chatBtnCenter_ = {cbCenter.x - computedRect_.x, cbCenter.y - computedRect_.y};
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

    // Cache hit-test region for onPress (LOCAL coords)
    menuOverlayRect_ = {mx - computedRect_.x, my - computedRect_.y, mw, mh};
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
