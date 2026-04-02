#include <metal_stdlib>
using namespace metal;

struct FullscreenOut {
    float4 position [[position]];
    float2 v_uv;
};

struct BloomExtractUniforms {
    float u_threshold;
};

fragment float4 bloom_extract_fragment(FullscreenOut in [[stage_in]],
                                       constant BloomExtractUniforms& u [[buffer(0)]],
                                       texture2d<float> u_scene [[texture(0)]],
                                       sampler samp [[sampler(0)]]) {
    float3 color = u_scene.sample(samp, in.v_uv).rgb;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    // Hard threshold — add knee parameter later if bloom edges are too harsh
    return float4(color * step(u.u_threshold, luminance), 1.0);
}
