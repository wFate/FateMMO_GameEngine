in vec2 v_uv;
in float v_layerIndex;
in vec4 v_color;

uniform sampler2DArray u_tileArray;

out vec4 fragColor;

void main() {
    vec4 texel = texture(u_tileArray, vec3(v_uv, v_layerIndex));
    fragColor = texel * v_color;
    if (fragColor.a < 0.01) discard;
}
