#include <metal_stdlib>
using namespace metal;

struct FullscreenOut {
    float4 position [[position]];
    float2 v_uv;
};

fragment float4 blit_fragment(FullscreenOut in [[stage_in]],
                              texture2d<float> u_texture [[texture(0)]],
                              sampler samp [[sampler(0)]]) {
    return u_texture.sample(samp, in.v_uv);
}
