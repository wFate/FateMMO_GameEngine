/**************************************************************************/
/*  sdf_text.h                                                            */
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
#include "engine/core/types.h"
#include "engine/render/text_style.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/font_registry.h"
#include "engine/render/gfx/types.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace fate {

// GlyphMetrics is defined in sdf_font.h
#include "engine/render/sdf_font.h"

// Layout modifiers for glyph placement. `lineHeight` multiplies the font's
// native line-height (1.0 = default). `letterSpacing` adds pixels between each
// glyph's advance (0 = default, negative to tighten). The identity-default
// keeps every existing call site behaving exactly as before.
struct TextLayout {
    float lineHeight    = 1.0f;
    float letterSpacing = 0.0f;
};

class SDFText {
public:
    static SDFText& instance();

    bool init(const std::string& atlasPath, const std::string& metricsPath);
    void shutdown();

    void drawWorld(SpriteBatch& batch, const std::string& text, Vec2 position,
                   float fontSize, Color color = Color::white(), float depth = 50.0f,
                   TextStyle style = TextStyle::Normal,
                   const TextLayout& layout = {});

    void drawScreen(SpriteBatch& batch, const std::string& text, Vec2 position,
                    float fontSize, Color color = Color::white(), float depth = 50.0f,
                    TextStyle style = TextStyle::Normal,
                    const TextLayout& layout = {});

    Vec2 measure(const std::string& text, float fontSize,
                 const TextLayout& layout = {}) const;

    void setFontRegistry(FontRegistry* registry);

    // Swap the global default font that drawScreen / drawWorld (the non-Ex
    // variants) render with. Looks up `name` in the registry and copies its
    // glyph table + atlas handle into the active font slot. Call this once
    // after the registry is loaded to upgrade every drawScreen-based bespoke
    // widget (menu tab bar, status bar, inventory, skill panels, etc.) to a
    // chosen UI font without editing each call site. Returns false if the
    // font isn't registered yet (no state change).
    bool setDefaultFont(const std::string& name);

    void drawScreenEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color = Color::white(), float depth = 50.0f,
                      TextStyle style = TextStyle::Normal,
                      const std::string& fontName = "default",
                      const TextLayout& layout = {});

    // Draw screen-space text with configurable outline / shadow / glow.
    // Pushes shader-side text-effect uniforms via SpriteBatch, draws, then
    // resets. Use this from widgets that have a UIStyle and want the chrome's
    // textEffects to actually affect rendering.
    void drawScreenEffects(SpriteBatch& batch, const std::string& text, Vec2 position,
                           float fontSize, Color color, float depth,
                           TextStyle style, const std::string& fontName,
                           const TextEffects& fx,
                           const TextLayout& layout = {});

    // World-space variant — same contract, but the text is drawn in world
    // (y-up) coordinates. Used by nameplates, world-anchored quest markers,
    // and any other in-world text that wants real shader-side outline+shadow
    // compositing instead of the legacy 8-directional offset hack.
    void drawWorldEffects(SpriteBatch& batch, const std::string& text, Vec2 position,
                          float fontSize, Color color, float depth,
                          TextStyle style, const std::string& fontName,
                          const TextEffects& fx,
                          const TextLayout& layout = {});

    void drawWorldEx(SpriteBatch& batch, const std::string& text, Vec2 position,
                     float fontSize, Color color = Color::white(), float depth = 50.0f,
                     TextStyle style = TextStyle::Normal,
                     const std::string& fontName = "default",
                     const TextLayout& layout = {});

    Vec2 measureEx(const std::string& text, float fontSize,
                   const std::string& fontName = "default",
                   const TextLayout& layout = {}) const;

    unsigned int atlasTextureId() const;
    gfx::TextureHandle atlasGfxHandle() const { return atlasGfxHandle_; }

    // Backend-safe readiness probe. True when an atlas + glyph table are loaded
    // and drawWorld/drawScreen will actually emit text. Prefer this over
    // atlasTextureId() — that helper returns 0 on Metal even when the atlas
    // is fully resident, which silently disables consumers that gated on it
    // (most notably the NameplateRenderSystem).
    bool isReady() const;

    static Mat4 screenProjection(int windowWidth, int windowHeight);

    static uint32_t decodeUTF8(const std::string& text, size_t& index);

private:
    SDFText() = default;
#ifndef FATEMMO_METAL
    unsigned int atlasTexId_ = 0;
#endif
    gfx::TextureHandle atlasGfxHandle_{};
    float atlasWidth_ = 512.0f, atlasHeight_ = 512.0f;
    float pxRange_ = 4.0f;
    float lineHeight_ = 1.2f;
    float ascender_ = 0.95f;
    float emSize_ = 48.0f;
    bool useAlphaDistance_ = false;
    std::unordered_map<uint32_t, GlyphMetrics> glyphs_;
    const std::unordered_map<uint32_t, GlyphMetrics>* activeGlyphs_ = nullptr;

    FontRegistry* fontRegistry_ = nullptr;

    void loadMetrics(const std::string& jsonPath);
    void drawInternal(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color, float depth, TextStyle style, bool yDown,
                      const TextLayout& layout = {});
    // Resolves fontName via the registry, swaps font state, calls drawInternal,
    // then restores. Does NOT touch text-effect uniforms — callers (public
    // drawScreenEx, drawScreenEffects) are responsible for that contract.
    void drawWithFont(SpriteBatch& batch, const std::string& text, Vec2 position,
                      float fontSize, Color color, float depth, TextStyle style,
                      const std::string& fontName, bool yDown,
                      const TextLayout& layout = {});
    void drawBitmap(SpriteBatch& batch, const SDFFont& font, const std::string& text,
                    Vec2 position, float fontSize, Color color, float depth, bool yDown,
                    const TextLayout& layout = {});
    Vec2 measureBitmap(const SDFFont& font, const std::string& text, float fontSize,
                       const TextLayout& layout = {}) const;
};

} // namespace fate
