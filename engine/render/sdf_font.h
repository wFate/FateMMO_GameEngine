/**************************************************************************/
/*  sdf_font.h                                                            */
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
    std::string family;  // e.g. "inter" — explicit from manifest or auto-derived from name
    std::string weight;  // e.g. "Regular"/"SemiBold"/"Bold" — explicit or auto-derived
    std::shared_ptr<Texture> atlas;

    // MSDF-specific (populated from metrics JSON)
    std::unordered_map<uint32_t, GlyphMetrics> glyphs;
    float lineHeight  = 1.2f;
    float ascender    = 0.95f;
    float emSize      = 48.0f;
    float atlasWidth  = 512.0f;
    float atlasHeight = 512.0f;
    float pxRange     = 4.0f;
    bool useAlphaDistance = false;

    // Bitmap-specific (populated from manifest)
    int glyphWidth  = 0;
    int glyphHeight = 0;
    int columns     = 16;
    int firstChar   = 32;
    int lastChar    = 126;
};

} // namespace fate
