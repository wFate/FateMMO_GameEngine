#include <metal_stdlib>
using namespace metal;

struct FullscreenOut {
    float4 position [[position]];
    float2 v_uv;
};

vertex FullscreenOut fullscreen_quad_vertex(uint vid [[vertex_id]]) {
    float2 pos = float2((vid << 1) & 2, vid & 2);
    FullscreenOut out;
    out.position = float4(pos * 2.0 - 1.0, 0.0, 1.0);
    out.v_uv = float2(pos.x, 1.0 - pos.y);  // flip Y for Metal top-left origin
    return out;
}
