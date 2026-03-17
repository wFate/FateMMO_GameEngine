#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_texture;

void main() {
    fragColor = texture(u_texture, v_uv);
}
