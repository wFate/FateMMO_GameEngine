#include "engine/render/text_renderer.h"
#include "engine/render/gl_loader.h"
#include "engine/core/logger.h"
#include "stb_truetype.h"
#include <fstream>
#include <vector>
#include <cstring>

namespace fate {

// ============================================================================
// Font
// ============================================================================

Font::~Font() {
    if (atlasTexId_) {
        glDeleteTextures(1, &atlasTexId_);
    }
}

bool Font::loadFromFile(const std::string& path, float pixelSize) {
    pixelSize_ = pixelSize;

    // Read font file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Font", "Cannot open font: %s", path.c_str());
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);
    std::vector<unsigned char> fontData(fileSize);
    file.read(reinterpret_cast<char*>(fontData.data()), fileSize);

    // Init stb_truetype
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData.data(), 0)) {
        LOG_ERROR("Font", "Failed to parse font: %s", path.c_str());
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&fontInfo, pixelSize);

    // Get font metrics
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    lineHeight_ = (ascent - descent + lineGap) * scale;

    // Bake ASCII glyphs (32-126) into an atlas
    atlasWidth_ = 512;
    atlasHeight_ = 512;
    std::vector<unsigned char> atlasData(atlasWidth_ * atlasHeight_, 0);

    int penX = 1, penY = 1;
    int rowHeight = 0;

    for (char c = 32; c < 127; c++) {
        int glyph = stbtt_FindGlyphIndex(&fontInfo, c);
        if (glyph == 0 && c != ' ') continue;

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&fontInfo, glyph, scale, scale, &x0, &y0, &x1, &y1);

        int gw = x1 - x0;
        int gh = y1 - y0;

        // Advance to next row if needed
        if (penX + gw + 1 >= atlasWidth_) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }

        if (penY + gh + 1 >= atlasHeight_) {
            LOG_WARN("Font", "Atlas full at char '%c', some glyphs missing", c);
            break;
        }

        // Render glyph to atlas
        stbtt_MakeGlyphBitmap(&fontInfo,
            atlasData.data() + penY * atlasWidth_ + penX,
            gw, gh, atlasWidth_, scale, scale, glyph);

        // Get horizontal metrics
        int advanceWidth, leftBearing;
        stbtt_GetGlyphHMetrics(&fontInfo, glyph, &advanceWidth, &leftBearing);

        GlyphInfo info;
        info.u0 = (float)penX / atlasWidth_;
        info.v0 = (float)penY / atlasHeight_;
        info.u1 = (float)(penX + gw) / atlasWidth_;
        info.v1 = (float)(penY + gh) / atlasHeight_;
        info.xoff = (float)x0;
        info.yoff = (float)y0;
        info.xadvance = advanceWidth * scale;
        info.width = gw;
        info.height = gh;

        glyphs_[c] = info;

        penX += gw + 1;
        if (gh > rowHeight) rowHeight = gh;
    }

    // Upload atlas to GPU
    glGenTextures(1, &atlasTexId_);
    glBindTexture(GL_TEXTURE_2D, atlasTexId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload as single-channel, then swizzle to RGBA (white text with alpha)
    // We'll convert to RGBA first for compatibility
    std::vector<unsigned char> rgbaData(atlasWidth_ * atlasHeight_ * 4);
    for (int i = 0; i < atlasWidth_ * atlasHeight_; i++) {
        rgbaData[i * 4 + 0] = 255;
        rgbaData[i * 4 + 1] = 255;
        rgbaData[i * 4 + 2] = 255;
        rgbaData[i * 4 + 3] = atlasData[i];
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasWidth_, atlasHeight_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgbaData.data());

    glBindTexture(GL_TEXTURE_2D, 0);

    LOG_INFO("Font", "Loaded '%s' at %.0fpx (%zu glyphs, %dx%d atlas)",
             path.c_str(), pixelSize, glyphs_.size(), atlasWidth_, atlasHeight_);
    return true;
}

const GlyphInfo* Font::getGlyph(char c) const {
    auto it = glyphs_.find(c);
    return (it != glyphs_.end()) ? &it->second : nullptr;
}

Vec2 Font::measureText(const std::string& text) const {
    float width = 0.0f;
    float maxWidth = 0.0f;
    int lines = 1;

    for (char c : text) {
        if (c == '\n') {
            if (width > maxWidth) maxWidth = width;
            width = 0.0f;
            lines++;
            continue;
        }
        auto* g = getGlyph(c);
        if (g) width += g->xadvance;
    }
    if (width > maxWidth) maxWidth = width;

    return {maxWidth, lines * lineHeight_};
}

// ============================================================================
// TextRenderer
// ============================================================================

bool TextRenderer::init(const std::string& defaultFontPath, float defaultSize) {
    defaultFont_ = std::make_unique<Font>();
    if (!defaultFont_->loadFromFile(defaultFontPath, defaultSize)) {
        LOG_ERROR("TextRenderer", "Failed to load default font: %s", defaultFontPath.c_str());
        return false;
    }
    LOG_INFO("TextRenderer", "Initialized with default font");
    return true;
}

void TextRenderer::shutdown() {
    defaultFont_.reset();
    fontCache_.clear();
}

Font* TextRenderer::loadFont(const std::string& path, float pixelSize) {
    char key[256];
    snprintf(key, sizeof(key), "%s_%.0f", path.c_str(), pixelSize);
    std::string keyStr(key);

    auto it = fontCache_.find(keyStr);
    if (it != fontCache_.end()) return it->second.get();

    auto font = std::make_unique<Font>();
    if (!font->loadFromFile(path, pixelSize)) return nullptr;

    Font* ptr = font.get();
    fontCache_[keyStr] = std::move(font);
    return ptr;
}

void TextRenderer::drawWorld(SpriteBatch& batch, const std::string& text,
                             const Vec2& position, const Color& color,
                             float scale, float depth) {
    if (!defaultFont_) return;

    Font* font = defaultFont_.get();
    float cursorX = position.x;
    float cursorY = position.y;

    for (char c : text) {
        if (c == '\n') {
            cursorX = position.x;
            cursorY += font->lineHeight() * scale; // Y-down in screen space
            continue;
        }

        const GlyphInfo* g = font->getGlyph(c);
        if (!g) continue;

        float w = (float)g->width * scale;
        float h = (float)g->height * scale;

        if (g->width > 0 && g->height > 0) {
            SpriteDrawParams params;
            // stb_truetype yoff: negative = above baseline, positive = below
            // In screen-space (Y-down), add yoff directly
            params.position = {
                cursorX + (g->xoff + g->width * 0.5f) * scale,
                cursorY + (g->yoff + g->height * 0.5f) * scale
            };
            params.size = {w, h};
            params.sourceRect = {g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0};
            params.color = color;
            params.depth = depth;

            batch.drawTexturedQuad(font->atlasTextureId(), params);
        }

        cursorX += g->xadvance * scale;
    }
}

void TextRenderer::drawWorldYUp(SpriteBatch& batch, const std::string& text,
                                const Vec2& position, const Color& color,
                                float scale, float depth) {
    if (!defaultFont_) return;

    Font* font = defaultFont_.get();
    float cursorX = position.x;
    float cursorY = position.y;

    for (char c : text) {
        if (c == '\n') {
            cursorX = position.x;
            cursorY -= font->lineHeight() * scale; // Y-up: newline goes DOWN
            continue;
        }

        const GlyphInfo* g = font->getGlyph(c);
        if (!g) continue;

        float w = (float)g->width * scale;
        float h = (float)g->height * scale;

        if (g->width > 0 && g->height > 0) {
            SpriteDrawParams params;
            // In Y-up world space, negate yoff so "above baseline" goes UP
            params.position = {
                cursorX + (g->xoff + g->width * 0.5f) * scale,
                cursorY - (g->yoff + g->height * 0.5f) * scale
            };
            params.size = {w, h};
            params.sourceRect = {g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0};
            params.color = color;
            params.depth = depth;
            params.flipY = true; // Flip glyph texture for Y-up coordinate system

            batch.drawTexturedQuad(font->atlasTextureId(), params);
        }

        cursorX += g->xadvance * scale;
    }
}

void TextRenderer::drawScreen(SpriteBatch& batch, const std::string& text,
                              const Vec2& position, const Color& color,
                              float scale, float depth) {
    drawWorld(batch, text, position, color, scale, depth);
}

Mat4 TextRenderer::screenProjection(int windowWidth, int windowHeight) {
    return Mat4::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
}

} // namespace fate
