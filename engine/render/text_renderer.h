#pragma once
#include "engine/core/types.h"
#include "engine/render/sprite_batch.h"
#include <string>
#include <memory>
#include <unordered_map>

namespace fate {

// Baked character info for one glyph
struct GlyphInfo {
    float u0, v0, u1, v1; // UV coords in atlas
    float xoff, yoff;      // offset from cursor when rendering
    float xadvance;        // how far to advance cursor
    int width, height;     // pixel size of glyph
};

// A loaded font at a specific pixel size
class Font {
public:
    Font() = default;
    ~Font();

    bool loadFromFile(const std::string& path, float pixelSize);

    const GlyphInfo* getGlyph(char c) const;
    float lineHeight() const { return lineHeight_; }
    float pixelSize() const { return pixelSize_; }
    unsigned int atlasTextureId() const { return atlasTexId_; }

    // Measure text dimensions without rendering
    Vec2 measureText(const std::string& text) const;

private:
    std::unordered_map<char, GlyphInfo> glyphs_;
    unsigned int atlasTexId_ = 0;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;
    float pixelSize_ = 0.0f;
    float lineHeight_ = 0.0f;
};

// Renders text using SpriteBatch
class TextRenderer {
public:
    static TextRenderer& instance() {
        static TextRenderer s_instance;
        return s_instance;
    }

    bool init(const std::string& defaultFontPath = "C:/Windows/Fonts/consola.ttf",
              float defaultSize = 16.0f);
    void shutdown();

    // Draw text in screen space (Y-down, used by HUD — origin top-left)
    void drawWorld(SpriteBatch& batch, const std::string& text,
                   const Vec2& position, const Color& color = Color::white(),
                   float scale = 1.0f, float depth = 50.0f);

    // Draw text in world/camera space (Y-up — for floating damage text, nameplates)
    void drawWorldYUp(SpriteBatch& batch, const std::string& text,
                      const Vec2& position, const Color& color = Color::white(),
                      float scale = 1.0f, float depth = 50.0f);

    // Draw text in screen space (fixed on screen, not affected by camera)
    // Call between batch.begin(screenProjection) and batch.end()
    void drawScreen(SpriteBatch& batch, const std::string& text,
                    const Vec2& position, const Color& color = Color::white(),
                    float scale = 1.0f, float depth = 50.0f);

    // Load additional font sizes
    Font* loadFont(const std::string& path, float pixelSize);

    Font* defaultFont() { return defaultFont_.get(); }

    // Get screen-space projection matrix (origin top-left)
    static Mat4 screenProjection(int windowWidth, int windowHeight);

private:
    TextRenderer() = default;
    std::unique_ptr<Font> defaultFont_;
    std::unordered_map<std::string, std::unique_ptr<Font>> fontCache_;
};

} // namespace fate
