# SDF Text Rendering Design Spec

**Date:** 2026-03-17
**Scope:** MTSDF atlas text rendering replacing bitmap font, uber-shader integration, damage numbers, nameplates
**Phase:** Phase 2b of research gap implementation
**Depends on:** SpriteBatch (completed), OpenGL 3.3 shader pipeline (completed), nlohmann/json (already a FetchContent dependency), stb_image (already integrated)

---

## Overview

Replaces the current `stb_truetype` bitmap font renderer with MTSDF (multi-channel signed distance field) text rendering. Text is resolution-independent — scales, rotates, and zooms without blur. Effects (outline, glow, shadow) are composited in the fragment shader at near-zero cost. All text renders through the existing SpriteBatch via an uber-shader with a `renderType` vertex attribute, eliminating batch breaks between sprites and text.

**Design decisions:**
- Offline atlas generation via `msdf-atlas-gen` (not runtime generation — CJK runtime via msdf_c deferred)
- Uber-shader approach (renderType in vertex format, not separate text shader)
- Full replacement of old TextRenderer (ImGui has its own text rendering, unaffected)
- 512x512 MTSDF atlas, 48px em size, 4px pixel range — covers Latin + common symbols

---

## Section 1: MTSDF Atlas & Shader

### Atlas Generation (Offline, One-Time)

Run `msdf-atlas-gen` to produce two asset files:
- `assets/fonts/default.png` — 512x512 MTSDF atlas (4-channel: RGB = MSDF for sharp edges, A = true SDF for glow/shadow)
- `assets/fonts/default.json` — per-glyph metrics (advance, bearing, atlas UV rect, size)

Command:
```
msdf-atlas-gen -font <FontFile.ttf> -type mtsdf -format png -imageout default.png -json default.json -size 48 -pxrange 4 -charset charset.txt
```

Character set: ASCII 32-126 + extended Latin (accented characters) + common symbols (arrows, currency, etc.).

Atlas loaded once at startup as a GL texture with:
- `GL_LINEAR` filtering (bilinear interpolation is essential for SDF quality)
- `GL_CLAMP_TO_EDGE` wrapping
- NO compression (DXT/BCn destroys distance data)
- `GL_RGBA8` internal format (NOT `GL_SRGB8_ALPHA8` — SDF distance values are linear, not color data)
- Loaded with all 4 channels preserved via stb_image

**Texture binding model:** The font atlas binds to `GL_TEXTURE0` — the same unit as sprites. The SpriteBatch's existing texture-change batching handles this naturally: sprite runs batch together, text runs batch together, and a batch break occurs when switching between them. This preserves the existing SpriteBatch architecture without modification to the flush/texture logic. The uber-shader's `renderType` attribute tells the fragment shader which sampling path to use, but both paths sample from `u_texture` (unit 0).

### Uber-Shader Vertex Format

Add `renderType` to `SpriteVertex`:
```cpp
struct SpriteVertex {
    float x, y;        // position
    float u, v;        // texcoord
    float r, g, b, a;  // color/tint
    float renderType;  // 0.0 = sprite, 1.0 = MSDF text
};
```

VAO attribute layout updated to include the new float (stride increases by 4 bytes).

**IMPORTANT:** All existing vertex construction sites in `sprite_batch.cpp` must be updated to include `renderType = 0.0f` in the SpriteVertex aggregate initialization. Every `push_back({x, y, u, v, r, g, b, a})` becomes `push_back({x, y, u, v, r, g, b, a, 0.0f})`. Failure to do this is undefined behavior (uninitialized field).

**TextStyle encoding via renderType values:**
- `0.0` = sprite (existing behavior)
- `1.0` = normal text (clean, no effects)
- `2.0` = outlined text (black outline behind fill, for nameplates)
- `3.0` = glow text (soft radiance, for crit damage)
- `4.0` = shadow text (drop shadow, for general text)

This avoids adding another vertex attribute. The shader branches on `renderType` ranges.

### Fragment Shader

The fragment shader branches on `renderType`:

```glsl
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

// In main():
if (v_renderType < 0.5) {
    // Standard sprite path (existing behavior)
    fragColor = texture(u_texture, v_uv) * v_color;
} else {
    // MSDF text path — all text styles sample from u_texture (same unit as sprites)
    vec4 sdf = texture(u_texture, v_uv);
    float sd = median(sdf.r, sdf.g, sdf.b);

    // Correct screenPxRange: how many screen pixels map to the atlas pxRange
    vec2 unitRange = vec2(u_pxRange) / u_atlasSize;  // u_atlasSize = vec2(512, 512)
    vec2 screenTexSize = vec2(1.0) / fwidth(v_uv);
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);

    float screenPxDist = screenPxRange * (sd - 0.5);
    float opacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

    if (v_renderType > 1.5 && v_renderType < 2.5) {
        // Outlined: black outline behind fill
        float outlineDist = screenPxRange * (sd - 0.35);
        float outlineOpacity = clamp(outlineDist + 0.5, 0.0, 1.0);
        vec4 outlineColor = vec4(0.0, 0.0, 0.0, v_color.a * outlineOpacity);
        vec4 fillColor = vec4(v_color.rgb, v_color.a * opacity);
        fragColor = mix(outlineColor, fillColor, opacity);
    } else if (v_renderType > 2.5 && v_renderType < 3.5) {
        // Glow: use alpha channel (true SDF) for soft radiance
        float glowOpacity = smoothstep(0.0, 0.5, sdf.a);
        vec4 glowColor = vec4(v_color.rgb, v_color.a * glowOpacity * 0.6);
        vec4 fillColor = vec4(v_color.rgb, v_color.a * opacity);
        fragColor = mix(glowColor, fillColor, opacity);
    } else if (v_renderType > 3.5 && v_renderType < 4.5) {
        // Shadow: sample at offset UV, composite behind
        vec2 shadowUV = v_uv - u_shadowOffset;
        float shadowSd = median(texture(u_texture, shadowUV).rgb);
        float shadowDist = screenPxRange * (shadowSd - 0.5);
        float shadowOpacity = clamp(shadowDist + 0.5, 0.0, 1.0) * 0.5;
        vec4 shadow = vec4(0.0, 0.0, 0.0, v_color.a * shadowOpacity);
        vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
        fragColor = mix(shadow, fill, opacity);
    } else {
        // Normal text (renderType ~1.0)
        fragColor = vec4(v_color.rgb, v_color.a * opacity);
    }
}
```

**Uniforms required:**
- `u_pxRange` (float) — raw integer pixel range from atlas generation (4.0)
- `u_atlasSize` (vec2) — atlas dimensions in pixels (512.0, 512.0)
- `u_shadowOffset` (vec2) — shadow offset in UV space, set as `vec2(1.0/512.0, -1.0/512.0)` for a 1-pixel diagonal offset

### Text Effects

Effects are selected by the `renderType` value in the vertex data (1.0=normal, 2.0=outlined, 3.0=glow, 4.0=shadow). Different text strings in the same batch can have different styles since the value is per-vertex. The shader implementation is in the fragment shader code above.

- **Outlined (2.0):** Black outline hardcoded in shader (no second color needed). Outline threshold at 0.35 (wider than fill at 0.5). Used for nameplates.
- **Glow (3.0):** Uses alpha channel (true SDF) with wide smoothstep for soft radiance. Used for crit damage.
- **Shadow (4.0):** Samples atlas at UV offset `u_shadowOffset` uniform (set as ~1 pixel diagonal). Used for general text.

---

## Section 2: SDFText Class

### API

```cpp
enum class TextStyle : uint8_t {
    Normal,      // clean text, no effects
    Outlined,    // dark outline (nameplates over varied backgrounds)
    Glow,        // bright glow (critical hit damage)
    Shadow       // drop shadow (general UI text)
};

struct GlyphMetrics {
    float advance;          // horizontal advance in em units
    float bearingX, bearingY;  // offset from baseline
    float width, height;    // glyph size in em units
    float uvX, uvY, uvW, uvH; // atlas UV rect (0-1 normalized)
};

class SDFText {
public:
    bool init(const std::string& atlasPath, const std::string& metricsPath);
    void shutdown();

    // Draw text into an active SpriteBatch (world-space)
    void draw(SpriteBatch& batch, const std::string& text, Vec2 position,
              float fontSize, Color color, float depth,
              TextStyle style = TextStyle::Normal);

    // Measure text dimensions without drawing (for centering, layout)
    Vec2 measure(const std::string& text, float fontSize) const;

    // Atlas texture ID for SpriteBatch binding
    unsigned int atlasTextureId() const { return atlasTexId_; }

private:
    unsigned int atlasTexId_ = 0;
    float atlasWidth_ = 0.0f;
    float atlasHeight_ = 0.0f;
    float pxRange_ = 4.0f;
    std::unordered_map<uint32_t, GlyphMetrics> glyphs_; // keyed by Unicode codepoint (not char)

    void loadMetrics(const nlohmann::json& metricsJson);

    // UTF-8 decoding: draw() decodes std::string to codepoints internally
    // Simple state machine, ~30 lines, handles full Unicode range
};
```

### How draw() works

1. For each character in the string, look up `GlyphMetrics` from the map
2. Compute quad position from current pen position + bearing + fontSize scaling
3. Submit a textured quad to SpriteBatch with:
   - UV coords from the glyph's atlas rect
   - `renderType = 1.0` (MSDF path)
   - Color = tint color
   - Texture = font atlas (bound to GL_TEXTURE1)
4. Advance pen position by `glyph.advance * fontSize`

The existing `drawTexturedQuad()` method on SpriteBatch already accepts raw GL texture IDs. Add an optional `float renderType = 0.0f` parameter to `drawTexturedQuad()` and store it in `BatchEntry`. When constructing vertices for text quads, use the renderType value corresponding to the TextStyle enum.

**Coordinate convention:** SDFText provides two draw methods matching the existing TextRenderer pattern:
- `drawWorld()` — Y-up world space (for damage numbers and nameplates rendered via SpriteBatch with the game camera projection). Glyph bearings use Y-up convention.
- `drawScreen()` — Y-down screen space (for any screen-space UI text not rendered via ImGui). Glyph bearings use Y-down convention.

The existing `screenProjection()` utility from TextRenderer is preserved as a static method on SDFText.

### measure() for layout

Iterates characters, sums advance widths, tracks max height. Returns `Vec2(totalWidth, lineHeight) * fontSize`. Used for centering damage numbers, aligning nameplates above entities.

---

## Section 3: Game Integration

### Damage Numbers (combat_action_system.h)

Current: `TextRenderer::drawText(text, x, y, size, color)`
New: `sdfText.draw(batch, text, {x, y}, size, color, depth, style)`

Style mapping:
- White damage → `TextStyle::Shadow`
- Orange crit → `TextStyle::Glow`
- Gray miss → `TextStyle::Shadow`
- Purple resist → `TextStyle::Shadow`
- Yellow XP → `TextStyle::Outlined`
- Gold level up → `TextStyle::Glow`

Numbers float upward with scale animation — SDF handles any scale perfectly.

### Nameplates (gameplay_system.h)

Current: `TextRenderer::drawText()` for player/mob names above heads
New: `sdfText.draw(batch, name, pos, 14.0f, color, depth, TextStyle::Outlined)`

The outline ensures readability over any terrain/sprite background. Mob nameplate colors (difficulty-based) pass through as the `color` parameter.

### Chat Text (future)

Not implemented now. When chat UI is built:
- Cache laid-out vertex data per chat message
- Regenerate only when message text changes
- Hundreds of characters batch into one draw call

---

## Files

### New Files
| File | Responsibility |
|---|---|
| `engine/render/sdf_text.h` | SDFText class, TextStyle enum, GlyphMetrics struct |
| `engine/render/sdf_text.cpp` | Atlas loading, JSON metrics parsing, glyph layout, SpriteBatch quad submission |
| `assets/fonts/default.png` | Pre-generated MTSDF atlas (512x512) |
| `assets/fonts/default.json` | Per-glyph metrics from msdf-atlas-gen |
| `tests/test_sdf_text.cpp` | Glyph metrics loading, text measurement, style enum tests |

### Modified Files
| File | Changes |
|---|---|
| `engine/render/sprite_batch.h` | Add `renderType` to SpriteVertex, add draw method accepting renderType parameter |
| `engine/render/sprite_batch.cpp` | Update vertex format stride/attributes, update shader source with MSDF branch, add font atlas uniform |
| `game/systems/combat_action_system.h` | Switch damage text from TextRenderer to SDFText |
| `game/systems/gameplay_system.h` | Switch nameplates from TextRenderer to SDFText |
| `game/game_app.h` / `game_app.cpp` | Init SDFText instead of TextRenderer, pass to systems |

### Removed Files
| File | Reason |
|---|---|
| `engine/render/text_renderer.h` | Replaced by SDFText |
| `engine/render/text_renderer.cpp` | Replaced by SDFText |

---

## What This Does NOT Include

- Runtime SDF generation for CJK/Unicode (deferred — msdf_c library integration for when localization is needed)
- Text input rendering for chat UI (chat system not built yet — SDFText API is ready for it)
- Rich text formatting (bold/italic/colored substrings — future feature)
- Text wrapping/paragraph layout (future — current use cases are single-line damage numbers and names)
- Font switching (single font atlas for now; multiple fonts would need multiple atlases or a font manager)
