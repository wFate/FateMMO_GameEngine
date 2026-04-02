#include "engine/render/sdf_font_atlas.h"
#include "engine/core/logger.h"
#include "stb_truetype.h"
#include "stb_image_write.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace fate {

bool SDFFontAtlas::generateIfMissing(const std::string& fontPath,
                                     const std::string& outDir,
                                     float pixelSize,
                                     const std::string& fontName) {
    const std::string pngPath  = outDir + "/" + fontName + ".png";
    const std::string jsonPath = outDir + "/" + fontName + ".json";

    // Skip if atlas already exists
    if (fs::exists(pngPath)) {
        LOG_INFO("SDFFontAtlas", "Atlas already exists: %s", pngPath.c_str());
        return true;
    }

    fs::create_directories(outDir);

    // ------------------------------------------------------------------
    // Load the TrueType font
    // ------------------------------------------------------------------
    std::ifstream fontFile(fontPath, std::ios::binary | std::ios::ate);
    if (!fontFile.is_open()) {
        LOG_ERROR("SDFFontAtlas", "Cannot open font file: %s", fontPath.c_str());
        return false;
    }

    const size_t fontFileSize = static_cast<size_t>(fontFile.tellg());
    fontFile.seekg(0);
    std::vector<unsigned char> fontData(fontFileSize);
    fontFile.read(reinterpret_cast<char*>(fontData.data()),
                  static_cast<std::streamsize>(fontFileSize));
    fontFile.close();

    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData.data(), 0)) {
        LOG_ERROR("SDFFontAtlas", "Failed to parse font: %s", fontPath.c_str());
        return false;
    }

    const float scale = stbtt_ScaleForPixelHeight(&fontInfo, pixelSize);

    // Font-level vertical metrics (in em units, emSize = pixelSize)
    int ascent_i, descent_i, lineGap_i;
    stbtt_GetFontVMetrics(&fontInfo, &ascent_i, &descent_i, &lineGap_i);
    const float ascender  =  ascent_i  * scale / pixelSize;
    const float descender =  descent_i * scale / pixelSize;  // negative
    const float lineHeight = (ascent_i - descent_i + lineGap_i) * scale / pixelSize;

    // ------------------------------------------------------------------
    // Rasterise ASCII 32-126 into a 512x512 RGBA atlas
    // ------------------------------------------------------------------
    constexpr int ATLAS_W = 512;
    constexpr int ATLAS_H = 512;
    constexpr int PADDING = 2;       // gap between glyphs
    constexpr int FIRST_CHAR = 32;
    constexpr int LAST_CHAR  = 126;

    // RGBA bitmap (white text on transparent black)
    std::vector<unsigned char> atlas(ATLAS_W * ATLAS_H * 4, 0);

    struct GlyphRecord {
        int unicode     = 0;
        float advance   = 0.0f;
        // planeBounds in em units
        float pb_left   = 0.0f;
        float pb_bottom = 0.0f;
        float pb_right  = 0.0f;
        float pb_top    = 0.0f;
        // atlasBounds in pixels
        int ab_left     = 0;
        int ab_bottom   = 0;
        int ab_right    = 0;
        int ab_top      = 0;
    };

    std::vector<GlyphRecord> records;
    records.reserve(LAST_CHAR - FIRST_CHAR + 1);

    int penX = PADDING;
    int penY = PADDING;
    int rowHeight = 0;

    for (int cp = FIRST_CHAR; cp <= LAST_CHAR; ++cp) {
        GlyphRecord rec;
        rec.unicode = cp;

        int glyphIdx = stbtt_FindGlyphIndex(&fontInfo, cp);

        // Horizontal advance (always needed, even for space)
        int advW = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&fontInfo, glyphIdx, &advW, &lsb);
        rec.advance = advW * scale / pixelSize;  // in em units

        // Space has no visible bitmap
        if (cp == ' ') {
            records.push_back(rec);
            continue;
        }

        // Bitmap bounding box
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&fontInfo, glyphIdx, scale, scale,
                                &x0, &y0, &x1, &y1);
        const int gw = x1 - x0;
        const int gh = y1 - y0;

        if (gw <= 0 || gh <= 0) {
            // Non-printing glyph (rare for 33-126, but just in case)
            records.push_back(rec);
            continue;
        }

        // Advance row if needed
        if (penX + gw + PADDING > ATLAS_W) {
            penX = PADDING;
            penY += rowHeight + PADDING;
            rowHeight = 0;
        }
        if (penY + gh + PADDING > ATLAS_H) {
            LOG_WARN("SDFFontAtlas", "Atlas full at codepoint %d, skipping rest", cp);
            records.push_back(rec);
            continue;
        }

        // Render glyph into a temporary single-channel buffer
        std::vector<unsigned char> glyphBmp(gw * gh, 0);
        stbtt_MakeGlyphBitmap(&fontInfo, glyphBmp.data(),
                               gw, gh, gw, scale, scale, glyphIdx);

        // Copy into RGBA atlas (white colour, alpha = glyph coverage)
        for (int gy = 0; gy < gh; ++gy) {
            for (int gx = 0; gx < gw; ++gx) {
                const int ax = penX + gx;
                const int ay = penY + gy;
                const int ai = (ay * ATLAS_W + ax) * 4;
                const unsigned char v = glyphBmp[gy * gw + gx];
                atlas[ai + 0] = 255;  // R
                atlas[ai + 1] = 255;  // G
                atlas[ai + 2] = 255;  // B
                atlas[ai + 3] = v;    // A = coverage
            }
        }

        // planeBounds: glyph box in em-space
        // x0/y0 from stbtt are pixel offsets from the glyph origin at baseline.
        // Convert to em units by dividing by pixelSize.
        // Note: stbtt y0 is negative-up (y0 < 0 means above baseline).
        // msdf-atlas-gen planeBounds use bottom < top, y-up from baseline.
        rec.pb_left   =  x0 / pixelSize;
        rec.pb_bottom = -y1 / pixelSize;   // stbtt y1 is bottom edge (positive = below baseline)
        rec.pb_right  =  x1 / pixelSize;
        rec.pb_top    = -y0 / pixelSize;   // stbtt y0 is top edge (negative = above baseline)

        // atlasBounds: pixel rect in atlas (y-origin top, matching yOrigin:"top")
        rec.ab_left   = penX;
        rec.ab_bottom = penY;
        rec.ab_right  = penX + gw;
        rec.ab_top    = penY + gh;

        records.push_back(rec);

        penX += gw + PADDING;
        if (gh > rowHeight) rowHeight = gh;
    }

    // ------------------------------------------------------------------
    // Write PNG
    // ------------------------------------------------------------------
    if (!stbi_write_png(pngPath.c_str(), ATLAS_W, ATLAS_H, 4,
                        atlas.data(), ATLAS_W * 4)) {
        LOG_ERROR("SDFFontAtlas", "Failed to write atlas PNG: %s", pngPath.c_str());
        return false;
    }
    LOG_INFO("SDFFontAtlas", "Wrote atlas PNG: %s (%d glyphs)", pngPath.c_str(),
             static_cast<int>(records.size()));

    // ------------------------------------------------------------------
    // Write / overwrite JSON metrics
    // ------------------------------------------------------------------
    json glyphsArr = json::array();
    for (const auto& r : records) {
        json g;
        g["unicode"] = r.unicode;
        g["advance"] = r.advance;

        if (r.unicode == ' ' ||
            (r.ab_right == 0 && r.ab_top == 0)) {
            // No visible bounds
            g["planeBounds"] = {
                {"left", 0.0}, {"bottom", 0.0},
                {"right", 0.0}, {"top", 0.0}
            };
            g["atlasBounds"] = {
                {"left", 0.0}, {"bottom", 0.0},
                {"right", 0.0}, {"top", 0.0}
            };
        } else {
            g["planeBounds"] = {
                {"left",   r.pb_left},
                {"bottom", r.pb_bottom},
                {"right",  r.pb_right},
                {"top",    r.pb_top}
            };
            g["atlasBounds"] = {
                {"left",   r.ab_left},
                {"bottom", r.ab_bottom},
                {"right",  r.ab_right},
                {"top",    r.ab_top}
            };
        }
        glyphsArr.push_back(g);
    }

    json root;
    root["atlas"] = {
        {"type",          "mtsdf"},
        {"distanceRange", 4},
        {"size",          static_cast<int>(pixelSize)},
        {"width",         ATLAS_W},
        {"height",        ATLAS_H},
        {"yOrigin",       "top"}
    };
    root["metrics"] = {
        {"emSize",     pixelSize},
        {"lineHeight", lineHeight},
        {"ascender",   ascender},
        {"descender",  descender}
    };
    root["glyphs"] = glyphsArr;

    std::ofstream jsonFile(jsonPath, std::ios::trunc);
    if (!jsonFile.is_open()) {
        LOG_ERROR("SDFFontAtlas", "Failed to write JSON: %s", jsonPath.c_str());
        return false;
    }
    jsonFile << root.dump(4) << "\n";
    jsonFile.close();

    LOG_INFO("SDFFontAtlas", "Wrote atlas JSON: %s", jsonPath.c_str());
    return true;
}

} // namespace fate
