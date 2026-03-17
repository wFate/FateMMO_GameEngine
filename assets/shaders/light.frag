#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform vec2 u_lightPos;
uniform vec3 u_lightColor;
uniform float u_lightRadius;
uniform float u_lightIntensity;
uniform float u_lightFalloff;
uniform vec2 u_resolution;

void main() {
    vec2 pixelPos = v_uv * u_resolution;
    float dist = distance(pixelPos, u_lightPos);
    float attenuation = 1.0 - pow(clamp(dist / u_lightRadius, 0.0, 1.0), u_lightFalloff);
    vec3 light = u_lightColor * u_lightIntensity * attenuation;
    fragColor = vec4(light, 1.0);
}
