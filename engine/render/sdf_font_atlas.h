/**************************************************************************/
/*  sdf_font_atlas.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
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
                           float pixelSize = 48.0f,
                           const std::string& fontName = "default");

} // namespace SDFFontAtlas

} // namespace fate
