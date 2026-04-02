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

class SDFText {
public:
    static SDFText& instance();

    bool init(const std::string& atlasPath, const std::string& metricsPath);
    void shutdown();

    void drawWorld(SpriteBatch& batch, const std::string& text, Vec2 position,
                   float fontSize, Color color = Color::white(), float depth = 50.0f,
                   TextStyle style = TextStyle::Normal);

    void drawScreen(SpriteBatch& batch, const std::string& text, Vec2 position,
                    float fontSize, Color color = Color::white(), float depth = 50.0f,
                    TextStyle style = TextStyle::Normal);

    Vec2 measure(const std::string& text, float fontSize) const;

    void setFontRegistry(FontRegistry* registry);

    void drawScreenEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color = Color::white(), float depth = 50.0f,
                      TextStyle style = TextStyle::Normal,
                      const std::string& fontName = "default");

    void drawWorldEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                     float fontSize, Color color = Color::white(), float depth = 50.0f,
                     TextStyle style = TextStyle::Normal,
                     const std::string& fontName = "default");

    Vec2 measureEx(const std::string& text, float fontSize,
                   const std::string& fontName = "default") const;

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

    FontRegistry* fontRegistry_ = nullptr;

    void loadMetrics(const std::string& jsonPath);
    void drawInternal(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color, float depth, TextStyle style, bool yDown);
    void drawBitmap(SpriteBatch& batch, const SDFFont& font, const std::string& text,
                    Vec2 position, float fontSize, Color color, float depth, bool yDown);
    Vec2 measureBitmap(const SDFFont& font, const std::string& text, float fontSize) const;
};

} // namespace fate
