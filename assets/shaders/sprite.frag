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

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
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

    if (v_renderType < 0.5) {
        fragColor = texture(uTexture, v_uv) * v_color;
        if (fragColor.a < 0.01) discard;
    } else {
        vec4 sdf = texture(uTexture, v_uv);
        float sd = median(sdf.r, sdf.g, sdf.b);
        vec2 unitRange = vec2(u_pxRange) / u_atlasSize;
        vec2 screenTexSize = vec2(1.0) / fwidth(v_uv);
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float screenPxDist = screenPxRange * (sd - 0.5);
        float opacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

        if (v_renderType < 1.5) {
            fragColor = vec4(v_color.rgb, v_color.a * opacity);
        } else if (v_renderType < 2.5) {
            float outlineDist = screenPxRange * (sd - 0.35);
            float outlineOp = clamp(outlineDist + 0.5, 0.0, 1.0);
            vec4 outline = vec4(0.0, 0.0, 0.0, v_color.a * outlineOp);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(outline, fill, opacity);
        } else if (v_renderType < 3.5) {
            float glowOp = smoothstep(0.0, 0.5, sdf.a);
            vec4 glow = vec4(v_color.rgb, v_color.a * glowOp * 0.6);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(glow, fill, opacity);
        } else {
            vec2 shadowUV = v_uv - u_shadowOffset;
            vec4 shadowSdf = texture(uTexture, shadowUV);
            float shadowSd = median(shadowSdf.r, shadowSdf.g, shadowSdf.b);
            float shadowDist = screenPxRange * (shadowSd - 0.5);
            float shadowOp = clamp(shadowDist + 0.5, 0.0, 1.0) * 0.5;
            vec4 shadow = vec4(0.0, 0.0, 0.0, v_color.a * shadowOp);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(shadow, fill, opacity);
        }

        if (fragColor.a < 0.01) discard;
    }
}
