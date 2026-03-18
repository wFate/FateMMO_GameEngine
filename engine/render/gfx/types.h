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
enum class TextureFormat : uint8_t { RGBA8, RGB8, R8, Depth24Stencil8 };
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
