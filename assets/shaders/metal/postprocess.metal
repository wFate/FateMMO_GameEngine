#include <metal_stdlib>
using namespace metal;

struct FullscreenOut {
    float4 position [[position]];
    float2 v_uv;
};

struct PostProcessUniforms {
    float u_bloomStrength;
    float u_vignetteRadius;
    float u_vignetteSmooth;
    float3 u_colorTint;
    float u_brightness;
    float u_contrast;
};

fragment float4 postprocess_fragment(FullscreenOut in [[stage_in]],
                                     constant PostProcessUniforms& u [[buffer(0)]],
                                     texture2d<float> u_scene [[texture(0)]],
                                     texture2d<float> u_bloom [[texture(1)]],
                                     sampler samp [[sampler(0)]]) {
    float3 color = u_scene.sample(samp, in.v_uv).rgb;

    // Bloom composite
    float3 bloom = u_bloom.sample(samp, in.v_uv).rgb;
    color += bloom * u.u_bloomStrength;

    // Vignette
    float dist = distance(in.v_uv, float2(0.5));
    float vignette = smoothstep(u.u_vignetteRadius, u.u_vignetteRadius - u.u_vignetteSmooth, dist);
    color *= vignette;

    // Color grading
    color *= u.u_colorTint;
    color = (color - 0.5) * u.u_contrast + 0.5;
    color *= u.u_brightness;

    return float4(clamp(color, 0.0, 1.0), 1.0);
}
