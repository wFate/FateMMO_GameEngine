#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_scene;
uniform float u_threshold;

void main() {
    vec3 color = texture(u_scene, v_uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // Hard threshold — add knee parameter later if bloom edges are too harsh
    fragColor = vec4(color * step(u_threshold, luminance), 1.0);
}
