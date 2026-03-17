# Phase 4: Render Graph, Particles, Lighting, Post-Processing & Editor Polish

## Overview

Five interlocking subsystems completing Phase 4 of the engine research upgrade:

- **Render Graph** — semi-static ordered pass list with FBO pool, replacing the monolithic render function
- **Particle System** — CPU emitters with GPU vertex buffer upload, hybrid architecture
- **2D Lighting** — ambient + point lights via light map FBO with multiplicative compositing
- **Post-Processing** — bloom (downsample + separable blur + composite), vignette, color grading
- **Editor Polish** — ImGuizmo transform handles, stencil-based selection outlines, infinite grid shader

## Render Graph

### Architecture

A `RenderGraph` class owns an ordered vector of `RenderPass` objects and an `FBOPool`. Passes are registered at init time (not every frame) and executed sequentially. The graph is semi-static — pass order changes only when features toggle.

```cpp
// engine/render/render_graph.h

struct RenderPass {
    const char* name;
    bool enabled = true;
    std::function<void(RenderPassContext& ctx)> execute;
};

struct RenderPassContext {
    SpriteBatch* spriteBatch;
    Camera* camera;
    World* world;
    RenderGraph* graph;
    int viewportWidth, viewportHeight;
};

class RenderGraph {
public:
    void addPass(RenderPass pass);
    void removePass(const char* name);
    void setPassEnabled(const char* name, bool enabled);

    void execute(SpriteBatch& batch, Camera& camera, World* world,
                 int viewportWidth, int viewportHeight);

    // FBO pool — get or create by name, optionally with depth/stencil
    Framebuffer& getFBO(const char* name, int width, int height, bool withDepthStencil = false);

    const std::vector<RenderPass>& passes() const;

private:
    std::vector<RenderPass> passes_;
    std::unordered_map<std::string, std::unique_ptr<Framebuffer>> fboPool_; // unique_ptr avoids GL handle leak on rehash
};
```

### Execution Flow

1. `execute()` iterates passes in order
2. For each enabled pass: call `pass.execute(ctx)`
3. Each pass binds/unbinds its target FBO via `ctx.graph->getFBO(name, w, h)`
4. Disabled passes are skipped (auto-cull)
5. FBO pool creates on first request, reuses across frames, resizes on viewport change

### Pass Order (10 passes)

| # | Pass | Reads | Writes | Blend Mode |
|---|------|-------|--------|------------|
| 1 | GroundTiles | — | Scene | Standard alpha |
| 2 | AboveGroundTiles | — | Scene | Standard alpha |
| 3 | Entities | — | Scene | Standard alpha |
| 4 | Particles | — | Scene | Additive or alpha (per emitter) |
| 5 | SDFText | — | Scene | Standard alpha |
| 6 | Lighting | — | LightMap (built from ambient + lights), then composites onto Scene | Additive (lights), multiplicative (composite) |
| 7 | BloomExtract | Scene | BloomDownsample (half-res) | None (threshold copy) |
| 8 | BloomBlur | BloomDownsample | BloomBlurH, BloomBlurV (ping-pong) | None (blur copy) |
| 9 | PostProcess | Scene + BloomBlurV | PostProcess | None (full composite) |
| 10 | EditorOverlays | PostProcess | Final output | Standard alpha |

Passes 1-5 all accumulate onto the same Scene FBO. Pass 6 builds the light map from scratch (ambient clear + additive point lights — it does NOT read the Scene FBO), then composites the LightMap onto Scene via multiplicative blend. Passes 7-9 are the post-process chain. Pass 10 renders editor-only visuals on top.

**Final output target:** Pass 10 (EditorOverlays) binds the editor's viewport FBO, blits the PostProcess result onto it as a fullscreen quad, then draws editor overlays (grid, selection outlines, ImGuizmo) on top. The ImGui scene viewport displays this viewport FBO texture as before.

### Integration with App::render()

The existing `App::render()` changes from direct SpriteBatch calls to:

```cpp
void App::render() {
    auto& fbo = editor.viewportFbo();
    if (fbo.isValid()) {
        camera_.setViewportSize(fbo.width(), fbo.height());
        renderGraph_.execute(spriteBatch_, camera_, world, fbo.width(), fbo.height());
    }
    // ImGui UI rendering unchanged
    editor.renderUI(world, &camera_, &spriteBatch_, &frameArena_);
    SDL_GL_SwapWindow(window_);
}
```

The game's `onRender()` callback is replaced by pass registration at init time. Game code registers callbacks for the tile/entity passes via `renderGraph_.addPass(...)`.

### FBO Requirements

| Name | Resolution | Format | Attachments |
|------|-----------|--------|-------------|
| Scene | Full viewport | RGBA8 | Color + Depth24Stencil8 (for selection outlines) |
| LightMap | Full viewport | RGBA8 | Color only |
| BloomDownsample | Half viewport | RGBA8 | Color only |
| BloomBlurH | Half viewport | RGBA8 | Color only |
| BloomBlurV | Half viewport | RGBA8 | Color only |
| PostProcess | Full viewport | RGBA8 | Color only |

The existing `Framebuffer::create()` gains an optional `bool withDepthStencil` parameter for the Scene FBO. The `Framebuffer` class needs additional members to support this:

```cpp
// Additional members in Framebuffer
unsigned int rbo_ = 0;          // renderbuffer for depth/stencil
bool hasDepthStencil_ = false;  // remembered for resize

// destroy() must also handle:
if (rbo_) { glDeleteRenderbuffers(1, &rbo_); rbo_ = 0; }

// resize() passes hasDepthStencil_ through:
void resize(int w, int h) { destroy(); create(w, h, hasDepthStencil_); }
```

The Scene FBO `glClear` must include `GL_STENCIL_BUFFER_BIT` when the stencil attachment is present.

**Note:** The stencil buffer lives only on the Scene FBO, not the default framebuffer. No changes to `SDL_GL_DEPTH_SIZE` or `SDL_GL_STENCIL_SIZE` context attributes are needed.

## Particle System

### Architecture

CPU emitters manage particle state. Each emitter owns a flat array of particles and rebuilds a vertex buffer each frame for GPU upload.

```cpp
// engine/particle/particle.h

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
    // Spawn
    float spawnRate = 10.0f;        // particles per second
    int burstCount = 0;             // one-shot burst (0 = continuous)

    // Initial ranges (randomized between min/max)
    Vec2 velocityMin = {-20, -50};
    Vec2 velocityMax = {20, -10};
    float lifetimeMin = 0.5f;
    float lifetimeMax = 1.5f;
    float sizeMin = 4.0f;
    float sizeMax = 8.0f;
    float rotationSpeedMin = 0.0f;
    float rotationSpeedMax = 0.0f;

    // Color
    Color colorStart = Color::white();
    Color colorEnd = {1, 1, 1, 0};  // fade to transparent

    // Physics
    Vec2 gravity = {0, 0};

    // Rendering
    AssetHandle texture;
    float depth = 5.0f;
    bool worldSpace = true;         // false = particles move with emitter
    bool additiveBlend = false;     // true for fire/magic, false for smoke/dust
};
```

### ParticleEmitter Class

```cpp
// engine/particle/particle_emitter.h

class ParticleEmitter {
public:
    void init(const EmitterConfig& config, size_t maxParticles = 256);

    void update(float dt, const Vec2& emitterPos);
    void burst(const Vec2& position, int count = -1);

    const std::vector<SpriteVertex>& vertices() const;
    size_t activeCount() const;

    EmitterConfig& config() { return config_; }
    bool isFinished() const;

private:
    EmitterConfig config_;
    std::vector<Particle> particles_;
    std::vector<SpriteVertex> vertices_;
    size_t activeCount_ = 0;
    float spawnAccumulator_ = 0.0f;

    void spawn(const Vec2& pos);
    void buildVertices(const Vec2& emitterPos);
};
```

### ECS Integration

```cpp
struct ParticleEmitterComponent {
    FATE_COMPONENT(ParticleEmitterComponent)
    ParticleEmitter emitter;
    bool autoDestroy = false;
};
```

`ParticleSystem` updates all emitters in the ECS update loop. The particle render pass resolves `config.texture` via `AssetRegistry::instance().get<Texture>(handle)->id()` to obtain the GL texture ID, then uploads vertices via `SpriteBatch::drawTexturedQuad(glTexId, ...)` for each emitter. When `activeCount == maxParticles`, new spawns are dropped until existing particles expire.

**Burst semantics:** `burst(count)` spawns `count` particles instantly. If `count == -1`, uses `config.burstCount`. If both are 0, does nothing.

**`isFinished()` contract:** Returns true only for burst emitters (burstCount > 0, spawnRate == 0) when `activeCount_ == 0` and all particles have expired. Continuous emitters (spawnRate > 0) never return true. The `autoDestroy` flag on `ParticleEmitterComponent` destroys the owning entity when `isFinished()` returns true — useful for one-shot spell impacts.

### GPU-Ready Vertex Format

Particles output `SpriteVertex` (same as SpriteBatch). Migration to compute shaders later changes the data source, not the render path. Each emitter is one draw call.

## 2D Lighting

### Architecture

A light map FBO is rendered with ambient + point lights, then composited onto the scene via multiplicative blending.

```cpp
// engine/render/lighting.h

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
```

### ECS Integration

```cpp
struct PointLightComponent {
    FATE_COMPONENT(PointLightComponent)
    PointLight light;
};
```

Attached to any entity — torches, spells, player glow, boss aura. Position follows the entity's Transform.

### Light Shader

A dedicated `light.frag` shader renders each light as a screen-aligned quad:

```glsl
uniform vec2 u_lightPos;        // screen-space
uniform vec3 u_lightColor;
uniform float u_lightRadius;
uniform float u_lightIntensity;
uniform float u_lightFalloff;

void main() {
    float dist = distance(gl_FragCoord.xy, u_lightPos);
    float attenuation = 1.0 - pow(clamp(dist / u_lightRadius, 0.0, 1.0), u_lightFalloff);
    vec3 light = u_lightColor * u_lightIntensity * attenuation;
    fragColor = vec4(light, 1.0);
}
```

### Lighting Pass Flow

1. Bind LightMap FBO
2. Clear to `ambientColor * ambientIntensity`
3. Set blend mode to additive (`GL_ONE, GL_ONE`)
4. For each visible `PointLightComponent`:
   - Convert world position to screen-space (pixel coords): `ndc = viewProjection * vec4(worldPos, 0, 1)`, then `screenPos = (ndc.xy * 0.5 + 0.5) * vec2(viewportWidth, viewportHeight)`. Note: `gl_FragCoord` is in window-space (pixels from bottom-left), so the y-axis matches the NDC→pixel conversion directly.
   - Upload `u_lightPos` as the screen-space position
   - Draw fullscreen quad with light shader (early-out via distance check in the shader limits cost to the light's footprint)
5. Unbind LightMap FBO
6. Composite LightMap onto Scene FBO via multiplicative blend (`GL_DST_COLOR, GL_ZERO`)

### Day/Night

Changing `ambientColor` and `ambientIntensity` over time. Full daylight = white at 1.0 (no darkening). Night = dark purple at 0.15. The lighting pass handles this automatically.

## Post-Processing

### Bloom Pipeline

Three sub-steps using separable Gaussian blur:

1. **Brightness extract** — Read Scene FBO, write pixels with `luminance > bloomThreshold` to BloomDownsample FBO at half resolution
2. **Horizontal blur** — Read BloomDownsample, write to BloomBlurH
3. **Vertical blur** — Read BloomBlurH, write to BloomBlurV

### Post-Process Shader

The final composite shader reads Scene + BloomBlurV:

```glsl
// assets/shaders/postprocess.frag
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

    // Bloom
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

### Configuration

```cpp
// engine/render/post_process.h

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
```

Exposed in editor for live tweaking.

### Bloom Shaders

```glsl
// assets/shaders/bloom_extract.frag
uniform sampler2D u_scene;
uniform float u_threshold;

void main() {
    vec3 color = texture(u_scene, v_uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(color * step(u_threshold, luminance), 1.0);
}

// assets/shaders/blur.frag
uniform sampler2D u_texture;
uniform vec2 u_direction;  // (1/w, 0) for horizontal, (0, 1/h) for vertical
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

## Shared Fullscreen Quad Vertex Shader

All post-processing, lighting, bloom, and grid passes use the same vertex shader. A fullscreen triangle or quad is generated from `gl_VertexID` with no vertex buffer:

```glsl
// assets/shaders/fullscreen_quad.vert
#version 330 core

out vec2 v_uv;

void main() {
    // Fullscreen triangle from vertex ID (3 vertices cover the screen)
    v_uv = vec2((gl_VertexID & 1) * 2.0, (gl_VertexID >> 1) * 2.0);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
```

Draw with `glDrawArrays(GL_TRIANGLES, 0, 3)` using an empty VAO. The oversized triangle is clipped to the viewport — no index buffer needed.

## Editor Polish

### ImGuizmo — Visual Transform Handles

ImGuizmo is a single-header library vendored via FetchContent. It draws translate/rotate/scale gizmos into ImGui's draw list over the scene viewport.

Integration:
- Set orthographic mode, configure rect to match scene viewport
- Build model matrix from entity Transform
- Call `ImGuizmo::Manipulate()` with camera view/projection
- Decompose result back to Transform with undo support
- **W** = Translate, **E** = Scale (replaces current resize handles), **R** = Rotate
- `ImGuizmo::IsOver()` prevents click-through to scene
- Only visible when entity is selected and editor is paused

### Selection Outlines — Stencil-Based

Selected entities get a colored outline via the stencil buffer:

1. Enable stencil test
2. For each selected entity:
   a. Draw entity sprite to stencil buffer only (stencil ref = 1, write always)
   b. Draw a scaled-up version (1.05x) with flat outline color where stencil != 1
3. Disable stencil test

Colors:
- Orange for single selection
- Blue for multi-selection

Requires adding a depth/stencil attachment (`GL_DEPTH24_STENCIL8`) to the Scene FBO. The `Framebuffer::create()` method gains a `bool withDepthStencil` parameter.

### Infinite Grid Shader

Replaces the current SpriteBatch-drawn grid with a fullscreen quad and dedicated shader.

```glsl
// assets/shaders/grid.frag
uniform mat4 u_inverseVP;
uniform float u_gridSize;
uniform float u_zoom;
uniform vec4 u_gridColor;
uniform vec2 u_cameraPos;

void main() {
    vec2 worldPos = (u_inverseVP * vec4(v_uv * 2.0 - 1.0, 0, 1)).xy;

    // Grid lines via fwidth anti-aliasing
    vec2 grid = abs(fract(worldPos / u_gridSize - 0.5) - 0.5);
    vec2 lineWidth = fwidth(worldPos / u_gridSize) * 1.5;
    vec2 lines = smoothstep(lineWidth, vec2(0.0), grid);
    float gridAlpha = max(lines.x, lines.y);

    // LOD fade at distance
    float dist = length(worldPos - u_cameraPos);
    float fade = 1.0 - smoothstep(u_gridSize * 20.0, u_gridSize * 30.0, dist);

    fragColor = u_gridColor * gridAlpha * fade;
}
```

- Fullscreen quad in EditorOverlays pass, behind ImGuizmo
- Only rendered when `showGrid_ && paused_`
- Sub-grid at `gridSize / 4` with lower alpha when zoom > 2x
- Old `drawSceneGrid()` SpriteBatch method removed

## New Files

```
engine/render/render_graph.h         -- RenderGraph, RenderPass, RenderPassContext
engine/render/render_graph.cpp       -- RenderGraph implementation, FBO pool
engine/render/lighting.h             -- PointLight, LightingConfig
engine/render/lighting.cpp           -- Lighting pass execution (light map render + composite)
engine/render/post_process.h         -- PostProcessConfig
engine/render/post_process.cpp       -- Bloom pipeline + final composite
engine/particle/particle.h           -- Particle struct, EmitterConfig
engine/particle/particle_emitter.h   -- ParticleEmitter class
engine/particle/particle_emitter.cpp -- Emitter update, spawn, vertex build
engine/particle/particle_system.h    -- ParticleSystem (ECS system)
engine/particle/particle_system.cpp  -- System update + particle render pass
assets/shaders/fullscreen_quad.vert   -- Shared fullscreen quad vertex shader (source below)
assets/shaders/light.frag            -- Point light radial falloff
assets/shaders/bloom_extract.frag    -- Brightness threshold
assets/shaders/blur.frag             -- Separable Gaussian blur
assets/shaders/postprocess.frag      -- Bloom composite + vignette + color grading
assets/shaders/grid.frag             -- Infinite grid with LOD fade
tests/test_render_graph.cpp          -- RenderGraph pass ordering, enable/disable
tests/test_particle.cpp              -- ParticleEmitter spawn, update, lifetime
```

## Modified Files

```
engine/render/framebuffer.h/cpp      -- Optional depth/stencil attachment (rbo_ member, hasDepthStencil_ flag)
engine/render/sprite_batch.h/cpp     -- Blend mode switching API (see below)
engine/render/gl_loader.h/cpp        -- Add renderbuffer functions: glGenRenderbuffers, glDeleteRenderbuffers, glBindRenderbuffer, glRenderbufferStorage, glFramebufferRenderbuffer
engine/core/types.h                  -- Add Mat4::inverse() for grid shader inverse VP uniform
engine/app.h/cpp                     -- RenderGraph member, replace direct render with graph execute
engine/editor/editor.h/cpp           -- ImGuizmo integration, selection outlines, grid shader, post-process config panel
game/game_app.cpp                    -- Register game render passes with graph
game/register_components.h           -- Register ParticleEmitterComponent, PointLightComponent
CMakeLists.txt                       -- ImGuizmo dependency, new sources
```

## SpriteBatch Blend Mode API

The particle pass needs additive blending while the sprite pass uses standard alpha. SpriteBatch gains a blend mode method:

```cpp
enum class BlendMode { Alpha, Additive };

// Flushes current batch, changes GL blend state
void SpriteBatch::setBlendMode(BlendMode mode);
```

A blend mode change forces a batch flush (same as a texture change). The lighting pass manages its own blend state directly via `glBlendFunc` since it uses a separate shader, not SpriteBatch.

## EditorTool Changes

`EditorTool::Resize` (E key) is renamed to `EditorTool::Scale` to match ImGuizmo's scale operation. The existing resize handle drawing code in `editor.cpp` is removed — ImGuizmo handles visual feedback. `EditorTool::Rotate` (R key) is added.

## CMake Changes

- Add ImGuizmo via FetchContent (single header + cpp)
- Add `engine/particle/` sources (auto-discovered by GLOB_RECURSE)
- New shader files copied to build directory via existing asset copy rule

## Test Coverage

- `RenderGraph`: pass add/remove, enable/disable, execution order, FBO pool reuse
- `ParticleEmitter`: spawn rate, burst mode, lifetime expiry, velocity/gravity integration, vertex count matches active particles, isFinished for bursts
- `PostProcessConfig`: default values, serialization round-trip
- Lighting/grid/selection outlines: manual verification (GL-dependent)

## Component Registration

New components to register in `game/register_components.h`:
- `ParticleEmitterComponent`
- `PointLightComponent`

Both need FATE_REFLECT macros and serializers for scene save/load.
