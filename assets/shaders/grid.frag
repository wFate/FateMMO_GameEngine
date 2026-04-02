in vec2 v_uv;
out vec4 fragColor;

uniform mat4 u_inverseVP;
uniform float u_gridSize;
uniform float u_zoom;
uniform vec4 u_gridColor;
uniform vec2 u_cameraPos;

float gridLine(vec2 worldPos, float size, float lineThickness) {
    vec2 coord = worldPos / size;
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    vec2 line = smoothstep(lineThickness, 0.0, grid);
    return max(line.x, line.y);
}

void main() {
    // Reconstruct world position from screen UV + inverse VP
    vec4 clipPos = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
    vec4 worldPos4 = u_inverseVP * clipPos;
    vec2 worldPos = worldPos4.xy / worldPos4.w;

    // Minor and major grid lines
    float minorLine = gridLine(worldPos, u_gridSize, 1.0);
    float majorLine = gridLine(worldPos, u_gridSize * 10.0, 1.5);

    // Sub-grid at 1/4 size when zoomed in past 2x
    float subLine = 0.0;
    if (u_zoom > 2.0) {
        subLine = gridLine(worldPos, u_gridSize * 0.25, 1.0) * 0.3;
    }

    // World-origin axes (X = red, Y = green)
    vec2 axisDeriv = fwidth(worldPos);
    vec2 axisGrid = abs(worldPos) / axisDeriv;
    vec2 axisLine = smoothstep(2.0, 0.0, axisGrid);

    // Fade minor grid when zoomed out to prevent moire
    float zoomFade = clamp(u_zoom, 0.1, 1.0);

    vec4 subColor   = u_gridColor * vec4(1.0, 1.0, 1.0, 0.2 * zoomFade);
    vec4 minorColor = u_gridColor * vec4(1.0, 1.0, 1.0, 0.4 * zoomFade);
    vec4 majorColor = u_gridColor * vec4(1.0, 1.0, 1.0, 0.8);
    vec4 xAxisColor = vec4(0.8, 0.2, 0.2, 0.9);
    vec4 yAxisColor = vec4(0.2, 0.8, 0.2, 0.9);

    // Combine layers: sub -> minor -> major -> axes
    vec4 finalColor = subColor * subLine;
    finalColor = mix(finalColor, minorColor, minorLine);
    finalColor = mix(finalColor, majorColor, majorLine);
    finalColor = mix(finalColor, xAxisColor, axisLine.y);
    finalColor = mix(finalColor, yAxisColor, axisLine.x);

    if (finalColor.a < 0.01) discard;

    fragColor = finalColor;
}
