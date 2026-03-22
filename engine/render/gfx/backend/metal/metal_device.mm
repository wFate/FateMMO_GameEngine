// metal_device.mm — Metal backend implementation of gfx::Device
// Compiled on Apple platforms only (FATEMMO_METAL must be defined).
#ifdef FATEMMO_METAL

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/metal/metal_types.h"
#include "engine/render/gfx/backend/metal/metal_shader_lib.h"
#include "engine/core/logger.h"

#include <unordered_map>
#include <utility>
#include <string>
#include <vector>
#include <cstring>

namespace gfx {

// ============================================================================
// Impl
// ============================================================================
struct Device::Impl {
    uint32_t nextId = 1;

    id<MTLDevice>       device       = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer*       metalLayer   = nil;
    id<MTLSamplerState> defaultSampler = nil;

    gfx::metal::MetalShaderLib shaderLib;

    std::unordered_map<uint32_t, gfx::metal::MetalTextureEntry>     textures;
    std::unordered_map<uint32_t, gfx::metal::MetalBufferEntry>      buffers;
    std::unordered_map<uint32_t, gfx::metal::MetalShaderEntry>      shaders;
    std::unordered_map<uint32_t, gfx::metal::MetalPipelineEntry>    pipelines;
    std::unordered_map<uint32_t, gfx::metal::MetalFramebufferEntry> framebuffers;
    std::unordered_map<uint32_t, std::pair<int,int>>                 fbSizes;
    std::unordered_map<uint32_t, uint32_t>                           fbTextureHandles;

    uint32_t allocId() { return nextId++; }
};

// ============================================================================
// Helpers (file-local)
// ============================================================================

// Map TextureFormat to MTLPixelFormat for uncompressed formats.
static MTLPixelFormat toMTLPixelFormat(TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8:           return MTLPixelFormatRGBA8Unorm;
        case TextureFormat::RGB8:            return MTLPixelFormatRGBA8Unorm; // padded to RGBA
        case TextureFormat::R8:              return MTLPixelFormatR8Unorm;
        case TextureFormat::Depth24Stencil8: return MTLPixelFormatDepth32Float_Stencil8;
        case TextureFormat::ETC2_RGBA8:      return MTLPixelFormatEAC_RGBA8;
        case TextureFormat::ASTC_4x4_RGBA:   return MTLPixelFormatASTC_4x4_LDR;
        case TextureFormat::ASTC_8x8_RGBA:   return MTLPixelFormatASTC_8x8_LDR;
    }
    return MTLPixelFormatRGBA8Unorm;
}

// Derive the base shader name from a file path.
// e.g. "assets/shaders/sprite.vert" -> "sprite"
static std::string deriveShaderBaseName(const std::string& path) {
    // Strip directory component
    size_t slashPos = path.find_last_of("/\\");
    std::string filename = (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
    // Strip extension
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        filename = filename.substr(0, dotPos);
    }
    return filename;
}

// ============================================================================
// Singleton
// ============================================================================
Device& Device::instance() {
    static Device s;
    return s;
}

// ============================================================================
// init / initMetal / shutdown
// ============================================================================
bool Device::init() {
    // No-op on Metal — use initMetal(metalLayer) instead.
    return true;
}

bool Device::initMetal(void* metalLayerPtr) {
    if (impl_) return true; // already initialised

    @autoreleasepool {
        impl_ = new Impl();

        // Store the CAMetalLayer
        impl_->metalLayer = (__bridge CAMetalLayer*)metalLayerPtr;

        // Create the MTLDevice (the system default GPU)
        impl_->device = MTLCreateSystemDefaultDevice();
        if (!impl_->device) {
            LOG_ERROR("gfx", "Metal: MTLCreateSystemDefaultDevice() returned nil");
            delete impl_;
            impl_ = nullptr;
            return false;
        }

        // Wire the layer to this device
        impl_->metalLayer.device = impl_->device;

        // Create the command queue
        impl_->commandQueue = [impl_->device newCommandQueue];
        if (!impl_->commandQueue) {
            LOG_ERROR("gfx", "Metal: failed to create MTLCommandQueue");
            delete impl_;
            impl_ = nullptr;
            return false;
        }

        // Create the default sampler (Nearest, ClampToEdge)
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        samplerDesc.minFilter    = MTLSamplerMinMagFilterNearest;
        samplerDesc.magFilter    = MTLSamplerMinMagFilterNearest;
        samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        impl_->defaultSampler = [impl_->device newSamplerStateWithDescriptor:samplerDesc];

        // Initialise the shader library (caller is responsible for calling
        // shaderLib.loadMetallib / shaderLib.compileSource before creating shaders)
        impl_->shaderLib.init((__bridge void*)impl_->device);

        LOG_INFO("gfx", "Metal Device initialised (%s)", [[impl_->device name] UTF8String]);
    }
    return true;
}

void Device::shutdown() {
    if (!impl_) return;

    // ARC releases all Metal objects when entries are cleared from the maps.
    impl_->textures.clear();
    impl_->buffers.clear();
    impl_->shaders.clear();
    impl_->pipelines.clear();
    impl_->framebuffers.clear();
    impl_->fbSizes.clear();
    impl_->fbTextureHandles.clear();

    impl_->shaderLib.shutdown();

    impl_->defaultSampler = nil;
    impl_->commandQueue   = nil;
    impl_->metalLayer     = nil;
    impl_->device         = nil;

    delete impl_;
    impl_ = nullptr;
    LOG_INFO("gfx", "Metal Device shut down");
}

// ============================================================================
// Shader
// ============================================================================
ShaderHandle Device::createShader(const std::string& vertSrc, const std::string& fragSrc) {
    // On Metal the "source strings" are treated as the base name to look up
    // pre-compiled functions from the shader library.
    // vertSrc is expected to be the base name (e.g. "sprite").
    if (!impl_) return {};

    @autoreleasepool {
        const std::string& baseName = vertSrc;

        void* vFunc = impl_->shaderLib.getFunction(baseName + "_vertex");
        void* fFunc = impl_->shaderLib.getFunction(baseName + "_fragment");

        if (!vFunc) {
            LOG_ERROR("gfx", "Metal: vertex function '%s_vertex' not found in shader library",
                      baseName.c_str());
            return {};
        }
        if (!fFunc) {
            LOG_ERROR("gfx", "Metal: fragment function '%s_fragment' not found in shader library",
                      baseName.c_str());
            return {};
        }

        gfx::metal::MetalShaderEntry entry;
        entry.vertexFunc   = (__bridge id<MTLFunction>)vFunc;
        entry.fragmentFunc = (__bridge id<MTLFunction>)fFunc;

        uint32_t id = impl_->allocId();
        impl_->shaders[id] = entry;
        return { id };
    }
}

ShaderHandle Device::createShaderFromFiles(const std::string& vertPath, const std::string& fragPath) {
    if (!impl_) return {};

    @autoreleasepool {
        std::string baseName = deriveShaderBaseName(vertPath);

        void* vFunc = impl_->shaderLib.getFunction(baseName + "_vertex");
        void* fFunc = impl_->shaderLib.getFunction(baseName + "_fragment");

        if (!vFunc) {
            LOG_ERROR("gfx", "Metal: vertex function '%s_vertex' not found (from path '%s')",
                      baseName.c_str(), vertPath.c_str());
            return {};
        }
        if (!fFunc) {
            LOG_ERROR("gfx", "Metal: fragment function '%s_fragment' not found (from path '%s')",
                      baseName.c_str(), fragPath.c_str());
            return {};
        }

        gfx::metal::MetalShaderEntry entry;
        entry.vertexFunc   = (__bridge id<MTLFunction>)vFunc;
        entry.fragmentFunc = (__bridge id<MTLFunction>)fFunc;

        uint32_t id = impl_->allocId();
        impl_->shaders[id] = entry;
        return { id };
    }
}

// ============================================================================
// Texture
// ============================================================================
TextureHandle Device::createTexture(int width, int height, TextureFormat format, const void* data) {
    if (!impl_) return {};

    @autoreleasepool {
        MTLPixelFormat mtlFmt = toMTLPixelFormat(format);

        MTLTextureDescriptor* texDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlFmt
                                                              width:(NSUInteger)width
                                                             height:(NSUInteger)height
                                                          mipmapped:NO];
        texDesc.usage         = MTLTextureUsageShaderRead;
        texDesc.storageMode   = MTLStorageModeShared;

        id<MTLTexture> texture = [impl_->device newTextureWithDescriptor:texDesc];
        if (!texture) {
            LOG_ERROR("gfx", "Metal: failed to create texture (%dx%d)", width, height);
            return {};
        }

        if (data) {
            MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height);

            if (format == TextureFormat::RGB8) {
                // Pad RGB8 to RGBA8 by inserting alpha=255 for each pixel
                size_t pixelCount = static_cast<size_t>(width) * height;
                std::vector<uint8_t> rgba(pixelCount * 4);
                const uint8_t* src = static_cast<const uint8_t*>(data);
                uint8_t* dst = rgba.data();
                for (size_t i = 0; i < pixelCount; ++i) {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 255;
                    src += 3;
                    dst += 4;
                }
                [texture replaceRegion:region
                           mipmapLevel:0
                             withBytes:rgba.data()
                           bytesPerRow:(NSUInteger)(width * 4)];
            } else if (format == TextureFormat::R8) {
                [texture replaceRegion:region
                           mipmapLevel:0
                             withBytes:data
                           bytesPerRow:(NSUInteger)(width * 1)];
            } else {
                // RGBA8 and others
                [texture replaceRegion:region
                           mipmapLevel:0
                             withBytes:data
                           bytesPerRow:(NSUInteger)(width * 4)];
            }
        }

        gfx::metal::MetalTextureEntry entry;
        entry.texture = texture;
        entry.width   = width;
        entry.height  = height;
        entry.format  = format;

        uint32_t id = impl_->allocId();
        impl_->textures[id] = entry;
        return { id };
    }
}

TextureHandle Device::createCompressedTexture(int width, int height, TextureFormat format,
                                               const void* data, size_t dataSize) {
    if (!impl_) return {};

    @autoreleasepool {
        MTLPixelFormat mtlFmt;
        NSUInteger blockSize = 0;
        NSUInteger blockDim  = 0;

        switch (format) {
            case TextureFormat::ETC2_RGBA8:
                mtlFmt    = MTLPixelFormatEAC_RGBA8;
                blockSize = 16; // 16 bytes per 4x4 block
                blockDim  = 4;
                break;
            case TextureFormat::ASTC_4x4_RGBA:
                mtlFmt    = MTLPixelFormatASTC_4x4_LDR;
                blockSize = 16;
                blockDim  = 4;
                break;
            case TextureFormat::ASTC_8x8_RGBA:
                mtlFmt    = MTLPixelFormatASTC_8x8_LDR;
                blockSize = 16;
                blockDim  = 8;
                break;
            default:
                LOG_ERROR("gfx", "Metal: createCompressedTexture called with non-compressed format");
                return {};
        }

        MTLTextureDescriptor* texDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlFmt
                                                              width:(NSUInteger)width
                                                             height:(NSUInteger)height
                                                          mipmapped:NO];
        texDesc.usage       = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;

        id<MTLTexture> texture = [impl_->device newTextureWithDescriptor:texDesc];
        if (!texture) {
            LOG_ERROR("gfx", "Metal: failed to create compressed texture (%dx%d)", width, height);
            return {};
        }

        if (data) {
            MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height);
            // Bytes per row for compressed formats: number of block columns * bytes per block
            NSUInteger blocksWide = ((NSUInteger)width  + blockDim - 1) / blockDim;
            NSUInteger bytesPerRow = blocksWide * blockSize;

            [texture replaceRegion:region
                       mipmapLevel:0
                         withBytes:data
                       bytesPerRow:bytesPerRow];
        }

        gfx::metal::MetalTextureEntry entry;
        entry.texture = texture;
        entry.width   = width;
        entry.height  = height;
        entry.format  = format;

        uint32_t id = impl_->allocId();
        impl_->textures[id] = entry;
        return { id };
    }
}

// ============================================================================
// Buffer
// ============================================================================
BufferHandle Device::createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data) {
    if (!impl_) return {};

    @autoreleasepool {
        id<MTLBuffer> buffer = nil;

        if (data) {
            buffer = [impl_->device newBufferWithBytes:data
                                               length:size
                                              options:MTLResourceStorageModeShared];
        } else {
            buffer = [impl_->device newBufferWithLength:size
                                               options:MTLResourceStorageModeShared];
        }

        if (!buffer) {
            LOG_ERROR("gfx", "Metal: failed to create buffer (size=%zu)", size);
            return {};
        }

        gfx::metal::MetalBufferEntry entry;
        entry.buffer = buffer;
        entry.type   = type;
        entry.usage  = usage;
        entry.size   = size;

        uint32_t id = impl_->allocId();
        impl_->buffers[id] = entry;
        return { id };
    }
}

// ============================================================================
// Pipeline
// ============================================================================
PipelineHandle Device::createPipeline(const PipelineDesc& desc) {
    if (!impl_) return {};

    @autoreleasepool {
        // Resolve the shader entry
        auto sit = impl_->shaders.find(desc.shader.id);
        if (sit == impl_->shaders.end()) {
            LOG_ERROR("gfx", "Metal: createPipeline — shader handle %u not found", desc.shader.id);
            return {};
        }
        const gfx::metal::MetalShaderEntry& shaderEntry = sit->second;

        // Build the render pipeline descriptor
        MTLRenderPipelineDescriptor* pipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pipeDesc.vertexFunction   = shaderEntry.vertexFunc;
        pipeDesc.fragmentFunction = shaderEntry.fragmentFunc;
        pipeDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        // Build vertex descriptor from VertexLayout
        MTLVertexDescriptor* vertDesc = [[MTLVertexDescriptor alloc] init];
        for (const auto& attr : desc.vertexLayout.attributes) {
            MTLVertexFormat fmt;
            switch (attr.components) {
                case 1: fmt = MTLVertexFormatFloat;  break;
                case 2: fmt = MTLVertexFormatFloat2; break;
                case 3: fmt = MTLVertexFormatFloat3; break;
                default: fmt = MTLVertexFormatFloat4; break;
            }
            vertDesc.attributes[attr.location].format      = fmt;
            vertDesc.attributes[attr.location].offset      = attr.offset;
            vertDesc.attributes[attr.location].bufferIndex = 0;
        }
        vertDesc.layouts[0].stride       = desc.vertexLayout.stride;
        vertDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        pipeDesc.vertexDescriptor = vertDesc;

        // Blend mode
        auto& ca = pipeDesc.colorAttachments[0];
        switch (desc.blendMode) {
            case BlendMode::None:
                ca.blendingEnabled = NO;
                break;

            case BlendMode::Alpha:
                ca.blendingEnabled               = YES;
                ca.sourceRGBBlendFactor          = MTLBlendFactorSourceAlpha;
                ca.destinationRGBBlendFactor     = MTLBlendFactorOneMinusSourceAlpha;
                ca.sourceAlphaBlendFactor        = MTLBlendFactorSourceAlpha;
                ca.destinationAlphaBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
                break;

            case BlendMode::Additive:
                ca.blendingEnabled               = YES;
                ca.sourceRGBBlendFactor          = MTLBlendFactorSourceAlpha;
                ca.destinationRGBBlendFactor     = MTLBlendFactorOne;
                ca.sourceAlphaBlendFactor        = MTLBlendFactorSourceAlpha;
                ca.destinationAlphaBlendFactor   = MTLBlendFactorOne;
                break;

            case BlendMode::Multiplicative:
                ca.blendingEnabled               = YES;
                ca.sourceRGBBlendFactor          = MTLBlendFactorDestinationColor;
                ca.destinationRGBBlendFactor     = MTLBlendFactorZero;
                ca.sourceAlphaBlendFactor        = MTLBlendFactorDestinationAlpha;
                ca.destinationAlphaBlendFactor   = MTLBlendFactorZero;
                break;
        }

        // Create the pipeline state
        NSError* pipeError = nil;
        id<MTLRenderPipelineState> pipelineState =
            [impl_->device newRenderPipelineStateWithDescriptor:pipeDesc error:&pipeError];
        if (!pipelineState) {
            LOG_ERROR("gfx", "Metal: failed to create pipeline state: %s",
                      [[pipeError localizedDescription] UTF8String]);
            return {};
        }

        // Depth/stencil state
        MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
        dsDesc.depthCompareFunction =
            desc.depthTest ? MTLCompareFunctionLess : MTLCompareFunctionAlways;
        dsDesc.depthWriteEnabled = desc.depthWrite ? YES : NO;
        id<MTLDepthStencilState> dsState =
            [impl_->device newDepthStencilStateWithDescriptor:dsDesc];

        gfx::metal::MetalPipelineEntry entry;
        entry.pipelineState      = pipelineState;
        entry.depthStencilState  = dsState;
        entry.desc               = desc;

        uint32_t id = impl_->allocId();
        impl_->pipelines[id] = entry;
        return { id };
    }
}

// ============================================================================
// Framebuffer
// ============================================================================
FramebufferHandle Device::createFramebuffer(int width, int height,
                                            TextureFormat colorFormat,
                                            bool withDepthStencil) {
    if (!impl_) return {};

    @autoreleasepool {
        // Color texture (renderable)
        MTLTextureDescriptor* colorDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:toMTLPixelFormat(colorFormat)
                                                              width:(NSUInteger)width
                                                             height:(NSUInteger)height
                                                          mipmapped:NO];
        colorDesc.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDesc.storageMode = MTLStorageModePrivate;

        id<MTLTexture> colorTexture = [impl_->device newTextureWithDescriptor:colorDesc];
        if (!colorTexture) {
            LOG_ERROR("gfx", "Metal: failed to create framebuffer color texture (%dx%d)", width, height);
            return {};
        }

        // Optional depth/stencil texture
        id<MTLTexture> depthStencilTexture = nil;
        if (withDepthStencil) {
            MTLTextureDescriptor* dsTexDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                  width:(NSUInteger)width
                                                                 height:(NSUInteger)height
                                                              mipmapped:NO];
            dsTexDesc.usage       = MTLTextureUsageRenderTarget;
            dsTexDesc.storageMode = MTLStorageModePrivate;
            depthStencilTexture = [impl_->device newTextureWithDescriptor:dsTexDesc];
        }

        // Build a reusable MTLRenderPassDescriptor
        MTLRenderPassDescriptor* rpDesc = [[MTLRenderPassDescriptor alloc] init];
        rpDesc.colorAttachments[0].texture     = colorTexture;
        rpDesc.colorAttachments[0].loadAction  = MTLLoadActionClear;
        rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpDesc.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

        if (depthStencilTexture) {
            rpDesc.depthAttachment.texture     = depthStencilTexture;
            rpDesc.depthAttachment.loadAction  = MTLLoadActionClear;
            rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
            rpDesc.depthAttachment.clearDepth  = 1.0;

            rpDesc.stencilAttachment.texture     = depthStencilTexture;
            rpDesc.stencilAttachment.loadAction  = MTLLoadActionClear;
            rpDesc.stencilAttachment.storeAction = MTLStoreActionDontCare;
        }

        // Register the color texture in the textures map so getFramebufferTexture works
        uint32_t texId = impl_->allocId();
        {
            gfx::metal::MetalTextureEntry texEntry;
            texEntry.texture = colorTexture;
            texEntry.width   = width;
            texEntry.height  = height;
            texEntry.format  = colorFormat;
            impl_->textures[texId] = texEntry;
        }

        // Store the framebuffer entry
        uint32_t fbId = impl_->allocId();
        {
            gfx::metal::MetalFramebufferEntry fbEntry;
            fbEntry.colorTexture         = colorTexture;
            fbEntry.depthStencilTexture  = depthStencilTexture;
            fbEntry.renderPassDesc       = rpDesc;
            fbEntry.width                = width;
            fbEntry.height               = height;
            fbEntry.colorTextureHandleId = texId;
            impl_->framebuffers[fbId] = fbEntry;
        }

        impl_->fbSizes[fbId]          = { width, height };
        impl_->fbTextureHandles[fbId] = texId;

        return { fbId };
    }
}

// ============================================================================
// Resource destruction
// ============================================================================
void Device::destroy(ShaderHandle h) {
    if (!h.valid() || !impl_) return;
    impl_->shaders.erase(h.id); // ARC releases the MTLFunction objects
}

void Device::destroy(TextureHandle h) {
    if (!h.valid() || !impl_) return;
    impl_->textures.erase(h.id); // ARC releases the MTLTexture
}

void Device::destroy(BufferHandle h) {
    if (!h.valid() || !impl_) return;
    impl_->buffers.erase(h.id); // ARC releases the MTLBuffer
}

void Device::destroy(PipelineHandle h) {
    if (!h.valid() || !impl_) return;
    impl_->pipelines.erase(h.id); // ARC releases pipeline/depth-stencil states
}

void Device::destroy(FramebufferHandle h) {
    if (!h.valid() || !impl_) return;

    // Remove the registered color texture handle from the textures map
    auto thit = impl_->fbTextureHandles.find(h.id);
    if (thit != impl_->fbTextureHandles.end()) {
        impl_->textures.erase(thit->second);
        impl_->fbTextureHandles.erase(thit);
    }

    impl_->fbSizes.erase(h.id);
    impl_->framebuffers.erase(h.id); // ARC releases textures and render pass descriptor
}

// ============================================================================
// Resource updates
// ============================================================================
void Device::updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) {
    if (!h.valid() || !impl_ || !data) return;
    auto it = impl_->buffers.find(h.id);
    if (it == impl_->buffers.end()) return;

    void* dst = static_cast<uint8_t*>([it->second.buffer contents]) + offset;
    std::memcpy(dst, data, size);
}

void Device::updateTexture(TextureHandle h, const void* data, int width, int height) {
    if (!h.valid() || !impl_ || !data) return;
    auto it = impl_->textures.find(h.id);
    if (it == impl_->textures.end()) return;

    MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height);

    // Determine bytes per row based on format
    NSUInteger bytesPerRow = 0;
    switch (it->second.format) {
        case TextureFormat::R8:   bytesPerRow = (NSUInteger)width * 1; break;
        default:                  bytesPerRow = (NSUInteger)width * 4; break;
    }

    [it->second.texture replaceRegion:region
                           mipmapLevel:0
                             withBytes:data
                           bytesPerRow:bytesPerRow];
}

// ============================================================================
// Queries
// ============================================================================
TextureHandle Device::getFramebufferTexture(FramebufferHandle h) {
    if (!h.valid() || !impl_) return {};
    auto it = impl_->fbTextureHandles.find(h.id);
    if (it == impl_->fbTextureHandles.end()) return {};
    return { it->second };
}

void Device::getFramebufferSize(FramebufferHandle h, int& outW, int& outH) {
    if (!h.valid() || !impl_) { outW = 0; outH = 0; return; }
    auto it = impl_->fbSizes.find(h.id);
    if (it == impl_->fbSizes.end()) { outW = 0; outH = 0; return; }
    outW = it->second.first;
    outH = it->second.second;
}

// ============================================================================
// Metal resolve helpers
// ============================================================================
void* Device::resolveMetalTexture(TextureHandle h) const {
    if (!h.valid() || !impl_) return nullptr;
    auto it = impl_->textures.find(h.id);
    if (it == impl_->textures.end()) return nullptr;
    return (__bridge void*)it->second.texture;
}

void* Device::resolveMetalBuffer(BufferHandle h) const {
    if (!h.valid() || !impl_) return nullptr;
    auto it = impl_->buffers.find(h.id);
    if (it == impl_->buffers.end()) return nullptr;
    return (__bridge void*)it->second.buffer;
}

void* Device::resolveMetalPipelineState(PipelineHandle h) const {
    if (!h.valid() || !impl_) return nullptr;
    auto it = impl_->pipelines.find(h.id);
    if (it == impl_->pipelines.end()) return nullptr;
    return (__bridge void*)it->second.pipelineState;
}

void* Device::resolveMetalDepthStencilState(PipelineHandle h) const {
    if (!h.valid() || !impl_) return nullptr;
    auto it = impl_->pipelines.find(h.id);
    if (it == impl_->pipelines.end()) return nullptr;
    return (__bridge void*)it->second.depthStencilState;
}

// ============================================================================
// Shared helpers
// ============================================================================
const PipelineDesc* Device::resolvePipelineDesc(PipelineHandle h) const {
    if (!h.valid() || !impl_) return nullptr;
    auto it = impl_->pipelines.find(h.id);
    return (it != impl_->pipelines.end()) ? &it->second.desc : nullptr;
}

BufferType Device::getBufferType(BufferHandle h) const {
    if (!h.valid() || !impl_) return BufferType::Vertex;
    auto it = impl_->buffers.find(h.id);
    return (it != impl_->buffers.end()) ? it->second.type : BufferType::Vertex;
}

} // namespace gfx

#endif // FATEMMO_METAL
