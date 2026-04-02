#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

struct TileChunkIn {
    float2 aPos        [[attribute(0)]];
    float2 aTexCoord   [[attribute(1)]];
    float  aLayerIndex [[attribute(2)]];
    float4 aColor      [[attribute(3)]];
};

struct TileChunkOut {
    float4 position   [[position]];
    float2 v_uv;
    float  v_layerIndex;
    float4 v_color;
};

struct TileChunkUniforms {
    float4x4 uViewProjection;
};

// ---------------------------------------------------------------------------
// Vertex function
// ---------------------------------------------------------------------------

vertex TileChunkOut tile_chunk_vertex(TileChunkIn in [[stage_in]],
                                      constant TileChunkUniforms& u [[buffer(1)]])
{
    TileChunkOut out;
    out.position    = u.uViewProjection * float4(in.aPos, 0.0, 1.0);
    out.v_uv        = in.aTexCoord;
    out.v_layerIndex = in.aLayerIndex;
    out.v_color     = in.aColor;
    return out;
}

// ---------------------------------------------------------------------------
// Fragment function
// ---------------------------------------------------------------------------

fragment float4 tile_chunk_fragment(TileChunkOut in [[stage_in]],
                                    texture2d_array<float> u_tileArray [[texture(0)]],
                                    sampler samp                        [[sampler(0)]])
{
    float4 texel  = u_tileArray.sample(samp, in.v_uv, (uint)in.v_layerIndex);
    float4 color  = texel * in.v_color;
    if (color.a < 0.01) discard_fragment();
    return color;
}
