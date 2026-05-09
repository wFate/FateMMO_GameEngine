/**************************************************************************/
/*  metal_types.h                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
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
