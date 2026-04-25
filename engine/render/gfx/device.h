#pragma once
#include "engine/render/gfx/types.h"
#include <string>

namespace gfx {

class Device {
public:
    static Device& instance();

    bool init();
#ifdef FATEMMO_METAL
    bool initMetal(void* metalLayer);
    bool compileMetalShaderSource(const std::string& source, const std::string& label = "");
    bool loadMetalShaderLibrary(const std::string& path);
    // VFS-aware: load a pre-compiled metallib from memory (bytes already read
    // via IAssetSource). Required for FATE_USE_VFS=ON on macOS/iOS, where the
    // metallib may live inside an assets.pak archive.
    bool loadMetalShaderLibraryFromBytes(const void* data, size_t size,
                                         const std::string& label = "");
#endif
    void shutdown();
    static bool isAlive();

    // Resource creation
    ShaderHandle createShader(const std::string& vertSrc, const std::string& fragSrc);
    ShaderHandle createShaderFromFiles(const std::string& vertPath, const std::string& fragPath);
    TextureHandle createTexture(int width, int height, TextureFormat format,
                                const void* data = nullptr);
    TextureHandle createCompressedTexture(int width, int height, TextureFormat format,
                                          const void* data, size_t dataSize);
    BufferHandle createBuffer(BufferType type, BufferUsage usage,
                              size_t size, const void* data = nullptr);
    PipelineHandle createPipeline(const PipelineDesc& desc);
    FramebufferHandle createFramebuffer(int width, int height,
                                        TextureFormat colorFormat = TextureFormat::RGBA8,
                                        bool withDepthStencil = false);

    // Resource destruction
    void destroy(ShaderHandle h);
    void destroy(TextureHandle h);
    void destroy(BufferHandle h);
    void destroy(PipelineHandle h);
    void destroy(FramebufferHandle h);

    // Resource updates
    void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset = 0);
    void updateTexture(TextureHandle h, const void* data, int width, int height);

    // Queries
    TextureHandle getFramebufferTexture(FramebufferHandle h);
    void getFramebufferSize(FramebufferHandle h, int& outW, int& outH);

#ifndef FATEMMO_METAL
    // GL backend helpers — resolve handles to GL names (for CommandList use)
    unsigned int resolveGLShader(ShaderHandle h) const;
    unsigned int resolveGLTexture(TextureHandle h) const;
    unsigned int resolveGLBuffer(BufferHandle h) const;
    unsigned int resolveGLFramebuffer(FramebufferHandle h) const;
    unsigned int resolveGLPipelineVAO(PipelineHandle h) const;

    // Uniform location cache (per-shader program)
    int getUniformLocation(ShaderHandle shader, const char* name);
#else
    // Metal backend helpers — resolve handles to Metal objects (for CommandList use)
    void* resolveMetalTexture(TextureHandle h) const;
    void* resolveMetalBuffer(BufferHandle h) const;
    void* resolveMetalPipelineState(PipelineHandle h) const;
    void* resolveMetalDepthStencilState(PipelineHandle h) const;
    void* resolveMetalCommandQueue() const;                  // returns id<MTLCommandQueue>
    void* resolveMetalFramebufferPassDesc(FramebufferHandle h) const; // returns MTLRenderPassDescriptor*
    void* resolveMetalDefaultSampler() const;                // returns id<MTLSamplerState>
#endif

    // Shared across backends
    const PipelineDesc* resolvePipelineDesc(PipelineHandle h) const;
    BufferType getBufferType(BufferHandle h) const;

private:
    Device() = default;
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace gfx
