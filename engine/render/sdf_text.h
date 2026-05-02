#pragma once
#include "engine/core/types.h"
#include "engine/render/text_style.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/font_registry.h"
#include "engine/render/gfx/types.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace fate {

// GlyphMetrics is defined in sdf_font.h
#include "engine/render/sdf_font.h"

// Layout modifiers for glyph placement. `lineHeight` multiplies the font's
// native line-height (1.0 = default). `letterSpacing` adds pixels between each
// glyph's advance (0 = default, negative to tighten). The identity-default
// keeps every existing call site behaving exactly as before.
struct TextLayout {
    float lineHeight    = 1.0f;
    float letterSpacing = 0.0f;
};

class SDFText {
public:
    static SDFText& instance();

    bool init(const std::string& atlasPath, const std::string& metricsPath);
    void shutdown();

    void drawWorld(SpriteBatch& batch, const std::string& text, Vec2 position,
                   float fontSize, Color color = Color::white(), float depth = 50.0f,
                   TextStyle style = TextStyle::Normal,
                   const TextLayout& layout = {});

    void drawScreen(SpriteBatch& batch, const std::string& text, Vec2 position,
                    float fontSize, Color color = Color::white(), float depth = 50.0f,
                    TextStyle style = TextStyle::Normal,
                    const TextLayout& layout = {});

    Vec2 measure(const std::string& text, float fontSize,
                 const TextLayout& layout = {}) const;

    void setFontRegistry(FontRegistry* registry);

    void drawScreenEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color = Color::white(), float depth = 50.0f,
                      TextStyle style = TextStyle::Normal,
                      const std::string& fontName = "default",
                      const TextLayout& layout = {});

    // Draw screen-space text with configurable outline / shadow / glow.
    // Pushes shader-side text-effect uniforms via SpriteBatch, draws, then
    // resets. Use this from widgets that have a UIStyle and want the chrome's
    // textEffects to actually affect rendering.
    void drawScreenEffects(SpriteBatch& batch, const std::string& text, Vec2 position,
                           float fontSize, Color color, float depth,
                           TextStyle style, const std::string& fontName,
                           const TextEffects& fx,
                           const TextLayout& layout = {});

    void drawWorldEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                     float fontSize, Color color = Color::white(), float depth = 50.0f,
                     TextStyle style = TextStyle::Normal,
                     const std::string& fontName = "default",
                     const TextLayout& layout = {});

    Vec2 measureEx(const std::string& text, float fontSize,
                   const std::string& fontName = "default",
                   const TextLayout& layout = {}) const;

    unsigned int atlasTextureId() const;
    gfx::TextureHandle atlasGfxHandle() const { return atlasGfxHandle_; }
    static Mat4 screenProjection(int windowWidth, int windowHeight);

    static uint32_t decodeUTF8(const std::string& text, size_t& index);

private:
    SDFText() = default;
#ifndef FATEMMO_METAL
    unsigned int atlasTexId_ = 0;
#endif
    gfx::TextureHandle atlasGfxHandle_{};
    float atlasWidth_ = 512.0f, atlasHeight_ = 512.0f;
    float pxRange_ = 4.0f;
    float lineHeight_ = 1.2f;
    float ascender_ = 0.95f;
    float emSize_ = 48.0f;
    std::unordered_map<uint32_t, GlyphMetrics> glyphs_;
    const std::unordered_map<uint32_t, GlyphMetrics>* activeGlyphs_ = nullptr;

    FontRegistry* fontRegistry_ = nullptr;

    void loadMetrics(const std::string& jsonPath);
    void drawInternal(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color, float depth, TextStyle style, bool yDown,
                      const TextLayout& layout = {});
    void drawBitmap(SpriteBatch& batch, const SDFFont& font, const std::string& text,
                    Vec2 position, float fontSize, Color color, float depth, bool yDown,
                    const TextLayout& layout = {});
    Vec2 measureBitmap(const SDFFont& font, const std::string& text, float fontSize,
                       const TextLayout& layout = {}) const;
};

} // namespace fate
