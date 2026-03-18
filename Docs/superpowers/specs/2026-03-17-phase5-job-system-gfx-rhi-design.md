# Phase 5: Job System + Graphics RHI — Design Spec

**Date:** 2026-03-17
**Status:** Approved

## Overview

Phase 5 adds two independent subsystems to the engine:
1. **Fiber-based Job System** — 4 fixed worker threads with fiber suspend/resume, targeting spatial grid rebuild, chunk lifecycle, and AI ticking
2. **Graphics RHI** — Full render hardware interface with Device, CommandList, and Pipeline State Objects. OpenGL 3.3 backend only (abstraction enables future backends)

---

## Part 1: Fiber-Based Job System

### Core Concepts

- `JobSystem` singleton manages 4 worker threads, each running a fiber scheduler loop
- **Fibers** are lightweight execution contexts (~64KB stack) created via `CreateFiber`/`ConvertThreadToFiber` on Windows
- A **Job** is a function pointer + data pointer submitted to a lock-free multi-producer/multi-consumer queue
- Workers pull jobs from the queue, run them on a fiber. When a job calls `waitForCounter()`, the fiber suspends and the worker picks up another job — no thread blocking
- A **Counter** (atomic int) tracks completion of job groups. `waitForCounter(counter, 0)` suspends the current fiber until the counter reaches zero, enabling dependent task chains
- **Fiber pool:** Pre-allocate ~32 fibers to avoid runtime allocation. When a fiber finishes or suspends, it returns to the pool

### API

```cpp
namespace fate {

struct Job {
    void (*function)(void* param);
    void* param = nullptr;
};

class JobSystem {
public:
    static JobSystem& instance();

    void init(int workerCount = 4);
    void shutdown();

    // Submit jobs, returns counter to wait on
    Counter* submit(Job* jobs, int count);

    // Suspend current fiber until counter reaches target
    void waitForCounter(Counter* counter, int target = 0);
};

} // namespace fate
```

### Workload Integration

**1. Spatial Grid Rebuild**
- Main thread submits a `rebuildSpatialGrid` job, continues with non-dependent work, waits on the counter before collision queries
- Double-buffer pattern: workers write to back buffer while main thread reads front buffer. Swap at sync point.

**2. Chunk Lifecycle**
- Each chunk transition (e.g., Loading → Staging) becomes a job. The chunk manager submits a batch of transitions per frame, with a counter to sync before the render pass.
- Chunk data loading (disk I/O) naturally benefits from fibers — the fiber suspends during I/O wait, worker picks up another chunk.

**3. AI Ticking**
- Partition mobs into groups of ~4, submit each group as a job. AI reads world state (positions, threat tables) but only writes to its own MobAIComponent — no cross-mob writes, so no synchronization needed.

**Sync point:** One `waitForCounter` per frame, after all three workloads are submitted, before the movement system runs. Fan out → join → proceed single-threaded for the rest of the frame.

### Memory: Fiber-Local Scratch Arenas

The existing `ScratchArena` uses `thread_local` storage, which is **per-thread, not per-fiber**. Multiple fibers on the same worker thread would corrupt each other's scratch memory. Each fiber in the pool gets its own arena pair stored in its fiber context struct (not `thread_local`). 32 fibers x 2 arenas x 256MB reserved = 16GB reserved virtual address space (reserved is not committed — actual physical memory usage stays small).

Jobs access their fiber's arena via `JobSystem::scratchArena()` instead of the global `GetScratchArena()`.

### Counter Ownership

Counters come from a fixed pool of 64, allocated in `JobSystem::init()`. `submit()` returns a `Counter*` from the pool. A counter is automatically returned to the pool when it reaches its target value and all waiting fibers have resumed. Debug assertion fires if the pool is exhausted.

### Fiber Pool Exhaustion

With 32 fibers and 4 workers, at most 28 fibers can be suspended simultaneously. With 3 workloads and ~4 AI groups, this leaves ample headroom. Debug assertion fires if no free fiber is available — this indicates a design bug (too many concurrent suspended tasks), not a runtime condition to handle gracefully.

### Chunk I/O and Fiber Suspension

Chunk loading does **not** use blocking I/O on the fiber. Instead, chunk transitions submit async I/O (Windows overlapped `ReadFile`), then the job completes. A follow-up job is submitted via a counter dependency when the I/O completion signals. The fiber does not block on disk — it completes and returns to the pool.

### Platform

- Windows fibers via `CreateFiber`/`ConvertThreadToFiber`/`SwitchToFiber` (matches existing Win32 FileWatcher pattern)
- `fiber.h` provides platform abstraction header; `fiber_win32.cpp` implements

---

## Part 2: Graphics RHI

### Resource Handles

Typed 32-bit handles (matching AssetHandle pattern from Phase 3). No raw GL integers leak outside the backend.

```cpp
namespace gfx {

struct ShaderHandle       { uint32_t id = 0; bool valid() const { return id != 0; } };
struct TextureHandle      { uint32_t id = 0; bool valid() const { return id != 0; } };
struct BufferHandle       { uint32_t id = 0; bool valid() const { return id != 0; } };
struct PipelineHandle     { uint32_t id = 0; bool valid() const { return id != 0; } };
struct FramebufferHandle  { uint32_t id = 0; bool valid() const { return id != 0; } };

enum class BufferType    { Vertex, Index, Uniform };
enum class BufferUsage   { Static, Dynamic, Stream };
enum class TextureFormat { RGBA8, RGB8, R8, Depth24Stencil8 };
enum class BlendMode     { None, Alpha, Additive, Multiplicative };  // replaces fate::BlendMode in sprite_batch.h
enum class PrimitiveType { Triangles, Lines, Points };

struct VertexAttribute {
    int location;
    int components;   // 1-4
    size_t offset;
    bool normalized = false;
};

struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    size_t stride = 0;
};

} // namespace gfx
```

### Pipeline State Object

Bundles shader + vertex layout + blend mode into a single immutable object. Created once, bound cheaply. Replaces the current pattern of setting shader/blend/vertex state separately each draw call.

```cpp
struct PipelineDesc {
    ShaderHandle shader;
    VertexLayout vertexLayout;
    BlendMode blendMode = BlendMode::Alpha;
    bool depthTest = false;
    bool depthWrite = false;
};
```

### Device

Creates and destroys resources. One per application.

```cpp
namespace gfx {

class Device {
public:
    static Device& instance();

    bool init();
    void shutdown();

    // Resource creation
    ShaderHandle createShader(const std::string& vertSrc, const std::string& fragSrc);
    TextureHandle createTexture(int width, int height, TextureFormat format,
                                const void* data = nullptr);
    BufferHandle createBuffer(BufferType type, BufferUsage usage,
                              size_t size, const void* data = nullptr);
    PipelineHandle createPipeline(const PipelineDesc& desc);
    FramebufferHandle createFramebuffer(int width, int height,
                                        TextureFormat colorFormat,
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
    void getFramebufferSize(FramebufferHandle h, int& w, int& h);
};

} // namespace gfx
```

### CommandList

Records render commands for a frame. One created per frame, submitted at the end.

```cpp
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

    // Uniforms
    void setUniform(const char* name, float value);
    void setUniform(const char* name, int value);
    void setUniform(const char* name, const Vec2& value);
    void setUniform(const char* name, const Vec3& value);
    void setUniform(const char* name, const Color& value);
    void setUniform(const char* name, const Mat4& value);

    // Draw
    void draw(PrimitiveType type, int vertexCount, int firstVertex = 0);
    void drawIndexed(PrimitiveType type, int indexCount, int firstIndex = 0);

    // Submit to GPU
    void submit();
};

} // namespace gfx
```

### Uniform Caching

The GL backend caches uniform locations per-pipeline (keyed by program ID + uniform name). `CommandList::setUniform` delegates to the cache — no `glGetUniformLocation` call per frame. This replaces the existing `Shader::uniformCache_`.

### Debug Validation

Under `#ifdef FATE_DEBUG`, the CommandList validates state before draws: pipeline must be bound, vertex buffer must be bound, etc. Invalid handle usage logs via `LOG_ERROR`. This catches integration bugs during migration without runtime cost in release builds.

### Migration Strategy

**SpriteBatch:** Constructor creates `BufferHandle` (vertex + index) and `PipelineHandle` via `gfx::Device`. `flush()` records commands to a `CommandList` instead of direct GL calls. BlendMode changes become different pre-created `PipelineHandle`s. The existing `fate::BlendMode` enum is removed; SpriteBatch uses `gfx::BlendMode`.

**SDF Text:** `SDFText::drawTexturedQuad()` currently takes a raw `unsigned int` GL texture ID. Migration wraps the SDF font atlas as a `gfx::TextureHandle` at creation time. `drawTexturedQuad` takes a `TextureHandle` instead.

**RenderGraph:** `RenderPassContext` gains a `CommandList*` member. Each pass lambda records commands to it. The render graph calls `begin()`/`end()`/`submit()` around each pass.

**Shader/Texture/Framebuffer classes:** Become thin wrappers holding a `gfx::Handle`. Public API stays the same but internally delegates to the RHI. Game code and editor code doesn't change.

**Lighting + PostProcess:** `FullscreenQuad::draw()` becomes `cmd.bindPipeline(fullscreenPSO); cmd.draw(Triangles, 3);`

**gl_loader.h:** Moves entirely inside the GL backend implementation. Nothing outside `engine/render/gfx/backend/gl/` includes it.

**Migration order:**
1. Build `gfx::Device` + `gfx::CommandList` with GL backend
2. Migrate `Shader`, `Texture`, `Framebuffer` to wrap gfx handles
3. Migrate `SpriteBatch` to use CommandList
4. Migrate render graph passes (lighting, post-process, blit)
5. Remove all direct GL calls outside `backend/`

---

## File Structure

```
engine/
  job/
    job_system.h          — JobSystem, Job, Counter, public API
    job_system.cpp        — Fiber pool, worker loop, queue, scheduling
    fiber.h               — Platform fiber abstraction
    fiber_win32.cpp       — Windows fiber implementation
  render/
    gfx/
      types.h             — Handles, enums, VertexLayout, PipelineDesc
      device.h            — gfx::Device interface
      command_list.h      — gfx::CommandList interface
      backend/
        gl/
          gl_device.cpp   — OpenGL Device implementation
          gl_command_list.cpp — OpenGL CommandList implementation
          gl_loader.h     — GL function pointers (moved from engine/render/)
          gl_loader.cpp   — GL function pointer loading
```

### Existing Files Modified

- `engine/render/shader.h/.cpp` — Wrap `gfx::ShaderHandle`
- `engine/render/texture.h/.cpp` — Wrap `gfx::TextureHandle`
- `engine/render/framebuffer.h/.cpp` — Wrap `gfx::FramebufferHandle`
- `engine/render/sprite_batch.h/.cpp` — Use CommandList for draws
- `engine/render/render_graph.h/.cpp` — Pass CommandList through context
- `engine/render/lighting.cpp` — Use CommandList
- `engine/render/post_process.cpp` — Use CommandList
- `engine/app.cpp` — Init JobSystem and gfx::Device in startup
- `game/game_app.cpp` — Submit AI/spatial/chunk jobs in update loop
