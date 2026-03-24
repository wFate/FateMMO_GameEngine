#include "engine/render/loading_screen.h"
#include "engine/render/texture.h"
#include "engine/core/logger.h"
#include <cstdio>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

namespace fate {

void LoadingScreen::begin(const std::string& sceneName, int, int) {
    sceneName_ = sceneName;
    displayName_ = sceneName;
    for (auto& c : displayName_) { if (c == '_') c = ' '; }
    if (!displayName_.empty()) displayName_[0] = static_cast<char>(toupper(displayName_[0]));

    std::string artPath = "assets/ui/loading/" + sceneName + ".png";
    if (fs::exists(artPath)) {
        backgroundTex_ = TextureCache::instance().load(artPath);
    } else {
        backgroundTex_ = nullptr;
    }
}

void LoadingScreen::render(SpriteBatch& batch, SDFText& sdf, float progress,
                            int screenWidth, int screenHeight) {
    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    // Background
    if (backgroundTex_) {
        SpriteDrawParams params;
        params.position = {sw * 0.5f, sh * 0.5f};
        params.size = {sw, sh};
        params.sourceRect = {0, 0, 1, 1};
        params.color = Color::white();
        params.depth = -1.0f;
        batch.draw(backgroundTex_, params);
    } else {
        Color bg = {0.102f, 0.102f, 0.180f, 1.0f};
        batch.drawRect({sw * 0.5f, sh * 0.5f}, {sw, sh}, bg, -1.0f);
    }

    // Progress bar (bottom, TWOM-style)
    float barPadX = sw * 0.05f;
    float barHeight = sh * 0.025f;
    float barY = sh - barHeight * 2.5f;
    float barWidth = sw - barPadX * 2.0f;
    float barCX = sw * 0.5f;
    float barCY = barY + barHeight * 0.5f;

    batch.drawRect({barCX, barCY}, {barWidth, barHeight}, {0, 0, 0, 0.6f}, 0.0f);

    float fillW = barWidth * progress;
    if (fillW > 0.0f) {
        float fillCX = barPadX + fillW * 0.5f;
        batch.drawRect({fillCX, barCY}, {fillW, barHeight}, {0.85f, 0.75f, 0.4f, 1.0f}, 0.1f);
    }

    // Zone name
    float fontSize = sh * 0.035f;
    Vec2 nameSize = sdf.measure(displayName_, fontSize);
    float nameX = (sw - nameSize.x) * 0.5f;
    float nameY = barY - nameSize.y - sh * 0.02f;
    sdf.drawScreen(batch, displayName_, {nameX + 1.5f, nameY + 1.5f}, fontSize, {0, 0, 0, 0.8f}, 0.3f);
    sdf.drawScreen(batch, displayName_, {nameX, nameY}, fontSize, {1, 1, 1, 1}, 0.4f);

    // Percentage
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>(progress * 100.0f));
    std::string pctStr(pctBuf);
    float pctFont = barHeight * 0.8f;
    Vec2 pctSize = sdf.measure(pctStr, pctFont);
    sdf.drawScreen(batch, pctStr, {(sw - pctSize.x) * 0.5f, barCY - pctSize.y * 0.5f},
                   pctFont, {1, 1, 1, 1}, 0.5f);
}

void LoadingScreen::end() {
    backgroundTex_ = nullptr;
    sceneName_.clear();
    displayName_.clear();
}

} // namespace fate
