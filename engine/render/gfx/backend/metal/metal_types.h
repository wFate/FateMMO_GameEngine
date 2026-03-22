#pragma once
#include "engine/render/gfx/types.h"
#include <cstdint>

#ifdef __OBJC__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

namespace gfx {
namespace metal {

struct MetalTextureEntry {
#ifdef __OBJC__
    id<MTLTexture> texture = nil;
#else
    void* texture = nullptr;
#endif
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::RGBA8;
};

struct MetalBufferEntry {
#ifdef __OBJC__
    id<MTLBuffer> buffer = nil;
#else
    void* buffer = nullptr;
#endif
    BufferType type = BufferType::Vertex;
    BufferUsage usage = BufferUsage::Static;
    size_t size = 0;
};

struct MetalShaderEntry {
#ifdef __OBJC__
    id<MTLFunction> vertexFunc = nil;
    id<MTLFunction> fragmentFunc = nil;
#else
    void* vertexFunc = nullptr;
    void* fragmentFunc = nullptr;
#endif
};

struct MetalPipelineEntry {
#ifdef __OBJC__
    id<MTLRenderPipelineState> pipelineState = nil;
    id<MTLDepthStencilState> depthStencilState = nil;
#else
    void* pipelineState = nullptr;
    void* depthStencilState = nullptr;
#endif
    PipelineDesc desc;
};

struct MetalFramebufferEntry {
#ifdef __OBJC__
    id<MTLTexture> colorTexture = nil;
    id<MTLTexture> depthStencilTexture = nil;
    MTLRenderPassDescriptor* renderPassDesc = nil;
#else
    void* colorTexture = nullptr;
    void* depthStencilTexture = nullptr;
    void* renderPassDesc = nullptr;
#endif
    int width = 0;
    int height = 0;
    uint32_t colorTextureHandleId = 0;
};

} // namespace metal
} // namespace gfx
