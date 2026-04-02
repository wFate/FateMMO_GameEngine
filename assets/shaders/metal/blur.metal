#include <metal_stdlib>
using namespace metal;

struct FullscreenOut {
    float4 position [[position]];
    float2 v_uv;
};

struct BlurUniforms {
    float2 u_direction; // (1/w, 0) for horizontal, (0, 1/h) for vertical
};

// 9-tap Gaussian weights
constant float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

fragment float4 blur_fragment(FullscreenOut in [[stage_in]],
                              constant BlurUniforms& u [[buffer(0)]],
                              texture2d<float> u_texture [[texture(0)]],
                              sampler samp [[sampler(0)]]) {
    float3 result = u_texture.sample(samp, in.v_uv).rgb * weights[0];
    for (int i = 1; i < 5; i++) {
        float2 offset = u.u_direction * float(i);
        result += u_texture.sample(samp, in.v_uv + offset).rgb * weights[i];
        result += u_texture.sample(samp, in.v_uv - offset).rgb * weights[i];
    }
    return float4(result, 1.0);
}
