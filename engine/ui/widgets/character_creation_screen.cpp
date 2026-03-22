#include "engine/ui/widgets/character_creation_screen.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
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

// Right panel origin + dimensions (55% of screen width)
static inline Rect rightPanel(const Rect& rect) {
    float lw = rect.w * 0.45f;
    return {rect.x + lw, rect.y, rect.w - lw, rect.h};
}

Rect CharacterCreationScreen::classButtonRect(int index) const {
    Rect rp    = rightPanel(computedRect_);
    // Class selector row starts at y=100 within the right panel
    float rowY  = rp.y + 100.0f;
    float size  = 50.0f;
    float gap   = 16.0f;
    float totalW = 3.0f * size + 2.0f * gap;
    float startX = rp.x + (rp.w - totalW) * 0.5f;
    float cx     = startX + static_cast<float>(index) * (size + gap);
    return {cx, rowY, size, size};
}

Rect CharacterCreationScreen::factionButtonRect(int index) const {
    Rect rp     = rightPanel(computedRect_);
    float rowY  = rp.y + 260.0f;
    float r     = 22.0f;
    float gap   = 12.0f;
    float totalW = 4.0f * (r * 2.0f) + 3.0f * gap;
    float startX = rp.x + (rp.w - totalW) * 0.5f;
    float cx     = startX + static_cast<float>(index) * (r * 2.0f + gap);
    return {cx, rowY, r * 2.0f, r * 2.0f};
}

Rect CharacterCreationScreen::nameFieldRect() const {
    Rect rp    = rightPanel(computedRect_);
    float fw   = rp.w - 40.0f;
    float fh   = 36.0f;
    float fx   = rp.x + 20.0f;
    float fy   = rp.y + 330.0f;
    return {fx, fy, fw, fh};
}

Rect CharacterCreationScreen::backButtonRect() const {
    Rect rp  = rightPanel(computedRect_);
    float r  = 18.0f;
    return {rp.x + 14.0f, rp.y + 14.0f, r * 2.0f, r * 2.0f};
}

Rect CharacterCreationScreen::nextButtonRect() const {
    Rect rp   = rightPanel(computedRect_);
    float bw  = rp.w - 40.0f;
    float bh  = 40.0f;
    float bx  = rp.x + 20.0f;
    float by  = rp.y + rp.h - bh - 60.0f;
    return {bx, by, bw, bh};
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void CharacterCreationScreen::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // --- Background ---
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h},
                   {0.04f, 0.04f, 0.07f, 1.0f}, d);

    // --- Left half: character illustration placeholder ---
    float leftW = rect.w * 0.45f;
    Color classColor;
    switch (selectedClass) {
        case 0: classColor = {0.85f, 0.30f, 0.20f, 1.0f}; break; // Warrior — red
        case 1: classColor = {0.35f, 0.45f, 0.95f, 1.0f}; break; // Magician — blue
        case 2: classColor = {0.25f, 0.75f, 0.35f, 1.0f}; break; // Archer — green
        default: classColor = {0.55f, 0.55f, 0.75f, 1.0f}; break;
    }
    batch.drawRect({rect.x + leftW * 0.5f, rect.y + rect.h * 0.5f},
                   {leftW, rect.h},
                   {0.07f, 0.07f, 0.10f, 1.0f}, d + 0.1f);
    // Class-colored border (right edge divider)
    {
        float bw = 3.0f;
        batch.drawRect({rect.x + leftW - bw * 0.5f, rect.y + rect.h * 0.5f},
                       {bw, rect.h}, classColor, d + 0.2f);
    }
    // Class name centered in the illustration area
    {
        const char* cn   = classNames[selectedClass];
        float        fs  = 22.0f;
        Vec2         csz = sdf.measure(cn, fs);
        sdf.drawScreen(batch, cn,
                       {rect.x + (leftW - csz.x) * 0.5f,
                        rect.y + rect.h * 0.5f - csz.y * 0.5f},
                       fs, classColor, d + 0.3f);
        const char* artPrompt = "(Art Placeholder)";
        float        apf  = 11.0f;
        Vec2         apSz = sdf.measure(artPrompt, apf);
        sdf.drawScreen(batch, artPrompt,
                       {rect.x + (leftW - apSz.x) * 0.5f,
                        rect.y + rect.h * 0.5f + csz.y * 0.5f + 6.0f},
                       apf, {0.35f, 0.35f, 0.45f, 1.0f}, d + 0.3f);
    }

    // --- Right half ---
    Rect rp = rightPanel(rect);

    // "Create Your Character" header
    {
        const char* header = "Create Your Character";
        float hfs  = 18.0f;
        Vec2  hsz  = sdf.measure(header, hfs);
        sdf.drawScreen(batch, header,
                       {rp.x + (rp.w - hsz.x) * 0.5f, rp.y + 20.0f},
                       hfs, {1.0f, 0.92f, 0.75f, 1.0f}, d + 0.3f);
    }

    // Back button (circle, blue, top-left of right half)
    {
        Rect  bbr = backButtonRect();
        float br  = bbr.w * 0.5f;
        Vec2  bc  = {bbr.x + br, bbr.y + br};
        batch.drawCircle(bc, br, {0.2f, 0.45f, 0.7f, 1.0f}, d + 0.3f, 20);
        batch.drawRing(bc, br, 1.5f, {0.4f, 0.65f, 0.9f, 1.0f}, d + 0.4f, 20);
        const char* bl  = "<";
        float       bfs = 14.0f;
        Vec2        bsz = sdf.measure(bl, bfs);
        sdf.drawScreen(batch, bl,
                       {bc.x - bsz.x * 0.5f, bc.y - bsz.y * 0.5f},
                       bfs, Color::white(), d + 0.5f);
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
        Color  ring   = sel ? Color{0.95f, 0.80f, 0.20f, 1.0f}
                            : Color{0.42f, 0.42f, 0.55f, 1.0f};

        batch.drawCircle({cx_c, cy_c}, r, fill, d + 0.3f, 4); // 4 segments = diamond
        batch.drawRing({cx_c, cy_c}, r, sel ? 3.0f : 1.5f, ring, d + 0.4f, 4);

        // Class initial letter
        char lbl[2] = {classNames[i][0], '\0'};
        float lfs   = 16.0f;
        Vec2  lsz   = sdf.measure(lbl, lfs);
        Color ltc   = sel ? Color{0.95f, 0.80f, 0.20f, 1.0f}
                          : Color{0.65f, 0.65f, 0.75f, 1.0f};
        sdf.drawScreen(batch, lbl,
                       {cx_c - lsz.x * 0.5f, cy_c - lsz.y * 0.5f},
                       lfs, ltc, d + 0.5f);
    }

    // Class name (large) + description below class selector
    {
        const char* cn  = classNames[selectedClass];
        float       cfs = 17.0f;
        Vec2        csz = sdf.measure(cn, cfs);
        float       cy0 = classButtonRect(0).y + 50.0f + 10.0f;
        sdf.drawScreen(batch, cn,
                       {rp.x + (rp.w - csz.x) * 0.5f, cy0},
                       cfs, classColor, d + 0.3f);

        // Description (single block — no line-wrapping at this stage)
        const char* desc = classDescs[selectedClass];
        float       dfs  = 11.0f;
        Vec2        dsz  = sdf.measure(desc, dfs);
        // Clamp x so it starts within the panel (may overflow for long text —
        // full word-wrap deferred to the animation-panel session)
        float       dx   = rp.x + 10.0f;
        float       dy   = cy0 + csz.y + 6.0f;
        sdf.drawScreen(batch, desc, {dx, dy}, dfs,
                       {0.70f, 0.70f, 0.80f, 1.0f}, d + 0.3f);
    }

    // --- Faction selector: 4 circular badges ---
    for (int i = 0; i < 4; ++i) {
        Rect  fbr  = factionButtonRect(i);
        float fr   = fbr.w * 0.5f;
        Vec2  fc   = {fbr.x + fr, fbr.y + fr};
        bool  sel  = (i == selectedFaction);
        Color fc_c = factionColor(i);

        float drawR = sel ? fr * 1.2f : fr;
        batch.drawCircle(fc, drawR, fc_c, d + 0.3f, 24);
        if (sel) {
            batch.drawRing(fc, drawR, 2.5f, Color::white(), d + 0.4f, 24);
        }

        // Faction initial
        char fLbl[2] = {factionNames[i][0], '\0'};
        float ffs    = 11.0f;
        Vec2  fsz    = sdf.measure(fLbl, ffs);
        sdf.drawScreen(batch, fLbl,
                       {fc.x - fsz.x * 0.5f, fc.y - fsz.y * 0.5f},
                       ffs, Color::white(), d + 0.5f);

        // Faction name below badge
        float nfs  = 10.0f;
        Vec2  nsz  = sdf.measure(factionNames[i], nfs);
        sdf.drawScreen(batch, factionNames[i],
                       {fc.x - nsz.x * 0.5f, fbr.y + fbr.h + 3.0f},
                       nfs, sel ? Color::white() : Color{0.55f, 0.55f, 0.65f, 1.0f},
                       d + 0.5f);
    }

    // --- Name input field ---
    Rect  nfr = nameFieldRect();
    Color nfBg = nameFieldFocused ? Color{0.15f, 0.15f, 0.22f, 1.0f}
                                  : Color{0.10f, 0.10f, 0.15f, 1.0f};
    batch.drawRect({nfr.x + nfr.w * 0.5f, nfr.y + nfr.h * 0.5f},
                   {nfr.w, nfr.h}, nfBg, d + 0.3f);
    {
        float bw = 1.5f;
        Color bc = nameFieldFocused ? Color{0.6f, 0.5f, 0.3f, 1.0f}
                                    : Color{0.35f, 0.35f, 0.50f, 1.0f};
        float ih = nfr.h - bw * 2.0f;
        batch.drawRect({nfr.x + nfr.w * 0.5f, nfr.y + bw * 0.5f}, {nfr.w, bw}, bc, d + 0.4f);
        batch.drawRect({nfr.x + nfr.w * 0.5f, nfr.y + nfr.h - bw * 0.5f}, {nfr.w, bw}, bc, d + 0.4f);
        batch.drawRect({nfr.x + bw * 0.5f, nfr.y + nfr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
        batch.drawRect({nfr.x + nfr.w - bw * 0.5f, nfr.y + nfr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
    }
    // Name label above field
    {
        const char* lbl = "Character Name";
        float lfs = 11.0f;
        sdf.drawScreen(batch, lbl,
                       {nfr.x, nfr.y - lfs - 2.0f},
                       lfs, {0.65f, 0.65f, 0.75f, 1.0f}, d + 0.4f);
    }
    // Text or placeholder
    {
        float       tfs  = 14.0f;
        float       tpad = 8.0f;
        float       ty   = nfr.y + (nfr.h - tfs) * 0.5f;
        if (characterName.empty()) {
            sdf.drawScreen(batch, "Enter name...",
                           {nfr.x + tpad, ty},
                           tfs, {0.40f, 0.40f, 0.50f, 1.0f}, d + 0.5f);
        } else {
            sdf.drawScreen(batch, characterName,
                           {nfr.x + tpad, ty},
                           tfs, Color::white(), d + 0.5f);
        }
        // Cursor
        if (nameFieldFocused) {
            std::string before = characterName.substr(0, static_cast<size_t>(cursorPos));
            Vec2 cOff = sdf.measure(before, tfs);
            float cx_f = nfr.x + tpad + cOff.x;
            batch.drawRect({cx_f + 0.5f, nfr.y + nfr.h * 0.5f},
                           {1.0f, nfr.h - 8.0f}, Color::white(), d + 0.6f);
        }
    }

    // --- Next button ---
    Rect  nbr = nextButtonRect();
    batch.drawRect({nbr.x + nbr.w * 0.5f, nbr.y + nbr.h * 0.5f},
                   {nbr.w, nbr.h},
                   {0.2f, 0.6f, 0.9f, 1.0f}, d + 0.3f);
    {
        float bw = 1.5f;
        Color bc = {0.5f, 0.8f, 1.0f, 1.0f};
        float ih = nbr.h - bw * 2.0f;
        batch.drawRect({nbr.x + nbr.w * 0.5f, nbr.y + bw * 0.5f}, {nbr.w, bw}, bc, d + 0.4f);
        batch.drawRect({nbr.x + nbr.w * 0.5f, nbr.y + nbr.h - bw * 0.5f}, {nbr.w, bw}, bc, d + 0.4f);
        batch.drawRect({nbr.x + bw * 0.5f, nbr.y + nbr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
        batch.drawRect({nbr.x + nbr.w - bw * 0.5f, nbr.y + nbr.h * 0.5f}, {bw, ih}, bc, d + 0.4f);
    }
    {
        const char* nextLabel = "Next";
        float       nfs2      = 15.0f;
        Vec2        nsz2      = sdf.measure(nextLabel, nfs2);
        sdf.drawScreen(batch, nextLabel,
                       {nbr.x + (nbr.w - nsz2.x) * 0.5f,
                        nbr.y + (nbr.h - nsz2.y) * 0.5f},
                       nfs2, Color::white(), d + 0.5f);
    }

    // --- Status message ---
    if (!statusMessage.empty()) {
        float       sfs = 12.0f;
        Vec2        ssz = sdf.measure(statusMessage, sfs);
        Color       sc  = isError ? Color{1.0f, 0.35f, 0.35f, 1.0f}
                                  : Color{0.35f, 0.95f, 0.45f, 1.0f};
        sdf.drawScreen(batch, statusMessage,
                       {rp.x + (rp.w - ssz.x) * 0.5f,
                        nextButtonRect().y + nextButtonRect().h + 8.0f},
                       sfs, sc, d + 0.5f);
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

} // namespace fate
