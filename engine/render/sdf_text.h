#pragma once
#include "engine/core/types.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/gfx/types.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace fate {

enum class TextStyle : uint8_t {
    Normal   = 1,
    Outlined = 2,
    Glow     = 3,
    Shadow   = 4
};

struct GlyphMetrics {
    float advance;
    float bearingX, bearingY;
    float width, height;
    float uvX, uvY, uvW, uvH; // UV rect in atlas (0-1 normalized)
};

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

    unsigned int atlasTextureId() const;
    gfx::TextureHandle atlasGfxHandle() const { return atlasGfxHandle_; }
    static Mat4 screenProjection(int windowWidth, int windowHeight);

    static uint32_t decodeUTF8(const std::string& text, size_t& index);

private:
    SDFText() = default;
    unsigned int atlasTexId_ = 0;
    gfx::TextureHandle atlasGfxHandle_{};
    float atlasWidth_ = 512.0f, atlasHeight_ = 512.0f;
    float pxRange_ = 4.0f;
    float lineHeight_ = 1.2f;
    float ascender_ = 0.95f;
    float emSize_ = 48.0f;
    std::unordered_map<uint32_t, GlyphMetrics> glyphs_;

    void loadMetrics(const std::string& jsonPath);
    void drawInternal(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color, float depth, TextStyle style, bool yDown);
};

} // namespace fate
