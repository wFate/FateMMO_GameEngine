#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform mat4 u_inverseVP;
uniform float u_gridSize;
uniform float u_zoom;
uniform vec4 u_gridColor;
uniform vec2 u_cameraPos;

void main() {
    // Reconstruct world position from screen UV + inverse VP
    vec4 clipPos = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
    vec4 worldPos4 = u_inverseVP * clipPos;
    vec2 worldPos = worldPos4.xy / worldPos4.w;

    // Primary grid lines via fwidth anti-aliasing
    vec2 grid = abs(fract(worldPos / u_gridSize - 0.5) - 0.5);
    vec2 lineWidth = fwidth(worldPos / u_gridSize) * 1.5;
    vec2 lines = smoothstep(lineWidth, vec2(0.0), grid);
    float gridAlpha = max(lines.x, lines.y);

    // Sub-grid at 1/4 size when zoomed in past 2x
    float subAlpha = 0.0;
    if (u_zoom > 2.0) {
        float subSize = u_gridSize * 0.25;
        vec2 subGrid = abs(fract(worldPos / subSize - 0.5) - 0.5);
        vec2 subLineWidth = fwidth(worldPos / subSize) * 1.5;
        vec2 subLines = smoothstep(subLineWidth, vec2(0.0), subGrid);
        subAlpha = max(subLines.x, subLines.y) * 0.3; // faint
    }

    float totalAlpha = max(gridAlpha, subAlpha);

    // LOD fade at distance
    float dist = length(worldPos - u_cameraPos);
    float fade = 1.0 - smoothstep(u_gridSize * 20.0, u_gridSize * 30.0, dist);

    fragColor = u_gridColor * totalAlpha * fade;
}
