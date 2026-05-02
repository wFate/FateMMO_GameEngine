#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

struct SpriteIn {
    float2 aPos        [[attribute(0)]];
    float2 aTexCoord   [[attribute(1)]];
    float4 aColor      [[attribute(2)]];
    float  aRenderType [[attribute(3)]];
};

struct SpriteOut {
    float4 position    [[position]];
    float2 v_uv;
    float4 v_color;
    float  v_renderType;
};

struct SpriteUniforms {
    float4x4 uViewProjection;
    float    uPxRange;
    float2   uAtlasSize;
    float2   uShadowOffset;
    // float, not int: SpriteBatch writes paletteSize as a float (matches the
    // GLSL `uniform float u_paletteSize` in assets/shaders/sprite.frag), and
    // CommandList::setUniform(name,int) is a deliberate no-op on Metal — so
    // an `int` field here would never be populated.
    float    uPaletteCount;
    float4   uPalette[16];

    // Configurable text effect uniforms
    float4   uOutlineColor;
    float    uOutlineThickness;
    float2   uTextShadowOffset;
    float4   uTextShadowColor;
    float4   uTextGlowColor;
    float    uTextGlowIntensity;
    float    uSdfUseAlpha;

    // Rounded rect uniforms (renderType 7)
    float2   uRectSize;
    float    uCornerRadius;
    float    uRRBorderWidth;
    float4   uRRBorderColor;
    float4   uGradientTop;
    float4   uGradientBottom;
    float2   uRRShadowOffset;
    float    uRRShadowBlur;
    float4   uRRShadowColor;
};

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static float sprite_median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

static float sprite_sdf_distance(float4 sample, float useAlpha) {
    return (useAlpha > 0.5) ? sample.a : sprite_median(sample.r, sample.g, sample.b);
}

// ---------------------------------------------------------------------------
// Vertex function
// ---------------------------------------------------------------------------

vertex SpriteOut sprite_vertex(SpriteIn in [[stage_in]],
                               constant SpriteUniforms& u [[buffer(1)]])
{
    SpriteOut out;
    out.position    = u.uViewProjection * float4(in.aPos, 0.0, 1.0);
    out.v_uv        = in.aTexCoord;
    out.v_color     = in.aColor;
    out.v_renderType = in.aRenderType;
    return out;
}

// ---------------------------------------------------------------------------
// Fragment function
// ---------------------------------------------------------------------------

fragment float4 sprite_fragment(SpriteOut in [[stage_in]],
                                constant SpriteUniforms& u [[buffer(0)]],
                                texture2d<float> uTexture [[texture(0)]],
                                sampler samp              [[sampler(0)]])
{
    float4 fragColor;

    // RenderType 5: Palette swap — grayscale index maps to palette color
    if (in.v_renderType > 4.5 && in.v_renderType < 5.5) {
        float4 texel = uTexture.sample(samp, in.v_uv);
        if (texel.a < 0.01) discard_fragment();
        int index = int(texel.r * 15.0 + 0.5); // 16-color palette (0-15)
        index = clamp(index, 0, int(u.uPaletteCount) - 1);
        fragColor = u.uPalette[index];
        fragColor.a *= texel.a * in.v_color.a;
        return fragColor;
    }

    // RenderType 7: SDF rounded rectangle
    if (in.v_renderType > 6.5 && in.v_renderType < 7.5) {
        // Convert UV (0-1) to pixel position relative to rect center
        float2 p = (in.v_uv - 0.5) * u.uRectSize;

        // SDF for rounded box
        float2 q = abs(p) - u.uRectSize * 0.5 + u.uCornerRadius;
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u.uCornerRadius;

        // Shadow: offset the SDF
        float2 pShadow = p - u.uRRShadowOffset;
        float2 qShadow = abs(pShadow) - u.uRectSize * 0.5 + u.uCornerRadius;
        float shadowDist = length(max(qShadow, 0.0)) + min(max(qShadow.x, qShadow.y), 0.0) - u.uCornerRadius;
        float shadowAlpha = 1.0 - smoothstep(-u.uRRShadowBlur, u.uRRShadowBlur, shadowDist);
        float4 shadow = float4(u.uRRShadowColor.rgb, u.uRRShadowColor.a * shadowAlpha);

        // Gradient fill: lerp between top and bottom based on v_uv.y
        float4 fill = mix(u.uGradientTop, u.uGradientBottom, in.v_uv.y);

        // AA edges: ~1px feather
        float fillAlpha = 1.0 - smoothstep(-1.0, 0.0, dist);

        // Border: band between (dist + borderWidth) and dist
        float borderOuter = 1.0 - smoothstep(-1.0, 0.0, dist);
        float borderInner = 1.0 - smoothstep(-1.0, 0.0, dist + u.uRRBorderWidth);
        float borderAlpha = borderOuter - borderInner;
        float4 border = float4(u.uRRBorderColor.rgb, u.uRRBorderColor.a * borderAlpha);

        // Fill only the interior (subtract border band)
        fill.a *= (fillAlpha - borderAlpha);

        // Composite: shadow behind, fill, border on top
        fragColor = shadow;
        fragColor = mix(fragColor, fill, fill.a);
        fragColor = mix(fragColor, border, border.a);
        fragColor.a *= in.v_color.a;

        if (fragColor.a < 0.01) discard_fragment();
        return fragColor;
    }

    if (in.v_renderType < 0.5) {
        // Mode 0: Simple sprite
        fragColor = uTexture.sample(samp, in.v_uv) * in.v_color;
        if (fragColor.a < 0.01) discard_fragment();
    } else {
        // Modes 1-4: SDF text variants
        float4 sdf = uTexture.sample(samp, in.v_uv);
        float sd = sprite_sdf_distance(sdf, u.uSdfUseAlpha);
        float2 unitRange     = float2(u.uPxRange) / u.uAtlasSize;
        float2 screenTexSize = float2(1.0) / fwidth(in.v_uv);
        float screenPxRange  = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float screenPxDist   = screenPxRange * (sd - 0.5);
        float opacity        = clamp(screenPxDist + 0.5, 0.0, 1.0);

        if (in.v_renderType < 1.5) {
            // Mode 1: Normal SDF text
            fragColor = float4(in.v_color.rgb, in.v_color.a * opacity);
        } else if (in.v_renderType < 2.5) {
            // Mode 2: Outlined SDF text. When uTextShadowColor.a > 0 we also
            // lay a drop-shadow under the outline+fill so TWOM-style chunky
            // text gets both at once in a single render pass.
            float outlineDist = screenPxRange * (sd - u.uOutlineThickness);
            float outlineOp   = clamp(outlineDist + 0.5, 0.0, 1.0);

            float2 shadowUV  = in.v_uv - u.uTextShadowOffset;
            float4 shadowSdf = uTexture.sample(samp, shadowUV);
            float  shadowSd  = sprite_sdf_distance(shadowSdf, u.uSdfUseAlpha);
            float  shadowDist = screenPxRange * (shadowSd - u.uOutlineThickness);
            float  shadowOp  = clamp(shadowDist + 0.5, 0.0, 1.0) * u.uTextShadowColor.a;

            float4 shadow  = float4(u.uTextShadowColor.rgb, in.v_color.a * shadowOp);
            float4 outline = float4(u.uOutlineColor.rgb,    in.v_color.a * outlineOp * u.uOutlineColor.a);
            float4 fill    = float4(in.v_color.rgb,         in.v_color.a * opacity);

            fragColor = shadow;
            fragColor = mix(fragColor, outline, outline.a);
            fragColor = mix(fragColor, fill,    opacity);
        } else if (in.v_renderType < 3.5) {
            // Mode 3: Glow SDF text
            float glowOp = smoothstep(0.0, 0.5, sd);
            float4 glow  = float4(u.uTextGlowColor.rgb, in.v_color.a * glowOp * u.uTextGlowIntensity);
            float4 fill  = float4(in.v_color.rgb, in.v_color.a * opacity);
            fragColor = mix(glow, fill, opacity);
        } else {
            // Mode 4: Shadow SDF text
            float2 shadowUV  = in.v_uv - u.uTextShadowOffset;
            float4 shadowSdf = uTexture.sample(samp, shadowUV);
            float shadowSd   = sprite_sdf_distance(shadowSdf, u.uSdfUseAlpha);
            float shadowDist = screenPxRange * (shadowSd - 0.5);
            float shadowOp   = clamp(shadowDist + 0.5, 0.0, 1.0) * u.uTextShadowColor.a;
            float4 shadow    = float4(u.uTextShadowColor.rgb, in.v_color.a * shadowOp);
            float4 fill      = float4(in.v_color.rgb, in.v_color.a * opacity);
            fragColor = mix(shadow, fill, opacity);
        }

        if (fragColor.a < 0.01) discard_fragment();
    }

    return fragColor;
}
