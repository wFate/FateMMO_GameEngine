#include <metal_stdlib>
using namespace metal;

struct FullscreenOut {
    float4 position [[position]];
    float2 v_uv;
};

struct LightUniforms {
    float2 u_lightPos;
    float3 u_lightColor;
    float u_lightRadius;
    float u_lightIntensity;
    float u_lightFalloff;
    float2 u_resolution;
};

fragment float4 light_fragment(FullscreenOut in [[stage_in]],
                               constant LightUniforms& u [[buffer(0)]]) {
    float2 pixelPos = in.v_uv * u.u_resolution;
    float dist = distance(pixelPos, u.u_lightPos);
    float attenuation = 1.0 - pow(clamp(dist / u.u_lightRadius, 0.0, 1.0), u.u_lightFalloff);
    float3 light = u.u_lightColor * u.u_lightIntensity * attenuation;
    return float4(light, 1.0);
}
