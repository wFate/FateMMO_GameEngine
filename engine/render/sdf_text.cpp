#include "engine/render/sdf_text.h"
#include "engine/render/sdf_font_atlas.h"
#include "engine/render/gfx/device.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#else
#import <Metal/Metal.h>
#endif
#include "engine/core/logger.h"
#include "stb_image.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

namespace fate {

SDFText& SDFText::instance() {
    static SDFText s_instance;
    return s_instance;
}

bool SDFText::init(const std::string& atlasPath, const std::string& metricsPath) {
    // Ensure atlas + metrics exist (generate from system font if missing)
    SDFFontAtlas::generateIfMissing("C:/Windows/Fonts/consola.ttf", "assets/fonts", emSize_);
    SDFFontAtlas::generateIfMissing("assets/fonts/PressStart2P-Regular.ttf", "assets/fonts", emSize_, "press_start_2p");
    SDFFontAtlas::generateIfMissing("assets/fonts/PixelifySans-Regular.ttf", "assets/fonts", emSize_, "pixelify_sans");

    // Load atlas PNG via stb_image (4 channels: RGBA)
    int w = 0, h = 0, channels = 0;
    // Atlas UVs are computed in image-space (y-down from top). Loading
    // without a vertical flip keeps the UV coordinates correct on all
    // backends.  Regular sprite textures flip for GL elsewhere, but the
    // SDF atlas deliberately does NOT flip.
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(atlasPath.c_str(), &w, &h, &channels, 4);
    if (!data) {
        LOG_ERROR("SDFText", "Failed to load atlas image: %s", atlasPath.c_str());
        return false;
    }

    atlasWidth_  = static_cast<float>(w);
    atlasHeight_ = static_cast<float>(h);

    // Create texture via Device (for gfx handle / drawTexturedQuad path)
    auto& device = gfx::Device::instance();
    atlasGfxHandle_ = device.createTexture(w, h, gfx::TextureFormat::RGBA8, data);

    if (!atlasGfxHandle_.valid()) {
        stbi_image_free(data);
        LOG_ERROR("SDFText", "Device::createTexture failed for atlas");
        return false;
    }

#ifndef FATEMMO_METAL
    atlasTexId_ = device.resolveGLTexture(atlasGfxHandle_);

    // SDF atlas needs linear filtering (Device defaults to nearest)
    glBindTexture(GL_TEXTURE_2D, atlasTexId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

    stbi_image_free(data);

    // Load glyph metrics from JSON
    loadMetrics(metricsPath);
    activeGlyphs_ = &glyphs_;

    LOG_INFO("SDFText", "Initialized with %dx%d atlas, %zu glyphs",
             w, h, glyphs_.size());
    return true;
}

void SDFText::shutdown() {
    if (atlasGfxHandle_.valid()) {
        gfx::Device::instance().destroy(atlasGfxHandle_);
        atlasGfxHandle_ = {};
#ifndef FATEMMO_METAL
        atlasTexId_ = 0;
#endif
    }
    glyphs_.clear();
    activeGlyphs_ = nullptr;
}

void SDFText::loadMetrics(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_ERROR("SDFText", "Cannot open metrics file: %s", jsonPath.c_str());
        return;
    }

    json root = json::parse(file);

    // Atlas metadata
    bool yOriginBottom = false;
    if (root.contains("atlas")) {
        const auto& atlas = root["atlas"];
        atlasWidth_  = atlas.value("width",  512.0f);
        atlasHeight_ = atlas.value("height", 512.0f);
        pxRange_     = atlas.value("distanceRange", 4.0f);
        std::string yOrig = atlas.value("yOrigin", std::string("top"));
        yOriginBottom = (yOrig == "bottom");
    }

    // Font metrics
    if (root.contains("metrics")) {
        const auto& metrics = root["metrics"];
        emSize_     = metrics.value("emSize",     48.0f);
        lineHeight_ = metrics.value("lineHeight", 1.2f);
        ascender_   = metrics.value("ascender",   0.95f);
    }

    // Glyphs
    if (root.contains("glyphs")) {
        for (const auto& g : root["glyphs"]) {
            uint32_t unicode = g.value("unicode", 0u);
            float advance    = g.value("advance", 0.0f);

            GlyphMetrics gm{};
            gm.advance = advance;

            // planeBounds: glyph box in em units (y-up from baseline)
            if (g.contains("planeBounds")) {
                const auto& pb = g["planeBounds"];
                float pbLeft   = pb.value("left",   0.0f);
                float pbBottom = pb.value("bottom", 0.0f);
                float pbRight  = pb.value("right",  0.0f);
                float pbTop    = pb.value("top",    0.0f);

                gm.bearingX = pbLeft;
                gm.bearingY = pbTop;     // top of glyph above baseline (em units)
                gm.width    = pbRight - pbLeft;
                gm.height   = pbTop - pbBottom;
            }

            // atlasBounds: pixel rect in atlas
            if (g.contains("atlasBounds")) {
                const auto& ab = g["atlasBounds"];
                float abLeft   = ab.value("left",   0.0f);
                float abBottom = ab.value("bottom", 0.0f);
                float abRight  = ab.value("right",  0.0f);
                float abTop    = ab.value("top",    0.0f);

                // Normalize to 0-1 UV space
                gm.uvX = abLeft / atlasWidth_;
                gm.uvW = (abRight - abLeft) / atlasWidth_;
                gm.uvH = (abTop - abBottom) / atlasHeight_;
                if (yOriginBottom) {
                    // msdf-atlas-gen yOrigin:"bottom": flip Y so UV origin is top-left
                    gm.uvY = (atlasHeight_ - abTop) / atlasHeight_;
                } else {
                    // yOrigin:"top": atlasBounds already in image-space
                    gm.uvY = abBottom / atlasHeight_;
                }
            }

            glyphs_[unicode] = gm;
        }
    }
}

void SDFText::drawWorld(SpriteBatch& batch, const std::string& text, Vec2 position,
                        float fontSize, Color color, float depth, TextStyle style,
                        const TextLayout& layout) {
    drawInternal(batch, text, position, fontSize, color, depth, style, false, layout);
}

void SDFText::drawScreen(SpriteBatch& batch, const std::string& text, Vec2 position,
                         float fontSize, Color color, float depth, TextStyle style,
                         const TextLayout& layout) {
    drawInternal(batch, text, position, fontSize, color, depth, style, true, layout);
}

void SDFText::drawInternal(SpriteBatch& batch, const std::string& text, Vec2 position,
                           float fontSize, Color color, float depth, TextStyle style,
                           bool yDown, const TextLayout& layout) {
#ifdef FATEMMO_METAL
    if (!activeGlyphs_ || activeGlyphs_->empty() || !atlasGfxHandle_.valid()) return;
#else
    if (!activeGlyphs_ || activeGlyphs_->empty() || !atlasTexId_) {
        static bool logged = false;
        if (!logged) {
            LOG_ERROR("SDFText", "drawInternal BAIL: glyphs=%zu atlasTexId=%u",
                      activeGlyphs_ ? activeGlyphs_->size() : 0u, atlasTexId_);
            logged = true;
        }
        return;
    }
#endif

    const auto& glyphs = *activeGlyphs_;
    const float scale = fontSize;
    float penX = position.x;
    float penY = position.y;
    const float startX = position.x;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = decodeUTF8(text, i);

        if (cp == '\n') {
            penX = startX;
            penY += (yDown ? 1.0f : -1.0f) * lineHeight_ * fontSize * layout.lineHeight;
            continue;
        }

        auto it = glyphs.find(cp);
        if (it == glyphs.end()) {
            it = glyphs.find(32);
            if (it == glyphs.end()) continue;
        }

        const GlyphMetrics& gm = it->second;

        if (gm.width > 0.0f && gm.height > 0.0f) {
            float quadW = gm.width  * scale;
            float quadH = gm.height * scale;

            float quadX = penX + gm.bearingX * scale + quadW * 0.5f;
            float quadY;
            if (yDown) {
                quadY = penY + (ascender_ - gm.bearingY) * fontSize + quadH * 0.5f;
            } else {
                quadY = penY + gm.bearingY * scale - quadH * 0.5f;
            }

            SpriteDrawParams params;
            params.position  = {quadX, quadY};
            params.size      = {quadW, quadH};
            params.sourceRect = {gm.uvX, gm.uvY, gm.uvW, gm.uvH};
            params.color     = color;
            params.depth     = depth;
            // World-space (y-up): quad bottom is -hh with v0 (glyph top), which
            // maps glyph top to screen bottom — upside down.  Flip UV Y to fix.
            if (!yDown) params.flipY = true;
            // Route through MSDF shader (renderType 1-4 based on TextStyle)
            float rt = static_cast<float>(style);
#ifdef FATEMMO_METAL
            batch.drawTexturedQuad(atlasGfxHandle_, 0, params, rt);
#else
            batch.drawTexturedQuad(atlasGfxHandle_, atlasTexId_, params, rt);
#endif
        }

        penX += gm.advance * scale + layout.letterSpacing;
    }
}

Vec2 SDFText::measure(const std::string& text, float fontSize, const TextLayout& layout) const {
    if (!activeGlyphs_) return {0.0f, 0.0f};
    const auto& glyphs = *activeGlyphs_;
    const float scale = fontSize;
    float totalWidth = 0.0f;
    float maxWidth   = 0.0f;
    int lines = 1;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = decodeUTF8(text, i);

        if (cp == '\n') {
            if (totalWidth > maxWidth) maxWidth = totalWidth;
            totalWidth = 0.0f;
            lines++;
            continue;
        }

        auto it = glyphs.find(cp);
        if (it != glyphs.end()) {
            totalWidth += it->second.advance * scale + layout.letterSpacing;
        }
    }

    if (totalWidth > maxWidth) maxWidth = totalWidth;
    return {maxWidth, lineHeight_ * fontSize * layout.lineHeight * lines};
}

void SDFText::setFontRegistry(FontRegistry* registry) {
    fontRegistry_ = registry;
}

void SDFText::drawScreenEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                           float fontSize, Color color, float depth, TextStyle style,
                           const std::string& fontName, const TextLayout& layout) {
    if (fontRegistry_) {
        SDFFont* font = fontRegistry_->getFont(fontName);
        if (font && font->atlas && !font->glyphs.empty()) {
            if (font->type == SDFFont::Type::Bitmap) {
                drawBitmap(batch, *font, text, position, fontSize, color, depth, true, layout);
                return;
            }
            // MSDF font from registry: temporarily swap in the font's data
            auto savedActiveGlyphs = activeGlyphs_;
#ifndef FATEMMO_METAL
            auto savedAtlasTexId  = atlasTexId_;
#endif
            auto savedAtlasGfx    = atlasGfxHandle_;
            auto savedAtlasWidth  = atlasWidth_;
            auto savedAtlasHeight = atlasHeight_;
            auto savedPxRange     = pxRange_;
            auto savedLineHeight  = lineHeight_;
            auto savedAscender    = ascender_;
            auto savedEmSize      = emSize_;

            activeGlyphs_ = &font->glyphs;
#ifndef FATEMMO_METAL
            atlasTexId_  = font->atlas->id();
#endif
            atlasGfxHandle_ = font->atlas->gfxHandle();
            atlasWidth_  = font->atlasWidth;
            atlasHeight_ = font->atlasHeight;
            pxRange_     = font->pxRange;
            lineHeight_  = font->lineHeight;
            ascender_    = font->ascender;
            emSize_      = font->emSize;

            drawInternal(batch, text, position, fontSize, color, depth, style, true, layout);

            activeGlyphs_   = savedActiveGlyphs;
#ifndef FATEMMO_METAL
            atlasTexId_     = savedAtlasTexId;
#endif
            atlasGfxHandle_ = savedAtlasGfx;
            atlasWidth_     = savedAtlasWidth;
            atlasHeight_    = savedAtlasHeight;
            pxRange_        = savedPxRange;
            lineHeight_     = savedLineHeight;
            ascender_       = savedAscender;
            emSize_         = savedEmSize;
            return;
        }
    }
    // Fallback: use default font
    drawInternal(batch, text, position, fontSize, color, depth, style, true, layout);
}

void SDFText::drawScreenEffects(SpriteBatch& batch, const std::string& text, Vec2 position,
                                float fontSize, Color color, float depth,
                                TextStyle style, const std::string& fontName,
                                const TextEffects& fx, const TextLayout& layout) {
    // Convert the pixel-space TextEffects into shader-space uniforms. The
    // outlineWidth field is the SDF expansion past the glyph edge (0..0.5);
    // the shader expects the threshold (0.5 = no outline, lower = thicker).
    TextEffectUniforms u{};
    if (fx.outlineColor.a > 0.0f) {
        u.outlineColor      = fx.outlineColor;
        const float w = fx.outlineWidth;
        u.outlineThickness  = (w > 0.0f) ? std::max(0.5f - w, 0.05f) : 0.5f;
    } else {
        // No outline requested — push fill all the way to the glyph edge.
        u.outlineColor      = Color{0.0f, 0.0f, 0.0f, 0.0f};
        u.outlineThickness  = 0.5f;
    }
    // shadowOffset on TextEffects is already in UV space (matches the editor's
    // 0.001 step / +/-0.01 range — see ui_editor_panel.cpp:5096).
    u.shadowOffsetUv = (fx.shadowColor.a > 0.0f)
        ? fx.shadowOffset
        : Vec2{0.0f, 0.0f};
    u.shadowColor    = fx.shadowColor;
    u.glowColor      = fx.glowColor;
    u.glowIntensity  = (fx.glowColor.a > 0.0f) ? fx.glowRadius : 0.0f;

    batch.setTextEffectUniforms(u);
    drawScreenEx(batch, text, position, fontSize, color, depth, style, fontName, layout);
    batch.resetTextEffectUniforms();
}

void SDFText::drawWorldEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                          float fontSize, Color color, float depth, TextStyle style,
                          const std::string& fontName, const TextLayout& layout) {
    if (fontRegistry_) {
        SDFFont* font = fontRegistry_->getFont(fontName);
        if (font && font->atlas && !font->glyphs.empty()) {
            if (font->type == SDFFont::Type::Bitmap) {
                drawBitmap(batch, *font, text, position, fontSize, color, depth, false, layout);
                return;
            }
            // MSDF font from registry: temporarily swap in the font's data
            auto savedActiveGlyphs = activeGlyphs_;
#ifndef FATEMMO_METAL
            auto savedAtlasTexId  = atlasTexId_;
#endif
            auto savedAtlasGfx    = atlasGfxHandle_;
            auto savedAtlasWidth  = atlasWidth_;
            auto savedAtlasHeight = atlasHeight_;
            auto savedPxRange     = pxRange_;
            auto savedLineHeight  = lineHeight_;
            auto savedAscender    = ascender_;
            auto savedEmSize      = emSize_;

            activeGlyphs_ = &font->glyphs;
#ifndef FATEMMO_METAL
            atlasTexId_  = font->atlas->id();
#endif
            atlasGfxHandle_ = font->atlas->gfxHandle();
            atlasWidth_  = font->atlasWidth;
            atlasHeight_ = font->atlasHeight;
            pxRange_     = font->pxRange;
            lineHeight_  = font->lineHeight;
            ascender_    = font->ascender;
            emSize_      = font->emSize;

            drawInternal(batch, text, position, fontSize, color, depth, style, false, layout);

            activeGlyphs_   = savedActiveGlyphs;
#ifndef FATEMMO_METAL
            atlasTexId_     = savedAtlasTexId;
#endif
            atlasGfxHandle_ = savedAtlasGfx;
            atlasWidth_     = savedAtlasWidth;
            atlasHeight_    = savedAtlasHeight;
            pxRange_        = savedPxRange;
            lineHeight_     = savedLineHeight;
            ascender_       = savedAscender;
            emSize_         = savedEmSize;
            return;
        }
    }
    // Fallback: use default font
    drawInternal(batch, text, position, fontSize, color, depth, style, false, layout);
}

Vec2 SDFText::measureEx(const std::string& text, float fontSize,
                        const std::string& fontName, const TextLayout& layout) const {
    if (fontRegistry_) {
        SDFFont* font = fontRegistry_->getFont(fontName);
        if (font && !font->glyphs.empty()) {
            if (font->type == SDFFont::Type::Bitmap) {
                return measureBitmap(*font, text, fontSize, layout);
            }
            // MSDF font from registry: compute using font's glyphs/metrics
            const float scale = fontSize;
            float totalWidth = 0.0f;
            float maxWidth   = 0.0f;
            int lines = 1;

            size_t i = 0;
            while (i < text.size()) {
                uint32_t cp = decodeUTF8(text, i);
                if (cp == '\n') {
                    if (totalWidth > maxWidth) maxWidth = totalWidth;
                    totalWidth = 0.0f;
                    lines++;
                    continue;
                }
                auto it = font->glyphs.find(cp);
                if (it != font->glyphs.end()) {
                    totalWidth += it->second.advance * scale + layout.letterSpacing;
                }
            }
            if (totalWidth > maxWidth) maxWidth = totalWidth;
            return {maxWidth, font->lineHeight * fontSize * layout.lineHeight * lines};
        }
    }
    return measure(text, fontSize, layout);
}

void SDFText::drawBitmap(SpriteBatch& batch, const SDFFont& font, const std::string& text,
                         Vec2 position, float fontSize, Color color, float depth, bool yDown,
                         const TextLayout& layout) {
    if (!font.atlas || font.glyphHeight <= 0 || font.columns <= 0) return;

    int scaleI = static_cast<int>(std::round(fontSize / font.glyphHeight));
    int scale = (scaleI > 1) ? scaleI : 1;
    float scaledW = static_cast<float>(font.glyphWidth * scale);
    float scaledH = static_cast<float>(font.glyphHeight * scale);
    float atlasW  = static_cast<float>(font.atlas->width());
    float atlasH  = static_cast<float>(font.atlas->height());
    float cellU   = static_cast<float>(font.glyphWidth) / atlasW;
    float cellV   = static_cast<float>(font.glyphHeight) / atlasH;

    float penX = position.x;
    float penY = position.y;
    float startX = position.x;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = decodeUTF8(text, i);

        if (cp == '\n') {
            penX = startX;
            penY += (yDown ? scaledH : -scaledH) * layout.lineHeight;
            continue;
        }

        int ch = static_cast<int>(cp);
        if (ch < font.firstChar || ch > font.lastChar) {
            penX += scaledW + layout.letterSpacing;
            continue;
        }

        int idx = ch - font.firstChar;
        int col = idx % font.columns;
        int row = idx / font.columns;

        float u = col * cellU;
        float v = row * cellV;

        SpriteDrawParams params;
        params.position  = {penX + scaledW * 0.5f, penY + scaledH * 0.5f};
        params.size      = {scaledW, scaledH};
        params.sourceRect = {u, v, cellU, cellV};
        params.color     = color;
        params.depth     = depth;

        batch.drawTexturedQuad(font.atlas->gfxHandle(), font.atlas->id(), params, 0.0f);

        penX += scaledW + layout.letterSpacing;
    }
}

Vec2 SDFText::measureBitmap(const SDFFont& font, const std::string& text, float fontSize,
                            const TextLayout& layout) const {
    if (font.glyphHeight <= 0) return {0.0f, 0.0f};

    int scaleI = static_cast<int>(std::round(fontSize / font.glyphHeight));
    int scale = (scaleI > 1) ? scaleI : 1;
    float scaledW = static_cast<float>(font.glyphWidth * scale);
    float scaledH = static_cast<float>(font.glyphHeight * scale);

    float totalWidth = 0.0f;
    float maxWidth   = 0.0f;
    int lines = 1;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = decodeUTF8(text, i);
        if (cp == '\n') {
            if (totalWidth > maxWidth) maxWidth = totalWidth;
            totalWidth = 0.0f;
            lines++;
            continue;
        }
        totalWidth += scaledW + layout.letterSpacing;
    }
    if (totalWidth > maxWidth) maxWidth = totalWidth;
    return {maxWidth, scaledH * layout.lineHeight * lines};
}

unsigned int SDFText::atlasTextureId() const {
#ifdef FATEMMO_METAL
    return 0;
#else
    return atlasTexId_;
#endif
}

Mat4 SDFText::screenProjection(int windowWidth, int windowHeight) {
    return Mat4::ortho(0.0f, static_cast<float>(windowWidth),
                       static_cast<float>(windowHeight), 0.0f,
                       -1.0f, 1.0f);
}

uint32_t SDFText::decodeUTF8(const std::string& text, size_t& index) {
    if (index >= text.size()) return 0;

    uint32_t cp = 0;
    unsigned char c = static_cast<unsigned char>(text[index]);

    if (c < 0x80) {
        // 0xxxxxxx - single byte (ASCII)
        cp = c;
        index += 1;
    } else if ((c & 0xE0) == 0xC0) {
        // 110xxxxx 10xxxxxx - two bytes
        cp = c & 0x1F;
        if (index + 1 < text.size()) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[index + 1]) & 0x3F);
        }
        index += 2;
    } else if ((c & 0xF0) == 0xE0) {
        // 1110xxxx 10xxxxxx 10xxxxxx - three bytes
        cp = c & 0x0F;
        if (index + 2 < text.size()) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[index + 1]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(text[index + 2]) & 0x3F);
        }
        index += 3;
    } else if ((c & 0xF8) == 0xF0) {
        // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx - four bytes
        cp = c & 0x07;
        if (index + 3 < text.size()) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[index + 1]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(text[index + 2]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(text[index + 3]) & 0x3F);
        }
        index += 4;
    } else {
        // Invalid byte -- skip it
        index += 1;
        cp = 0xFFFD; // replacement character
    }

    return cp;
}

} // namespace fate
