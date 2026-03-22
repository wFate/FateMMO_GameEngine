#include "engine/ui/widgets/character_select_screen.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace fate {

CharacterSelectScreen::CharacterSelectScreen(const std::string& id)
    : UINode(id, "character_select_screen") {}

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

Vec2 CharacterSelectScreen::slotCenter(int index) const {
    const auto& rect = computedRect_;
    // Slots sit near bottom center; spacing = slotCircleSize + 12px gap
    float spacing  = slotCircleSize + 12.0f;
    int   count    = static_cast<int>(slots.size());
    // Total width of all slots; entry button adds one more unit to the right
    float totalSlots = static_cast<float>(count) * spacing - 12.0f;
    float totalRow   = totalSlots + 12.0f + entryButtonWidth;
    float startX     = rect.x + (rect.w - totalRow) * 0.5f;
    float cy         = rect.y + rect.h - slotCircleSize * 0.5f - 32.0f;
    float cx         = startX + slotCircleSize * 0.5f + static_cast<float>(index) * spacing;
    return {cx, cy};
}

Rect CharacterSelectScreen::entryButtonRect() const {
    const auto& rect = computedRect_;
    int   count      = static_cast<int>(slots.size());
    float spacing    = slotCircleSize + 12.0f;
    float totalSlots = static_cast<float>(count) * spacing - 12.0f;
    float totalRow   = totalSlots + 12.0f + entryButtonWidth;
    float startX     = rect.x + (rect.w - totalRow) * 0.5f;
    float bx         = startX + totalSlots + 12.0f;
    float by         = rect.y + rect.h - slotCircleSize - 32.0f;
    return {bx, by, entryButtonWidth, slotCircleSize};
}

Rect CharacterSelectScreen::swapButtonRect() const {
    const auto& rect = computedRect_;
    float size = slotCircleSize * 0.75f;
    return {rect.x + 20.0f, rect.y + rect.h - size - 20.0f, size, size};
}

Rect CharacterSelectScreen::deleteButtonRect() const {
    const auto& rect = computedRect_;
    float size = slotCircleSize * 0.75f;
    return {rect.x + rect.w - size - 20.0f, rect.y + rect.h - size - 20.0f, size, size};
}

// ---------------------------------------------------------------------------
// Class color helper (by class name)
// ---------------------------------------------------------------------------

static Color classRingColor(const std::string& className) {
    if (className == "Warrior")   return {0.85f, 0.30f, 0.20f, 1.0f};
    if (className == "Magician")  return {0.35f, 0.45f, 0.95f, 1.0f};
    if (className == "Archer")    return {0.25f, 0.75f, 0.35f, 1.0f};
    return {0.55f, 0.55f, 0.75f, 1.0f};
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void CharacterSelectScreen::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // --- Background ---
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h},
                   {0.05f, 0.05f, 0.08f, 1.0f}, d);

    // --- Center character display area ---
    float dispW = rect.w * 0.45f;
    float dispH = rect.h * 0.55f;
    float dispX = rect.x + (rect.w - dispW) * 0.5f;
    float dispY = rect.y + rect.h * 0.08f;
    batch.drawRect({dispX + dispW * 0.5f, dispY + dispH * 0.5f},
                   {dispW, dispH},
                   {0.12f, 0.10f, 0.08f, 0.85f}, d + 0.1f);
    // Parchment border
    {
        float bw = 2.0f;
        Color bc = {0.55f, 0.48f, 0.35f, 0.9f};
        float ih = dispH - bw * 2.0f;
        batch.drawRect({dispX + dispW * 0.5f, dispY + bw * 0.5f}, {dispW, bw}, bc, d + 0.2f);
        batch.drawRect({dispX + dispW * 0.5f, dispY + dispH - bw * 0.5f}, {dispW, bw}, bc, d + 0.2f);
        batch.drawRect({dispX + bw * 0.5f, dispY + dispH * 0.5f}, {bw, ih}, bc, d + 0.2f);
        batch.drawRect({dispX + dispW - bw * 0.5f, dispY + dispH * 0.5f}, {bw, ih}, bc, d + 0.2f);
    }

    // Show selected slot's character info in the display area
    if (selectedSlot >= 0 && selectedSlot < static_cast<int>(slots.size())) {
        const CharacterSlot& sel = slots[static_cast<size_t>(selectedSlot)];
        if (!sel.empty) {
            // Name label background (rounded-ish rectangle at top of display)
            float nameBgH  = 28.0f;
            float nameBgW  = dispW * 0.7f;
            float nameBgX  = dispX + (dispW - nameBgW) * 0.5f;
            float nameBgY  = dispY + 8.0f;
            batch.drawRect({nameBgX + nameBgW * 0.5f, nameBgY + nameBgH * 0.5f},
                           {nameBgW, nameBgH},
                           {0.08f, 0.08f, 0.12f, 0.9f}, d + 0.3f);

            // Name text
            float nameFontSize = 16.0f;
            Vec2  nameSize     = sdf.measure(sel.name, nameFontSize);
            sdf.drawScreen(batch, sel.name,
                           {nameBgX + (nameBgW - nameSize.x) * 0.5f,
                            nameBgY + (nameBgH - nameSize.y) * 0.5f},
                           nameFontSize, {1.0f, 0.92f, 0.75f, 1.0f}, d + 0.4f);

            // Class name
            float classFontSize = 13.0f;
            Vec2  classSize     = sdf.measure(sel.className, classFontSize);
            sdf.drawScreen(batch, sel.className,
                           {dispX + (dispW - classSize.x) * 0.5f,
                            nameBgY + nameBgH + 6.0f},
                           classFontSize, {0.75f, 0.75f, 0.9f, 1.0f}, d + 0.4f);

            // Level text
            char lvBuf[32];
            snprintf(lvBuf, sizeof(lvBuf), "Lv %d", sel.level);
            float lvFontSize = 12.0f;
            Vec2  lvSize     = sdf.measure(lvBuf, lvFontSize);
            sdf.drawScreen(batch, lvBuf,
                           {dispX + (dispW - lvSize.x) * 0.5f,
                            nameBgY + nameBgH + 6.0f + classSize.y + 4.0f},
                           lvFontSize, {0.6f, 0.9f, 0.6f, 1.0f}, d + 0.4f);
        } else {
            // Empty slot — prompt text
            const char* prompt = "No character selected";
            float pFontSize = 14.0f;
            Vec2  pSize     = sdf.measure(prompt, pFontSize);
            sdf.drawScreen(batch, prompt,
                           {dispX + (dispW - pSize.x) * 0.5f,
                            dispY + (dispH - pSize.y) * 0.5f},
                           pFontSize, {0.45f, 0.45f, 0.55f, 1.0f}, d + 0.4f);
        }
    }

    // --- Bottom slot bar ---
    int count = static_cast<int>(slots.size());
    for (int i = 0; i < count; ++i) {
        Vec2   ctr = slotCenter(i);
        float  r   = slotCircleSize * 0.5f;
        bool   sel = (i == selectedSlot);

        if (slots[static_cast<size_t>(i)].empty) {
            // Empty slot — grey circle + "+" text
            batch.drawCircle(ctr, r, {0.22f, 0.22f, 0.28f, 1.0f}, d + 0.3f, 24);
            batch.drawRing(ctr, r, 2.0f,
                           sel ? Color{0.95f, 0.8f, 0.2f, 1.0f}
                               : Color{0.38f, 0.38f, 0.48f, 1.0f},
                           d + 0.4f, 24);
            float pFontSize = 20.0f;
            Vec2  pSize     = sdf.measure("+", pFontSize);
            sdf.drawScreen(batch, "+",
                           {ctr.x - pSize.x * 0.5f, ctr.y - pSize.y * 0.5f},
                           pFontSize, {0.5f, 0.5f, 0.6f, 1.0f}, d + 0.5f);
        } else {
            const CharacterSlot& cs = slots[static_cast<size_t>(i)];
            // Filled slot — dark circle with class ring
            batch.drawCircle(ctr, r, {0.12f, 0.12f, 0.18f, 1.0f}, d + 0.3f, 24);
            Color ring = sel ? Color{0.95f, 0.8f, 0.2f, 1.0f}
                             : classRingColor(cs.className);
            batch.drawRing(ctr, r, sel ? 3.0f : 2.0f, ring, d + 0.4f, 24);

            // Level text below slot
            char lvBuf[16];
            snprintf(lvBuf, sizeof(lvBuf), "%d", cs.level);
            float lvFontSize = 10.0f;
            Vec2  lvSize     = sdf.measure(lvBuf, lvFontSize);
            sdf.drawScreen(batch, lvBuf,
                           {ctr.x - lvSize.x * 0.5f, ctr.y + r + 2.0f},
                           lvFontSize, {0.75f, 0.75f, 0.85f, 1.0f}, d + 0.5f);
        }
    }

    // --- Entry button ---
    Rect ebr = entryButtonRect();
    batch.drawRect({ebr.x + ebr.w * 0.5f, ebr.y + ebr.h * 0.5f},
                   {ebr.w, ebr.h},
                   {0.2f, 0.6f, 0.9f, 1.0f}, d + 0.3f);
    {
        float bw = 1.5f;
        Color bc = {0.5f, 0.8f, 1.0f, 1.0f};
        float ih = ebr.h - bw * 2.0f;
        batch.drawRect({ebr.x + ebr.w * 0.5f, ebr.y + bw * 0.5f}, {ebr.w, bw}, bc, d + 0.4f);
        batch.drawRect({ebr.x + ebr.w * 0.5f, ebr.y + ebr.h - bw * 0.5f}, {ebr.w, bw}, bc, d + 0.4f);
        batch.drawRect({ebr.x + bw * 0.5f, ebr.y + ebr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
        batch.drawRect({ebr.x + ebr.w - bw * 0.5f, ebr.y + ebr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
    }
    {
        const char* entryLabel = "Entry";
        float eFontSize = 15.0f;
        Vec2  eSize     = sdf.measure(entryLabel, eFontSize);
        sdf.drawScreen(batch, entryLabel,
                       {ebr.x + (ebr.w - eSize.x) * 0.5f,
                        ebr.y + (ebr.h - eSize.y) * 0.5f},
                       eFontSize, Color::white(), d + 0.5f);
    }

    // --- Swap button (bottom-left circle) ---
    Rect sbr = swapButtonRect();
    float swapR = sbr.w * 0.5f;
    Vec2  swapC = {sbr.x + swapR, sbr.y + swapR};
    batch.drawCircle(swapC, swapR, {0.2f, 0.45f, 0.65f, 1.0f}, d + 0.3f, 24);
    batch.drawRing(swapC, swapR, 1.5f, {0.4f, 0.65f, 0.85f, 1.0f}, d + 0.4f, 24);
    {
        const char* swapLabel = "Swap";
        float sFontSize = 10.0f;
        Vec2  sSize     = sdf.measure(swapLabel, sFontSize);
        sdf.drawScreen(batch, swapLabel,
                       {swapC.x - sSize.x * 0.5f, swapC.y - sSize.y * 0.5f},
                       sFontSize, Color::white(), d + 0.5f);
    }

    // --- Delete button (bottom-right circle, red tint) ---
    Rect dbr = deleteButtonRect();
    float delR = dbr.w * 0.5f;
    Vec2  delC = {dbr.x + delR, dbr.y + delR};
    batch.drawCircle(delC, delR, {0.5f, 0.15f, 0.15f, 1.0f}, d + 0.3f, 24);
    batch.drawRing(delC, delR, 1.5f, {0.85f, 0.3f, 0.3f, 1.0f}, d + 0.4f, 24);
    {
        const char* delLabel = "Del";
        float dFontSize = 10.0f;
        Vec2  dSize     = sdf.measure(delLabel, dFontSize);
        sdf.drawScreen(batch, delLabel,
                       {delC.x - dSize.x * 0.5f, delC.y - dSize.y * 0.5f},
                       dFontSize, Color::white(), d + 0.5f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool CharacterSelectScreen::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // localPos is relative to computedRect_ top-left
    Vec2 absPos = {computedRect_.x + localPos.x, computedRect_.y + localPos.y};

    // Check each slot circle
    int count = static_cast<int>(slots.size());
    for (int i = 0; i < count; ++i) {
        Vec2  ctr = slotCenter(i);
        float r   = slotCircleSize * 0.5f;
        float dx  = absPos.x - ctr.x;
        float dy  = absPos.y - ctr.y;
        if (std::sqrt(dx * dx + dy * dy) <= r) {
            selectedSlot = i;
            if (slots[static_cast<size_t>(i)].empty) {
                if (onCreateNew) onCreateNew(id_);
            }
            return true;
        }
    }

    // Entry button
    Rect ebr = entryButtonRect();
    if (ebr.contains(absPos)) {
        if (onEntry) onEntry(id_);
        return true;
    }

    // Swap button
    {
        Rect  sbr  = swapButtonRect();
        float sr   = sbr.w * 0.5f;
        Vec2  swpC = {sbr.x + sr, sbr.y + sr};
        float dx   = absPos.x - swpC.x;
        float dy   = absPos.y - swpC.y;
        if (std::sqrt(dx * dx + dy * dy) <= sr) {
            if (onSwap) onSwap(id_);
            return true;
        }
    }

    // Delete button
    {
        Rect  dbr  = deleteButtonRect();
        float dr   = dbr.w * 0.5f;
        Vec2  delC = {dbr.x + dr, dbr.y + dr};
        float dx   = absPos.x - delC.x;
        float dy   = absPos.y - delC.y;
        if (std::sqrt(dx * dx + dy * dy) <= dr) {
            if (onDelete) onDelete(id_);
            return true;
        }
    }

    return true; // consume all presses on this full-screen widget
}

} // namespace fate
