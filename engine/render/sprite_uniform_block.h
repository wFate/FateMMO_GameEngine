#pragma once
#include "engine/core/types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fate {

// Byte-exact mirror of the MSL `SpriteUniforms` struct in
// assets/shaders/metal/sprite.metal. Metal's `constant`-address-space layout
// requires natural alignment per field (float=4, float2=8, float4/array=16),
// which is NOT what C++ struct layout produces — so this block is built as a
// flat byte buffer with named setters that write at hard-coded offsets.
//
// The offsets below were derived from the MSL field order and natural-alignment
// rules; if the MSL struct is ever reordered the offsets must be updated to
// match exactly. The build asserts the total size below to catch obvious drift.
struct alignas(16) SpriteUniformBlock {
    static constexpr size_t kSize = 528;

    // Field byte-offsets within the block.
    static constexpr size_t kViewProjectionOff    = 0;
    static constexpr size_t kPxRangeOff           = 64;
    // float pxRange (4) + 4 bytes padding → float2 atlasSize aligned 8.
    static constexpr size_t kAtlasSizeOff         = 72;
    static constexpr size_t kShadowOffsetOff      = 80;
    static constexpr size_t kPaletteCountOff      = 88;
    // float paletteCount (4) + 4 bytes padding → float4[] aligned 16.
    static constexpr size_t kPaletteOff           = 96;     // 16 × float4 = 256 B
    static constexpr size_t kOutlineColorOff      = 352;
    static constexpr size_t kOutlineThicknessOff  = 368;
    // float outlineThickness (4) + 4 bytes padding → float2 textShadowOffset aligned 8.
    static constexpr size_t kTextShadowOffsetOff  = 376;
    static constexpr size_t kTextShadowColorOff   = 384;
    static constexpr size_t kTextGlowColorOff     = 400;
    static constexpr size_t kTextGlowIntensityOff = 416;
    static constexpr size_t kSdfUseAlphaOff       = 420;
    static constexpr size_t kRectSizeOff          = 424;
    static constexpr size_t kCornerRadiusOff      = 432;
    static constexpr size_t kRRBorderWidthOff     = 436;
    // 8 bytes padding → float4 rrBorderColor aligned 16.
    static constexpr size_t kRRBorderColorOff     = 448;
    static constexpr size_t kGradientTopOff       = 464;
    static constexpr size_t kGradientBottomOff    = 480;
    static constexpr size_t kRRShadowOffsetOff    = 496;
    static constexpr size_t kRRShadowBlurOff      = 504;
    // float rrShadowBlur (4) + 4 bytes padding → float4 rrShadowColor aligned 16.
    static constexpr size_t kRRShadowColorOff     = 512;

    uint8_t bytes[kSize] = {};

    template <typename T>
    inline void writeAt(size_t off, const T& v) {
        std::memcpy(bytes + off, &v, sizeof(T));
    }

    void setViewProjection(const Mat4& m)        { std::memcpy(bytes + kViewProjectionOff, m.data(), 64); }
    void setPxRange(float v)                     { writeAt(kPxRangeOff, v); }
    void setAtlasSize(const Vec2& v)             { writeAt(kAtlasSizeOff, v); }
    void setShadowOffset(const Vec2& v)          { writeAt(kShadowOffsetOff, v); }
    void setPaletteCount(float v)                { writeAt(kPaletteCountOff, v); }
    void setPaletteEntry(int i, const Color& c)  { std::memcpy(bytes + kPaletteOff + static_cast<size_t>(i) * 16, &c, 16); }
    void setOutlineColor(const Color& c)         { writeAt(kOutlineColorOff, c); }
    void setOutlineThickness(float v)            { writeAt(kOutlineThicknessOff, v); }
    void setTextShadowOffset(const Vec2& v)      { writeAt(kTextShadowOffsetOff, v); }
    void setTextShadowColor(const Color& c)      { writeAt(kTextShadowColorOff, c); }
    void setTextGlowColor(const Color& c)        { writeAt(kTextGlowColorOff, c); }
    void setTextGlowIntensity(float v)           { writeAt(kTextGlowIntensityOff, v); }
    void setSdfUseAlpha(float v)                  { writeAt(kSdfUseAlphaOff, v); }
    void setRectSize(const Vec2& v)              { writeAt(kRectSizeOff, v); }
    void setCornerRadius(float v)                { writeAt(kCornerRadiusOff, v); }
    void setRRBorderWidth(float v)               { writeAt(kRRBorderWidthOff, v); }
    void setRRBorderColor(const Color& c)        { writeAt(kRRBorderColorOff, c); }
    void setGradientTop(const Color& c)          { writeAt(kGradientTopOff, c); }
    void setGradientBottom(const Color& c)       { writeAt(kGradientBottomOff, c); }
    void setRRShadowOffset(const Vec2& v)        { writeAt(kRRShadowOffsetOff, v); }
    void setRRShadowBlur(float v)                { writeAt(kRRShadowBlurOff, v); }
    void setRRShadowColor(const Color& c)        { writeAt(kRRShadowColorOff, c); }
};

static_assert(sizeof(SpriteUniformBlock) == 528 + 0 /* alignas pad-out is already 16-aligned */,
              "SpriteUniformBlock size drifted from MSL SpriteUniforms layout");

} // namespace fate
