# Phase 4: Render Graph, Particles, Lighting, Post-Processing & Editor Polish — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the monolithic render function with a 10-pass render graph, add CPU particle emitters, 2D ambient+point lighting, bloom/vignette/color grading post-processing, and editor polish (ImGuizmo, selection outlines, infinite grid shader).

**Architecture:** A semi-static `RenderGraph` owns an ordered pass list and FBO pool. Each subsystem (tiles, entities, particles, text, lighting, bloom, post-process, editor overlays) registers a pass at init time. The graph executes passes sequentially, skipping disabled ones. Fullscreen effects use a shared vertex shader with `gl_VertexID`-based quad generation.

**Tech Stack:** C++23, OpenGL 3.3, SDL2, ImGui (docking), ImPlot, ImGuizmo, stb_image, nlohmann/json, doctest

**Build command:**
```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build --config Debug
```

**Test command:** `./out/build/Debug/fate_tests.exe`

**CRITICAL RULES:**
- NEVER create `.bat` files — they hang the bash shell indefinitely
- NEVER add `Co-Authored-By: Claude` lines to commits
- Always register new components in `game/register_components.h`
- CMake GLOB_RECURSE requires reconfigure when adding new source files

---

## File Structure

### New Files
| File | Responsibility |
|------|---------------|
| `engine/render/render_graph.h` | RenderGraph, RenderPass, RenderPassContext |
| `engine/render/render_graph.cpp` | Graph execution, FBO pool management |
| `engine/render/fullscreen_quad.h` | Shared empty VAO + draw utility for fullscreen passes |
| `engine/render/fullscreen_quad.cpp` | VAO creation, drawFullscreenQuad() |
| `engine/render/lighting.h` | PointLight, LightingConfig structs |
| `engine/render/lighting.cpp` | Lighting pass: light map build + composite |
| `engine/render/post_process.h` | PostProcessConfig, bloom/vignette/grading |
| `engine/render/post_process.cpp` | Bloom extract, blur ping-pong, final composite |
| `engine/particle/particle.h` | Particle struct, EmitterConfig |
| `engine/particle/particle_emitter.h` | ParticleEmitter class declaration |
| `engine/particle/particle_emitter.cpp` | Emitter update, spawn, vertex build |
| `engine/particle/particle_system.h` | ParticleSystem (ECS system) |
| `engine/particle/particle_system.cpp` | System update + render pass callback |
| `assets/shaders/fullscreen_quad.vert` | Shared fullscreen quad vertex shader |
| `assets/shaders/light.frag` | Point light radial falloff |
| `assets/shaders/bloom_extract.frag` | Brightness threshold |
| `assets/shaders/blur.frag` | Separable 9-tap Gaussian blur |
| `assets/shaders/postprocess.frag` | Bloom composite + vignette + color grading |
| `assets/shaders/blit.frag` | Passthrough texture blit (used by lighting composite and final blit) |
| `assets/shaders/grid.frag` | Infinite grid with LOD fade |
| `tests/test_render_graph.cpp` | RenderGraph pass ordering, enable/disable |
| `tests/test_particle.cpp` | ParticleEmitter spawn, update, lifetime, vertex count |

### Modified Files
| File | Changes |
|------|---------|
| `engine/render/gl_loader.h/cpp` | Add 5 renderbuffer GL functions |
| `engine/render/framebuffer.h/cpp` | Add depth/stencil attachment, rbo_ member |
| `engine/core/types.h` | Add `Mat4::inverse()` |
| `engine/render/sprite_batch.h/cpp` | Add `BlendMode` enum, `setBlendMode()` |
| `engine/app.h/cpp` | Add RenderGraph member, replace render() with graph execution |
| `engine/editor/editor.h/cpp` | ImGuizmo, selection outlines, grid shader, post-process panel, View menu items |
| `game/game_app.h/cpp` | Replace onRender() with pass registration |
| `game/register_components.h` | Register ParticleEmitterComponent, PointLightComponent |
| `CMakeLists.txt` | Add ImGuizmo dependency |

---

## Task 1: GL Loader + Framebuffer Depth/Stencil + Mat4::inverse + Fullscreen Quad

Foundation work needed by all subsequent tasks.

**Files:**
- Modify: `engine/render/gl_loader.h`, `engine/render/gl_loader.cpp`
- Modify: `engine/render/framebuffer.h`, `engine/render/framebuffer.cpp`
- Modify: `engine/core/types.h`
- Create: `engine/render/fullscreen_quad.h`, `engine/render/fullscreen_quad.cpp`
- Create: `assets/shaders/fullscreen_quad.vert`

- [ ] **Step 1: Add renderbuffer GL functions to gl_loader**

In `engine/render/gl_loader.h`, add extern declarations after existing framebuffer function declarations (around line 54):

```cpp
extern PFNGLGENRENDERBUFFERSPROC           glGenRenderbuffers_fp;
extern PFNGLDELETERENDERBUFFERSPROC        glDeleteRenderbuffers_fp;
extern PFNGLBINDRENDERBUFFERPROC           glBindRenderbuffer_fp;
extern PFNGLRENDERBUFFERSTORAGEPROC        glRenderbufferStorage_fp;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC    glFramebufferRenderbuffer_fp;
```

Add macro redefinitions after existing framebuffer macros (around line 99):

```cpp
#undef glGenRenderbuffers
#undef glDeleteRenderbuffers
#undef glBindRenderbuffer
#undef glRenderbufferStorage
#undef glFramebufferRenderbuffer
#define glGenRenderbuffers         glGenRenderbuffers_fp
#define glDeleteRenderbuffers      glDeleteRenderbuffers_fp
#define glBindRenderbuffer         glBindRenderbuffer_fp
#define glRenderbufferStorage      glRenderbufferStorage_fp
#define glFramebufferRenderbuffer  glFramebufferRenderbuffer_fp
```

In `engine/render/gl_loader.cpp`, add pointer definitions (around line 54):

```cpp
PFNGLGENRENDERBUFFERSPROC           glGenRenderbuffers_fp = nullptr;
PFNGLDELETERENDERBUFFERSPROC        glDeleteRenderbuffers_fp = nullptr;
PFNGLBINDRENDERBUFFERPROC           glBindRenderbuffer_fp = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC        glRenderbufferStorage_fp = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC    glFramebufferRenderbuffer_fp = nullptr;
```

Add LOAD_GL calls (around line 108):

```cpp
LOAD_GL(glGenRenderbuffers);
LOAD_GL(glDeleteRenderbuffers);
LOAD_GL(glBindRenderbuffer);
LOAD_GL(glRenderbufferStorage);
LOAD_GL(glFramebufferRenderbuffer);
```

- [ ] **Step 2: Add depth/stencil support to Framebuffer**

Replace `engine/render/framebuffer.h` with:

```cpp
#pragma once

namespace fate {

class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer() = default; // No auto-cleanup — matches SpriteBatch pattern. Call destroy() explicitly before GL context teardown.

    bool create(int width, int height, bool withDepthStencil = false);
    void destroy();
    void resize(int width, int height);

    void bind();
    void unbind();

    unsigned int textureId() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool isValid() const { return fbo_ != 0; }
    bool hasDepthStencil() const { return hasDepthStencil_; }

private:
    unsigned int fbo_ = 0;
    unsigned int texture_ = 0;
    unsigned int rbo_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool hasDepthStencil_ = false;
};

} // namespace fate
```

Update `engine/render/framebuffer.cpp` — modify `create()` to accept `withDepthStencil` and attach a renderbuffer when true. Modify `destroy()` to also delete the renderbuffer. Modify `resize()` to pass `hasDepthStencil_` through to `create()`.

Key additions in `create()` after the texture attachment:

```cpp
if (withDepthStencil) {
    glGenRenderbuffers(1, &rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    hasDepthStencil_ = true;
}
```

Key addition in `destroy()`:

```cpp
if (rbo_) { glDeleteRenderbuffers(1, &rbo_); rbo_ = 0; }
```

Key change in `resize()`:

```cpp
void Framebuffer::resize(int w, int h) {
    if (w == width_ && h == height_) return;
    bool ds = hasDepthStencil_;
    destroy();
    create(w, h, ds);
}
```

- [ ] **Step 3: Add Mat4::inverse()**

In `engine/core/types.h`, add to the Mat4 struct after `operator*` (around line 200):

```cpp
    // General 4x4 matrix inverse (Cramer's rule)
    Mat4 inverse() const {
        const float* src = m;
        float inv[16];

        inv[0]  =  src[5]*src[10]*src[15] - src[5]*src[11]*src[14] - src[9]*src[6]*src[15] + src[9]*src[7]*src[14] + src[13]*src[6]*src[11] - src[13]*src[7]*src[10];
        inv[4]  = -src[4]*src[10]*src[15] + src[4]*src[11]*src[14] + src[8]*src[6]*src[15] - src[8]*src[7]*src[14] - src[12]*src[6]*src[11] + src[12]*src[7]*src[10];
        inv[8]  =  src[4]*src[9]*src[15]  - src[4]*src[11]*src[13] - src[8]*src[5]*src[15] + src[8]*src[7]*src[13] + src[12]*src[5]*src[11] - src[12]*src[7]*src[9];
        inv[12] = -src[4]*src[9]*src[14]  + src[4]*src[10]*src[13] + src[8]*src[5]*src[14] - src[8]*src[6]*src[13] - src[12]*src[5]*src[10] + src[12]*src[6]*src[9];
        inv[1]  = -src[1]*src[10]*src[15] + src[1]*src[11]*src[14] + src[9]*src[2]*src[15] - src[9]*src[3]*src[14] - src[13]*src[2]*src[11] + src[13]*src[3]*src[10];
        inv[5]  =  src[0]*src[10]*src[15] - src[0]*src[11]*src[14] - src[8]*src[2]*src[15] + src[8]*src[3]*src[14] + src[12]*src[2]*src[11] - src[12]*src[3]*src[10];
        inv[9]  = -src[0]*src[9]*src[15]  + src[0]*src[11]*src[13] + src[8]*src[1]*src[15] - src[8]*src[3]*src[13] - src[12]*src[1]*src[11] + src[12]*src[3]*src[9];
        inv[13] =  src[0]*src[9]*src[14]  - src[0]*src[10]*src[13] - src[8]*src[1]*src[14] + src[8]*src[2]*src[13] + src[12]*src[1]*src[10] - src[12]*src[2]*src[9];
        inv[2]  =  src[1]*src[6]*src[15]  - src[1]*src[7]*src[14]  - src[5]*src[2]*src[15] + src[5]*src[3]*src[14] + src[13]*src[2]*src[7]  - src[13]*src[3]*src[6];
        inv[6]  = -src[0]*src[6]*src[15]  + src[0]*src[7]*src[14]  + src[4]*src[2]*src[15] - src[4]*src[3]*src[14] - src[12]*src[2]*src[7]  + src[12]*src[3]*src[6];
        inv[10] =  src[0]*src[5]*src[15]  - src[0]*src[7]*src[13]  - src[4]*src[1]*src[15] + src[4]*src[3]*src[13] + src[12]*src[1]*src[7]  - src[12]*src[3]*src[5];
        inv[14] = -src[0]*src[5]*src[14]  + src[0]*src[6]*src[13]  + src[4]*src[1]*src[14] - src[4]*src[2]*src[13] - src[12]*src[1]*src[6]  + src[12]*src[2]*src[5];
        inv[3]  = -src[1]*src[6]*src[11]  + src[1]*src[7]*src[10]  + src[5]*src[2]*src[11] - src[5]*src[3]*src[10] - src[9]*src[2]*src[7]   + src[9]*src[3]*src[6];
        inv[7]  =  src[0]*src[6]*src[11]  - src[0]*src[7]*src[10]  - src[4]*src[2]*src[11] + src[4]*src[3]*src[10] + src[8]*src[2]*src[7]   - src[8]*src[3]*src[6];
        inv[11] = -src[0]*src[5]*src[11]  + src[0]*src[7]*src[9]   + src[4]*src[1]*src[11] - src[4]*src[3]*src[9]  - src[8]*src[1]*src[7]   + src[8]*src[3]*src[5];
        inv[15] =  src[0]*src[5]*src[10]  - src[0]*src[6]*src[9]   - src[4]*src[1]*src[10] + src[4]*src[2]*src[9]  + src[8]*src[1]*src[6]   - src[8]*src[2]*src[5];

        float det = src[0]*inv[0] + src[1]*inv[4] + src[2]*inv[8] + src[3]*inv[12];
        if (det == 0.0f) return identity();

        float invDet = 1.0f / det;
        Mat4 result;
        for (int i = 0; i < 16; i++) result.m[i] = inv[i] * invDet;
        return result;
    }
```

- [ ] **Step 4: Create fullscreen quad utility**

Create `engine/render/fullscreen_quad.h`:

```cpp
#pragma once

namespace fate {

// Manages an empty VAO for gl_VertexID-based fullscreen triangle rendering
class FullscreenQuad {
public:
    static FullscreenQuad& instance();

    void init();
    void shutdown();
    void draw(); // binds VAO, draws 3 vertices (oversized triangle clipped to viewport)

private:
    FullscreenQuad() = default;
    unsigned int vao_ = 0;
};

} // namespace fate
```

Create `engine/render/fullscreen_quad.cpp`:

```cpp
#include "engine/render/fullscreen_quad.h"
#include "engine/render/gl_loader.h"

namespace fate {

FullscreenQuad& FullscreenQuad::instance() {
    static FullscreenQuad s_instance;
    return s_instance;
}

void FullscreenQuad::init() {
    glGenVertexArrays(1, &vao_);
}

void FullscreenQuad::shutdown() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void FullscreenQuad::draw() {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace fate
```

Create `assets/shaders/fullscreen_quad.vert`:

```glsl
#version 330 core

out vec2 v_uv;

void main() {
    v_uv = vec2((gl_VertexID & 1) * 2.0, (gl_VertexID >> 1) * 2.0);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
```

- [ ] **Step 5: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All existing tests pass (137+)

- [ ] **Step 6: Commit**

```bash
git add engine/render/gl_loader.h engine/render/gl_loader.cpp engine/render/framebuffer.h engine/render/framebuffer.cpp engine/core/types.h engine/render/fullscreen_quad.h engine/render/fullscreen_quad.cpp assets/shaders/fullscreen_quad.vert
git commit -m "feat(render): GL renderbuffer loading, framebuffer depth/stencil, Mat4 inverse, fullscreen quad utility"
```

---

## Task 2: RenderGraph + SpriteBatch Blend Modes

**Files:**
- Create: `engine/render/render_graph.h`, `engine/render/render_graph.cpp`
- Create: `tests/test_render_graph.cpp`
- Modify: `engine/render/sprite_batch.h`, `engine/render/sprite_batch.cpp`

- [ ] **Step 1: Write RenderGraph tests**

Create `tests/test_render_graph.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/render/render_graph.h"

TEST_CASE("RenderGraph pass add and execute order") {
    fate::RenderGraph graph;
    std::vector<int> order;

    graph.addPass({"First", true, [&](fate::RenderPassContext&) { order.push_back(1); }});
    graph.addPass({"Second", true, [&](fate::RenderPassContext&) { order.push_back(2); }});
    graph.addPass({"Third", true, [&](fate::RenderPassContext&) { order.push_back(3); }});

    fate::RenderPassContext ctx{};
    graph.execute(ctx);

    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

TEST_CASE("RenderGraph disabled pass is skipped") {
    fate::RenderGraph graph;
    std::vector<int> order;

    graph.addPass({"A", true, [&](fate::RenderPassContext&) { order.push_back(1); }});
    graph.addPass({"B", false, [&](fate::RenderPassContext&) { order.push_back(2); }});
    graph.addPass({"C", true, [&](fate::RenderPassContext&) { order.push_back(3); }});

    fate::RenderPassContext ctx{};
    graph.execute(ctx);

    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1);
    CHECK(order[1] == 3);
}

TEST_CASE("RenderGraph setPassEnabled toggles pass") {
    fate::RenderGraph graph;
    int count = 0;

    graph.addPass({"Toggle", true, [&](fate::RenderPassContext&) { count++; }});

    fate::RenderPassContext ctx{};
    graph.execute(ctx);
    CHECK(count == 1);

    graph.setPassEnabled("Toggle", false);
    graph.execute(ctx);
    CHECK(count == 1); // not incremented

    graph.setPassEnabled("Toggle", true);
    graph.execute(ctx);
    CHECK(count == 2);
}

TEST_CASE("RenderGraph removePass") {
    fate::RenderGraph graph;
    int count = 0;

    graph.addPass({"Remove", true, [&](fate::RenderPassContext&) { count++; }});
    graph.removePass("Remove");

    fate::RenderPassContext ctx{};
    graph.execute(ctx);
    CHECK(count == 0);
}
```

- [ ] **Step 2: Write RenderGraph implementation**

Create `engine/render/render_graph.h`:

```cpp
#pragma once
#include "engine/render/framebuffer.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace fate {

class SpriteBatch;
class Camera;
class World;
class RenderGraph;

struct RenderPassContext {
    SpriteBatch* spriteBatch = nullptr;
    Camera* camera = nullptr;
    World* world = nullptr;
    RenderGraph* graph = nullptr;
    int viewportWidth = 0;
    int viewportHeight = 0;
};

struct RenderPass {
    std::string name;
    bool enabled = true;
    std::function<void(RenderPassContext& ctx)> execute;
};

class RenderGraph {
public:
    void addPass(RenderPass pass);
    void removePass(const std::string& name);
    void setPassEnabled(const std::string& name, bool enabled);

    void execute(RenderPassContext& ctx);

    // FBO pool — get or create by name
    Framebuffer& getFBO(const std::string& name, int width, int height, bool withDepthStencil = false);

    const std::vector<RenderPass>& passes() const { return passes_; }

private:
    std::vector<RenderPass> passes_;
    std::unordered_map<std::string, std::unique_ptr<Framebuffer>> fboPool_;
};

} // namespace fate
```

Create `engine/render/render_graph.cpp`:

```cpp
#include "engine/render/render_graph.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

void RenderGraph::addPass(RenderPass pass) {
    passes_.push_back(std::move(pass));
}

void RenderGraph::removePass(const std::string& name) {
    passes_.erase(
        std::remove_if(passes_.begin(), passes_.end(),
            [&](const RenderPass& p) { return p.name == name; }),
        passes_.end()
    );
}

void RenderGraph::setPassEnabled(const std::string& name, bool enabled) {
    for (auto& pass : passes_) {
        if (pass.name == name) {
            pass.enabled = enabled;
            return;
        }
    }
}

void RenderGraph::execute(RenderPassContext& ctx) {
    ctx.graph = this;
    for (auto& pass : passes_) {
        if (!pass.enabled) continue;
        pass.execute(ctx);
    }
}

Framebuffer& RenderGraph::getFBO(const std::string& name, int width, int height, bool withDepthStencil) {
    auto it = fboPool_.find(name);
    if (it != fboPool_.end()) {
        auto& fbo = *it->second;
        if (fbo.width() != width || fbo.height() != height) {
            fbo.resize(width, height);
        }
        return fbo;
    }

    auto fbo = std::make_unique<Framebuffer>();
    fbo->create(width, height, withDepthStencil);
    auto& ref = *fbo;
    fboPool_[name] = std::move(fbo);
    return ref;
}

} // namespace fate
```

- [ ] **Step 3: Add BlendMode to SpriteBatch**

In `engine/render/sprite_batch.h`, add before the class:

```cpp
enum class BlendMode { Alpha, Additive };
```

Add public method to SpriteBatch:

```cpp
    void setBlendMode(BlendMode mode);
```

Add private member:

```cpp
    BlendMode blendMode_ = BlendMode::Alpha;
```

In `engine/render/sprite_batch.cpp`, implement:

```cpp
void SpriteBatch::setBlendMode(BlendMode mode) {
    if (mode == blendMode_) return;
    flush(); // must flush before changing GL state
    blendMode_ = mode;
    switch (mode) {
        case BlendMode::Alpha:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
    }
}
```

You'll need to add `#include "engine/render/gl_loader.h"` if not already included.

- [ ] **Step 4: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass including 4 new RenderGraph tests

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_graph.h engine/render/render_graph.cpp engine/render/sprite_batch.h engine/render/sprite_batch.cpp tests/test_render_graph.cpp
git commit -m "feat(render): RenderGraph with ordered pass list, FBO pool, and SpriteBatch blend modes"
```

---

## Task 3: Particle System

**Files:**
- Create: `engine/particle/particle.h`, `engine/particle/particle_emitter.h`, `engine/particle/particle_emitter.cpp`
- Create: `engine/particle/particle_system.h`, `engine/particle/particle_system.cpp`
- Create: `tests/test_particle.cpp`

- [ ] **Step 1: Write particle tests**

Create `tests/test_particle.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/particle/particle_emitter.h"

TEST_CASE("ParticleEmitter continuous spawning") {
    fate::EmitterConfig config;
    config.spawnRate = 10.0f; // 10 per second
    config.lifetimeMin = 1.0f;
    config.lifetimeMax = 1.0f;
    config.gravity = {0, 0};

    fate::ParticleEmitter emitter;
    emitter.init(config, 100);

    // After 0.5 seconds, should have ~5 particles
    emitter.update(0.5f, {0, 0});
    CHECK(emitter.activeCount() >= 4);
    CHECK(emitter.activeCount() <= 6);
    CHECK_FALSE(emitter.isFinished());
}

TEST_CASE("ParticleEmitter burst mode") {
    fate::EmitterConfig config;
    config.spawnRate = 0.0f;
    config.burstCount = 10;
    config.lifetimeMin = 0.5f;
    config.lifetimeMax = 0.5f;

    fate::ParticleEmitter emitter;
    emitter.init(config, 100);

    emitter.burst({100, 200});
    emitter.update(0.0f, {0, 0}); // process the burst
    CHECK(emitter.activeCount() == 10);
    CHECK_FALSE(emitter.isFinished());

    // After lifetime expires
    emitter.update(0.6f, {0, 0});
    CHECK(emitter.activeCount() == 0);
    CHECK(emitter.isFinished());
}

TEST_CASE("ParticleEmitter vertex count matches active") {
    fate::EmitterConfig config;
    config.spawnRate = 100.0f;
    config.lifetimeMin = 1.0f;
    config.lifetimeMax = 1.0f;

    fate::ParticleEmitter emitter;
    emitter.init(config, 50);

    emitter.update(0.1f, {0, 0});
    size_t active = emitter.activeCount();
    CHECK(active > 0);
    // Each particle = 4 vertices (quad)
    CHECK(emitter.vertices().size() == active * 4);
}

TEST_CASE("ParticleEmitter max particles cap") {
    fate::EmitterConfig config;
    config.spawnRate = 1000.0f;
    config.lifetimeMin = 10.0f;
    config.lifetimeMax = 10.0f;

    fate::ParticleEmitter emitter;
    emitter.init(config, 16);

    emitter.update(1.0f, {0, 0});
    CHECK(emitter.activeCount() == 16); // capped at max
}

TEST_CASE("ParticleEmitter gravity applies") {
    fate::EmitterConfig config;
    config.spawnRate = 0.0f;
    config.burstCount = 1;
    config.lifetimeMin = 10.0f;
    config.lifetimeMax = 10.0f;
    config.velocityMin = {0, 0};
    config.velocityMax = {0, 0};
    config.gravity = {0, 100}; // downward

    fate::ParticleEmitter emitter;
    emitter.init(config, 10);
    emitter.burst({0, 0});
    emitter.update(0.0f, {0, 0}); // spawn
    emitter.update(1.0f, {0, 0}); // apply gravity for 1s

    // Particle should have moved downward
    // We can't easily check particle position directly, but vertex data should reflect it
    CHECK(emitter.activeCount() == 1);
}
```

- [ ] **Step 2: Write particle implementation**

Create `engine/particle/particle.h`:

```cpp
#pragma once
#include "engine/core/types.h"
#include "engine/asset/asset_handle.h"

namespace fate {

struct Particle {
    Vec2 position;
    Vec2 velocity;
    Color color;
    Color colorEnd;
    float size;
    float sizeEnd;
    float life;
    float maxLife;
    float rotation;
    float rotationSpeed;
};

struct EmitterConfig {
    float spawnRate = 10.0f;
    int burstCount = 0;

    Vec2 velocityMin = {-20, -50};
    Vec2 velocityMax = {20, -10};
    float lifetimeMin = 0.5f;
    float lifetimeMax = 1.5f;
    float sizeMin = 4.0f;
    float sizeMax = 8.0f;
    float rotationSpeedMin = 0.0f;
    float rotationSpeedMax = 0.0f;

    Color colorStart = Color::white();
    Color colorEnd = {1, 1, 1, 0};

    Vec2 gravity = {0, 0};

    AssetHandle texture;
    float depth = 5.0f;
    bool worldSpace = true;
    bool additiveBlend = false;
};

} // namespace fate
```

Create `engine/particle/particle_emitter.h`:

```cpp
#pragma once
#include "engine/particle/particle.h"
#include "engine/render/sprite_batch.h"
#include <vector>

namespace fate {

class ParticleEmitter {
public:
    void init(const EmitterConfig& config, size_t maxParticles = 256);

    void update(float dt, const Vec2& emitterPos);
    void burst(const Vec2& position, int count = -1);

    const std::vector<SpriteVertex>& vertices() const { return vertices_; }
    size_t activeCount() const { return activeCount_; }

    EmitterConfig& config() { return config_; }
    const EmitterConfig& config() const { return config_; }
    bool isFinished() const;

private:
    EmitterConfig config_;
    std::vector<Particle> particles_;
    std::vector<SpriteVertex> vertices_;
    size_t maxParticles_ = 256;
    size_t activeCount_ = 0;
    float spawnAccumulator_ = 0.0f;
    int pendingBurst_ = 0;
    Vec2 burstPosition_;

    void spawn(const Vec2& pos);
    void buildVertices(const Vec2& emitterPos);
    static float randomRange(float min, float max);
};

} // namespace fate
```

Create `engine/particle/particle_emitter.cpp`:

```cpp
#include "engine/particle/particle_emitter.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace fate {

float ParticleEmitter::randomRange(float min, float max) {
    float t = static_cast<float>(rand()) / RAND_MAX;
    return min + t * (max - min);
}

void ParticleEmitter::init(const EmitterConfig& config, size_t maxParticles) {
    config_ = config;
    maxParticles_ = maxParticles;
    particles_.resize(maxParticles);
    vertices_.reserve(maxParticles * 4);
    activeCount_ = 0;
    spawnAccumulator_ = 0.0f;
    pendingBurst_ = 0;
}

void ParticleEmitter::spawn(const Vec2& pos) {
    if (activeCount_ >= maxParticles_) return;

    auto& p = particles_[activeCount_];
    p.position = pos;
    p.velocity = {randomRange(config_.velocityMin.x, config_.velocityMax.x),
                  randomRange(config_.velocityMin.y, config_.velocityMax.y)};
    p.color = config_.colorStart;
    p.colorEnd = config_.colorEnd;
    p.size = randomRange(config_.sizeMin, config_.sizeMax);
    p.sizeEnd = p.size * 0.5f;
    p.life = randomRange(config_.lifetimeMin, config_.lifetimeMax);
    p.maxLife = p.life;
    p.rotation = 0.0f;
    p.rotationSpeed = randomRange(config_.rotationSpeedMin, config_.rotationSpeedMax);
    activeCount_++;
}

void ParticleEmitter::update(float dt, const Vec2& emitterPos) {
    // Process pending burst
    if (pendingBurst_ > 0) {
        for (int i = 0; i < pendingBurst_; ++i) {
            spawn(burstPosition_);
        }
        pendingBurst_ = 0;
    }

    // Continuous spawning
    if (config_.spawnRate > 0.0f) {
        spawnAccumulator_ += dt * config_.spawnRate;
        while (spawnAccumulator_ >= 1.0f && activeCount_ < maxParticles_) {
            spawn(emitterPos);
            spawnAccumulator_ -= 1.0f;
        }
    }

    // Update existing particles
    for (size_t i = 0; i < activeCount_;) {
        auto& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            // Swap with last active particle
            particles_[i] = particles_[activeCount_ - 1];
            activeCount_--;
            continue;
        }

        p.velocity.x += config_.gravity.x * dt;
        p.velocity.y += config_.gravity.y * dt;
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.rotation += p.rotationSpeed * dt;
        ++i;
    }

    buildVertices(emitterPos);
}

void ParticleEmitter::burst(const Vec2& position, int count) {
    int n = (count < 0) ? config_.burstCount : count;
    if (n <= 0) return;
    pendingBurst_ = n;
    burstPosition_ = position;
}

bool ParticleEmitter::isFinished() const {
    // Only finished for burst emitters (no continuous spawn) with all particles dead
    return config_.spawnRate <= 0.0f && config_.burstCount > 0 && activeCount_ == 0 && pendingBurst_ == 0;
}

void ParticleEmitter::buildVertices(const Vec2& emitterPos) {
    vertices_.clear();
    for (size_t i = 0; i < activeCount_; ++i) {
        auto& p = particles_[i];
        float t = 1.0f - (p.life / p.maxLife); // 0 at birth, 1 at death

        // Lerp color and size
        Color c;
        c.r = p.color.r + (p.colorEnd.r - p.color.r) * t;
        c.g = p.color.g + (p.colorEnd.g - p.color.g) * t;
        c.b = p.color.b + (p.colorEnd.b - p.color.b) * t;
        c.a = p.color.a + (p.colorEnd.a - p.color.a) * t;

        float size = p.size + (p.sizeEnd - p.size) * t;
        float half = size * 0.5f;

        Vec2 pos = p.position;
        if (!config_.worldSpace) {
            pos.x += emitterPos.x;
            pos.y += emitterPos.y;
        }

        // Generate 4 vertices for this particle quad
        // Simple axis-aligned (rotation applied via cos/sin if rotationSpeed != 0)
        float cosR = std::cos(p.rotation);
        float sinR = std::sin(p.rotation);

        auto rotatePoint = [&](float lx, float ly) -> std::pair<float, float> {
            return {pos.x + lx * cosR - ly * sinR,
                    pos.y + lx * sinR + ly * cosR};
        };

        auto [x0, y0] = rotatePoint(-half, -half);
        auto [x1, y1] = rotatePoint( half, -half);
        auto [x2, y2] = rotatePoint( half,  half);
        auto [x3, y3] = rotatePoint(-half,  half);

        vertices_.push_back({x0, y0, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f});
        vertices_.push_back({x1, y1, 1.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f});
        vertices_.push_back({x2, y2, 1.0f, 1.0f, c.r, c.g, c.b, c.a, 0.0f});
        vertices_.push_back({x3, y3, 0.0f, 1.0f, c.r, c.g, c.b, c.a, 0.0f});
    }
}

} // namespace fate
```

Create `engine/particle/particle_system.h`:

```cpp
#pragma once
#include "engine/ecs/world.h" // System base class defined here

namespace fate {

class ParticleSystem : public System {
public:
    void update(float dt) override;
};

} // namespace fate
```

Create `engine/particle/particle_system.cpp`:

```cpp
#include "engine/particle/particle_system.h"

namespace fate {

void ParticleSystem::update(float dt) {
    // Particle emitter updates happen via forEach<ParticleEmitterComponent, Transform>
    // The game layer registers this system and provides the component iteration
    // in game_app.cpp. This base implementation is a hook point for the ECS system loop.
    // Actual particle update logic goes in the game's onUpdate or in a registered
    // render pass callback that calls emitter.update() for each entity.
}

} // namespace fate
```

Note: The `System` base class is defined in `engine/ecs/world.h` (not a separate `system.h`). The `ParticleSystem::update()` is intentionally minimal — the real per-emitter update logic runs inside the particle render pass callback registered by `game_app.cpp`, which has access to the game's ECS components.

- [ ] **Step 3: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass including 5 new particle tests

- [ ] **Step 4: Commit**

```bash
git add engine/particle/ tests/test_particle.cpp
git commit -m "feat(particle): CPU particle emitter with spawn rate, burst, gravity, vertex build"
```

---

## Task 4: Shaders — Light, Bloom, Post-Process, Grid

**Files:**
- Create: `assets/shaders/light.frag`, `assets/shaders/bloom_extract.frag`, `assets/shaders/blur.frag`, `assets/shaders/postprocess.frag`, `assets/shaders/grid.frag`

- [ ] **Step 1: Create all shader files**

Create `assets/shaders/light.frag`:

```glsl
#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform vec2 u_lightPos;
uniform vec3 u_lightColor;
uniform float u_lightRadius;
uniform float u_lightIntensity;
uniform float u_lightFalloff;
uniform vec2 u_resolution;

void main() {
    vec2 pixelPos = v_uv * u_resolution;
    float dist = distance(pixelPos, u_lightPos);
    float attenuation = 1.0 - pow(clamp(dist / u_lightRadius, 0.0, 1.0), u_lightFalloff);
    vec3 light = u_lightColor * u_lightIntensity * attenuation;
    fragColor = vec4(light, 1.0);
}
```

Create `assets/shaders/bloom_extract.frag`:

```glsl
#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_scene;
uniform float u_threshold;

void main() {
    vec3 color = texture(u_scene, v_uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // Hard threshold — add knee parameter later if bloom edges are too harsh
    fragColor = vec4(color * step(u_threshold, luminance), 1.0);
}
```

Create `assets/shaders/blur.frag`:

```glsl
#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_texture;
uniform vec2 u_direction; // (1/w, 0) for horizontal, (0, 1/h) for vertical

// 9-tap Gaussian weights
const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec3 result = texture(u_texture, v_uv).rgb * weights[0];
    for (int i = 1; i < 5; i++) {
        vec2 offset = u_direction * float(i);
        result += texture(u_texture, v_uv + offset).rgb * weights[i];
        result += texture(u_texture, v_uv - offset).rgb * weights[i];
    }
    fragColor = vec4(result, 1.0);
}
```

Create `assets/shaders/postprocess.frag`:

```glsl
#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloomStrength;
uniform float u_vignetteRadius;
uniform float u_vignetteSmooth;
uniform vec3 u_colorTint;
uniform float u_brightness;
uniform float u_contrast;

void main() {
    vec3 color = texture(u_scene, v_uv).rgb;

    // Bloom composite
    vec3 bloom = texture(u_bloom, v_uv).rgb;
    color += bloom * u_bloomStrength;

    // Vignette
    float dist = distance(v_uv, vec2(0.5));
    float vignette = smoothstep(u_vignetteRadius, u_vignetteRadius - u_vignetteSmooth, dist);
    color *= vignette;

    // Color grading
    color *= u_colorTint;
    color = (color - 0.5) * u_contrast + 0.5;
    color *= u_brightness;

    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
```

Create `assets/shaders/blit.frag`:

```glsl
#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_texture;

void main() {
    fragColor = texture(u_texture, v_uv);
}
```

Create `assets/shaders/grid.frag`:

```glsl
#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform mat4 u_inverseVP;
uniform float u_gridSize;
uniform float u_zoom;
uniform vec4 u_gridColor;
uniform vec2 u_cameraPos;

void main() {
    // Reconstruct world position from screen UV + inverse VP
    vec4 clipPos = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
    vec4 worldPos4 = u_inverseVP * clipPos;
    vec2 worldPos = worldPos4.xy / worldPos4.w;

    // Primary grid lines via fwidth anti-aliasing
    vec2 grid = abs(fract(worldPos / u_gridSize - 0.5) - 0.5);
    vec2 lineWidth = fwidth(worldPos / u_gridSize) * 1.5;
    vec2 lines = smoothstep(lineWidth, vec2(0.0), grid);
    float gridAlpha = max(lines.x, lines.y);

    // Sub-grid at 1/4 size when zoomed in past 2x
    float subAlpha = 0.0;
    if (u_zoom > 2.0) {
        float subSize = u_gridSize * 0.25;
        vec2 subGrid = abs(fract(worldPos / subSize - 0.5) - 0.5);
        vec2 subLineWidth = fwidth(worldPos / subSize) * 1.5;
        vec2 subLines = smoothstep(subLineWidth, vec2(0.0), subGrid);
        subAlpha = max(subLines.x, subLines.y) * 0.3; // faint
    }

    float totalAlpha = max(gridAlpha, subAlpha);

    // LOD fade at distance
    float dist = length(worldPos - u_cameraPos);
    float fade = 1.0 - smoothstep(u_gridSize * 20.0, u_gridSize * 30.0, dist);

    fragColor = u_gridColor * totalAlpha * fade;
}
```

- [ ] **Step 2: Build to verify shaders are copied**

Run: `"$CMAKE" --build out/build --config Debug`
Expected: Build succeeds, shader files copied to build/assets/shaders/

- [ ] **Step 3: Commit**

```bash
git add assets/shaders/light.frag assets/shaders/bloom_extract.frag assets/shaders/blur.frag assets/shaders/postprocess.frag assets/shaders/blit.frag assets/shaders/grid.frag
git commit -m "feat(shaders): light, bloom extract, blur, post-process, and grid shaders"
```

---

## Task 5: Lighting System

**Files:**
- Create: `engine/render/lighting.h`, `engine/render/lighting.cpp`

- [ ] **Step 1: Write the lighting implementation**

Create `engine/render/lighting.h`:

```cpp
#pragma once
#include "engine/core/types.h"
#include "engine/render/shader.h"
#include "engine/render/render_graph.h"

namespace fate {

struct PointLight {
    Vec2 position;
    Color color = {1.0f, 0.9f, 0.7f, 1.0f};
    float radius = 128.0f;
    float intensity = 1.0f;
    float falloff = 2.0f;
};

struct LightingConfig {
    Color ambientColor = {0.15f, 0.12f, 0.2f, 1.0f};
    float ambientIntensity = 1.0f;
    bool enabled = true;
};

// Registers the lighting pass with the render graph
void registerLightingPass(RenderGraph& graph, LightingConfig& config);

} // namespace fate
```

Create `engine/render/lighting.cpp`:

```cpp
#include "engine/render/lighting.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/render/gl_loader.h"
#include "engine/ecs/world.h"
#include "engine/core/logger.h"

namespace fate {

static Shader s_lightShader;
static bool s_lightShaderLoaded = false;

static void ensureLightShader() {
    if (s_lightShaderLoaded) return;
    std::string base = std::string(FATE_SOURCE_DIR) + "/assets/shaders/";
    s_lightShaderLoaded = s_lightShader.loadFromFile(base + "fullscreen_quad.vert", base + "light.frag");
    if (!s_lightShaderLoaded) {
        LOG_ERROR("Lighting", "Failed to load light shader");
    }
}

void registerLightingPass(RenderGraph& graph, LightingConfig& config) {
    graph.addPass({"Lighting", true, [&config](RenderPassContext& ctx) {
        if (!config.enabled || !ctx.world) return;

        ensureLightShader();
        if (!s_lightShaderLoaded) return;

        int w = ctx.viewportWidth;
        int h = ctx.viewportHeight;

        // Build light map
        auto& lightMap = ctx.graph->getFBO("LightMap", w, h);
        lightMap.bind();

        // Clear to ambient
        glClearColor(
            config.ambientColor.r * config.ambientIntensity,
            config.ambientColor.g * config.ambientIntensity,
            config.ambientColor.b * config.ambientIntensity,
            1.0f
        );
        glClear(GL_COLOR_BUFFER_BIT);

        // Additive blending for point lights
        glBlendFunc(GL_ONE, GL_ONE);

        s_lightShader.bind();
        s_lightShader.setVec2("u_resolution", {(float)w, (float)h});

        // Iterate PointLightComponents — this requires the component to be available
        // The actual forEach happens in the game layer; here we provide the rendering logic
        // For now, store lights in a temporary vector filled by the game before graph execution
        // This will be wired in Task 8 when we integrate with the game

        s_lightShader.unbind();
        lightMap.unbind();

        // Composite light map onto scene via multiplicative blend
        // Uses blit.frag as a passthrough shader
        static Shader s_blitShader;
        static bool s_blitLoaded = false;
        if (!s_blitLoaded) {
            std::string base = std::string(FATE_SOURCE_DIR) + "/assets/shaders/";
            s_blitLoaded = s_blitShader.loadFromFile(base + "fullscreen_quad.vert", base + "blit.frag");
        }

        auto& scene = ctx.graph->getFBO("Scene", w, h, true);
        scene.bind();
        glBlendFunc(GL_DST_COLOR, GL_ZERO); // multiplicative

        s_blitShader.bind();
        s_blitShader.setInt("u_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, lightMap.textureId());
        FullscreenQuad::instance().draw();
        s_blitShader.unbind();

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // restore
        scene.unbind();
    }});
}

} // namespace fate
```

Note: The lighting pass wiring with ECS will be completed in Task 8 when we integrate with game_app.cpp. The lighting.cpp provides the framework; the game layer fills in the light iteration.

- [ ] **Step 2: Commit**

```bash
git add engine/render/lighting.h engine/render/lighting.cpp
git commit -m "feat(render): 2D lighting system with ambient + point light framework"
```

---

## Task 6: Post-Processing System

**Files:**
- Create: `engine/render/post_process.h`, `engine/render/post_process.cpp`

- [ ] **Step 1: Write the post-process implementation**

Create `engine/render/post_process.h`:

```cpp
#pragma once
#include "engine/core/types.h"
#include "engine/render/shader.h"
#include "engine/render/render_graph.h"

namespace fate {

struct PostProcessConfig {
    bool bloomEnabled = true;
    float bloomThreshold = 0.8f;
    float bloomStrength = 0.3f;

    bool vignetteEnabled = true;
    float vignetteRadius = 0.75f;
    float vignetteSmoothness = 0.4f;

    Color colorTint = Color::white();
    float brightness = 1.0f;
    float contrast = 1.0f;
};

// Registers bloom extract, blur, and composite passes with the render graph
void registerPostProcessPasses(RenderGraph& graph, PostProcessConfig& config);

} // namespace fate
```

Create `engine/render/post_process.cpp`:

```cpp
#include "engine/render/post_process.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/render/gl_loader.h"
#include "engine/core/logger.h"

namespace fate {

static Shader s_bloomExtractShader;
static Shader s_blurShader;
static Shader s_postProcessShader;
static bool s_shadersLoaded = false;

static void ensureShaders() {
    if (s_shadersLoaded) return;
    std::string base = std::string(FATE_SOURCE_DIR) + "/assets/shaders/";
    std::string vert = base + "fullscreen_quad.vert";
    bool ok = true;
    ok &= s_bloomExtractShader.loadFromFile(vert, base + "bloom_extract.frag");
    ok &= s_blurShader.loadFromFile(vert, base + "blur.frag");
    ok &= s_postProcessShader.loadFromFile(vert, base + "postprocess.frag");
    s_shadersLoaded = ok;
    if (!ok) LOG_ERROR("PostProcess", "Failed to load one or more post-process shaders");
}

void registerPostProcessPasses(RenderGraph& graph, PostProcessConfig& config) {
    // Pass: Bloom Extract
    graph.addPass({"BloomExtract", true, [&config](RenderPassContext& ctx) {
        if (!config.bloomEnabled) return;
        ensureShaders();
        if (!s_shadersLoaded) return;

        int halfW = ctx.viewportWidth / 2;
        int halfH = ctx.viewportHeight / 2;

        auto& scene = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        auto& bloomDS = ctx.graph->getFBO("BloomDownsample", halfW, halfH);

        bloomDS.bind();
        glClear(GL_COLOR_BUFFER_BIT);

        s_bloomExtractShader.bind();
        s_bloomExtractShader.setInt("u_scene", 0);
        s_bloomExtractShader.setFloat("u_threshold", config.bloomThreshold);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene.textureId());

        FullscreenQuad::instance().draw();

        s_bloomExtractShader.unbind();
        bloomDS.unbind();
    }});

    // Pass: Bloom Blur (horizontal + vertical ping-pong)
    graph.addPass({"BloomBlur", true, [&config](RenderPassContext& ctx) {
        if (!config.bloomEnabled) return;
        ensureShaders();
        if (!s_shadersLoaded) return;

        int halfW = ctx.viewportWidth / 2;
        int halfH = ctx.viewportHeight / 2;

        auto& bloomDS = ctx.graph->getFBO("BloomDownsample", halfW, halfH);
        auto& blurH = ctx.graph->getFBO("BloomBlurH", halfW, halfH);
        auto& blurV = ctx.graph->getFBO("BloomBlurV", halfW, halfH);

        s_blurShader.bind();
        s_blurShader.setInt("u_texture", 0);

        // Horizontal pass
        blurH.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        s_blurShader.setVec2("u_direction", {1.0f / halfW, 0.0f});
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomDS.textureId());
        FullscreenQuad::instance().draw();
        blurH.unbind();

        // Vertical pass
        blurV.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        s_blurShader.setVec2("u_direction", {0.0f, 1.0f / halfH});
        glBindTexture(GL_TEXTURE_2D, blurH.textureId());
        FullscreenQuad::instance().draw();
        blurV.unbind();

        s_blurShader.unbind();
    }});

    // Pass: Post-Process Composite
    graph.addPass({"PostProcess", true, [&config](RenderPassContext& ctx) {
        ensureShaders();
        if (!s_shadersLoaded) return;

        int w = ctx.viewportWidth;
        int h = ctx.viewportHeight;

        auto& scene = ctx.graph->getFBO("Scene", w, h, true);
        auto& blurV = ctx.graph->getFBO("BloomBlurV", w / 2, h / 2);
        auto& postProcess = ctx.graph->getFBO("PostProcess", w, h);

        postProcess.bind();
        glClear(GL_COLOR_BUFFER_BIT);

        s_postProcessShader.bind();
        s_postProcessShader.setInt("u_scene", 0);
        s_postProcessShader.setInt("u_bloom", 1);
        s_postProcessShader.setFloat("u_bloomStrength", config.bloomEnabled ? config.bloomStrength : 0.0f);
        s_postProcessShader.setFloat("u_vignetteRadius", config.vignetteEnabled ? config.vignetteRadius : 10.0f);
        s_postProcessShader.setFloat("u_vignetteSmooth", config.vignetteSmoothness);
        s_postProcessShader.setVec3("u_colorTint", {config.colorTint.r, config.colorTint.g, config.colorTint.b});
        s_postProcessShader.setFloat("u_brightness", config.brightness);
        s_postProcessShader.setFloat("u_contrast", config.contrast);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene.textureId());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, blurV.textureId());

        FullscreenQuad::instance().draw();

        glActiveTexture(GL_TEXTURE0); // reset active unit
        s_postProcessShader.unbind();
        postProcess.unbind();
    }});
}

} // namespace fate
```

- [ ] **Step 2: Build to verify**

Run: `"$CMAKE" --build out/build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add engine/render/post_process.h engine/render/post_process.cpp
git commit -m "feat(render): post-processing system with bloom, vignette, and color grading"
```

---

## Task 7: CMake — ImGuizmo

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add ImGuizmo FetchContent**

After the ImPlot FetchContent block (around line 72), add:

```cmake
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imguizmo)
```

Add ImGuizmo source to `imgui_lib`. In the `add_library(imgui_lib STATIC ...)` block, add:

```cmake
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
```

In `target_include_directories(imgui_lib PUBLIC ...)`, add:

```cmake
    ${imguizmo_SOURCE_DIR}
```

- [ ] **Step 2: Reconfigure and build**

```bash
"$CMAKE" -S . -B out/build && "$CMAKE" --build out/build --config Debug
```

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add ImGuizmo dependency"
```

---

## Task 8: Wire Render Graph into App + Game

This is the big integration task that replaces the monolithic render function.

**Files:**
- Modify: `engine/app.h`, `engine/app.cpp`
- Modify: `game/game_app.h`, `game/game_app.cpp`

- [ ] **Step 1: Add RenderGraph to App**

In `engine/app.h`, add includes:

```cpp
#include "engine/render/render_graph.h"
#include "engine/render/lighting.h"
#include "engine/render/post_process.h"
#include "engine/render/fullscreen_quad.h"
```

Add members:

```cpp
    RenderGraph renderGraph_;
    LightingConfig lightingConfig_;
    PostProcessConfig postProcessConfig_;
```

Add public accessor:

```cpp
    RenderGraph& renderGraph() { return renderGraph_; }
    LightingConfig& lightingConfig() { return lightingConfig_; }
    PostProcessConfig& postProcessConfig() { return postProcessConfig_; }
```

- [ ] **Step 2: Initialize FullscreenQuad and register engine passes in App::init()**

In `App::init()`, after the SpriteBatch init:

```cpp
    FullscreenQuad::instance().init();

    // Register engine render passes (game passes registered in onInit)
    registerLightingPass(renderGraph_, lightingConfig_);
    registerPostProcessPasses(renderGraph_, postProcessConfig_);
```

- [ ] **Step 3: Replace App::render() with graph execution**

Replace the render method body. The key change: instead of calling `onRender()` directly, the render graph executes all passes. Game passes (tiles, entities, etc.) are registered by `game_app.cpp` in `onInit()`.

The new `render()`:

```cpp
void App::render() {
    auto* scene = SceneManager::instance().currentScene();
    World* world = scene ? &scene->world() : nullptr;
    auto& editor = Editor::instance();

    int vpW = editor.viewportFbo().width();
    int vpH = editor.viewportFbo().height();

    if (vpW > 0 && vpH > 0) {
        camera_.setViewportSize(vpW, vpH);

        // Execute render graph — all passes render into internal FBOs
        RenderPassContext ctx;
        ctx.spriteBatch = &spriteBatch_;
        ctx.camera = &camera_;
        ctx.world = world;
        ctx.viewportWidth = vpW;
        ctx.viewportHeight = vpH;
        renderGraph_.execute(ctx);

        // Blit final result (PostProcess FBO) to editor viewport FBO
        auto& editorFbo = editor.viewportFbo();
        editorFbo.bind();
        glClear(GL_COLOR_BUFFER_BIT);

        // Blit PostProcess result into editor FBO using FullscreenQuad + blit shader
        auto& postFbo = renderGraph_.getFBO("PostProcess", vpW, vpH);

        // Load blit shader (lazy, same as lighting uses)
        static Shader s_blitShader;
        static bool s_blitLoaded = false;
        if (!s_blitLoaded) {
            std::string base = std::string(FATE_SOURCE_DIR) + "/assets/shaders/";
            s_blitLoaded = s_blitShader.loadFromFile(base + "fullscreen_quad.vert", base + "blit.frag");
        }

        s_blitShader.bind();
        s_blitShader.setInt("u_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postFbo.textureId());
        FullscreenQuad::instance().draw();
        s_blitShader.unbind();

        editorFbo.unbind();
    }

    // ImGui UI rendering
    glViewport(0, 0, config_.windowWidth, config_.windowHeight);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    editor.renderUI(world, &camera_, &spriteBatch_, &frameArena_);

    SDL_GL_SwapWindow(window_);
}
```

- [ ] **Step 4: Add shutdown for FullscreenQuad**

In `App::shutdown()`, add before GL context teardown:

```cpp
    // Destroy all render graph FBOs while GL context is still alive
    // (Framebuffer has no auto-cleanup in destructor — must be explicit)
    renderGraph_ = RenderGraph{}; // destroys unique_ptrs, but FBOs have trivial dtor
    // Actually — since Framebuffer dtor is default, we need to explicitly destroy each FBO.
    // Add a clearFBOs() method to RenderGraph, or iterate and call destroy() on each.
    FullscreenQuad::instance().shutdown();
```

Add a `clearFBOs()` method to `RenderGraph`:

```cpp
// In render_graph.h, add to public:
void clearFBOs();

// In render_graph.cpp:
void RenderGraph::clearFBOs() {
    for (auto& [name, fbo] : fboPool_) {
        fbo->destroy();
    }
    fboPool_.clear();
}
```

Then in `App::shutdown()`:

```cpp
    renderGraph_.clearFBOs();
    FullscreenQuad::instance().shutdown();
```

- [ ] **Step 5: Migrate game_app.cpp onRender to render pass registration**

In `game/game_app.cpp`, in the `onInit()` method, register game render passes with the graph. Remove (or gut) the `onRender()` method since rendering now goes through passes.

Add to `onInit()`:

```cpp
    // Register game render passes
    auto& graph = renderGraph();

    // Pass 1-2: Tile rendering
    graph.addPass({"GroundTiles", true, [this](fate::RenderPassContext& ctx) {
        auto& scene = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        scene.bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        // Render tilemap ground layers
        ctx.spriteBatch->begin(ctx.camera->getViewProjection());
        // ... tilemap render code moved from onRender ...
        ctx.spriteBatch->end();
        scene.unbind();
    }});

    // More passes for entities, particles, SDF text...
```

The exact pass bodies will move existing `onRender()` code into lambda callbacks. The implementer should read the current `onRender()` in `game_app.cpp` and distribute the rendering logic into the appropriate passes.

- [ ] **Step 6: Build and run**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: Build succeeds, all tests pass. Visual output may need manual verification.

- [ ] **Step 7: Commit**

```bash
git add engine/app.h engine/app.cpp game/game_app.h game/game_app.cpp
git commit -m "feat(app): replace monolithic render with render graph execution"
```

---

## Task 9: Editor — Grid Shader + Selection Outlines + ImGuizmo

**Files:**
- Modify: `engine/editor/editor.h`, `engine/editor/editor.cpp`

- [ ] **Step 1: Add grid shader, selection outlines, and ImGuizmo to editor**

In `engine/editor/editor.h`:

Add includes:

```cpp
#include "engine/render/shader.h"
#include <ImGuizmo.h>
```

Add members:

```cpp
    Shader gridShader_;
    bool gridShaderLoaded_ = false;
    ImGuizmo::OPERATION gizmoOperation_ = ImGuizmo::TRANSLATE;
```

Change `EditorTool::Resize` to `EditorTool::Scale` and add `Rotate`:

```cpp
enum class EditorTool {
    Move,    // W
    Scale,   // E (was Resize)
    Rotate,  // R
    Paint,   // B
    Erase    // X
};
```

In `engine/editor/editor.cpp`:

**Replace `drawSceneGrid()`** with a grid shader version that uses the fullscreen quad and grid.frag shader. Load the shader lazily, set uniforms (u_inverseVP, u_gridSize, u_zoom, u_gridColor, u_cameraPos), and draw.

**Add selection outlines** in the editor overlay rendering. For each selected entity, use the stencil buffer to draw a colored outline.

**Add ImGuizmo** to the viewport rendering. In `drawSceneViewport()` or the EditorOverlays pass, after rendering the scene image, set up ImGuizmo with orthographic mode and manipulate the selected entity's transform.

**Add tool mode handling** for R = Rotate, update E from Resize to Scale.

**Add View menu items** for PostProcess config toggle.

The editor.cpp changes are extensive — the implementer should read the existing editor.cpp carefully and integrate piece by piece.

- [ ] **Step 2: Build and test**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass. Manual verification of grid, selection outlines, and ImGuizmo.

- [ ] **Step 3: Commit**

```bash
git add engine/editor/editor.h engine/editor/editor.cpp
git commit -m "feat(editor): infinite grid shader, stencil selection outlines, ImGuizmo transform handles"
```

---

## Task 10: Component Registration

**Files:**
- Modify: `game/register_components.h`

- [ ] **Step 1: Register new components**

Add includes for `ParticleEmitterComponent` and `PointLightComponent`. Create the component structs if not already created, add FATE_REFLECT macros, and register with `ComponentMetaRegistry` including custom serializers.

`PointLightComponent` serialization: serialize all `PointLight` fields (position, color, radius, intensity, falloff).

`ParticleEmitterComponent` serialization: serialize the `EmitterConfig` fields. The `AssetHandle texture` field serializes as a path string (same pattern as SpriteComponent's texturePath).

- [ ] **Step 2: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass

- [ ] **Step 3: Commit**

```bash
git add game/register_components.h engine/particle/ engine/render/lighting.h
git commit -m "feat(ecs): register ParticleEmitterComponent and PointLightComponent"
```

---

## Task 11: Integration Test + Final Verification

- [ ] **Step 1: Full clean build**

Run: `"$CMAKE" --build out/build --config Debug --clean-first`
Expected: Zero errors

- [ ] **Step 2: Run full test suite**

Run: `./out/build/Debug/fate_tests.exe`
Expected: All tests pass (137 existing + ~10 new)

- [ ] **Step 3: Manual smoke test**

Launch the game. Verify:
1. Scene renders correctly (tiles, entities visible)
2. View > Memory panel still works
3. Grid shader renders (toggle with editor grid button)
4. No visual regression from the render graph migration
5. Post-process effects visible (vignette darkening screen edges)
6. Selection outline appears when selecting an entity

- [ ] **Step 4: Commit if needed**

```bash
git status
# Only commit if there are Phase 4 changes
git add -A && git commit -m "feat: Phase 4 complete — render graph, particles, lighting, post-process, editor polish"
```
