#include <metal_stdlib>
using namespace metal;

struct FullscreenOut {
    float4 position [[position]];
    float2 v_uv;
};

struct GridUniforms {
    float4x4 u_inverseVP;
    float u_gridSize;
    float u_zoom;
    float4 u_gridColor;
    float2 u_cameraPos;
};

fragment float4 grid_fragment(FullscreenOut in [[stage_in]],
                              constant GridUniforms& u [[buffer(0)]]) {
    // Reconstruct world position from screen UV + inverse VP
    float4 clipPos = float4(in.v_uv * 2.0 - 1.0, 0.0, 1.0);
    float4 worldPos4 = u.u_inverseVP * clipPos;
    float2 worldPos = worldPos4.xy / worldPos4.w;

    // Primary grid lines via fwidth anti-aliasing
    float2 grid = abs(fract(worldPos / u.u_gridSize - 0.5) - 0.5);
    float2 lineWidth = fwidth(worldPos / u.u_gridSize) * 1.5;
    float2 lines = smoothstep(lineWidth, float2(0.0), grid);
    float gridAlpha = max(lines.x, lines.y);

    // Sub-grid at 1/4 size when zoomed in past 2x
    float subAlpha = 0.0;
    if (u.u_zoom > 2.0) {
        float subSize = u.u_gridSize * 0.25;
        float2 subGrid = abs(fract(worldPos / subSize - 0.5) - 0.5);
        float2 subLineWidth = fwidth(worldPos / subSize) * 1.5;
        float2 subLines = smoothstep(subLineWidth, float2(0.0), subGrid);
        subAlpha = max(subLines.x, subLines.y) * 0.3; // faint
    }

    float totalAlpha = max(gridAlpha, subAlpha);

    // LOD fade at distance
    float dist = length(worldPos - u.u_cameraPos);
    float fade = 1.0 - smoothstep(u.u_gridSize * 20.0, u.u_gridSize * 30.0, dist);

    return u.u_gridColor * totalAlpha * fade;
}
