#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;
layout (location = 3) in float aRenderType;

out vec2 v_uv;
out vec4 v_color;
out float v_renderType;

uniform mat4 uViewProjection;

void main() {
    gl_Position = uViewProjection * vec4(aPos, 0.0, 1.0);
    v_uv = aTexCoord;
    v_color = aColor;
    v_renderType = aRenderType;
}
