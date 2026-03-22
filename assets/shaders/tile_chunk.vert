layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in float aLayerIndex;
layout(location = 3) in vec4 aColor;

uniform mat4 uViewProjection;

out vec2 v_uv;
out float v_layerIndex;
out vec4 v_color;

void main() {
    gl_Position = uViewProjection * vec4(aPos, 0.0, 1.0);
    v_uv = aTexCoord;
    v_layerIndex = aLayerIndex;
    v_color = aColor;
}
