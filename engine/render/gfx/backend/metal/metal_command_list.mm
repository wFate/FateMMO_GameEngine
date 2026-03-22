// metal_command_list.mm — CommandList implementation for the Metal backend.
// Compiled on Apple platforms only (FATEMMO_METAL).
//
// Design: Metal is a deferred, command-buffer API.  GL could issue draw calls
// immediately; Metal requires an MTLRenderCommandEncoder that is created from a
// render-pass descriptor, which in turn names the target texture.  We therefore
// accumulate framebuffer/clear state and create the encoder lazily just before
// the first draw-related call via ensureEncoder().
//
// File-static state is used (matching the GL backend's minimal-member pattern)
// because CommandList objects are cheap value types and only one is ever active
// per frame.
//
// Uniform buffer contract:
//   The scratch buffer (s_uniformData) is reset to offset 0 after every draw
//   call.  Callers must re-set all uniforms needed for the next draw.  The
//   order in which setUniform() is called MUST match the field order of the
//   corresponding MSL uniform struct (e.g. LightUniforms, PostProcessUniforms)
//   because Metal uses positional buffer layout, not named bindings.
//
//   Integer setUniform() calls are intentional no-ops on Metal: in GLSL they
//   bind texture samplers by name (e.g. setUniform("u_scene", 0)), but Metal
//   uses explicit [[texture(N)]] bindings — no integer slots in the uniform
//   buffer.

#import "engine/render/gfx/command_list.h"
#import "engine/render/gfx/device.h"
#import "engine/render/gfx/backend/metal/metal_types.h"
#import "engine/core/logger.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstring>
#include <cstdint>

namespace gfx {

// ---------------------------------------------------------------------------
// File-static Metal rendering state
// ---------------------------------------------------------------------------

static id<MTLCommandBuffer>         s_commandBuffer  = nil;
static id<MTLRenderCommandEncoder>  s_encoder        = nil;
static id<CAMetalDrawable>          s_drawable       = nil;

// Deferred encoder creation state
static FramebufferHandle            s_pendingFB{};
static bool                         s_hasPendingFB   = false;
static float                        s_clearColor[4]  = {0.f, 0.f, 0.f, 1.f};
static bool                         s_clearDepth     = false;
static bool                         s_needsClear     = false;

// Bound resources
static PipelineHandle               s_boundPipeline{};
static BufferHandle                 s_boundIndexBuffer{};

// Uniform scratch buffer (written with setUniform, flushed before each draw).
// Reset to offset 0 after every draw so each draw starts with a clean block.
static uint8_t                      s_uniformData[4096];
static size_t                       s_uniformOffset  = 0;
static bool                         s_uniformsDirty  = false;

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// Align offset up to a 4-byte boundary.
static inline size_t align4(size_t v) { return (v + 3u) & ~3u; }

// Write raw bytes into the scratch buffer, advance offset.
static void writeUniform(const void* src, size_t bytes) {
    size_t aligned = align4(bytes);
    if (s_uniformOffset + aligned > sizeof(s_uniformData)) {
        LOG_ERROR("metal", "CommandList: uniform scratch buffer overflow");
        return;
    }
    std::memcpy(s_uniformData + s_uniformOffset, src, bytes);
    // Zero-pad the alignment gap so the GPU sees clean padding.
    if (aligned > bytes) {
        std::memset(s_uniformData + s_uniformOffset + bytes, 0, aligned - bytes);
    }
    s_uniformOffset += aligned;
    s_uniformsDirty  = true;
}

// Create the MTLRenderCommandEncoder for the pending framebuffer.
// Must be called before any draw or state-setting call.
static void ensureEncoder() {
    if (s_encoder) return;
    if (!s_hasPendingFB) return;

    @autoreleasepool {
        MTLRenderPassDescriptor* passDesc = nil;

        if (s_pendingFB.id == 0) {
            // Screen / default back-buffer target.
            if (!s_drawable) {
                LOG_ERROR("metal", "CommandList::ensureEncoder: no drawable set for screen target");
                return;
            }
            passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
            passDesc.colorAttachments[0].texture = s_drawable.texture;
        } else {
            // Offscreen framebuffer — fetch the pre-built render pass descriptor
            // from the Device (created in metal_device.mm).
            passDesc = (__bridge MTLRenderPassDescriptor*)
                Device::instance().resolveMetalFramebufferPassDesc(s_pendingFB);
            if (!passDesc) {
                LOG_ERROR("metal", "CommandList::ensureEncoder: invalid framebuffer handle %u",
                          s_pendingFB.id);
                return;
            }
        }

        if (s_needsClear) {
            passDesc.colorAttachments[0].loadAction  = MTLLoadActionClear;
            passDesc.colorAttachments[0].clearColor  =
                MTLClearColorMake(s_clearColor[0], s_clearColor[1],
                                  s_clearColor[2], s_clearColor[3]);
            if (s_clearDepth && passDesc.depthAttachment.texture) {
                passDesc.depthAttachment.loadAction  = MTLLoadActionClear;
                passDesc.depthAttachment.clearDepth  = 1.0;
            }
        } else {
            passDesc.colorAttachments[0].loadAction  = MTLLoadActionLoad;
            if (passDesc.depthAttachment.texture) {
                passDesc.depthAttachment.loadAction  = MTLLoadActionLoad;
            }
        }

        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        if (passDesc.depthAttachment.texture) {
            passDesc.depthAttachment.storeAction = MTLStoreActionStore;
        }

        s_encoder = [s_commandBuffer renderCommandEncoderWithDescriptor:passDesc];
        if (!s_encoder) {
            LOG_ERROR("metal", "CommandList::ensureEncoder: failed to create render encoder");
            s_hasPendingFB = false;
            s_needsClear   = false;
            return;
        }

        // Bind the default sampler (Nearest/ClampToEdge) at [[sampler(0)]].
        // All Metal shaders declare `sampler samp [[sampler(0)]]` — without
        // this binding the GPU would use undefined sampler state.
        id<MTLSamplerState> samp =
            (__bridge id<MTLSamplerState>)Device::instance().resolveMetalDefaultSampler();
        if (samp) {
            [s_encoder setFragmentSamplerState:samp atIndex:0];
        } else {
            LOG_WARN("metal", "CommandList::ensureEncoder: default sampler not available");
        }

        s_hasPendingFB = false;
        s_needsClear   = false;
    }
}

// Push all accumulated uniform data to both vertex and fragment stages, then
// reset the scratch buffer offset so the next draw starts fresh.
static void flushUniforms() {
    if (!s_uniformsDirty || s_uniformOffset == 0) return;
    [s_encoder setVertexBytes:s_uniformData   length:s_uniformOffset atIndex:1];
    [s_encoder setFragmentBytes:s_uniformData length:s_uniformOffset atIndex:0];
    // Reset offset after each draw so callers re-supply uniforms per draw.
    s_uniformOffset = 0;
    s_uniformsDirty = false;
}

// Map our backend-agnostic PrimitiveType to the Metal enum.
static MTLPrimitiveType toMTLPrimitive(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Triangles: return MTLPrimitiveTypeTriangle;
        case PrimitiveType::Lines:     return MTLPrimitiveTypeLine;
        case PrimitiveType::Points:    return MTLPrimitiveTypePoint;
    }
    return MTLPrimitiveTypeTriangle;
}

// Reset all file-static state to a clean slate.
static void resetState() {
    s_commandBuffer     = nil;
    s_encoder           = nil;
    // NOTE: s_drawable is reset only after presentDrawable so the caller can
    // re-set it once per frame; we clear it in submit() after presenting.
    s_pendingFB         = {};
    s_hasPendingFB      = false;
    s_clearColor[0]     = 0.f;
    s_clearColor[1]     = 0.f;
    s_clearColor[2]     = 0.f;
    s_clearColor[3]     = 1.f;
    s_clearDepth        = false;
    s_needsClear        = false;
    s_boundPipeline     = {};
    s_boundIndexBuffer  = {};
    s_uniformOffset     = 0;
    s_uniformsDirty     = false;
}

// ---------------------------------------------------------------------------
// CommandList implementation
// ---------------------------------------------------------------------------

void CommandList::begin() {
    currentPipeline_ = {};

    id<MTLCommandQueue> queue =
        (__bridge id<MTLCommandQueue>)Device::instance().resolveMetalCommandQueue();
    if (!queue) {
        LOG_ERROR("metal", "CommandList::begin: no Metal command queue available");
        return;
    }

    resetState();
    s_commandBuffer = [queue commandBuffer];
    if (!s_commandBuffer) {
        LOG_ERROR("metal", "CommandList::begin: failed to create command buffer");
    }
}

void CommandList::end() {
    // No-op — matches GL immediate-mode behaviour.
    // submit() is the actual flush point.
}

void CommandList::setFramebuffer(FramebufferHandle fb) {
    // End any in-progress encoder for the previous render pass.
    if (s_encoder) {
        [s_encoder endEncoding];
        s_encoder = nil;
    }

    s_pendingFB    = fb;
    s_hasPendingFB = true;
    // Clear state is reset; the next clear() call will set loadAction.
    s_needsClear   = false;
}

void CommandList::setViewport(int x, int y, int w, int h) {
    ensureEncoder();
    if (!s_encoder) return;
    MTLViewport vp;
    vp.originX = static_cast<double>(x);
    vp.originY = static_cast<double>(y);
    vp.width   = static_cast<double>(w);
    vp.height  = static_cast<double>(h);
    vp.znear   = 0.0;
    vp.zfar    = 1.0;
    [s_encoder setViewport:vp];
}

void CommandList::clear(float r, float g, float b, float a, bool clearDepth) {
    // Store the clear parameters; they are consumed by ensureEncoder() when the
    // encoder is actually created.  If the encoder already exists (e.g., a
    // redundant clear call) we can't retroactively change the load action, so
    // log a warning and skip silently.
    if (s_encoder) {
        LOG_WARN("metal",
                 "CommandList::clear called after encoder was already created — ignored");
        return;
    }
    s_clearColor[0] = r;
    s_clearColor[1] = g;
    s_clearColor[2] = b;
    s_clearColor[3] = a;
    s_clearDepth    = clearDepth;
    s_needsClear    = true;
}

void CommandList::bindPipeline(PipelineHandle pipeline) {
    ensureEncoder();
    if (!s_encoder) return;

    currentPipeline_ = pipeline;
    s_boundPipeline  = pipeline;

    auto& dev = Device::instance();

    id<MTLRenderPipelineState> ps =
        (__bridge id<MTLRenderPipelineState>)dev.resolveMetalPipelineState(pipeline);
    if (ps) {
        [s_encoder setRenderPipelineState:ps];
    } else {
        LOG_ERROR("metal", "CommandList::bindPipeline: invalid pipeline state");
    }

    id<MTLDepthStencilState> ds =
        (__bridge id<MTLDepthStencilState>)dev.resolveMetalDepthStencilState(pipeline);
    if (ds) {
        [s_encoder setDepthStencilState:ds];
    }
}

void CommandList::bindTexture(int slot, TextureHandle texture) {
    ensureEncoder();
    if (!s_encoder) return;

    id<MTLTexture> tex =
        (__bridge id<MTLTexture>)Device::instance().resolveMetalTexture(texture);
    if (tex) {
        [s_encoder setFragmentTexture:tex atIndex:static_cast<NSUInteger>(slot)];
    } else {
        LOG_WARN("metal", "CommandList::bindTexture: invalid texture handle %u", texture.id);
    }
}

void CommandList::bindVertexBuffer(BufferHandle buffer) {
    ensureEncoder();
    if (!s_encoder) return;

    id<MTLBuffer> buf =
        (__bridge id<MTLBuffer>)Device::instance().resolveMetalBuffer(buffer);
    if (buf) {
        // Vertex data is bound at index 0; uniforms are bound at index 1.
        [s_encoder setVertexBuffer:buf offset:0 atIndex:0];
    } else {
        LOG_WARN("metal", "CommandList::bindVertexBuffer: invalid buffer handle %u", buffer.id);
    }
}

void CommandList::bindIndexBuffer(BufferHandle buffer) {
    // Store the handle; the actual MTLBuffer is resolved at draw time.
    s_boundIndexBuffer = buffer;
}

// ---------------------------------------------------------------------------
// setUniform overloads — write typed values into the scratch buffer.
//
// IMPORTANT: The order of setUniform() calls before each draw must match the
// field order of the MSL uniform struct for the bound shader.  Metal uses
// positional buffer layout (no named bindings).
//
// Integer overload is a deliberate no-op: in GLSL, integer uniforms are used
// to bind texture samplers by unit index (e.g. setUniform("u_scene", 0)).
// Metal uses explicit [[texture(N)]] attribute bindings instead, so writing
// an integer into the uniform buffer would corrupt the struct layout.
// ---------------------------------------------------------------------------

void CommandList::setUniform(const char* name, float value) {
    (void)name;  // Metal uses buffer slots, not named uniforms.
    writeUniform(&value, sizeof(float));
}

void CommandList::setUniform(const char* name, int value) {
    // No-op on Metal: integer uniforms are GLSL sampler-slot hints and have no
    // corresponding field in any MSL uniform struct.
    (void)name;
    (void)value;
}

void CommandList::setUniform(const char* name, const fate::Vec2& value) {
    (void)name;
    writeUniform(&value, sizeof(fate::Vec2));
}

void CommandList::setUniform(const char* name, const fate::Vec3& value) {
    (void)name;
    writeUniform(&value, sizeof(fate::Vec3));
}

void CommandList::setUniform(const char* name, const fate::Color& value) {
    (void)name;
    writeUniform(&value, sizeof(fate::Color));
}

void CommandList::setUniform(const char* name, const fate::Mat4& value) {
    (void)name;
    writeUniform(value.data(), sizeof(float) * 16);
}

// ---------------------------------------------------------------------------
// Draw calls
// ---------------------------------------------------------------------------

void CommandList::draw(PrimitiveType type, int vertexCount, int firstVertex) {
    ensureEncoder();
    if (!s_encoder) return;
    flushUniforms();
    [s_encoder drawPrimitives:toMTLPrimitive(type)
                  vertexStart:static_cast<NSUInteger>(firstVertex)
                  vertexCount:static_cast<NSUInteger>(vertexCount)];
}

void CommandList::drawIndexed(PrimitiveType type, int indexCount, int firstIndex) {
    ensureEncoder();
    if (!s_encoder) return;
    flushUniforms();

    id<MTLBuffer> ebo =
        (__bridge id<MTLBuffer>)Device::instance().resolveMetalBuffer(s_boundIndexBuffer);
    if (!ebo) {
        LOG_ERROR("metal", "CommandList::drawIndexed: no index buffer bound");
        return;
    }

    NSUInteger byteOffset = static_cast<NSUInteger>(firstIndex) * sizeof(uint32_t);
    [s_encoder drawIndexedPrimitives:toMTLPrimitive(type)
                          indexCount:static_cast<NSUInteger>(indexCount)
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:ebo
                   indexBufferOffset:byteOffset];
}

// ---------------------------------------------------------------------------
// Frame submission
// ---------------------------------------------------------------------------

void CommandList::submit() {
    if (!s_commandBuffer) return;

    if (s_encoder) {
        [s_encoder endEncoding];
        s_encoder = nil;
    }

    if (s_drawable) {
        [s_commandBuffer presentDrawable:s_drawable];
    }

    [s_commandBuffer commit];

    // Clear everything — next begin() will start fresh.
    s_drawable = nil;
    resetState();
}

// ---------------------------------------------------------------------------
// Metal-specific public helpers
// ---------------------------------------------------------------------------

#ifdef FATEMMO_METAL

void* CommandList::currentEncoder() const {
    ensureEncoder();
    return (__bridge void*)s_encoder;
}

void CommandList::setDrawable(void* drawable) {
    s_drawable = (__bridge id<CAMetalDrawable>)drawable;
}

#endif // FATEMMO_METAL

} // namespace gfx
