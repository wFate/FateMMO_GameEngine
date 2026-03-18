#include "engine/render/sdf_text.h"
#include "engine/render/sdf_font_atlas.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"
#include "stb_image.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace fate {

SDFText& SDFText::instance() {
    static SDFText s_instance;
    return s_instance;
}

bool SDFText::init(const std::string& atlasPath, const std::string& metricsPath) {
    // Ensure atlas + metrics exist (generate from system font if missing)
    SDFFontAtlas::generateIfMissing("C:/Windows/Fonts/consola.ttf", "assets/fonts", emSize_);

    // Load atlas PNG via stb_image (4 channels: RGBA)
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(atlasPath.c_str(), &w, &h, &channels, 4);
    if (!data) {
        LOG_ERROR("SDFText", "Failed to load atlas image: %s", atlasPath.c_str());
        return false;
    }

    atlasWidth_  = static_cast<float>(w);
    atlasHeight_ = static_cast<float>(h);

    // Create texture via Device
    auto& device = gfx::Device::instance();
    atlasGfxHandle_ = device.createTexture(w, h, gfx::TextureFormat::RGBA8, data);
    stbi_image_free(data);

    if (!atlasGfxHandle_.valid()) {
        LOG_ERROR("SDFText", "Device::createTexture failed for atlas");
        return false;
    }

    atlasTexId_ = device.resolveGLTexture(atlasGfxHandle_);

    // SDF atlas needs linear filtering (Device defaults to nearest)
    glBindTexture(GL_TEXTURE_2D, atlasTexId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Load glyph metrics from JSON
    loadMetrics(metricsPath);

    LOG_INFO("SDFText", "Initialized with %dx%d atlas, %zu glyphs",
             w, h, glyphs_.size());
    return true;
}

void SDFText::shutdown() {
    if (atlasGfxHandle_.valid()) {
        gfx::Device::instance().destroy(atlasGfxHandle_);
        atlasGfxHandle_ = {};
        atlasTexId_ = 0;
    }
    glyphs_.clear();
}

void SDFText::loadMetrics(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_ERROR("SDFText", "Cannot open metrics file: %s", jsonPath.c_str());
        return;
    }

    json root = json::parse(file);

    // Atlas metadata
    if (root.contains("atlas")) {
        const auto& atlas = root["atlas"];
        atlasWidth_  = atlas.value("width",  512.0f);
        atlasHeight_ = atlas.value("height", 512.0f);
        pxRange_     = atlas.value("distanceRange", 4.0f);
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
                gm.uvX = abLeft   / atlasWidth_;
                gm.uvY = abBottom / atlasHeight_;  // top of glyph in atlas (yOrigin:top)
                gm.uvW = (abRight - abLeft) / atlasWidth_;
                gm.uvH = (abTop - abBottom) / atlasHeight_;
            }

            glyphs_[unicode] = gm;
        }
    }
}

void SDFText::drawWorld(SpriteBatch& batch, const std::string& text, Vec2 position,
                        float fontSize, Color color, float depth, TextStyle style) {
    drawInternal(batch, text, position, fontSize, color, depth, style, false);
}

void SDFText::drawScreen(SpriteBatch& batch, const std::string& text, Vec2 position,
                         float fontSize, Color color, float depth, TextStyle style) {
    drawInternal(batch, text, position, fontSize, color, depth, style, true);
}

void SDFText::drawInternal(SpriteBatch& batch, const std::string& text, Vec2 position,
                           float fontSize, Color color, float depth, TextStyle style,
                           bool yDown) {
    // planeBounds are in em units (normalized by emSize_), so multiplying
    // by fontSize directly converts to pixel size at the requested font size.
    const float scale = fontSize;
    float penX = position.x;
    float penY = position.y;
    const float startX = position.x;

    // DEBUG: log once per second
    static int sdfDbgFrame = 0;
    bool sdfDbgLog = (sdfDbgFrame++ % 600 == 0);
    int sdfEmitted = 0, sdfSkipped = 0;

    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = decodeUTF8(text, i);

        // Handle newlines
        if (cp == '\n') {
            penX = startX;
            if (yDown) {
                penY += lineHeight_ * fontSize;
            } else {
                penY -= lineHeight_ * fontSize;
            }
            continue;
        }

        // Look up glyph metrics
        auto it = glyphs_.find(cp);
        if (it == glyphs_.end()) {
            // Unknown glyph -- try space as fallback
            it = glyphs_.find(32);
            if (it == glyphs_.end()) {
                continue;
            }
        }

        const GlyphMetrics& gm = it->second;

        // Skip drawing for space (no atlas bounds), but still advance
        if (gm.width > 0.0f && gm.height > 0.0f) {
            float quadW = gm.width  * scale;
            float quadH = gm.height * scale;

            float quadX, quadY;
            if (yDown) {
                // Screen space: Y-down. bearingY is distance from baseline to top (em units).
                // Top of glyph = penY + (ascender - bearingY) * scale
                quadX = penX + gm.bearingX * scale + quadW * 0.5f;
                quadY = penY + (ascender_ - gm.bearingY) * fontSize + quadH * 0.5f;
            } else {
                // World space: Y-up. bearingY is top of glyph above baseline.
                quadX = penX + gm.bearingX * scale + quadW * 0.5f;
                quadY = penY + gm.bearingY * scale - quadH * 0.5f;
            }

            SpriteDrawParams params;
            params.position  = {quadX, quadY};
            params.size      = {quadW, quadH};
            params.sourceRect = {gm.uvX, gm.uvY, gm.uvW, gm.uvH};
            params.color     = color;
            params.depth     = depth;
            params.flipY     = false; // DEBUG: disable flipY to test UV sampling

            // Use renderType=0 (regular sprite) since atlas is raster bitmap, not actual SDF.
            // The raster atlas stores white glyphs with alpha coverage — standard alpha blend works.
            batch.drawTexturedQuad(atlasGfxHandle_, atlasTexId_, params, 0.0f);
            sdfEmitted++;
        } else {
            sdfSkipped++;
        }

        penX += gm.advance * scale;
    }

    if (sdfDbgLog && !text.empty()) {
        // Log first glyph's metrics for debugging
        uint32_t firstCp = 0;
        size_t tmpIdx = 0;
        firstCp = decodeUTF8(text, tmpIdx);
        auto firstIt = glyphs_.find(firstCp);
        if (firstIt != glyphs_.end()) {
            const auto& g = firstIt->second;
            float qw = g.width * scale, qh = g.height * scale;
            LOG_INFO("SDFText", "'%c' uv=(%.4f,%.4f,%.4f,%.4f) size=(%.1f,%.1f) pos=(%.1f,%.1f) flipY=%d",
                     (char)firstCp, g.uvX, g.uvY, g.uvW, g.uvH, qw, qh, position.x, position.y, !yDown);
        }
    }
}

Vec2 SDFText::measure(const std::string& text, float fontSize) const {
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

        auto it = glyphs_.find(cp);
        if (it != glyphs_.end()) {
            totalWidth += it->second.advance * scale;
        }
    }

    if (totalWidth > maxWidth) maxWidth = totalWidth;
    return {maxWidth, lineHeight_ * fontSize * lines};
}

unsigned int SDFText::atlasTextureId() const {
    return atlasTexId_;
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
