// RHI Migration Status (Phase 5):
// MIGRATED:
//   - Shader: creation/destruction via Device, bind/uniforms direct GL
//   - Texture: creation/destruction via Device, bind direct GL
//   - Framebuffer: creation/destruction via Device, bind direct GL
//   - SpriteBatch: resources via Device, draws via CommandList (with direct GL fallback)
//   - Lighting passes: fully migrated to CommandList
//   - Post-process passes: fully migrated to CommandList
//   - FullscreenQuad: VAO via Device
//   - SDFText: atlas texture via Device
// INTENTIONALLY DIRECT GL:
//   - App initialization (glGetString, initial state, window resize)
//   - App FBO blit (interleaved with ImGui)
//   - Editor (ImGui integration)

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace gfx {

struct ShaderHandle      { uint32_t id = 0; bool valid() const { return id != 0; } };
struct TextureHandle     { uint32_t id = 0; bool valid() const { return id != 0; } };
struct BufferHandle      { uint32_t id = 0; bool valid() const { return id != 0; } };
struct PipelineHandle    { uint32_t id = 0; bool valid() const { return id != 0; } };
struct FramebufferHandle { uint32_t id = 0; bool valid() const { return id != 0; } };

inline bool operator==(ShaderHandle a, ShaderHandle b) { return a.id == b.id; }
inline bool operator==(TextureHandle a, TextureHandle b) { return a.id == b.id; }
inline bool operator==(BufferHandle a, BufferHandle b) { return a.id == b.id; }
inline bool operator==(PipelineHandle a, PipelineHandle b) { return a.id == b.id; }
inline bool operator==(FramebufferHandle a, FramebufferHandle b) { return a.id == b.id; }

enum class BufferType    : uint8_t { Vertex, Index, Uniform };
enum class BufferUsage   : uint8_t { Static, Dynamic, Stream };
enum class TextureFormat : uint8_t {
    RGBA8, RGB8, R8, Depth24Stencil8,
    // Compressed formats (GPU-native, loaded from KTX files)
    ETC2_RGBA8,      // GL_COMPRESSED_RGBA8_ETC2_EAC — mandatory in GLES 3.0, 8 bpp
    ASTC_4x4_RGBA,   // GL_COMPRESSED_RGBA_ASTC_4x4_KHR — best quality, 8 bpp
    ASTC_8x8_RGBA    // GL_COMPRESSED_RGBA_ASTC_8x8_KHR — best compression, 2 bpp
};

// Returns true if the format uses GPU-compressed blocks (uploaded via glCompressedTexImage2D).
inline bool isCompressedFormat(TextureFormat fmt) {
    return fmt == TextureFormat::ETC2_RGBA8 ||
           fmt == TextureFormat::ASTC_4x4_RGBA ||
           fmt == TextureFormat::ASTC_8x8_RGBA;
}

// Estimate VRAM bytes for a texture in the given format.
inline size_t estimateTextureBytes(int width, int height, TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8:           return static_cast<size_t>(width) * height * 4;
        case TextureFormat::RGB8:            return static_cast<size_t>(width) * height * 3;
        case TextureFormat::R8:              return static_cast<size_t>(width) * height;
        case TextureFormat::Depth24Stencil8: return static_cast<size_t>(width) * height * 4;
        // Compressed: 16 bytes per block
        case TextureFormat::ETC2_RGBA8:      // 4x4 blocks
        case TextureFormat::ASTC_4x4_RGBA:   return static_cast<size_t>(((width+3)/4)) * ((height+3)/4) * 16;
        case TextureFormat::ASTC_8x8_RGBA:   return static_cast<size_t>(((width+7)/8)) * ((height+7)/8) * 16;
    }
    return static_cast<size_t>(width) * height * 4;
}
enum class BlendMode     : uint8_t { None, Alpha, Additive, Multiplicative };
enum class PrimitiveType : uint8_t { Triangles, Lines, Points };

struct VertexAttribute {
    int location = 0;
    int components = 0;
    size_t offset = 0;
    bool normalized = false;
};

struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    size_t stride = 0;
};

struct PipelineDesc {
    ShaderHandle shader;
    VertexLayout vertexLayout;
    BlendMode blendMode = BlendMode::Alpha;
    bool depthTest = false;
    bool depthWrite = false;
};

} // namespace gfx
