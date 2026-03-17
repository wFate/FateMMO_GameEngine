#pragma once
#include <string>

namespace fate {

// Generates a placeholder font atlas (bitmap, not true MTSDF) at runtime
// using stb_truetype if the atlas PNG does not already exist on disk.
//
// The generated atlas matches the layout described in the companion JSON
// metrics file (default.json) so the SDF text pipeline can render with it
// immediately.  A real MTSDF atlas produced by msdf-atlas-gen can be
// swapped in later by replacing default.png + default.json without any
// engine code changes.
namespace SDFFontAtlas {

    // Generate assets/fonts/default.png from a system TrueType font.
    // Updates assets/fonts/default.json glyph metrics to match the actual
    // rendered layout.  Skips generation if the PNG already exists.
    //
    // fontPath:  path to a .ttf file (e.g. "C:/Windows/Fonts/consola.ttf")
    // outDir:    directory for output files (e.g. "assets/fonts")
    // pixelSize: rasterisation height in pixels (default 48)
    //
    // Returns true on success or if the atlas already exists.
    bool generateIfMissing(const std::string& fontPath,
                           const std::string& outDir = "assets/fonts",
                           float pixelSize = 48.0f);

} // namespace SDFFontAtlas

} // namespace fate
