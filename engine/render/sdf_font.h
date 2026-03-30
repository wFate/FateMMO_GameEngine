#pragma once
#include "engine/render/texture.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace fate {

// GlyphMetrics defined here to break circular include (sdf_font.h <-> sdf_text.h)
struct GlyphMetrics {
    float advance;
    float bearingX, bearingY;
    float width, height;
    float uvX, uvY, uvW, uvH; // UV rect in atlas (0-1 normalized)
};

struct SDFFont {
    enum class Type { MSDF, Bitmap };

    Type type = Type::MSDF;
    std::string name;
    std::shared_ptr<Texture> atlas;

    // MSDF-specific (populated from metrics JSON)
    std::unordered_map<uint32_t, GlyphMetrics> glyphs;
    float lineHeight  = 1.2f;
    float ascender    = 0.95f;
    float emSize      = 48.0f;
    float atlasWidth  = 512.0f;
    float atlasHeight = 512.0f;
    float pxRange     = 4.0f;

    // Bitmap-specific (populated from manifest)
    int glyphWidth  = 0;
    int glyphHeight = 0;
    int columns     = 16;
    int firstChar   = 32;
    int lastChar    = 126;
};

} // namespace fate
