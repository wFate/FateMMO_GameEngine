in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloomStrength;
uniform float u_vignetteRadius;
uniform float u_vignetteSmooth;
uniform vec3 u_colorTint;
uniform float u_brightness;
uniform float u_contrast;

void main() {
    vec3 color = texture(u_scene, v_uv).rgb;

    // Bloom composite
    vec3 bloom = texture(u_bloom, v_uv).rgb;
    color += bloom * u_bloomStrength;

    // Vignette
    float dist = distance(v_uv, vec2(0.5));
    float vignette = smoothstep(u_vignetteRadius, u_vignetteRadius - u_vignetteSmooth, dist);
    color *= vignette;

    // Color grading
    color *= u_colorTint;
    color = (color - 0.5) * u_contrast + 0.5;
    color *= u_brightness;

    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
