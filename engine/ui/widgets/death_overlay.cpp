#include "engine/ui/widgets/death_overlay.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <string>

namespace fate {

DeathOverlay::DeathOverlay(const std::string& id) : UINode(id, "death_overlay") {
    visible_ = false;
}

void DeathOverlay::onDeath(int32_t xp, int32_t honor, float timer, uint8_t source) {
    xpLost = xp;
    honorLost = honor;
    countdown = timer;
    deathSource = source;
    respawnPending = false;
    active = true;
    setVisible(true);
}

void DeathOverlay::update(float dt) {
    if (!active) return;
    if (countdown > 0.0f) {
        countdown -= dt;
        if (countdown < 0.0f) countdown = 0.0f;
    }
}

void DeathOverlay::reset() {
    xpLost = 0;
    honorLost = 0;
    countdown = 0.0f;
    respawnPending = false;
    deathSource = 0;
    active = false;
    pressedBtn_ = -1;
    townBtnRect_ = {};
    spawnBtnRect_ = {};
    phoenixBtnRect_ = {};
    setVisible(false);
}

bool DeathOverlay::onPress(const Vec2& localPos) {
    pressedBtn_ = -1;

    if (townBtnRect_.w > 0.0f && townBtnRect_.contains(localPos)) {
        pressedBtn_ = 0;
    } else if (spawnBtnRect_.w > 0.0f && spawnBtnRect_.contains(localPos)) {
        pressedBtn_ = 1;
    } else if (phoenixBtnRect_.w > 0.0f && phoenixBtnRect_.contains(localPos)) {
        pressedBtn_ = 2;
    }

    return true; // modal -- always consume
}

void DeathOverlay::onRelease(const Vec2& localPos) {
    if (respawnPending) {
        pressedBtn_ = -1;
        return;
    }

    if (pressedBtn_ == 0 && townBtnRect_.contains(localPos)) {
        respawnPending = true;
        if (onRespawnRequested) onRespawnRequested(0); // town
    } else if (pressedBtn_ == 1 && spawnBtnRect_.contains(localPos)) {
        respawnPending = true;
        if (onRespawnRequested) onRespawnRequested(1); // spawn
    } else if (pressedBtn_ == 2 && phoenixBtnRect_.contains(localPos)) {
        respawnPending = true;
        if (onRespawnRequested) onRespawnRequested(2); // phoenix / here
    }

    pressedBtn_ = -1;
}

void DeathOverlay::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Dark fullscreen overlay
    Color overlayBg(0.0f, 0.0f, 0.0f, 0.75f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, overlayBg, d);

    // Layout from vertical center
    float centerX = rect.w * 0.5f;
    float cursorY = rect.h * 0.25f; // start 25% from top

    // "You have died" title
    {
        Color titleColor(0.9f, 0.2f, 0.2f, 1.0f);
        float titleSize = 22.0f;
        std::string title = "You have died";
        Vec2 ts = sdf.measure(title, titleSize);
        float tx = rect.x + (centerX - ts.x * 0.5f);
        float ty = rect.y + cursorY;
        sdf.drawScreen(batch, title, {tx, ty}, titleSize, titleColor, d + 0.2f);
        cursorY += ts.y + 16.0f;
    }

    // XP loss line
    if (xpLost > 0) {
        Color white = Color::white();
        float fontSize = 13.0f;
        std::string line = "XP Lost: " + std::to_string(xpLost);
        Vec2 ts = sdf.measure(line, fontSize);
        float tx = rect.x + (centerX - ts.x * 0.5f);
        float ty = rect.y + cursorY;
        sdf.drawScreen(batch, line, {tx, ty}, fontSize, white, d + 0.2f);
        cursorY += ts.y + 6.0f;
    }

    // Honor loss line
    if (honorLost > 0) {
        Color white = Color::white();
        float fontSize = 13.0f;
        std::string line = "Honor Lost: " + std::to_string(honorLost);
        Vec2 ts = sdf.measure(line, fontSize);
        float tx = rect.x + (centerX - ts.x * 0.5f);
        float ty = rect.y + cursorY;
        sdf.drawScreen(batch, line, {tx, ty}, fontSize, white, d + 0.2f);
        cursorY += ts.y + 6.0f;
    }

    // Countdown timer
    if (countdown > 0.0f) {
        Color gold(0.85f, 0.75f, 0.35f, 1.0f);
        float fontSize = 14.0f;
        int secs = static_cast<int>(countdown) + 1;
        std::string line = "Respawn in: " + std::to_string(secs) + "s";
        Vec2 ts = sdf.measure(line, fontSize);
        float tx = rect.x + (centerX - ts.x * 0.5f);
        float ty = rect.y + cursorY;
        sdf.drawScreen(batch, line, {tx, ty}, fontSize, gold, d + 0.2f);
        cursorY += ts.y + 16.0f;
    } else {
        cursorY += 16.0f;
    }

    // Button constants
    float btnW = 200.0f;
    float btnH = 36.0f;
    float btnSpacing = 8.0f;
    Color btnBg(0.12f, 0.10f, 0.08f, 0.95f);
    Color btnBorder(0.6f, 0.5f, 0.25f, 1.0f);
    Color btnText = Color::white();
    float btnFontSize = 13.0f;
    float borderW = 2.0f;

    auto drawButton = [&](const std::string& label, Rect& outRect) {
        float bx = centerX - btnW * 0.5f;
        float by = cursorY;
        outRect = {bx, by, btnW, btnH};

        float absX = rect.x + bx;
        float absY = rect.y + by;

        // Button background
        batch.drawRect({absX + btnW * 0.5f, absY + btnH * 0.5f},
                       {btnW, btnH}, btnBg, d + 0.1f);

        // Border (top, bottom, left, right)
        float innerH = btnH - borderW * 2.0f;
        batch.drawRect({absX + btnW * 0.5f, absY + borderW * 0.5f}, {btnW, borderW}, btnBorder, d + 0.15f);
        batch.drawRect({absX + btnW * 0.5f, absY + btnH - borderW * 0.5f}, {btnW, borderW}, btnBorder, d + 0.15f);
        batch.drawRect({absX + borderW * 0.5f, absY + btnH * 0.5f}, {borderW, innerH}, btnBorder, d + 0.15f);
        batch.drawRect({absX + btnW - borderW * 0.5f, absY + btnH * 0.5f}, {borderW, innerH}, btnBorder, d + 0.15f);

        // Button text centered
        Vec2 ts = sdf.measure(label, btnFontSize);
        float tx = absX + (btnW - ts.x) * 0.5f;
        float ty = absY + (btnH - ts.y) * 0.5f;
        sdf.drawScreen(batch, label, {tx, ty}, btnFontSize, btnText, d + 0.25f);

        cursorY += btnH + btnSpacing;
    };

    // Always show "Respawn in Town"
    drawButton("Respawn in Town", townBtnRect_);

    // Only show spawn/phoenix options for non-Aurora (deathSource == 0) deaths
    if (deathSource == 0) {
        drawButton("Respawn at Spawn", spawnBtnRect_);
        drawButton("Respawn Here", phoenixBtnRect_);
    } else {
        spawnBtnRect_ = {};
        phoenixBtnRect_ = {};
    }

    renderChildren(batch, sdf);
}

} // namespace fate
