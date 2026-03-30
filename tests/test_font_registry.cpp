#include <doctest/doctest.h>
#include "engine/render/font_registry.h"
#include <fstream>
#include <cstdio>

using namespace fate;

TEST_CASE("FontRegistry manifest parsing") {
    // Write a temporary manifest with one MSDF and one bitmap font entry.
    const char* manifestPath = "test_fonts_manifest.json";
    const char* metricsPath  = "test_fonts_metrics.json";

    // Minimal MSDF metrics JSON (matches the format from msdf-atlas-gen).
    {
        std::ofstream f(metricsPath);
        f << R"({
            "atlas": { "width": 256, "height": 256, "distanceRange": 6, "yOrigin": "bottom" },
            "metrics": { "emSize": 32, "lineHeight": 1.5, "ascender": 0.8 },
            "glyphs": [
                { "unicode": 65, "advance": 0.6,
                  "planeBounds": { "left": 0.0, "bottom": 0.0, "right": 0.5, "top": 0.8 },
                  "atlasBounds": { "left": 10, "bottom": 10, "right": 30, "top": 40 } }
            ]
        })";
    }

    // Manifest referencing both font types.
    {
        std::ofstream f(manifestPath);
        f << R"({
            "fonts": [
                {
                    "name": "default",
                    "type": "msdf",
                    "atlas": "fake_atlas.png",
                    "metrics": ")" << metricsPath << R"("
                },
                {
                    "name": "mono",
                    "type": "bitmap",
                    "atlas": "fake_mono.png",
                    "metrics": "",
                    "glyphWidth": 8,
                    "glyphHeight": 12,
                    "columns": 16,
                    "firstChar": 32,
                    "lastChar": 126
                }
            ]
        })";
    }

    auto& reg = FontRegistry::instance();
    reg.clear();

    REQUIRE(reg.parseManifest(manifestPath));

    SUBCASE("font names are registered") {
        auto names = reg.fontNames();
        CHECK(names.size() == 2);
    }

    SUBCASE("MSDF font entry is created with parsed metrics") {
        SDFFont* msdf = reg.getFont("default");
        REQUIRE(msdf != nullptr);
        CHECK(msdf->type == SDFFont::Type::MSDF);
        CHECK(msdf->name == "default");
        CHECK(msdf->atlasWidth  == doctest::Approx(256.0f));
        CHECK(msdf->atlasHeight == doctest::Approx(256.0f));
        CHECK(msdf->pxRange     == doctest::Approx(6.0f));
        CHECK(msdf->emSize      == doctest::Approx(32.0f));
        CHECK(msdf->lineHeight  == doctest::Approx(1.5f));
        CHECK(msdf->ascender    == doctest::Approx(0.8f));
        // Glyph 'A' (unicode 65) should be present.
        CHECK(msdf->glyphs.count(65) == 1);
        CHECK(msdf->glyphs[65].advance == doctest::Approx(0.6f));
    }

    SUBCASE("bitmap font entry is created with manifest fields") {
        SDFFont* bmp = reg.getFont("mono");
        REQUIRE(bmp != nullptr);
        CHECK(bmp->type == SDFFont::Type::Bitmap);
        CHECK(bmp->name == "mono");
        CHECK(bmp->glyphWidth  == 8);
        CHECK(bmp->glyphHeight == 12);
        CHECK(bmp->columns     == 16);
        CHECK(bmp->firstChar   == 32);
        CHECK(bmp->lastChar    == 126);
    }

    SUBCASE("unknown font name returns nullptr") {
        CHECK(reg.getFont("nonexistent") == nullptr);
    }

    SUBCASE("defaultFont returns the 'default' entry") {
        SDFFont* def = reg.defaultFont();
        REQUIRE(def != nullptr);
        CHECK(def->name == "default");
    }

    // Cleanup temp files.
    std::remove(manifestPath);
    std::remove(metricsPath);
    reg.clear();
}

TEST_CASE("Bitmap glyph UV calculation") {
    // Verify the math for computing UV coordinates from a grid-based bitmap font.
    //
    // Setup: 16-column grid, 8x12 glyphs, 128x96 atlas, firstChar=32.
    // Character 'A' = ASCII 65, index = 65 - 32 = 33.
    // col = 33 % 16 = 1, row = 33 / 16 = 2.
    // Pixel rect: x = 1*8 = 8, y = 2*12 = 24.
    // UV: u0 = 8/128 = 0.0625, v0 = 24/96 = 0.25.
    //     u1 = (8+8)/128 = 0.125, v1 = (24+12)/96 = 0.375.

    const int glyphWidth  = 8;
    const int glyphHeight = 12;
    const int columns     = 16;
    const int firstChar   = 32;
    const float atlasW    = 128.0f;
    const float atlasH    = 96.0f;

    int charCode = 'A'; // 65
    int index = charCode - firstChar; // 33
    CHECK(index == 33);

    int col = index % columns; // 1
    int row = index / columns; // 2
    CHECK(col == 1);
    CHECK(row == 2);

    float pixelX = static_cast<float>(col * glyphWidth);  // 8
    float pixelY = static_cast<float>(row * glyphHeight); // 24
    CHECK(pixelX == doctest::Approx(8.0f));
    CHECK(pixelY == doctest::Approx(24.0f));

    float u0 = pixelX / atlasW;                                     // 0.0625
    float v0 = pixelY / atlasH;                                     // 0.25
    float u1 = (pixelX + static_cast<float>(glyphWidth)) / atlasW;  // 0.125
    float v1 = (pixelY + static_cast<float>(glyphHeight)) / atlasH; // 0.375

    CHECK(u0 == doctest::Approx(0.0625f));
    CHECK(v0 == doctest::Approx(0.25f));
    CHECK(u1 == doctest::Approx(0.125f));
    CHECK(v1 == doctest::Approx(0.375f));

    // UV width and height.
    float uvW = u1 - u0; // 0.0625
    float uvH = v1 - v0; // 0.125
    CHECK(uvW == doctest::Approx(0.0625f));
    CHECK(uvH == doctest::Approx(0.125f));
}
