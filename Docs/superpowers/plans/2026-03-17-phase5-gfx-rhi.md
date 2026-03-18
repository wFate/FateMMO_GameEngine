# Phase 5B: Graphics RHI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Abstract all OpenGL calls behind a render hardware interface (Device, CommandList, Pipeline State Objects) so render code never touches GL directly.

**Architecture:** `gfx::Device` creates resources as typed handles, `gfx::CommandList` records draw commands, GL backend replays them. Existing Shader/Texture/Framebuffer classes become thin wrappers. Migration is incremental — each task leaves the engine buildable and running.

**Tech Stack:** C++20, OpenGL 3.3 Core (via existing SDL2 GL context)

**Spec:** `Docs/superpowers/specs/2026-03-17-phase5-job-system-gfx-rhi-design.md` (Part 2)

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine
```

---

## File Structure

```
engine/render/gfx/
  types.h                  — Handles, enums, VertexLayout, PipelineDesc
  device.h                 — gfx::Device class declaration
  command_list.h           — gfx::CommandList class declaration
  backend/gl/
    gl_device.cpp          — OpenGL Device implementation
    gl_command_list.cpp    — OpenGL CommandList implementation
    gl_loader.h            — GL function pointers (moved from engine/render/)
    gl_loader.cpp          — GL function pointer loading (moved from engine/render/)
```

**Modified files:**
- `engine/render/shader.h/.cpp` — Wrap `gfx::ShaderHandle`
- `engine/render/texture.h/.cpp` — Wrap `gfx::TextureHandle`
- `engine/render/framebuffer.h/.cpp` — Wrap `gfx::FramebufferHandle`
- `engine/render/sprite_batch.h/.cpp` — Use CommandList, remove `fate::BlendMode`
- `engine/render/render_graph.h/.cpp` — Add `CommandList*` to RenderPassContext
- `engine/render/fullscreen_quad.h/.cpp` — Use CommandList
- `engine/render/lighting.cpp` — Use CommandList
- `engine/render/post_process.cpp` — Use CommandList
- `engine/render/sdf_text.h/.cpp` — Replace raw GL texture ID with TextureHandle
- `engine/app.cpp` — Init gfx::Device, pass CommandList through render loop

---

### Task 1: RHI Types

**Files:**
- Create: `engine/render/gfx/types.h`

- [ ] **Step 1: Create the types header**

```cpp
// engine/render/gfx/types.h
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace gfx {

// Typed resource handles — no raw GL integers outside the backend
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
    int components = 0;   // 1-4
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
```

- [ ] **Step 2: Build to verify header compiles**

Expected: compiles clean (header-only, included by nothing yet).

- [ ] **Step 3: Commit**

```bash
git add engine/render/gfx/types.h
git commit -m "feat(gfx): add RHI type definitions — handles, enums, pipeline desc"
```

---

### Task 2: Move gl_loader Into Backend

**Files:**
- Move: `engine/render/gl_loader.h` → `engine/render/gfx/backend/gl/gl_loader.h`
- Move: `engine/render/gl_loader.cpp` → `engine/render/gfx/backend/gl/gl_loader.cpp`
- Modify: All files that `#include "engine/render/gl_loader.h"`

- [ ] **Step 1: Create backend directory and move files**

```bash
mkdir -p engine/render/gfx/backend/gl
git mv engine/render/gl_loader.h engine/render/gfx/backend/gl/gl_loader.h
git mv engine/render/gl_loader.cpp engine/render/gfx/backend/gl/gl_loader.cpp
```

- [ ] **Step 2: Update all includes**

Search for `#include "engine/render/gl_loader.h"` and replace with `#include "engine/render/gfx/backend/gl/gl_loader.h"` in:
- `engine/render/shader.cpp`
- `engine/render/texture.cpp`
- `engine/render/framebuffer.cpp`
- `engine/render/sprite_batch.cpp`
- `engine/render/fullscreen_quad.cpp` (if exists, or `.h`)
- `engine/render/lighting.cpp`
- `engine/render/post_process.cpp`
- `engine/render/sdf_text.cpp`
- `engine/editor/editor.cpp`
- `engine/app.cpp`

- [ ] **Step 3: Build to verify nothing broke**

Expected: compiles clean — just moved files and updated paths.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor(gfx): move gl_loader into backend/gl/ directory"
```

---

### Task 3: gfx::Device — Resource Creation

**Files:**
- Create: `engine/render/gfx/device.h`
- Create: `engine/render/gfx/backend/gl/gl_device.cpp`

- [ ] **Step 1: Write Device interface**

```cpp
// engine/render/gfx/device.h
#pragma once
#include "engine/render/gfx/types.h"
#include <string>

namespace gfx {

class Device {
public:
    static Device& instance();

    bool init();
    void shutdown();

    // Resource creation
    ShaderHandle createShader(const std::string& vertSrc, const std::string& fragSrc);
    ShaderHandle createShaderFromFiles(const std::string& vertPath, const std::string& fragPath);
    TextureHandle createTexture(int width, int height, TextureFormat format,
                                const void* data = nullptr);
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

    // Uniform location cache (per-shader program)
    int getUniformLocation(ShaderHandle shader, const char* name);

private:
    Device() = default;
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace gfx
```

- [ ] **Step 2: Write GL backend implementation**

Create `engine/render/gfx/backend/gl/gl_device.cpp` — implements all `Device` methods using the GL function pointers from `gl_loader.h`. Internally maintains lookup tables mapping handle IDs to GL object names:

```cpp
// engine/render/gfx/backend/gl/gl_device.cpp
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"
#include <unordered_map>
#include <fstream>
#include <sstream>

namespace gfx {

struct Device::Impl {
    uint32_t nextId = 1;

    // GL name lookup tables
    std::unordered_map<uint32_t, unsigned int> shaders;       // handle → GL program
    std::unordered_map<uint32_t, unsigned int> textures;      // handle → GL texture
    std::unordered_map<uint32_t, unsigned int> buffers;       // handle → GL buffer
    std::unordered_map<uint32_t, unsigned int> framebuffers;  // handle → GL FBO
    std::unordered_map<uint32_t, unsigned int> fbTextures;    // handle → GL FBO color texture
    std::unordered_map<uint32_t, unsigned int> fbRenderbuffers; // handle → GL FBO RBO
    std::unordered_map<uint32_t, std::pair<int,int>> fbSizes; // handle → (width, height)
    std::unordered_map<uint32_t, PipelineDesc> pipelines;     // handle → pipeline desc
    std::unordered_map<uint32_t, unsigned int> pipelineVAOs;  // handle → GL VAO
    std::unordered_map<uint32_t, uint32_t> fbTextureHandles;  // FBO handle → cached texture handle
    std::unordered_map<uint32_t, BufferType> bufferTypes;     // handle → buffer type (for correct bind target)

    // Uniform location cache: (GL program, uniform name) → location
    std::unordered_map<uint64_t, int> uniformCache;

    uint32_t allocId() { return nextId++; }
};

Device& Device::instance() {
    static Device s;
    return s;
}

bool Device::init() {
    if (impl_) return true;
    impl_ = new Impl();
    // GL function pointers should already be loaded by App::init via loadGLFunctions()
    return true;
}

void Device::shutdown() {
    if (!impl_) return;
    // Destroy all remaining GL resources
    for (auto& [id, gl] : impl_->shaders) glDeleteProgram(gl);
    for (auto& [id, gl] : impl_->textures) glDeleteTextures(1, &gl);
    for (auto& [id, gl] : impl_->buffers) glDeleteBuffers(1, &gl);
    for (auto& [id, gl] : impl_->framebuffers) {
        glDeleteFramebuffers(1, &gl);
    }
    for (auto& [id, gl] : impl_->fbTextures) glDeleteTextures(1, &gl);
    for (auto& [id, gl] : impl_->fbRenderbuffers) glDeleteRenderbuffers(1, &gl);
    for (auto& [id, gl] : impl_->pipelineVAOs) glDeleteVertexArrays(1, &gl);
    delete impl_;
    impl_ = nullptr;
}

// --- Shader ---

ShaderHandle Device::createShader(const std::string& vertSrc, const std::string& fragSrc) {
    // Compile vertex shader
    unsigned int vert = glCreateShader(GL_VERTEX_SHADER);
    const char* vSrc = vertSrc.c_str();
    glShaderSource(vert, 1, &vSrc, nullptr);
    glCompileShader(vert);

    int ok;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(vert, 512, nullptr, log);
        LOG_ERROR("gfx::Device", "Vertex shader compile error: %s", log);
        glDeleteShader(vert);
        return {};
    }

    // Compile fragment shader
    unsigned int frag = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fSrc = fragSrc.c_str();
    glShaderSource(frag, 1, &fSrc, nullptr);
    glCompileShader(frag);

    glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(frag, 512, nullptr, log);
        LOG_ERROR("gfx::Device", "Fragment shader compile error: %s", log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return {};
    }

    // Link program
    unsigned int program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        LOG_ERROR("gfx::Device", "Shader link error: %s", log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        glDeleteProgram(program);
        return {};
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    uint32_t id = impl_->allocId();
    impl_->shaders[id] = program;
    return {id};
}

ShaderHandle Device::createShaderFromFiles(const std::string& vertPath, const std::string& fragPath) {
    auto readFile = [](const std::string& path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) return "";
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    std::string vertSrc = readFile(vertPath);
    std::string fragSrc = readFile(fragPath);
    if (vertSrc.empty() || fragSrc.empty()) {
        LOG_ERROR("gfx::Device", "Failed to read shader files: %s, %s", vertPath.c_str(), fragPath.c_str());
        return {};
    }
    return createShader(vertSrc, fragSrc);
}

// --- Texture ---

TextureHandle Device::createTexture(int width, int height, TextureFormat format, const void* data) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum glFormat, glInternal;
    switch (format) {
        case TextureFormat::RGBA8: glFormat = GL_RGBA; glInternal = GL_RGBA8; break;
        case TextureFormat::RGB8:  glFormat = GL_RGB;  glInternal = GL_RGB8;  break;
        case TextureFormat::R8:    glFormat = GL_RED;  glInternal = GL_R8;    break;
        default: glFormat = GL_RGBA; glInternal = GL_RGBA8; break;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, glInternal, width, height, 0, glFormat, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t id = impl_->allocId();
    impl_->textures[id] = tex;
    return {id};
}

void Device::updateTexture(TextureHandle h, const void* data, int width, int height) {
    auto it = impl_->textures.find(h.id);
    if (it == impl_->textures.end()) return;
    glBindTexture(GL_TEXTURE_2D, it->second);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// --- Buffer ---

BufferHandle Device::createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data) {
    unsigned int buf;
    glGenBuffers(1, &buf);

    GLenum target = (type == BufferType::Index) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    GLenum glUsage;
    switch (usage) {
        case BufferUsage::Static:  glUsage = GL_STATIC_DRAW; break;
        case BufferUsage::Dynamic: glUsage = GL_DYNAMIC_DRAW; break;
        case BufferUsage::Stream:  glUsage = GL_STREAM_DRAW; break;
    }

    glBindBuffer(target, buf);
    glBufferData(target, size, data, glUsage);
    glBindBuffer(target, 0);

    uint32_t id = impl_->allocId();
    impl_->buffers[id] = buf;
    impl_->bufferTypes[id] = type;
    return {id};
}

void Device::updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) {
    auto it = impl_->buffers.find(h.id);
    if (it == impl_->buffers.end()) return;
    auto tIt = impl_->bufferTypes.find(h.id);
    GLenum target = (tIt != impl_->bufferTypes.end() && tIt->second == BufferType::Index)
        ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    glBindBuffer(target, it->second);
    glBufferSubData(target, offset, size, data);
    glBindBuffer(target, 0);
}

// --- Framebuffer ---

FramebufferHandle Device::createFramebuffer(int width, int height, TextureFormat colorFormat, bool withDepthStencil) {
    unsigned int fbo, tex, rbo = 0;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color attachment
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    // Depth/stencil attachment
    if (withDepthStencil) {
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    uint32_t id = impl_->allocId();
    impl_->framebuffers[id] = fbo;
    impl_->fbTextures[id] = tex;
    impl_->fbRenderbuffers[id] = rbo;
    impl_->fbSizes[id] = {width, height};
    return {id};
}

TextureHandle Device::getFramebufferTexture(FramebufferHandle h) {
    // Return a stable texture handle for this FBO's color attachment
    // Cache it so repeated calls return the same handle (no leak)
    auto cacheIt = impl_->fbTextureHandles.find(h.id);
    if (cacheIt != impl_->fbTextureHandles.end()) return {cacheIt->second};

    auto it = impl_->fbTextures.find(h.id);
    if (it == impl_->fbTextures.end()) return {};

    uint32_t texId = impl_->allocId();
    impl_->textures[texId] = it->second;
    impl_->fbTextureHandles[h.id] = texId;
    return {texId};
}

void Device::getFramebufferSize(FramebufferHandle h, int& outW, int& outH) {
    auto it = impl_->fbSizes.find(h.id);
    if (it != impl_->fbSizes.end()) {
        outW = it->second.first;
        outH = it->second.second;
    }
}

// --- Pipeline ---

PipelineHandle Device::createPipeline(const PipelineDesc& desc) {
    // Create a VAO for this pipeline's vertex layout
    // NOTE: Vertex attribute pointers are NOT set here because no buffer is bound yet.
    // They are set in CommandList::bindVertexBuffer() using the pipeline's layout.
    unsigned int vao;
    glGenVertexArrays(1, &vao);

    uint32_t id = impl_->allocId();
    impl_->pipelines[id] = desc;
    impl_->pipelineVAOs[id] = vao;
    return {id};
}

// --- Destroy ---

void Device::destroy(ShaderHandle h) {
    auto it = impl_->shaders.find(h.id);
    if (it != impl_->shaders.end()) { glDeleteProgram(it->second); impl_->shaders.erase(it); }
}

void Device::destroy(TextureHandle h) {
    auto it = impl_->textures.find(h.id);
    if (it != impl_->textures.end()) { glDeleteTextures(1, &it->second); impl_->textures.erase(it); }
}

void Device::destroy(BufferHandle h) {
    auto it = impl_->buffers.find(h.id);
    if (it != impl_->buffers.end()) { glDeleteBuffers(1, &it->second); impl_->buffers.erase(it); }
}

void Device::destroy(PipelineHandle h) {
    auto vIt = impl_->pipelineVAOs.find(h.id);
    if (vIt != impl_->pipelineVAOs.end()) { glDeleteVertexArrays(1, &vIt->second); impl_->pipelineVAOs.erase(vIt); }
    impl_->pipelines.erase(h.id);
}

void Device::destroy(FramebufferHandle h) {
    auto it = impl_->framebuffers.find(h.id);
    if (it != impl_->framebuffers.end()) { glDeleteFramebuffers(1, &it->second); impl_->framebuffers.erase(it); }
    auto tIt = impl_->fbTextures.find(h.id);
    if (tIt != impl_->fbTextures.end()) { glDeleteTextures(1, &tIt->second); impl_->fbTextures.erase(tIt); }
    auto rIt = impl_->fbRenderbuffers.find(h.id);
    if (rIt != impl_->fbRenderbuffers.end() && rIt->second) { glDeleteRenderbuffers(1, &rIt->second); impl_->fbRenderbuffers.erase(rIt); }
    impl_->fbSizes.erase(h.id);
}

// --- Uniform Cache ---

int Device::getUniformLocation(ShaderHandle shader, const char* name) {
    auto sIt = impl_->shaders.find(shader.id);
    if (sIt == impl_->shaders.end()) return -1;
    unsigned int program = sIt->second;

    // Hash: program << 32 | nameHash
    uint64_t nameHash = std::hash<std::string>{}(name);
    uint64_t key = (static_cast<uint64_t>(program) << 32) | (nameHash & 0xFFFFFFFF);

    auto it = impl_->uniformCache.find(key);
    if (it != impl_->uniformCache.end()) return it->second;

    int loc = glGetUniformLocation(program, name);
    impl_->uniformCache[key] = loc;
    return loc;
}

} // namespace gfx
```

- [ ] **Step 3: Build to verify compiles**

Expected: compiles clean — Device is self-contained.

- [ ] **Step 4: Commit**

```bash
git add engine/render/gfx/device.h engine/render/gfx/backend/gl/gl_device.cpp
git commit -m "feat(gfx): add Device with GL backend — shader, texture, buffer, FBO, pipeline creation"
```

---

### Task 4: gfx::CommandList

**Files:**
- Create: `engine/render/gfx/command_list.h`
- Create: `engine/render/gfx/backend/gl/gl_command_list.cpp`

- [ ] **Step 1: Write CommandList interface**

```cpp
// engine/render/gfx/command_list.h
#pragma once
#include "engine/render/gfx/types.h"
#include "engine/core/types.h" // Vec2, Vec3, Color, Mat4

namespace gfx {

class CommandList {
public:
    void begin();
    void end();

    // Render target (FramebufferHandle{0} = default backbuffer)
    void setFramebuffer(FramebufferHandle fb);
    void setViewport(int x, int y, int w, int h);
    void clear(float r, float g, float b, float a, bool clearDepth = false);

    // Pipeline and resources
    void bindPipeline(PipelineHandle pipeline);
    void bindTexture(int slot, TextureHandle texture);
    void bindVertexBuffer(BufferHandle buffer);
    void bindIndexBuffer(BufferHandle buffer);

    // Uniforms (cached location lookup via Device)
    void setUniform(const char* name, float value);
    void setUniform(const char* name, int value);
    void setUniform(const char* name, const fate::Vec2& value);
    void setUniform(const char* name, const fate::Vec3& value);
    void setUniform(const char* name, const fate::Color& value);
    void setUniform(const char* name, const fate::Mat4& value);

    // Draw
    void draw(PrimitiveType type, int vertexCount, int firstVertex = 0);
    void drawIndexed(PrimitiveType type, int indexCount, int firstIndex = 0);

    // Submit all recorded commands to GPU
    void submit();

private:
    PipelineHandle currentPipeline_{}; // currently bound pipeline (per-CommandList state)
};

} // namespace gfx
```

- [ ] **Step 2: Write GL backend implementation**

Create `engine/render/gfx/backend/gl/gl_command_list.cpp` — each method directly executes the corresponding GL call (immediate mode, not deferred recording, for simplicity). The GL backend's CommandList is effectively a thin wrapper:

```cpp
// engine/render/gfx/backend/gl/gl_command_list.cpp
#include "engine/render/gfx/command_list.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"

namespace gfx {

// GL backend CommandList needs to resolve handles to GL names.
// Device exposes these via public methods (GL backend only):
//   Device::resolveGLShader(ShaderHandle) → unsigned int
//   Device::resolveGLTexture(TextureHandle) → unsigned int
//   Device::resolveGLBuffer(BufferHandle) → unsigned int
//   Device::resolveGLFramebuffer(FramebufferHandle) → unsigned int
//   Device::resolveGLPipelineVAO(PipelineHandle) → unsigned int
//   Device::resolvePipelineDesc(PipelineHandle) → const PipelineDesc*
//
// These are added to device.h and implemented in gl_device.cpp by looking up
// the handle ID in the Impl maps. Example:
//   unsigned int Device::resolveGLShader(ShaderHandle h) {
//       auto it = impl_->shaders.find(h.id);
//       return (it != impl_->shaders.end()) ? it->second : 0;
//   }
// Add all 6 resolve methods to Device following this pattern.

void CommandList::begin() {
    currentPipeline_ = {}; // member variable, not static
}

void CommandList::end() {
    // Nothing to do for immediate-mode GL backend
}

void CommandList::setFramebuffer(FramebufferHandle fb) {
    if (fb.id == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        unsigned int fbo = Device::instance().resolveGLFramebuffer(fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
}

void CommandList::setViewport(int x, int y, int w, int h) {
    glViewport(x, y, w, h);
}

void CommandList::clear(float r, float g, float b, float a, bool clearDepth) {
    glClearColor(r, g, b, a);
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (clearDepth) mask |= GL_DEPTH_BUFFER_BIT;
    glClear(mask);
}

void CommandList::bindPipeline(PipelineHandle pipeline) {
    currentPipeline_ = pipeline;
    const auto* desc = Device::instance().resolvePipelineDesc(pipeline);
    if (!desc) return;

    // Bind shader program
    unsigned int program = Device::instance().resolveGLShader(desc->shader);
    glUseProgram(program);

    // Bind VAO (vertex layout)
    unsigned int vao = Device::instance().resolveGLPipelineVAO(pipeline);
    glBindVertexArray(vao);

    // Set blend mode
    switch (desc->blendMode) {
        case BlendMode::None:
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            break;
        case BlendMode::Multiplicative:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
    }

    // Depth state
    if (desc->depthTest) { glEnable(GL_DEPTH_TEST); } else { glDisable(GL_DEPTH_TEST); }
    glDepthMask(desc->depthWrite ? GL_TRUE : GL_FALSE);
}

void CommandList::bindTexture(int slot, TextureHandle texture) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, Device::instance().resolveGLTexture(texture));
}

void CommandList::bindVertexBuffer(BufferHandle buffer) {
    glBindBuffer(GL_ARRAY_BUFFER, Device::instance().resolveGLBuffer(buffer));

    // Set up vertex attribute pointers now that a buffer is bound
    // Uses the currently bound pipeline's vertex layout
    if (currentPipeline_.valid()) {
        const auto* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
        if (desc) {
            for (const auto& attr : desc->vertexLayout.attributes) {
                glEnableVertexAttribArray(attr.location);
                glVertexAttribPointer(attr.location, attr.components, GL_FLOAT,
                                      attr.normalized ? GL_TRUE : GL_FALSE,
                                      (GLsizei)desc->vertexLayout.stride,
                                      reinterpret_cast<const void*>(attr.offset));
            }
        }
    }
}

void CommandList::bindIndexBuffer(BufferHandle buffer) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Device::instance().resolveGLBuffer(buffer));
}

// --- Uniforms ---

void CommandList::setUniform(const char* name, float value) {
    if (!currentPipeline_.valid()) return;
    const auto* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform1f(loc, value);
}

void CommandList::setUniform(const char* name, int value) {
    if (!currentPipeline_.valid()) return;
    const auto* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform1i(loc, value);
}

void CommandList::setUniform(const char* name, const fate::Vec2& value) {
    if (!currentPipeline_.valid()) return;
    const auto* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform2f(loc, value.x, value.y);
}

void CommandList::setUniform(const char* name, const fate::Vec3& value) {
    if (!currentPipeline_.valid()) return;
    const auto* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform3f(loc, value.x, value.y, value.z);
}

void CommandList::setUniform(const char* name, const fate::Color& value) {
    if (!currentPipeline_.valid()) return;
    const auto* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform4f(loc, value.r, value.g, value.b, value.a);
}

void CommandList::setUniform(const char* name, const fate::Mat4& value) {
    if (!currentPipeline_.valid()) return;
    const auto* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, value.data());
}

// --- Draw ---

void CommandList::draw(PrimitiveType type, int vertexCount, int firstVertex) {
    GLenum mode = GL_TRIANGLES;
    if (type == PrimitiveType::Lines) mode = GL_LINES;
    if (type == PrimitiveType::Points) mode = GL_POINTS;
    glDrawArrays(mode, firstVertex, vertexCount);
}

void CommandList::drawIndexed(PrimitiveType type, int indexCount, int firstIndex) {
    GLenum mode = GL_TRIANGLES;
    if (type == PrimitiveType::Lines) mode = GL_LINES;
    if (type == PrimitiveType::Points) mode = GL_POINTS;
    glDrawElements(mode, indexCount, GL_UNSIGNED_INT,
                   reinterpret_cast<const void*>(static_cast<size_t>(firstIndex) * sizeof(unsigned int)));
}

void CommandList::submit() {
    // Immediate-mode GL backend — commands already executed, nothing to replay
}

} // namespace gfx
```

**Note:** The `resolveGL*` methods are added to Device's public interface (GL backend only). They look up handle IDs in the Impl maps and return the GL name. See the comment block at the top of `gl_command_list.cpp` for the full list. The `resolvePipelineDesc` method returns `const PipelineDesc*`.

- [ ] **Step 3: Build to verify compiles**

Expected: compiles clean.

- [ ] **Step 4: Commit**

```bash
git add engine/render/gfx/command_list.h engine/render/gfx/backend/gl/gl_command_list.cpp
git commit -m "feat(gfx): add CommandList with GL backend — immediate mode execution"
```

---

### Task 5: Wire Device Init Into App

**Files:**
- Modify: `engine/app.cpp`

- [ ] **Step 1: Add Device init after GL context creation**

In `engine/app.cpp`, add include:
```cpp
#include "engine/render/gfx/device.h"
```

After `loadGLFunctions()` (around line 64), add:
```cpp
    if (!gfx::Device::instance().init()) {
        LOG_ERROR("App", "Failed to init gfx::Device");
        return false;
    }
```

Before GL context destruction in shutdown, add:
```cpp
    gfx::Device::instance().shutdown();
```

- [ ] **Step 2: Build and run to verify no regressions**

Expected: compiles and runs identically — Device init is a no-op at this stage since existing code still uses direct GL.

- [ ] **Step 3: Commit**

```bash
git add engine/app.cpp
git commit -m "feat(gfx): wire Device init/shutdown into App lifecycle"
```

---

### Task 6: Migrate Shader to Wrap gfx::ShaderHandle

**Files:**
- Modify: `engine/render/shader.h`
- Modify: `engine/render/shader.cpp`

- [ ] **Step 1: Add gfx handle to Shader class**

Add `#include "engine/render/gfx/types.h"` to `shader.h`. Add a `gfx::ShaderHandle gfxHandle_` member. The existing `programId_` stays for now — both coexist during migration.

- [ ] **Step 2: In Shader::loadFromSource, also create a gfx::ShaderHandle**

After the existing GL program is created and linked, register it with Device so the handle maps to the same GL program. This allows new code to use the handle while old code still uses `programId_` directly.

- [ ] **Step 3: Build and verify no regressions**

Expected: identical behavior — Shader still works via `programId_`, handle is bonus metadata.

- [ ] **Step 4: Commit**

```bash
git add engine/render/shader.h engine/render/shader.cpp
git commit -m "refactor(gfx): add gfx::ShaderHandle to Shader class (dual-track migration)"
```

---

### Task 7: Migrate Texture to Wrap gfx::TextureHandle

**Files:**
- Modify: `engine/render/texture.h`
- Modify: `engine/render/texture.cpp`

- [ ] **Step 1: Same pattern as Shader**

Add `gfx::TextureHandle gfxHandle_` to Texture. After `glGenTextures`/`glTexImage2D`, register with Device. Add `gfxHandle()` accessor.

- [ ] **Step 2: Build and verify**

- [ ] **Step 3: Commit**

```bash
git add engine/render/texture.h engine/render/texture.cpp
git commit -m "refactor(gfx): add gfx::TextureHandle to Texture class"
```

---

### Task 8: Migrate Framebuffer to Wrap gfx::FramebufferHandle

**Files:**
- Modify: `engine/render/framebuffer.h`
- Modify: `engine/render/framebuffer.cpp`

- [ ] **Step 1: Same pattern — add gfx::FramebufferHandle**

- [ ] **Step 2: Build and verify**

- [ ] **Step 3: Commit**

```bash
git add engine/render/framebuffer.h engine/render/framebuffer.cpp
git commit -m "refactor(gfx): add gfx::FramebufferHandle to Framebuffer class"
```

---

### Task 9: Add CommandList to RenderPassContext

**Files:**
- Modify: `engine/render/render_graph.h`
- Modify: `engine/render/render_graph.cpp`

- [ ] **Step 1: Add CommandList to context**

In `render_graph.h`, add:
```cpp
#include "engine/render/gfx/command_list.h"
```

Add to `RenderPassContext`:
```cpp
    gfx::CommandList* commandList = nullptr;
```

- [ ] **Step 2: In RenderGraph::execute(), create and pass CommandList**

In `render_graph.cpp`, modify `execute()` to create a `gfx::CommandList`, pass it through context:
```cpp
void RenderGraph::execute(RenderPassContext& ctx) {
    gfx::CommandList cmdList;
    ctx.commandList = &cmdList;
    ctx.graph = this;
    for (auto& pass : passes_) {
        if (!pass.enabled) continue;
        cmdList.begin();
        pass.execute(ctx);
        cmdList.end();
        cmdList.submit();
    }
}
```

- [ ] **Step 3: Build and verify — existing passes ignore CommandList, still use direct GL**

Expected: identical behavior.

- [ ] **Step 4: Commit**

```bash
git add engine/render/render_graph.h engine/render/render_graph.cpp
git commit -m "refactor(gfx): add CommandList to RenderPassContext"
```

---

### Task 10: Migrate SpriteBatch to Use CommandList

**Files:**
- Modify: `engine/render/sprite_batch.h`
- Modify: `engine/render/sprite_batch.cpp`

- [ ] **Step 1: Replace fate::BlendMode with gfx::BlendMode**

Remove `enum class BlendMode { Alpha, Additive };` from `sprite_batch.h`. Replace with `using BlendMode = gfx::BlendMode;` or update all references to use `gfx::BlendMode`.

- [ ] **Step 2: Add gfx handles as members**

Add `gfx::BufferHandle vboHandle_`, `gfx::BufferHandle eboHandle_`, `gfx::PipelineHandle pipelineAlpha_`, `gfx::PipelineHandle pipelineAdditive_` to SpriteBatch.

- [ ] **Step 3: In init(), create gfx resources alongside existing GL ones**

Create the pipeline handles and buffer handles via `gfx::Device`. The existing `vao_`/`vbo_`/`ebo_` stay for now.

- [ ] **Step 4: In flush(), use CommandList if available**

When a `CommandList*` is available (set via a new `setCommandList()` method), use it for draws. Otherwise fall back to direct GL (for editor/ImGui rendering that goes through SpriteBatch outside the render graph).

- [ ] **Step 5: Build and verify visual output is identical**

- [ ] **Step 6: Commit**

```bash
git add engine/render/sprite_batch.h engine/render/sprite_batch.cpp
git commit -m "refactor(gfx): migrate SpriteBatch to use CommandList and gfx::BlendMode"
```

---

### Task 11: Migrate Lighting and PostProcess Passes

**Files:**
- Modify: `engine/render/lighting.cpp`
- Modify: `engine/render/post_process.cpp`
- Modify: `engine/render/fullscreen_quad.h` (if needed)

- [ ] **Step 1: In lighting.cpp, use ctx.commandList for state changes**

Replace direct `glBindFramebuffer`, `glClear`, `glBlendFunc`, `glUseProgram` calls with equivalent CommandList calls. The shaders become `gfx::PipelineHandle`s created once.

- [ ] **Step 2: In post_process.cpp, same migration**

Bloom extract, blur, and composite passes use CommandList.

- [ ] **Step 3: Build and verify visual output is identical**

- [ ] **Step 4: Commit**

```bash
git add engine/render/lighting.cpp engine/render/post_process.cpp engine/render/fullscreen_quad.h
git commit -m "refactor(gfx): migrate lighting and post-process passes to CommandList"
```

---

### Task 12: Migrate SDF Text — Remove Raw GL Texture ID

**Files:**
- Modify: `engine/render/sdf_text.h`
- Modify: `engine/render/sdf_text.cpp`
- Modify: `engine/render/sprite_batch.h` (update `drawTexturedQuad` signature)

- [ ] **Step 1: Change atlasTexId_ to gfx::TextureHandle**

In `sdf_text.h`, replace `unsigned int atlasTexId_` with `gfx::TextureHandle atlasHandle_`. Update `atlasTextureId()` to return the handle.

- [ ] **Step 2: Update drawTexturedQuad to accept TextureHandle**

In `sprite_batch.h`, change `drawTexturedQuad(unsigned int glTexId, ...)` to `drawTexturedQuad(gfx::TextureHandle tex, ...)`.

- [ ] **Step 3: Build and verify SDF text renders correctly**

- [ ] **Step 4: Commit**

```bash
git add engine/render/sdf_text.h engine/render/sdf_text.cpp engine/render/sprite_batch.h engine/render/sprite_batch.cpp
git commit -m "refactor(gfx): replace raw GL texture ID in SDF text with TextureHandle"
```

---

### Task 13: Remove Direct GL from Render Code

**Files:**
- Modify: `engine/render/shader.cpp` — remove direct GL calls, delegate to Device
- Modify: `engine/render/texture.cpp` — same
- Modify: `engine/render/framebuffer.cpp` — same
- Modify: `engine/render/sprite_batch.cpp` — remove `#include "gl_loader.h"`

- [ ] **Step 1: Remove gl_loader.h includes from all render files outside backend/**

Grep for `#include.*gl_loader` and remove from any file not in `engine/render/gfx/backend/gl/`.

- [ ] **Step 2: Ensure all GL calls go through Device or CommandList**

Any remaining direct GL calls (e.g., `glEnable(GL_BLEND)` in passes) should be replaced with CommandList equivalents.

- [ ] **Step 3: Build and verify — engine works with zero direct GL outside backend/**

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor(gfx): remove all direct GL calls from render code — all GL isolated to backend/"
```

---

### Task 14: Debug Validation Layer

**Files:**
- Modify: `engine/render/gfx/backend/gl/gl_command_list.cpp`

- [ ] **Step 1: Add FATE_DEBUG guards around state validation**

```cpp
#ifdef FATE_DEBUG
    if (!currentPipeline_.valid()) {
        LOG_ERROR("gfx::CommandList", "draw() called without a bound pipeline");
        return;
    }
#endif
```

Add similar checks for:
- `draw`/`drawIndexed` without pipeline bound
- `setUniform` without pipeline bound
- `bindTexture` with invalid handle

- [ ] **Step 2: Build in Debug and verify no false positives**

- [ ] **Step 3: Commit**

```bash
git add engine/render/gfx/backend/gl/gl_command_list.cpp
git commit -m "feat(gfx): add debug validation layer to CommandList"
```
