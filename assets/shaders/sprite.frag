in vec2 v_uv;
in vec4 v_color;
in float v_renderType;

out vec4 fragColor;

uniform sampler2D uTexture;
uniform float u_pxRange;
uniform vec2 u_atlasSize;
uniform vec2 u_shadowOffset;
uniform vec4 u_palette[16];
uniform float u_paletteSize;

// Configurable text effect uniforms
uniform vec4 u_outlineColor;
uniform float u_outlineThickness;
uniform vec2 u_textShadowOffset;
uniform vec4 u_textShadowColor;
uniform vec4 u_textGlowColor;
uniform float u_textGlowIntensity;
uniform float u_sdfUseAlpha;

// Rounded rect uniforms (renderType 7)
uniform vec2 u_rectSize;
uniform float u_cornerRadius;
uniform float u_rrBorderWidth;
uniform vec4 u_rrBorderColor;
uniform vec4 u_gradientTop;
uniform vec4 u_gradientBottom;
uniform vec2 u_rrShadowOffset;
uniform float u_rrShadowBlur;
uniform vec4 u_rrShadowColor;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float sdfDistance(vec4 sample) {
    return (u_sdfUseAlpha > 0.5) ? sample.a : median(sample.r, sample.g, sample.b);
}

void main() {
    // RenderType 5: Palette swap — grayscale index maps to palette color
    if (v_renderType > 4.5 && v_renderType < 5.5) {
        vec4 texel = texture(uTexture, v_uv);
        if (texel.a < 0.01) discard;
        int index = int(texel.r * 15.0 + 0.5); // 16-color palette (0-15)
        index = clamp(index, 0, int(u_paletteSize) - 1);
        fragColor = u_palette[index];
        fragColor.a *= texel.a * v_color.a;
        return;
    }

    // RenderType 7: SDF rounded rectangle
    if (v_renderType > 6.5 && v_renderType < 7.5) {
        // Convert UV (0-1) to pixel position relative to rect center
        vec2 p = (v_uv - 0.5) * u_rectSize;

        // SDF for rounded box
        vec2 q = abs(p) - u_rectSize * 0.5 + u_cornerRadius;
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_cornerRadius;

        // Shadow: offset the SDF
        vec2 pShadow = p - u_rrShadowOffset;
        vec2 qShadow = abs(pShadow) - u_rectSize * 0.5 + u_cornerRadius;
        float shadowDist = length(max(qShadow, 0.0)) + min(max(qShadow.x, qShadow.y), 0.0) - u_cornerRadius;
        float shadowAlpha = 1.0 - smoothstep(-u_rrShadowBlur, u_rrShadowBlur, shadowDist);
        vec4 shadow = vec4(u_rrShadowColor.rgb, u_rrShadowColor.a * shadowAlpha);

        // Gradient fill: lerp between top and bottom based on v_uv.y
        vec4 fill = mix(u_gradientTop, u_gradientBottom, v_uv.y);

        // AA edges: ~1px feather
        float fillAlpha = 1.0 - smoothstep(-1.0, 0.0, dist);

        // Border: band between (dist + borderWidth) and dist
        float borderOuter = 1.0 - smoothstep(-1.0, 0.0, dist);
        float borderInner = 1.0 - smoothstep(-1.0, 0.0, dist + u_rrBorderWidth);
        float borderAlpha = borderOuter - borderInner;
        vec4 border = vec4(u_rrBorderColor.rgb, u_rrBorderColor.a * borderAlpha);

        // Fill only the interior (subtract border band)
        fill.a *= (fillAlpha - borderAlpha);

        // Composite: shadow behind, fill, border on top
        fragColor = shadow;
        fragColor = mix(fragColor, fill, fill.a);
        fragColor = mix(fragColor, border, border.a);
        fragColor.a *= v_color.a;

        if (fragColor.a < 0.01) discard;
        return;
    }

    if (v_renderType < 0.5) {
        fragColor = texture(uTexture, v_uv) * v_color;
        if (fragColor.a < 0.01) discard;
    } else {
        vec4 sdf = texture(uTexture, v_uv);
        float sd = sdfDistance(sdf);
        vec2 unitRange = vec2(u_pxRange) / u_atlasSize;
        vec2 screenTexSize = vec2(1.0) / fwidth(v_uv);
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float screenPxDist = screenPxRange * (sd - 0.5);
        float opacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

        if (v_renderType < 1.5) {
            fragColor = vec4(v_color.rgb, v_color.a * opacity);
        } else if (v_renderType < 2.5) {
            // Outlined SDF text. When u_textShadowColor.a > 0 the same branch
            // also lays a drop-shadow underneath the outline+fill — TWOM-style
            // chunky text needs both at once, so we composite here rather than
            // making the caller pick mutually-exclusive Outlined/Shadow modes.
            float outlineDist = screenPxRange * (sd - u_outlineThickness);
            float outlineOp = clamp(outlineDist + 0.5, 0.0, 1.0);

            // Drop-shadow silhouette: sample the SDF at an offset and use the
            // outline threshold so the shadow has the same chunky shape as
            // the outline (rather than just the thin glyph stroke).
            vec2 shadowUV = v_uv - u_textShadowOffset;
            vec4 shadowSdf = texture(uTexture, shadowUV);
            float shadowSd = sdfDistance(shadowSdf);
            float shadowDist = screenPxRange * (shadowSd - u_outlineThickness);
            float shadowOp = clamp(shadowDist + 0.5, 0.0, 1.0) * u_textShadowColor.a;

            vec4 shadow  = vec4(u_textShadowColor.rgb, v_color.a * shadowOp);
            vec4 outline = vec4(u_outlineColor.rgb,    v_color.a * outlineOp * u_outlineColor.a);
            vec4 fill    = vec4(v_color.rgb,           v_color.a * opacity);

            // Back-to-front composite: shadow, outline, fill.
            fragColor = shadow;
            fragColor = mix(fragColor, outline, outline.a);
            fragColor = mix(fragColor, fill,    opacity);
        } else if (v_renderType < 3.5) {
            float glowOp = smoothstep(0.0, 0.5, sd);
            vec4 glow = vec4(u_textGlowColor.rgb, v_color.a * glowOp * u_textGlowIntensity);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(glow, fill, opacity);
        } else {
            vec2 shadowUV = v_uv - u_textShadowOffset;
            vec4 shadowSdf = texture(uTexture, shadowUV);
            float shadowSd = sdfDistance(shadowSdf);
            float shadowDist = screenPxRange * (shadowSd - 0.5);
            float shadowOp = clamp(shadowDist + 0.5, 0.0, 1.0) * u_textShadowColor.a;
            vec4 shadow = vec4(u_textShadowColor.rgb, v_color.a * shadowOp);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(shadow, fill, opacity);
        }

        if (fragColor.a < 0.01) discard;
    }
}
