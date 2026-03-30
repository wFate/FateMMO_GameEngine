#include "engine/ui/widgets/character_select_screen.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include "engine/render/texture.h"
#include "game/data/paper_doll_catalog.h"
#include <SDL_scancode.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace fate {

CharacterSelectScreen::CharacterSelectScreen(const std::string& id)
    : UINode(id, "character_select_screen") {}

// ---------------------------------------------------------------------------
// Layout helpers  (all pixel values scaled by layoutScale_)
// ---------------------------------------------------------------------------

Vec2 CharacterSelectScreen::slotCenter(int index) const {
    const auto& rect = computedRect_;
    float s = layoutScale_;
    float circleSize = slotCircleSize * s;
    float spacing  = circleSize + slotSpacing * s;
    int   count    = static_cast<int>(slots.size());
    float totalSlots = static_cast<float>(count) * spacing - slotSpacing * s;
    float totalRow   = totalSlots + slotSpacing * s + entryButtonWidth * s;
    float startX     = rect.x + (rect.w - totalRow) * 0.5f;
    float cy         = rect.y + rect.h - circleSize * 0.5f - slotBottomMargin * s;
    float cx         = startX + circleSize * 0.5f + static_cast<float>(index) * spacing;
    return {cx, cy};
}

Rect CharacterSelectScreen::entryButtonRect() const {
    const auto& rect = computedRect_;
    float s = layoutScale_;
    float circleSize = slotCircleSize * s;
    int   count      = static_cast<int>(slots.size());
    float spacing    = circleSize + slotSpacing * s;
    float totalSlots = static_cast<float>(count) * spacing - slotSpacing * s;
    float totalRow   = totalSlots + slotSpacing * s + entryButtonWidth * s;
    float startX     = rect.x + (rect.w - totalRow) * 0.5f;
    float bx         = startX + totalSlots + slotSpacing * s;
    float by         = rect.y + rect.h - circleSize - slotBottomMargin * s;
    return {bx, by, entryButtonWidth * s, circleSize};
}

Rect CharacterSelectScreen::swapButtonRect() const {
    const auto& rect = computedRect_;
    float s = layoutScale_;
    float size = slotCircleSize * s * swapDeleteScale;
    float margin = swapDeleteMargin * s;
    return {rect.x + margin, rect.y + rect.h - size - margin, size, size};
}

Rect CharacterSelectScreen::deleteButtonRect() const {
    const auto& rect = computedRect_;
    float s = layoutScale_;
    float size = slotCircleSize * s * swapDeleteScale;
    float margin = swapDeleteMargin * s;
    return {rect.x + rect.w - size - margin, rect.y + rect.h - size - margin, size, size};
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
    float s = layoutScale_;

    // --- Background ---
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h},
                   backgroundColor, d);

    // --- Center character display area ---
    float dispW = rect.w * displayWidthRatio;
    float dispH = rect.h * displayHeightRatio;
    float dispX = rect.x + (rect.w - dispW) * 0.5f;
    float dispY = rect.y + rect.h * displayTopRatio;
    batch.drawRect({dispX + dispW * 0.5f, dispY + dispH * 0.5f},
                   {dispW, dispH},
                   displayBgColor, d + 0.1f);
    // Parchment border
    {
        float bw = displayBorderWidth * s;
        Color bc = displayBorderColor;
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
            // --- Paper doll sprite preview ---
            resolvePreviewTextures();

            float spriteW = 48.0f * previewScale * s;
            float spriteH = 96.0f * previewScale * s;
            float spriteCX = dispX + dispW * 0.5f;
            float spriteCY = dispY + dispH * previewCenterYRatio;

            auto drawLayer = [&](const std::shared_ptr<Texture>& tex, float layerD) {
                if (!tex) return;
                int tw = tex->width(), th = tex->height();
                if (tw == 0 || th == 0) return;
                SpriteDrawParams params;
                params.position = {spriteCX, spriteCY};
                params.size = {spriteW, spriteH};
                params.sourceRect = {0, 0, 48.0f / static_cast<float>(tw),
                                     96.0f / static_cast<float>(th)};
                params.depth = layerD;
                params.flipY = true;
                batch.draw(tex, params);
            };

            drawLayer(previewBodyTex_,   d + 0.3f);
            drawLayer(previewHairTex_,   d + 0.32f);
            drawLayer(previewArmorTex_,  d + 0.35f);
            drawLayer(previewHatTex_,    d + 0.4f);
            drawLayer(previewWeaponTex_, d + 0.45f);

            // Name label background
            float nbH = nameBgHeight * s;
            float nbW = dispW * nameBgWidthRatio;
            float nbX = dispX + (dispW - nbW) * 0.5f;
            float nbY = dispY + nameTextY * s;
            batch.drawRect({nbX + nbW * 0.5f, nbY + nbH * 0.5f},
                           {nbW, nbH},
                           nameBgColor, d + 0.5f);

            // Name text (centered in name bg)
            float nfs = scaledFont(nameFontSize);
            Vec2 nameSize = sdf.measure(sel.name, nfs);
            sdf.drawScreen(batch, sel.name,
                           {nbX + (nbW - nameSize.x) * 0.5f,
                            nbY + (nbH - nameSize.y) * 0.5f},
                           nfs, nameColor, d + 0.6f);

            // Class name (absolute Y from display top)
            float cfs = scaledFont(classFontSize);
            Vec2 classSize = sdf.measure(sel.className, cfs);
            sdf.drawScreen(batch, sel.className,
                           {dispX + (dispW - classSize.x) * 0.5f,
                            dispY + classTextY * s},
                           cfs, classColor, d + 0.6f);

            // Level text (absolute Y from display top)
            float lfs = scaledFont(levelFontSize);
            char lvBuf[32];
            snprintf(lvBuf, sizeof(lvBuf), "Lv %d", sel.level);
            Vec2 lvSize = sdf.measure(lvBuf, lfs);
            sdf.drawScreen(batch, lvBuf,
                           {dispX + (dispW - lvSize.x) * 0.5f,
                            dispY + levelTextY * s},
                           lfs, levelColor, d + 0.6f);
        } else {
            // Empty slot — prompt text
            float epfs = scaledFont(emptyPromptFontSize);
            const char* prompt = "No character selected";
            Vec2 pSize = sdf.measure(prompt, epfs);
            sdf.drawScreen(batch, prompt,
                           {dispX + (dispW - pSize.x) * 0.5f,
                            dispY + (dispH - pSize.y) * 0.5f},
                           epfs, emptyPromptColor, d + 0.4f);
        }
    }

    // --- Bottom slot bar ---
    float circleSize = slotCircleSize * s;
    int count = static_cast<int>(slots.size());
    for (int i = 0; i < count; ++i) {
        Vec2   ctr = slotCenter(i);
        float  r   = circleSize * 0.5f;
        bool   sel = (i == selectedSlot);

        if (slots[static_cast<size_t>(i)].empty) {
            // Empty slot — grey circle + "+" text
            batch.drawCircle(ctr, r, emptySlotColor, d + 0.3f, 24);
            batch.drawRing(ctr, r, normalRingWidth * s,
                           sel ? selectedRingColor : emptyRingColor,
                           d + 0.4f, 24);
            float pfs = scaledFont(plusFontSize);
            Vec2 pSize = sdf.measure("+", pfs);
            sdf.drawScreen(batch, "+",
                           {ctr.x - pSize.x * 0.5f, ctr.y - pSize.y * 0.5f},
                           pfs, plusColor, d + 0.5f);
        } else {
            const CharacterSlot& cs = slots[static_cast<size_t>(i)];
            // Filled slot — dark circle with class ring
            batch.drawCircle(ctr, r, filledSlotColor, d + 0.3f, 24);
            Color ring = sel ? selectedRingColor : classRingColor(cs.className);
            batch.drawRing(ctr, r, (sel ? selectedRingWidth : normalRingWidth) * s, ring, d + 0.4f, 24);

            // Level text below slot
            float slfs = scaledFont(slotLevelFontSize);
            char lvBuf[16];
            snprintf(lvBuf, sizeof(lvBuf), "%d", cs.level);
            Vec2 lvSize = sdf.measure(lvBuf, slfs);
            sdf.drawScreen(batch, lvBuf,
                           {ctr.x - lvSize.x * 0.5f, ctr.y + r + 2.0f * s},
                           slfs, slotLevelColor, d + 0.5f);
        }
    }

    // --- Entry button ---
    Rect ebr = entryButtonRect();
    batch.drawRect({ebr.x + ebr.w * 0.5f, ebr.y + ebr.h * 0.5f},
                   {ebr.w, ebr.h},
                   entryBtnColor, d + 0.3f);
    {
        float bw = entryBtnBorderWidth * s;
        Color bc = entryBtnBorderColor;
        float ih = ebr.h - bw * 2.0f;
        batch.drawRect({ebr.x + ebr.w * 0.5f, ebr.y + bw * 0.5f}, {ebr.w, bw}, bc, d + 0.4f);
        batch.drawRect({ebr.x + ebr.w * 0.5f, ebr.y + ebr.h - bw * 0.5f}, {ebr.w, bw}, bc, d + 0.4f);
        batch.drawRect({ebr.x + bw * 0.5f, ebr.y + ebr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
        batch.drawRect({ebr.x + ebr.w - bw * 0.5f, ebr.y + ebr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
    }
    {
        float efs = scaledFont(entryFontSize);
        const char* entryLabel = "Entry";
        Vec2 eSize = sdf.measure(entryLabel, efs);
        sdf.drawScreen(batch, entryLabel,
                       {ebr.x + (ebr.w - eSize.x) * 0.5f,
                        ebr.y + (ebr.h - eSize.y) * 0.5f},
                       efs, Color::white(), d + 0.5f);
    }

    // --- Swap button (bottom-left circle) ---
    Rect sbr = swapButtonRect();
    float swapR = sbr.w * 0.5f;
    Vec2  swapC = {sbr.x + swapR, sbr.y + swapR};
    batch.drawCircle(swapC, swapR, swapBtnColor, d + 0.3f, 24);
    batch.drawRing(swapC, swapR, swapBtnRingWidth * s, swapBtnRingColor, d + 0.4f, 24);
    {
        float sfs = scaledFont(swapFontSize);
        const char* swapLabel = "Swap";
        Vec2 sSize = sdf.measure(swapLabel, sfs);
        sdf.drawScreen(batch, swapLabel,
                       {swapC.x - sSize.x * 0.5f, swapC.y - sSize.y * 0.5f},
                       sfs, Color::white(), d + 0.5f);
    }

    // --- Delete button (bottom-right circle, red tint) ---
    Rect dbr = deleteButtonRect();
    float delR = dbr.w * 0.5f;
    Vec2  delC = {dbr.x + delR, dbr.y + delR};
    batch.drawCircle(delC, delR, deleteBtnColor, d + 0.3f, 24);
    batch.drawRing(delC, delR, deleteBtnRingWidth * s, deleteBtnRingColor, d + 0.4f, 24);
    {
        float dfs = scaledFont(deleteFontSize);
        const char* delLabel = "Del";
        Vec2 dSize = sdf.measure(delLabel, dfs);
        sdf.drawScreen(batch, delLabel,
                       {delC.x - dSize.x * 0.5f, delC.y - dSize.y * 0.5f},
                       dfs, Color::white(), d + 0.5f);
    }

    // --- Delete confirmation dialog overlay ---
    if (showDeleteConfirm) {
        float overlayD = d + 5.0f;

        // Semi-transparent dark overlay covering entire screen
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                       {rect.w, rect.h},
                       dialogOverlayColor, overlayD);

        // Centered dialog box
        float dW = dialogWidth * s, dH = dialogHeight * s;
        float dx = rect.x + (rect.w - dW) / 2;
        float dy = rect.y + (rect.h - dH) / 2;

        // Dialog background
        batch.drawRect({dx + dW * 0.5f, dy + dH * 0.5f},
                       {dW, dH},
                       dialogBgColor, overlayD + 0.1f);

        // Dialog border
        {
            float bw = dialogBorderWidth * s;
            Color bc = dialogBorderColor;
            float ih = dH - bw * 2.0f;
            batch.drawRect({dx + dW * 0.5f, dy + bw * 0.5f}, {dW, bw}, bc, overlayD + 0.2f);
            batch.drawRect({dx + dW * 0.5f, dy + dH - bw * 0.5f}, {dW, bw}, bc, overlayD + 0.2f);
            batch.drawRect({dx + bw * 0.5f, dy + dH * 0.5f}, {bw, ih}, bc, overlayD + 0.2f);
            batch.drawRect({dx + dW - bw * 0.5f, dy + dH * 0.5f}, {bw, ih}, bc, overlayD + 0.2f);
        }

        float dlgBtnMargin = dialogBtnMargin * s;

        // Title: "Delete Character"
        {
            float tfs = scaledFont(dialogTitleFontSize);
            const char* title = "Delete Character";
            Vec2 titleSize = sdf.measure(title, tfs);
            sdf.drawScreen(batch, title,
                           {dx + (dW - titleSize.x) * 0.5f, dy + dlgBtnMargin},
                           tfs, dialogTitleColor, overlayD + 0.3f);
        }

        // Prompt: "Type character name to confirm:"
        {
            float pfs = scaledFont(dialogPromptFontSize);
            const char* prompt = "Type character name to confirm:";
            Vec2 promptSize = sdf.measure(prompt, pfs);
            sdf.drawScreen(batch, prompt,
                           {dx + (dW - promptSize.x) * 0.5f, dy + 50.0f * s},
                           pfs, dialogPromptColor, overlayD + 0.3f);
        }

        // Target name reference
        {
            float rfs = scaledFont(dialogRefNameFontSize);
            Vec2 refSize = sdf.measure(deleteTargetName, rfs);
            sdf.drawScreen(batch, deleteTargetName,
                           {dx + (dW - refSize.x) * 0.5f, dy + 75.0f * s},
                           rfs, dialogRefNameColor, overlayD + 0.3f);
        }

        // Text input field
        {
            float inputW = dW - dialogInputPadding * s;
            float inputH = dialogInputHeight * s;
            float inputX = dx + (dW - inputW) * 0.5f;
            float inputY = dy + 110.0f * s;

            // Input background
            batch.drawRect({inputX + inputW * 0.5f, inputY + inputH * 0.5f},
                           {inputW, inputH},
                           dialogInputBgColor, overlayD + 0.3f);

            // Input border
            {
                float bw = dialogInputBorderWidth * s;
                Color bc = dialogInputBorderColor;
                float ih = inputH - bw * 2.0f;
                batch.drawRect({inputX + inputW * 0.5f, inputY + bw * 0.5f}, {inputW, bw}, bc, overlayD + 0.4f);
                batch.drawRect({inputX + inputW * 0.5f, inputY + inputH - bw * 0.5f}, {inputW, bw}, bc, overlayD + 0.4f);
                batch.drawRect({inputX + bw * 0.5f, inputY + inputH * 0.5f}, {bw, ih}, bc, overlayD + 0.4f);
                batch.drawRect({inputX + inputW - bw * 0.5f, inputY + inputH * 0.5f}, {bw, ih}, bc, overlayD + 0.4f);
            }

            // Input text
            float ifs = scaledFont(dialogInputFontSize);
            float textPad = 6.0f * s;
            if (!deleteConfirmInput.empty()) {
                sdf.drawScreen(batch, deleteConfirmInput,
                               {inputX + textPad, inputY + (inputH - ifs) * 0.5f},
                               ifs, Color::white(), overlayD + 0.5f);
            }

            // Cursor
            std::string beforeCursor = deleteConfirmInput.substr(0, static_cast<size_t>(deleteConfirmCursor));
            Vec2 cursorOffset = sdf.measure(beforeCursor, ifs);
            float cx = inputX + textPad + cursorOffset.x;
            batch.drawRect({cx + 0.5f, inputY + inputH * 0.5f},
                           {1.0f * s, inputH - 8.0f * s}, Color::white(), overlayD + 0.5f);
        }

        // Buttons
        float btnW = dialogBtnWidth * s, btnH = dialogBtnHeight * s;
        float btnY = dy + dH - btnH - dlgBtnMargin;
        float confirmX = dx + dW * 0.5f - btnW - 10.0f * s;
        float cancelX  = dx + dW * 0.5f + 10.0f * s;

        // Confirm button
        {
            float bfs = scaledFont(dialogBtnFontSize);
            bool namesMatch = (deleteConfirmInput == deleteTargetName);
            Color confirmBg = namesMatch ? dialogConfirmColor : dialogConfirmDisabledColor;
            batch.drawRect({confirmX + btnW * 0.5f, btnY + btnH * 0.5f},
                           {btnW, btnH}, confirmBg, overlayD + 0.3f);
            const char* confirmLabel = "Confirm";
            Vec2 cSize = sdf.measure(confirmLabel, bfs);
            Color cTextColor = namesMatch ? Color::white() : dialogConfirmDisabledTextColor;
            sdf.drawScreen(batch, confirmLabel,
                           {confirmX + (btnW - cSize.x) * 0.5f,
                            btnY + (btnH - cSize.y) * 0.5f},
                           bfs, cTextColor, overlayD + 0.4f);
        }

        // Cancel button
        {
            float bfs = scaledFont(dialogBtnFontSize);
            batch.drawRect({cancelX + btnW * 0.5f, btnY + btnH * 0.5f},
                           {btnW, btnH},
                           dialogCancelColor, overlayD + 0.3f);
            const char* cancelLabel = "Cancel";
            Vec2 canSize = sdf.measure(cancelLabel, bfs);
            sdf.drawScreen(batch, cancelLabel,
                           {cancelX + (btnW - canSize.x) * 0.5f,
                            btnY + (btnH - canSize.y) * 0.5f},
                           bfs, Color::white(), overlayD + 0.4f);
        }
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Preview texture resolution
// ---------------------------------------------------------------------------

void CharacterSelectScreen::resolvePreviewTextures() {
    if (cachedSlotIndex_ == selectedSlot) return;
    cachedSlotIndex_ = selectedSlot;

    previewBodyTex_   = nullptr;
    previewHairTex_   = nullptr;
    previewArmorTex_  = nullptr;
    previewHatTex_    = nullptr;
    previewWeaponTex_ = nullptr;

    if (selectedSlot < 0 || selectedSlot >= static_cast<int>(slots.size())) return;
    const auto& slot = slots[static_cast<size_t>(selectedSlot)];
    if (slot.empty) return;

    auto& catalog = PaperDollCatalog::instance();
    if (!catalog.isLoaded()) return;

    const char* gender = slot.gender == 0 ? "Male" : "Female";
    previewBodyTex_ = catalog.getBody(gender).front;

    std::string hairName = catalog.getHairstyleNameByIndex(gender, slot.hairstyle);
    previewHairTex_ = catalog.getHairstyle(gender, hairName).front;

    if (!slot.armorStyle.empty())
        previewArmorTex_ = catalog.getEquipment("armor", slot.armorStyle).front;
    if (!slot.hatStyle.empty())
        previewHatTex_ = catalog.getEquipment("hat", slot.hatStyle).front;
    if (!slot.weaponStyle.empty())
        previewWeaponTex_ = catalog.getEquipment("weapon", slot.weaponStyle).front;
}

// ---------------------------------------------------------------------------
// Text / Key input (delete confirmation dialog)
// ---------------------------------------------------------------------------

bool CharacterSelectScreen::onTextInput(const std::string& input) {
    if (!showDeleteConfirm) return false;
    deleteConfirmInput.insert(static_cast<size_t>(deleteConfirmCursor), input);
    deleteConfirmCursor += static_cast<int>(input.size());
    return true;
}

bool CharacterSelectScreen::onKeyInput(int scancode, bool pressed) {
    if (!pressed || !showDeleteConfirm) return false;

    switch (scancode) {
        case SDL_SCANCODE_BACKSPACE:
            if (deleteConfirmCursor > 0 && !deleteConfirmInput.empty()) {
                deleteConfirmInput.erase(static_cast<size_t>(deleteConfirmCursor - 1), 1);
                deleteConfirmCursor--;
            }
            return true;
        case SDL_SCANCODE_DELETE:
            if (deleteConfirmCursor < static_cast<int>(deleteConfirmInput.size()))
                deleteConfirmInput.erase(static_cast<size_t>(deleteConfirmCursor), 1);
            return true;
        case SDL_SCANCODE_LEFT:
            if (deleteConfirmCursor > 0) deleteConfirmCursor--;
            return true;
        case SDL_SCANCODE_RIGHT:
            if (deleteConfirmCursor < static_cast<int>(deleteConfirmInput.size())) deleteConfirmCursor++;
            return true;
        case SDL_SCANCODE_HOME:
            deleteConfirmCursor = 0;
            return true;
        case SDL_SCANCODE_END:
            deleteConfirmCursor = static_cast<int>(deleteConfirmInput.size());
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (deleteConfirmInput == deleteTargetName) {
                if (onDelete) onDelete(id_);
                showDeleteConfirm = false;
            }
            return true;
        case SDL_SCANCODE_ESCAPE:
            showDeleteConfirm = false;
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool CharacterSelectScreen::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // localPos is relative to computedRect_ top-left
    Vec2 absPos = {computedRect_.x + localPos.x, computedRect_.y + localPos.y};
    float s = layoutScale_;

    // --- Delete confirmation dialog intercepts all presses ---
    if (showDeleteConfirm) {
        const auto& rect = computedRect_;
        float dW = dialogWidth * s, dH = dialogHeight * s;
        float dx = rect.x + (rect.w - dW) / 2;
        float dy = rect.y + (rect.h - dH) / 2;

        // Confirm button (bottom-left of dialog)
        float btnW = dialogBtnWidth * s, btnH = dialogBtnHeight * s;
        float btnY = dy + dH - btnH - dialogBtnMargin * s;
        float confirmX = dx + dW * 0.5f - btnW - 10.0f * s;
        float cancelX  = dx + dW * 0.5f + 10.0f * s;

        Rect confirmBtn = {confirmX, btnY, btnW, btnH};
        Rect cancelBtn  = {cancelX, btnY, btnW, btnH};

        if (confirmBtn.contains(absPos)) {
            if (deleteConfirmInput == deleteTargetName) {
                if (onDelete) onDelete(id_);
                showDeleteConfirm = false;
            }
            return true;
        }
        if (cancelBtn.contains(absPos)) {
            showDeleteConfirm = false;
            return true;
        }
        return true; // block all other presses while dialog is showing
    }

    // Check each slot circle
    float circleSize = slotCircleSize * s;
    int count = static_cast<int>(slots.size());
    for (int i = 0; i < count; ++i) {
        Vec2  ctr = slotCenter(i);
        float r   = circleSize * 0.5f;
        float sdx = absPos.x - ctr.x;
        float sdy = absPos.y - ctr.y;
        if (std::sqrt(sdx * sdx + sdy * sdy) <= r) {
            if (slots[static_cast<size_t>(i)].empty) {
                selectedSlot = i;
                if (onCreateNew) onCreateNew(id_);
            } else {
                selectedSlot = i;
                if (onSlotSelected) {
                    onSlotSelected(i, slots[static_cast<size_t>(i)].characterId);
                }
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
        float sdx  = absPos.x - swpC.x;
        float sdy  = absPos.y - swpC.y;
        if (std::sqrt(sdx * sdx + sdy * sdy) <= sr) {
            if (onSwap) onSwap(id_);
            return true;
        }
    }

    // Delete button — show confirmation dialog instead of directly firing onDelete
    {
        Rect  dbr  = deleteButtonRect();
        float dr   = dbr.w * 0.5f;
        Vec2  delC = {dbr.x + dr, dbr.y + dr};
        float sdx  = absPos.x - delC.x;
        float sdy  = absPos.y - delC.y;
        if (std::sqrt(sdx * sdx + sdy * sdy) <= dr) {
            if (selectedSlot >= 0 && selectedSlot < static_cast<int>(slots.size())
                && !slots[static_cast<size_t>(selectedSlot)].empty) {
                showDeleteConfirm = true;
                deleteConfirmInput.clear();
                deleteConfirmCursor = 0;
                deleteTargetName = slots[static_cast<size_t>(selectedSlot)].name;
                deleteTargetId = slots[static_cast<size_t>(selectedSlot)].characterId;
            }
            return true;
        }
    }

    return true; // consume all presses on this full-screen widget
}

} // namespace fate
