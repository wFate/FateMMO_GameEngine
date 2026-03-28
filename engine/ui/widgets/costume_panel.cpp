#include "engine/ui/widgets/costume_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <cstdio>
#include <cmath>
#include <SDL.h>

namespace fate {

// ---------------------------------------------------------------------------
// Filter tab definitions: label -> slotType value
// ---------------------------------------------------------------------------
static const char* kFilterLabels[] = {"All", "Hat", "Armor", "Wpn", "Shld", "Glv", "Boot"};
static const uint8_t kFilterSlots[] = {0, 1, 2, 10, 11, 3, 4};
static constexpr int kFilterCount = 7;

// Rarity border colors: Common=gray, Uncommon=green, Rare=blue, Epic=purple, Legendary=gold
static Color rarityColor(uint8_t rarity) {
    switch (rarity) {
        case 1: return {0.3f, 0.8f, 0.3f, 1.0f};   // Uncommon - green
        case 2: return {0.3f, 0.5f, 1.0f, 1.0f};   // Rare - blue
        case 3: return {0.7f, 0.3f, 0.9f, 1.0f};   // Epic - purple
        case 4: return {1.0f, 0.8f, 0.2f, 1.0f};   // Legendary - gold
        default: return {0.5f, 0.5f, 0.5f, 1.0f};  // Common - gray
    }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CostumePanel::CostumePanel(const std::string& id)
    : UINode(id, "costume_panel") {}

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

void CostumePanel::open() {
    isOpen_ = true;
    selectedIndex = -1;
    rebuildFilter();
    setVisible(true);
}

void CostumePanel::close() {
    isOpen_ = false;
    setVisible(false);
    if (onClose) onClose(id_);
}

// ---------------------------------------------------------------------------
// rebuildFilter
// ---------------------------------------------------------------------------

void CostumePanel::rebuildFilter() {
    filteredIndices_.clear();
    for (int i = 0; i < static_cast<int>(ownedCostumes.size()); ++i) {
        if (filterSlot == 0 || ownedCostumes[i].slotType == filterSlot) {
            filteredIndices_.push_back(i);
        }
    }
    // Reset selection if out of range
    if (selectedIndex >= static_cast<int>(filteredIndices_.size())) {
        selectedIndex = -1;
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void CostumePanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Clear hit-test rects
    filterTabRects_.clear();
    gridSlotRects_.clear();

    // ---- Scaled dimensions ----
    float bw      = borderWidth * layoutScale_;
    float headerH = headerHeight * layoutScale_;
    float titleFS = scaledFont(titleFontSize);
    float bodyFS  = scaledFont(bodyFontSize);
    float infoFS  = scaledFont(infoFontSize);
    float ss      = slotSize * layoutScale_;
    float ssp     = slotSpacing * layoutScale_;
    float btnH    = buttonHeight * layoutScale_;
    float ftH     = filterTabHeight * layoutScale_;
    float botRes  = bottomReserveHeight * layoutScale_;

    // ---- Background (dark panel) ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, backgroundColor, d);
    // Border edges
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f},           {rect.w, bw}, borderColor, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f},  {rect.w, bw}, borderColor, d + 0.1f);
    batch.drawRect({rect.x + bw * 0.5f,     rect.y + rect.h * 0.5f},       {bw, innerH}, borderColor, d + 0.1f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f},  {bw, innerH}, borderColor, d + 0.1f);

    // ---- Title bar ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + headerH * 0.5f},
                   {rect.w - bw * 2.0f, headerH}, titleBarColor, d + 0.05f);

    Vec2 titleSize = sdf.measure("Costumes", titleFS);
    float titleX = rect.x + (rect.w - titleSize.x) * 0.5f;
    sdf.drawScreen(batch, "Costumes",
        {titleX, rect.y + (headerH - titleSize.y) * 0.5f},
        titleFS, titleColor, d + 0.2f);

    // ---- Close button (X at top-right) ----
    float closeSize = 20.0f * layoutScale_;
    float closeCX = rect.x + rect.w - closeSize * 0.5f - 6.0f * layoutScale_;
    float closeCY = rect.y + headerH * 0.5f;
    batch.drawRect({closeCX, closeCY}, {closeSize, closeSize}, closeBtnColor, d + 0.2f);
    float closeFS = scaledFont(12.0f);
    Vec2 xts = sdf.measure("X", closeFS);
    sdf.drawScreen(batch, "X",
        {closeCX - xts.x * 0.5f, closeCY - xts.y * 0.5f},
        closeFS, titleColor, d + 0.3f);
    closeBtnRect_ = {closeCX - closeSize * 0.5f, closeCY - closeSize * 0.5f, closeSize, closeSize};

    // ---- ON/OFF toggle button (left of close) ----
    const char* toggleLabel = showCostumes ? "ON" : "OFF";
    Color toggleBg = showCostumes ? Color{0.15f, 0.35f, 0.15f, 0.9f} : Color{0.35f, 0.15f, 0.15f, 0.9f};
    float toggleW = 36.0f * layoutScale_;
    float toggleH = 18.0f * layoutScale_;
    float toggleCX = closeCX - closeSize * 0.5f - 8.0f * layoutScale_ - toggleW * 0.5f;
    float toggleCY = rect.y + headerH * 0.5f;
    batch.drawRect({toggleCX, toggleCY}, {toggleW, toggleH}, toggleBg, d + 0.2f);
    float toggleFS = scaledFont(10.0f);
    Vec2 tls = sdf.measure(toggleLabel, toggleFS);
    sdf.drawScreen(batch, toggleLabel,
        {toggleCX - tls.x * 0.5f, toggleCY - tls.y * 0.5f},
        toggleFS, titleColor, d + 0.3f);
    toggleBtnRect_ = {toggleCX - toggleW * 0.5f, toggleCY - toggleH * 0.5f, toggleW, toggleH};

    // ---- Divider below title ----
    Color divColor = {borderColor.r, borderColor.g, borderColor.b, 0.6f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + headerH},
                   {rect.w - bw * 2.0f, 1.0f * layoutScale_}, divColor, d + 0.1f);

    // ---- Filter tabs ----
    float curY = rect.y + headerH + 4.0f * layoutScale_;
    float tabPad = 4.0f * layoutScale_;
    float availW = rect.w - bw * 2.0f - tabPad * 2.0f;
    float tabW = availW / static_cast<float>(kFilterCount);

    for (int i = 0; i < kFilterCount; ++i) {
        float tx = rect.x + bw + tabPad + tabW * static_cast<float>(i);
        float tcx = tx + tabW * 0.5f;
        float tcy = curY + ftH * 0.5f;
        bool active = (filterSlot == kFilterSlots[i]);
        batch.drawRect({tcx, tcy}, {tabW - 2.0f * layoutScale_, ftH}, active ? tabActiveColor : tabColor, d + 0.15f);

        Vec2 ls = sdf.measure(kFilterLabels[i], infoFS);
        sdf.drawScreen(batch, kFilterLabels[i],
            {tcx - ls.x * 0.5f, tcy - ls.y * 0.5f},
            infoFS, active ? tabActiveTextColor : tabTextColor, d + 0.25f);

        filterTabRects_.push_back({tx, curY, tabW, ftH});
    }
    curY += ftH + 6.0f * layoutScale_;

    // ---- Grid of costume slots ----
    float gridLeft = rect.x + bw + 8.0f * layoutScale_;
    float cellSize = ss + ssp;

    // Reserve space at bottom for info + equip button
    float gridMaxY = rect.y + rect.h - bw - botRes;

    float rarityBw = 2.0f * layoutScale_;
    float eqFS = scaledFont(9.0f);

    for (int fi = 0; fi < static_cast<int>(filteredIndices_.size()); ++fi) {
        int col = fi % gridCols;
        int row = fi / gridCols;
        float sx = gridLeft + static_cast<float>(col) * cellSize;
        float sy = curY + static_cast<float>(row) * cellSize;

        // Stop rendering if below grid area
        if (sy + ss > gridMaxY) break;

        float scx = sx + ss * 0.5f;
        float scy = sy + ss * 0.5f;

        const auto& costume = ownedCostumes[filteredIndices_[fi]];
        bool isSelected = (fi == selectedIndex);

        // Slot background
        batch.drawRect({scx, scy}, {ss, ss}, isSelected ? slotSelectedColor : slotColor, d + 0.12f);

        // Rarity border
        Color rc = rarityColor(costume.rarity);
        batch.drawRect({scx, sy + rarityBw * 0.5f},            {ss, rarityBw}, rc, d + 0.18f);
        batch.drawRect({scx, sy + ss - rarityBw * 0.5f},       {ss, rarityBw}, rc, d + 0.18f);
        batch.drawRect({sx + rarityBw * 0.5f, scy},            {rarityBw, ss - rarityBw * 2.0f}, rc, d + 0.18f);
        batch.drawRect({sx + ss - rarityBw * 0.5f, scy},       {rarityBw, ss - rarityBw * 2.0f}, rc, d + 0.18f);

        // Costume name abbreviated (first 4 chars) in center
        std::string abbr = costume.displayName.substr(0, 4);
        Vec2 aSize = sdf.measure(abbr, infoFS);
        sdf.drawScreen(batch, abbr,
            {scx - aSize.x * 0.5f, scy - aSize.y * 0.5f},
            infoFS, nameColor, d + 0.22f);

        // "E" indicator if equipped
        auto it = equippedBySlot.find(costume.slotType);
        if (it != equippedBySlot.end() && it->second == costume.costumeDefId) {
            Vec2 eSize = sdf.measure("E", eqFS);
            sdf.drawScreen(batch, "E",
                {sx + ss - eSize.x - 2.0f * layoutScale_, sy + 2.0f * layoutScale_},
                eqFS, equippedIndicatorColor, d + 0.25f);
        }

        gridSlotRects_.push_back({sx, sy, ss, ss});
    }

    // ---- Bottom info area + Equip/Unequip button ----
    float infoY = rect.y + rect.h - bw - botRes + 4.0f * layoutScale_;

    // Divider above info
    batch.drawRect({rect.x + rect.w * 0.5f, infoY},
                   {rect.w - bw * 2.0f, 1.0f * layoutScale_}, divColor, d + 0.1f);
    infoY += 4.0f * layoutScale_;

    equipBtnRect_ = {};

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(filteredIndices_.size())) {
        const auto& sel = ownedCostumes[filteredIndices_[selectedIndex]];

        // Selected costume name
        sdf.drawScreen(batch, sel.displayName,
            {rect.x + 12.0f * layoutScale_, infoY},
            bodyFS, nameColor, d + 0.2f);

        // Rarity label
        const char* rarityNames[] = {"Common", "Uncommon", "Rare", "Epic", "Legendary"};
        const char* rn = (sel.rarity <= 4) ? rarityNames[sel.rarity] : "Unknown";
        Color rnColor = rarityColor(sel.rarity);
        Vec2 rnSize = sdf.measure(rn, infoFS);
        sdf.drawScreen(batch, rn,
            {rect.x + rect.w - 12.0f * layoutScale_ - rnSize.x, infoY + 2.0f * layoutScale_},
            infoFS, rnColor, d + 0.2f);

        infoY += 20.0f * layoutScale_;

        // Equip / Unequip button
        auto eqIt = equippedBySlot.find(sel.slotType);
        bool isEquipped = (eqIt != equippedBySlot.end() && eqIt->second == sel.costumeDefId);
        const char* btnLabel = isEquipped ? "Unequip" : "Equip";

        float btnW = rect.w - 24.0f * layoutScale_;
        float btnCX = rect.x + rect.w * 0.5f;
        float btnCY = infoY + btnH * 0.5f;
        Color btnBg = isEquipped ? unequipBtnColor : equipBtnColor;

        batch.drawRect({btnCX, btnCY}, {btnW, btnH}, btnBg, d + 0.15f);
        Vec2 blSize = sdf.measure(btnLabel, bodyFS);
        sdf.drawScreen(batch, btnLabel,
            {btnCX - blSize.x * 0.5f, btnCY - blSize.y * 0.5f},
            bodyFS, buttonTextColor, d + 0.25f);

        equipBtnRect_ = {rect.x + 12.0f * layoutScale_, infoY, btnW, btnH};
    } else {
        // No selection hint
        const char* hint = "Select a costume";
        Vec2 hSize = sdf.measure(hint, bodyFS);
        sdf.drawScreen(batch, hint,
            {rect.x + (rect.w - hSize.x) * 0.5f, infoY + 10.0f * layoutScale_},
            bodyFS, hintColor, d + 0.2f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool CostumePanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // Close button
    Rect localClose = {closeBtnRect_.x - computedRect_.x, closeBtnRect_.y - computedRect_.y,
                       closeBtnRect_.w, closeBtnRect_.h};
    if (localClose.contains(localPos)) {
        close();
        return true;
    }

    // Toggle button
    Rect localToggle = {toggleBtnRect_.x - computedRect_.x, toggleBtnRect_.y - computedRect_.y,
                        toggleBtnRect_.w, toggleBtnRect_.h};
    if (localToggle.contains(localPos)) {
        showCostumes = !showCostumes;
        if (onToggleCostumes) onToggleCostumes(showCostumes);
        return true;
    }

    // Filter tabs
    for (int i = 0; i < static_cast<int>(filterTabRects_.size()); ++i) {
        Rect localTab = {filterTabRects_[i].x - computedRect_.x, filterTabRects_[i].y - computedRect_.y,
                         filterTabRects_[i].w, filterTabRects_[i].h};
        if (localTab.contains(localPos)) {
            filterSlot = kFilterSlots[i];
            selectedIndex = -1;
            rebuildFilter();
            return true;
        }
    }

    // Grid slots
    for (int i = 0; i < static_cast<int>(gridSlotRects_.size()); ++i) {
        Rect localSlot = {gridSlotRects_[i].x - computedRect_.x, gridSlotRects_[i].y - computedRect_.y,
                          gridSlotRects_[i].w, gridSlotRects_[i].h};
        if (localSlot.contains(localPos)) {
            selectedIndex = i;
            return true;
        }
    }

    // Equip/Unequip button
    if (equipBtnRect_.w > 0.0f && selectedIndex >= 0 &&
        selectedIndex < static_cast<int>(filteredIndices_.size())) {
        Rect localEquip = {equipBtnRect_.x - computedRect_.x, equipBtnRect_.y - computedRect_.y,
                           equipBtnRect_.w, equipBtnRect_.h};
        if (localEquip.contains(localPos)) {
            const auto& sel = ownedCostumes[filteredIndices_[selectedIndex]];
            auto eqIt = equippedBySlot.find(sel.slotType);
            bool isEquipped = (eqIt != equippedBySlot.end() && eqIt->second == sel.costumeDefId);
            if (isEquipped) {
                if (onUnequipCostume) onUnequipCostume(sel.slotType);
            } else {
                if (onEquipCostume) onEquipCostume(sel.costumeDefId);
            }
            return true;
        }
    }

    return true; // consume all clicks on panel
}

bool CostumePanel::onKeyInput(int scancode, bool pressed) {
    if (!pressed) return false;
    if (scancode == SDL_SCANCODE_ESCAPE) {
        close();
        return true;
    }
    return false;
}

} // namespace fate
