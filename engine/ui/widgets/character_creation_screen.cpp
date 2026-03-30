#include "engine/ui/widgets/character_creation_screen.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include "game/data/paper_doll_catalog.h"
#include <SDL_scancode.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace fate {

// ---------------------------------------------------------------------------
// Static data definitions
// ---------------------------------------------------------------------------

constexpr const char* CharacterCreationScreen::classNames[];
constexpr const char* CharacterCreationScreen::classDescs[];
constexpr const char* CharacterCreationScreen::factionNames[];

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CharacterCreationScreen::CharacterCreationScreen(const std::string& id)
    : UINode(id, "character_creation_screen") {}

// ---------------------------------------------------------------------------
// Colors & layout constants
// ---------------------------------------------------------------------------

Color CharacterCreationScreen::factionColor(int faction) const {
    switch (faction) {
        case 0: return {0.85f, 0.25f, 0.25f, 1.0f}; // Xyros  — red
        case 1: return {0.25f, 0.55f, 0.85f, 1.0f}; // Fenor  — blue
        case 2: return {0.25f, 0.80f, 0.40f, 1.0f}; // Zethos — green
        case 3: return {0.95f, 0.75f, 0.20f, 1.0f}; // Solis  — gold
        default: return {0.55f, 0.55f, 0.55f, 1.0f};
    }
}

// Right panel origin + dimensions
Rect CharacterCreationScreen::rightPanel() const {
    float lw = computedRect_.w * leftPanelRatio;
    return {computedRect_.x + lw, computedRect_.y, computedRect_.w - lw, computedRect_.h};
}

Rect CharacterCreationScreen::genderButtonRect(int index) const {
    Rect rp    = rightPanel();
    float S    = layoutScale_;
    float rowY = rp.y + genderRowY * S;
    float bw   = genderBtnWidth * S;
    float bh   = genderBtnHeight * S;
    float gap  = genderGap * S;
    float totalW = 2.0f * bw + gap;
    float startX = rp.x + (rp.w - totalW) * 0.5f;
    float cx     = startX + static_cast<float>(index) * (bw + gap);
    return {cx, rowY, bw, bh};
}

Rect CharacterCreationScreen::hairstyleButtonRect(int index) const {
    Rect rp    = rightPanel();
    float S    = layoutScale_;
    float rowY = rp.y + hairstyleRowY * S;
    float size = hairstyleBtnSize * S;
    float gap  = hairstyleGap * S;
    const char* g = selectedGender == 0 ? "Male" : "Female";
    float count = static_cast<float>(PaperDollCatalog::instance().getHairstyleCount(g));
    if (count < 1.0f) count = 1.0f;
    float totalW = count * size + (count - 1.0f) * gap;
    float startX = rp.x + (rp.w - totalW) * 0.5f;
    float cx     = startX + static_cast<float>(index) * (size + gap);
    return {cx, rowY, size, size};
}

Rect CharacterCreationScreen::classButtonRect(int index) const {
    Rect rp    = rightPanel();
    float S    = layoutScale_;
    // Class selector row starts below gender/hairstyle pickers
    float rowY  = rp.y + classRowY * S;
    float size  = classBtnSize * S;
    float gap   = classGap * S;
    float totalW = 3.0f * size + 2.0f * gap;
    float startX = rp.x + (rp.w - totalW) * 0.5f;
    float cx     = startX + static_cast<float>(index) * (size + gap);
    return {cx, rowY, size, size};
}

Rect CharacterCreationScreen::factionButtonRect(int index) const {
    Rect rp     = rightPanel();
    float S     = layoutScale_;
    float rowY  = rp.y + factionRowY * S;
    float r     = factionRadius * S;
    float gap   = factionGap * S;
    float totalW = 4.0f * (r * 2.0f) + 3.0f * gap;
    float startX = rp.x + (rp.w - totalW) * 0.5f;
    float cx     = startX + static_cast<float>(index) * (r * 2.0f + gap);
    return {cx, rowY, r * 2.0f, r * 2.0f};
}

Rect CharacterCreationScreen::nameFieldRect() const {
    Rect rp    = rightPanel();
    float S    = layoutScale_;
    float pad  = nameFieldPadX * S;
    float fw   = rp.w - pad * 2.0f;
    float fh   = nameFieldHeight * S;
    float fx   = rp.x + pad;
    float fy   = rp.y + nameFieldY * S;
    return {fx, fy, fw, fh};
}

Rect CharacterCreationScreen::backButtonRect() const {
    Rect rp  = rightPanel();
    float S  = layoutScale_;
    float r  = backBtnRadius * S;
    return {rp.x + backBtnOffsetX * S, rp.y + backBtnOffsetY * S, r * 2.0f, r * 2.0f};
}

Rect CharacterCreationScreen::nextButtonRect() const {
    Rect rp   = rightPanel();
    float S   = layoutScale_;
    float pad = nextBtnPadX * S;
    float bw  = rp.w - pad * 2.0f;
    float bh  = nextBtnHeight * S;
    float bx  = rp.x + pad;
    float by  = rp.y + rp.h - bh - nextBtnBottomMargin * S;
    return {bx, by, bw, bh};
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void CharacterCreationScreen::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float S = layoutScale_;

    // Scaled font locals
    float fHeader         = scaledFont(headerFontSize);
    float fClass          = scaledFont(classFontSize);
    float fClassInitial   = scaledFont(classInitialFontSize);
    float fDesc           = scaledFont(descFontSize);
    float fButton         = scaledFont(buttonFontSize);
    float fLabel          = scaledFont(labelFontSize);
    float fNameLabel      = scaledFont(nameLabelFontSize);
    float fNameInput      = scaledFont(nameInputFontSize);
    float fNextBtn        = scaledFont(nextBtnFontSize);
    float fStatus         = scaledFont(statusFontSize);
    float fFactionInitial = scaledFont(factionInitialFontSize);
    float fFactionName    = scaledFont(factionNameFontSize);
    float fBackBtn        = scaledFont(backBtnFontSize);

    // --- Background ---
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h},
                   backgroundColor, d);

    // --- Left half: paper doll preview ---
    float leftW = rect.w * leftPanelRatio;
    Color classColor;
    switch (selectedClass) {
        case 0: classColor = {0.85f, 0.30f, 0.20f, 1.0f}; break; // Warrior — red
        case 1: classColor = {0.35f, 0.45f, 0.95f, 1.0f}; break; // Magician — blue
        case 2: classColor = {0.25f, 0.75f, 0.35f, 1.0f}; break; // Archer — green
        default: classColor = {0.55f, 0.55f, 0.75f, 1.0f}; break;
    }
    batch.drawRect({rect.x + leftW * 0.5f, rect.y + rect.h * 0.5f},
                   {leftW, rect.h},
                   leftPanelColor, d + 0.1f);
    // Class-colored border (right edge divider)
    {
        float bw = dividerWidth * S;
        batch.drawRect({rect.x + leftW - bw * 0.5f, rect.y + rect.h * 0.5f},
                       {bw, rect.h}, classColor, d + 0.2f);
    }
    // Paper doll preview: body + hair sprites centered in the left panel
    {
        resolvePreviewTextures();
        auto& bodyTex = previewBodyTex_;
        auto& hairTex = previewHairTex_;

        float previewCenterX = rect.x + leftW * 0.5f;
        float previewCenterY = rect.y + rect.h * 0.5f;
        float scale = previewScale;
        Vec2 size = {48.0f * scale, 96.0f * scale};
        Vec2 pos = {previewCenterX - size.x / 2.0f, previewCenterY - size.y / 2.0f};

        if (bodyTex) {
            SpriteDrawParams params;
            params.position = {pos.x + size.x * 0.5f, pos.y + size.y * 0.5f};
            params.size = size;
            params.sourceRect = {0, 0, 48.0f / static_cast<float>(bodyTex->width()),
                                 96.0f / static_cast<float>(bodyTex->height())};
            params.depth = d + 0.3f;
            params.flipY = true; // UI projection Y-down vs texture Y-up
            batch.draw(bodyTex, params);
        }
        if (hairTex) {
            SpriteDrawParams params;
            params.position = {pos.x + size.x * 0.5f, pos.y + size.y * 0.5f};
            params.size = size;
            params.sourceRect = {0, 0, 48.0f / static_cast<float>(hairTex->width()),
                                 96.0f / static_cast<float>(hairTex->height())};
            params.depth = d + 0.4f;
            params.flipY = true;
            batch.draw(hairTex, params);
        }

        // Fallback label if textures not loaded yet
        if (!bodyTex && !hairTex) {
            const char* cn   = classNames[selectedClass];
            float        fs  = scaledFont(22.0f);
            Vec2         csz = sdf.measure(cn, fs);
            sdf.drawScreen(batch, cn,
                           {rect.x + (leftW - csz.x) * 0.5f,
                            rect.y + rect.h * 0.5f - csz.y * 0.5f},
                           fs, classColor, d + 0.3f);
            const char* artPrompt = "(Preview)";
            float        apf  = fDesc;
            Vec2         apSz = sdf.measure(artPrompt, apf);
            sdf.drawScreen(batch, artPrompt,
                           {rect.x + (leftW - apSz.x) * 0.5f,
                            rect.y + rect.h * 0.5f + csz.y * 0.5f + 6.0f * S},
                           apf, {0.35f, 0.35f, 0.45f, 1.0f}, d + 0.3f);
        }
    }

    // --- Right half ---
    Rect rp = rightPanel();

    // "Create Your Character" header
    {
        const char* header = "Create Your Character";
        Vec2  hsz  = sdf.measure(header, fHeader);
        sdf.drawScreen(batch, header,
                       {rp.x + (rp.w - hsz.x) * 0.5f, rp.y + headerY * S},
                       fHeader, headerColor, d + 0.3f);
    }

    // Back button (circle, blue, top-left of right half)
    {
        Rect  bbr = backButtonRect();
        float br  = bbr.w * 0.5f;
        Vec2  bc  = {bbr.x + br, bbr.y + br};
        batch.drawCircle(bc, br, backBtnColor, d + 0.3f, 20);
        batch.drawRing(bc, br, backBtnRingWidth * S, backBtnBorderColor, d + 0.4f, 20);
        const char* bl  = "<";
        Vec2        bsz = sdf.measure(bl, fBackBtn);
        sdf.drawScreen(batch, bl,
                       {bc.x - bsz.x * 0.5f, bc.y - bsz.y * 0.5f},
                       fBackBtn, Color::white(), d + 0.5f);
    }

    // --- Gender toggle: Male / Female side by side ---
    {
        const char* genderLabels[] = {"Male", "Female"};
        for (int i = 0; i < 2; ++i) {
            Rect gbr = genderButtonRect(i);
            bool sel = (i == static_cast<int>(selectedGender));

            Color bgCol   = sel ? selectedBgColor   : unselectedBgColor;
            Color ringCol = sel ? selectedColor      : unselectedBorderColor;

            batch.drawRect({gbr.x + gbr.w * 0.5f, gbr.y + gbr.h * 0.5f},
                           {gbr.w, gbr.h}, bgCol, d + 0.3f);
            // Border
            float bw = sel ? genderSelBorderWidth * S : genderBorderWidth * S;
            float ih = gbr.h - bw * 2.0f;
            batch.drawRect({gbr.x + gbr.w * 0.5f, gbr.y + bw * 0.5f}, {gbr.w, bw}, ringCol, d + 0.4f);
            batch.drawRect({gbr.x + gbr.w * 0.5f, gbr.y + gbr.h - bw * 0.5f}, {gbr.w, bw}, ringCol, d + 0.4f);
            batch.drawRect({gbr.x + bw * 0.5f, gbr.y + gbr.h * 0.5f}, {bw, ih}, ringCol, d + 0.4f);
            batch.drawRect({gbr.x + gbr.w - bw * 0.5f, gbr.y + gbr.h * 0.5f}, {bw, ih}, ringCol, d + 0.4f);

            Vec2 gsz = sdf.measure(genderLabels[i], fButton);
            Color gtc = sel ? selectedColor : unselectedTextColor;
            sdf.drawScreen(batch, genderLabels[i],
                           {gbr.x + (gbr.w - gsz.x) * 0.5f,
                            gbr.y + (gbr.h - gsz.y) * 0.5f},
                           fButton, gtc, d + 0.5f);
        }
    }

    // --- Hairstyle picker: numbered buttons ---
    {
        const char* hsGender = selectedGender == 0 ? "Male" : "Female";
        int hsCount = static_cast<int>(PaperDollCatalog::instance().getHairstyleCount(hsGender));
        if (hsCount < 1) hsCount = 1;
        for (int i = 0; i < hsCount; ++i) {
            Rect hbr = hairstyleButtonRect(i);
            bool sel = (i == static_cast<int>(selectedHairstyle));
            float r  = hbr.w * 0.5f;
            Vec2  hc = {hbr.x + r, hbr.y + r};

            Color fillCol = sel ? selectedBgColor      : unselectedBgColor;
            Color ringCol = sel ? selectedColor         : unselectedBorderColor;

            batch.drawCircle(hc, r, fillCol, d + 0.3f, 20);
            batch.drawRing(hc, r, sel ? hairstyleSelRingWidth * S : hairstyleRingWidth * S, ringCol, d + 0.4f, 20);

            char lbl[2] = {static_cast<char>('1' + i), '\0'};
            Vec2 hsz = sdf.measure(lbl, fButton);
            Color htc = sel ? selectedColor : unselectedTextColor;
            sdf.drawScreen(batch, lbl,
                           {hc.x - hsz.x * 0.5f, hc.y - hsz.y * 0.5f},
                           fButton, htc, d + 0.5f);
        }
        // "Hairstyle" label above the row
        const char* hlbl = "Hairstyle";
        Vec2 hlsz = sdf.measure(hlbl, fLabel);
        Rect h0 = hairstyleButtonRect(0);
        Rect hLast = hairstyleButtonRect(hsCount - 1);
        float centerX = (h0.x + hLast.x + hLast.w) * 0.5f;
        sdf.drawScreen(batch, hlbl,
                       {centerX - hlsz.x * 0.5f, h0.y - fLabel - hairstyleLabelGap * S},
                       fLabel, labelColor, d + 0.4f);
    }

    // --- Class selector row: 3 diamond-shaped buttons ---
    for (int i = 0; i < 3; ++i) {
        Rect  cbr  = classButtonRect(i);
        float cx_c = cbr.x + cbr.w * 0.5f;
        float cy_c = cbr.y + cbr.h * 0.5f;
        bool  sel  = (i == selectedClass);

        // Draw a rotated square by drawing 2 overlapping rects at 45 deg —
        // We approximate with a circle + ring (hardware-friendly, matches the
        // "diamond badge" look more than two rects at this size)
        float  r      = cbr.w * 0.5f;
        Color  fill   = sel ? Color{0.15f, 0.13f, 0.05f, 1.0f}
                            : Color{0.15f, 0.15f, 0.20f, 1.0f};
        Color  ring   = sel ? selectedColor
                            : Color{0.42f, 0.42f, 0.55f, 1.0f};

        batch.drawCircle({cx_c, cy_c}, r, fill, d + 0.3f, 4); // 4 segments = diamond
        batch.drawRing({cx_c, cy_c}, r, sel ? classSelRingWidth * S : classRingWidth * S, ring, d + 0.4f, 4);

        // Class initial letter
        char lbl[2] = {classNames[i][0], '\0'};
        Vec2  lsz   = sdf.measure(lbl, fClassInitial);
        Color ltc   = sel ? selectedColor : nameLabelColor;
        sdf.drawScreen(batch, lbl,
                       {cx_c - lsz.x * 0.5f, cy_c - lsz.y * 0.5f},
                       fClassInitial, ltc, d + 0.5f);
    }

    // Class name (large) + description below class selector
    {
        const char* cn  = classNames[selectedClass];
        Vec2        csz = sdf.measure(cn, fClass);
        float       cy0 = classButtonRect(0).y + classBtnSize * S + classNameGap * S;
        sdf.drawScreen(batch, cn,
                       {rp.x + (rp.w - csz.x) * 0.5f, cy0},
                       fClass, classColor, d + 0.3f);

        // Description (single block — no line-wrapping at this stage)
        const char* desc = classDescs[selectedClass];
        Vec2        dsz  = sdf.measure(desc, fDesc);
        // Clamp x so it starts within the panel (may overflow for long text —
        // full word-wrap deferred to the animation-panel session)
        float       dx   = rp.x + classDescPadX * S;
        float       dy   = cy0 + csz.y + classDescGap * S;
        sdf.drawScreen(batch, desc, {dx, dy}, fDesc,
                       descColor, d + 0.3f);
    }

    // --- Faction selector: 4 circular badges ---
    for (int i = 0; i < 4; ++i) {
        Rect  fbr  = factionButtonRect(i);
        float fr   = fbr.w * 0.5f;
        Vec2  fc   = {fbr.x + fr, fbr.y + fr};
        bool  sel  = (i == selectedFaction);
        Color fc_c = factionColor(i);

        float drawR = sel ? fr * factionSelScale : fr;
        batch.drawCircle(fc, drawR, fc_c, d + 0.3f, 24);
        if (sel) {
            batch.drawRing(fc, drawR, factionSelRingWidth * S, Color::white(), d + 0.4f, 24);
        }

        // Faction initial
        char fLbl[2] = {factionNames[i][0], '\0'};
        Vec2  fsz    = sdf.measure(fLbl, fFactionInitial);
        sdf.drawScreen(batch, fLbl,
                       {fc.x - fsz.x * 0.5f, fc.y - fsz.y * 0.5f},
                       fFactionInitial, Color::white(), d + 0.5f);

        // Faction name below badge
        Vec2  nsz  = sdf.measure(factionNames[i], fFactionName);
        sdf.drawScreen(batch, factionNames[i],
                       {fc.x - nsz.x * 0.5f, fbr.y + fbr.h + factionNameGap * S},
                       fFactionName, sel ? Color::white() : labelColor,
                       d + 0.5f);
    }

    // --- Name input field ---
    Rect  nfr = nameFieldRect();
    Color nfBg = nameFieldFocused ? nameFieldFocusBgColor : nameFieldBgColor;
    batch.drawRect({nfr.x + nfr.w * 0.5f, nfr.y + nfr.h * 0.5f},
                   {nfr.w, nfr.h}, nfBg, d + 0.3f);
    {
        float bw = nameFieldBorderWidth * S;
        Color bc = nameFieldFocused ? nameFieldFocusBorderColor : nameFieldBorderColor;
        float ih = nfr.h - bw * 2.0f;
        batch.drawRect({nfr.x + nfr.w * 0.5f, nfr.y + bw * 0.5f}, {nfr.w, bw}, bc, d + 0.4f);
        batch.drawRect({nfr.x + nfr.w * 0.5f, nfr.y + nfr.h - bw * 0.5f}, {nfr.w, bw}, bc, d + 0.4f);
        batch.drawRect({nfr.x + bw * 0.5f, nfr.y + nfr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
        batch.drawRect({nfr.x + nfr.w - bw * 0.5f, nfr.y + nfr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
    }
    // Name label above field
    {
        const char* lbl = "Character Name";
        sdf.drawScreen(batch, lbl,
                       {nfr.x, nfr.y - fNameLabel - nameFieldLabelGap * S},
                       fNameLabel, nameLabelColor, d + 0.4f);
    }
    // Text or placeholder
    {
        float       tpad = nameFieldTextPad * S;
        float       ty   = nfr.y + (nfr.h - fNameInput) * 0.5f;
        if (characterName.empty()) {
            sdf.drawScreen(batch, "Enter name...",
                           {nfr.x + tpad, ty},
                           fNameInput, placeholderColor, d + 0.5f);
        } else {
            sdf.drawScreen(batch, characterName,
                           {nfr.x + tpad, ty},
                           fNameInput, Color::white(), d + 0.5f);
        }
        // Cursor
        if (nameFieldFocused) {
            std::string before = characterName.substr(0, static_cast<size_t>(cursorPos));
            Vec2 cOff = sdf.measure(before, fNameInput);
            float cx_f = nfr.x + tpad + cOff.x;
            batch.drawRect({cx_f + 0.5f * S, nfr.y + nfr.h * 0.5f},
                           {nameFieldCursorWidth * S, nfr.h - nameFieldCursorPad * S}, Color::white(), d + 0.6f);
        }
    }

    // --- Next button ---
    Rect  nbr = nextButtonRect();
    batch.drawRect({nbr.x + nbr.w * 0.5f, nbr.y + nbr.h * 0.5f},
                   {nbr.w, nbr.h},
                   nextBtnColor, d + 0.3f);
    {
        float bw = nextBtnBorderWidth * S;
        float ih = nbr.h - bw * 2.0f;
        batch.drawRect({nbr.x + nbr.w * 0.5f, nbr.y + bw * 0.5f}, {nbr.w, bw}, nextBtnBorderColor, d + 0.4f);
        batch.drawRect({nbr.x + nbr.w * 0.5f, nbr.y + nbr.h - bw * 0.5f}, {nbr.w, bw}, nextBtnBorderColor, d + 0.4f);
        batch.drawRect({nbr.x + bw * 0.5f, nbr.y + nbr.h * 0.5f}, {bw, ih}, nextBtnBorderColor, d + 0.4f);
        batch.drawRect({nbr.x + nbr.w - bw * 0.5f, nbr.y + nbr.h * 0.5f}, {bw, ih}, nextBtnBorderColor, d + 0.4f);
    }
    {
        const char* nextLabel = "Next";
        Vec2        nsz2      = sdf.measure(nextLabel, fNextBtn);
        sdf.drawScreen(batch, nextLabel,
                       {nbr.x + (nbr.w - nsz2.x) * 0.5f,
                        nbr.y + (nbr.h - nsz2.y) * 0.5f},
                       fNextBtn, Color::white(), d + 0.5f);
    }

    // --- Status message ---
    if (!statusMessage.empty()) {
        Vec2        ssz = sdf.measure(statusMessage, fStatus);
        Color       sc  = isError ? errorColor : successColor;
        sdf.drawScreen(batch, statusMessage,
                       {rp.x + (rp.w - ssz.x) * 0.5f,
                        nextButtonRect().y + nextButtonRect().h + statusGap * S},
                       fStatus, sc, d + 0.5f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input — press
// ---------------------------------------------------------------------------

bool CharacterCreationScreen::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    Vec2 absPos = {computedRect_.x + localPos.x, computedRect_.y + localPos.y};

    // Back button
    {
        Rect  bbr = backButtonRect();
        float br  = bbr.w * 0.5f;
        Vec2  bc  = {bbr.x + br, bbr.y + br};
        float dx  = absPos.x - bc.x;
        float dy  = absPos.y - bc.y;
        if (std::sqrt(dx * dx + dy * dy) <= br) {
            if (onBack) onBack(id_);
            return true;
        }
    }

    // Gender toggle (2)
    for (int i = 0; i < 2; ++i) {
        Rect gbr = genderButtonRect(i);
        if (gbr.contains(absPos)) {
            if (static_cast<int>(selectedGender) != i) {
                selectedGender = static_cast<uint8_t>(i);
                selectedHairstyle = 0; // reset hairstyle on gender change
            }
            return true;
        }
    }

    // Hairstyle buttons
    {
    const char* pressGender = selectedGender == 0 ? "Male" : "Female";
    int pressHsCount = static_cast<int>(PaperDollCatalog::instance().getHairstyleCount(pressGender));
    if (pressHsCount < 1) pressHsCount = 1;
    for (int i = 0; i < pressHsCount; ++i) {
        Rect hbr = hairstyleButtonRect(i);
        float hr  = hbr.w * 0.5f;
        Vec2  hc  = {hbr.x + hr, hbr.y + hr};
        float dx2 = absPos.x - hc.x;
        float dy2 = absPos.y - hc.y;
        if (std::sqrt(dx2 * dx2 + dy2 * dy2) <= hr * 1.2f) {
            selectedHairstyle = static_cast<uint8_t>(i);
            return true;
        }
    }
    }

    // Class diamonds (3)
    for (int i = 0; i < 3; ++i) {
        Rect cbr = classButtonRect(i);
        if (cbr.contains(absPos)) {
            selectedClass = i;
            return true;
        }
    }

    // Faction circles (4)
    for (int i = 0; i < 4; ++i) {
        Rect  fbr = factionButtonRect(i);
        float fr  = fbr.w * 0.5f;
        Vec2  fc  = {fbr.x + fr, fbr.y + fr};
        float dx  = absPos.x - fc.x;
        float dy  = absPos.y - fc.y;
        if (std::sqrt(dx * dx + dy * dy) <= fr * 1.25f) { // slightly larger hit area
            selectedFaction = i;
            return true;
        }
    }

    // Name field
    {
        Rect nfr = nameFieldRect();
        if (nfr.contains(absPos)) {
            nameFieldFocused = true;
            focused_ = true;
            return true;
        }
        // Click outside name field — unfocus
        nameFieldFocused = false;
    }

    // Next button
    {
        Rect nbr = nextButtonRect();
        if (nbr.contains(absPos)) {
            if (onNext) onNext(id_);
            return true;
        }
    }

    return true; // full-screen — consume all presses
}

// ---------------------------------------------------------------------------
// Text input (character name editing)
// ---------------------------------------------------------------------------

bool CharacterCreationScreen::onTextInput(const std::string& input) {
    if (!nameFieldFocused) return false;
    for (char c : input) {
        if (static_cast<int>(characterName.size()) >= MAX_NAME_LENGTH) break;
        characterName.insert(characterName.begin() + cursorPos, c);
        cursorPos++;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Key input (backspace, delete, arrows, enter)
// ---------------------------------------------------------------------------

bool CharacterCreationScreen::onKeyInput(int scancode, bool pressed) {
    if (!pressed || !nameFieldFocused) return false;
    switch (scancode) {
        case SDL_SCANCODE_BACKSPACE:
            if (cursorPos > 0 && !characterName.empty()) {
                characterName.erase(static_cast<size_t>(cursorPos - 1), 1);
                cursorPos--;
            }
            return true;
        case SDL_SCANCODE_DELETE:
            if (cursorPos < static_cast<int>(characterName.size()))
                characterName.erase(static_cast<size_t>(cursorPos), 1);
            return true;
        case SDL_SCANCODE_LEFT:
            if (cursorPos > 0) cursorPos--;
            return true;
        case SDL_SCANCODE_RIGHT:
            if (cursorPos < static_cast<int>(characterName.size())) cursorPos++;
            return true;
        case SDL_SCANCODE_HOME:
            cursorPos = 0;
            return true;
        case SDL_SCANCODE_END:
            cursorPos = static_cast<int>(characterName.size());
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (onNext) onNext(id_);
            return true;
        default: return false;
    }
}

void CharacterCreationScreen::resolvePreviewTextures() {
    if (cachedGender_ == selectedGender && cachedHairstyle_ == selectedHairstyle) return;
    cachedGender_ = selectedGender;
    cachedHairstyle_ = selectedHairstyle;

    auto& catalog = PaperDollCatalog::instance();
    if (!catalog.isLoaded()) return;

    const char* gender = selectedGender == 0 ? "Male" : "Female";
    previewBodyTex_ = catalog.getBody(gender).front;

    std::string hairName = catalog.getHairstyleNameByIndex(gender, static_cast<uint8_t>(selectedHairstyle));
    previewHairTex_ = catalog.getHairstyle(gender, hairName).front;
}

} // namespace fate
