#pragma once
#include "engine/core/types.h"
#include "engine/render/shader.h"
#include "engine/render/texture.h"
#include "engine/render/nine_slice.h"
#include "engine/render/gfx/types.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/command_list.h"
#include <vector>
#include <memory>

namespace fate {

using BlendMode = gfx::BlendMode;

// A single sprite draw call's data
struct SpriteVertex {
    float x, y;       // position
    float u, v;       // texcoord
    float r, g, b, a; // color tint
    float renderType; // 0.0=sprite, 1.0=normal text, 2.0=outlined, 3.0=glow, 4.0=shadow
};

// Parameters for drawing a sprite
struct SpriteDrawParams {
    Vec2 position;                          // world position (center)
    Vec2 size = {32.0f, 32.0f};            // pixel size
    Rect sourceRect = {0, 0, 1, 1};        // UV rect (0-1 normalized)
    Color color = Color::white();
    float rotation = 0.0f;                  // radians
    float depth = 0.0f;                     // z-order (higher = drawn later)
    bool flipX = false;
    bool flipY = false;
};

struct RoundedRectParams {
    Vec2  position;                          // center
    Vec2  size;                              // width, height
    float cornerRadius     = 0.0f;          // 0 = sharp corners
    Color fillTop          = Color::white();
    Color fillBottom       = Color::white(); // set equal for flat fill
    float borderWidth      = 0.0f;
    Color borderColor      = Color::clear();
    Vec2  shadowOffset     = {0.0f, 0.0f};
    float shadowBlur       = 0.0f;
    Color shadowColor      = {0.0f, 0.0f, 0.0f, 0.0f};
    float depth            = 0.0f;
};

// Batched 2D sprite renderer
// Collects sprites, sorts by texture+depth, renders in minimal draw calls
class SpriteBatch {
public:
    SpriteBatch();
    ~SpriteBatch();

    bool init();
    void shutdown();

    // Call begin() before drawing, end() to flush
    void begin(const Mat4& viewProjection);
    void draw(const std::shared_ptr<Texture>& texture, const SpriteDrawParams& params);
    void end();

    // Draw a solid colored rectangle (no texture)
    void drawRect(const Vec2& position, const Vec2& size, const Color& color, float depth = 0.0f);

    // Draw a 9-slice panel: corners fixed, edges stretch, center fills
    // NOTE: Phase 1 assumes texture occupies full UV space (0-1).
    void drawNineSlice(const std::shared_ptr<Texture>& texture,
                       const Rect& dest,
                       const NineSlice& sliceInsets,
                       const Color& tint = Color::white(),
                       float depth = 0.0f);

    // Draw a filled circle using N quad segments (uses white texture)
    void drawCircle(const Vec2& center, float radius, const Color& color,
                    float depth = 0.0f, int segments = 24);

    // Draw a circle outline (ring) with given thickness
    void drawRing(const Vec2& center, float radius, float thickness, const Color& color,
                  float depth = 0.0f, int segments = 24);

    // Draw a filled ellipse with separate X and Y radii (perspective-squashed circle)
    void drawEllipse(const Vec2& center, float radiusX, float radiusY, const Color& color,
                     float depth = 0.0f, int segments = 24);

    // Draw an ellipse outline (ring) with separate X and Y radii
    void drawEllipseRing(const Vec2& center, float radiusX, float radiusY, float thickness,
                         const Color& color, float depth = 0.0f, int segments = 24);

    // Draw a filled arc (pie slice) from startAngle to endAngle (radians, 0=right, CCW)
    void drawArc(const Vec2& center, float radius, float startAngle, float endAngle,
                 const Color& color, float depth = 0.0f, int segments = 24);

    // Draw an SDF rounded rectangle with optional gradient, border, and shadow
    void drawRoundedRect(const RoundedRectParams& params);

#ifndef FATEMMO_METAL
    // Draw a quad with a raw GL texture ID (for font atlas, custom textures)
    void drawTexturedQuad(unsigned int glTexId, const SpriteDrawParams& params, float renderType = 0.0f);
#endif
    // Draw a quad with a gfx::TextureHandle (preferred — works with CommandList path)
    // Under Metal the glTexId parameter is ignored
    void drawTexturedQuad(gfx::TextureHandle gfxTex, unsigned int glTexId, const SpriteDrawParams& params, float renderType = 0.0f);

    int drawCallCount() const { return drawCallCount_; }
    int spriteCount() const { return spriteCount_; }
    const Mat4& viewProjection() const { return viewProjection_; }

    void setBlendMode(BlendMode mode);

    // Palette swap support (RenderType 5)
    void setPalette(const Color* colors, int count);
    void clearPalette();
    void drawPaletteSwapped(std::shared_ptr<Texture>& texture,
                            const SpriteDrawParams& params,
                            const Color* palette, int paletteSize);

    // Scissor clipping (nestable stack — intersection of all active rects)
    // Coordinates are screen-space (top-left origin, pixels).
    // Calling push/pop forces a mid-frame flush of pending sprites.
    void pushScissorRect(const Rect& rect);
    void popScissorRect();

    // Set the CommandList for gfx-abstracted drawing; nullptr = direct GL fallback
    void setCommandList(gfx::CommandList* cmd) { cmdList_ = cmd; }

#ifdef FATEMMO_METAL
    void setMetalEncoder(void* encoder);
#endif

private:
    static constexpr int MAX_SPRITES = 10000;
    static constexpr int MAX_VERTICES = MAX_SPRITES * 4;
    static constexpr int MAX_INDICES = MAX_SPRITES * 6;

    struct BatchEntry {
        std::shared_ptr<Texture> texture;
        unsigned int rawTexId = 0; // for font atlas / raw GL textures
        SpriteDrawParams params;
        float renderType = 0.0f;
        gfx::TextureHandle gfxTexHandle{}; // device-managed handle (for CommandList path)
    };

    Shader shader_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    unsigned int whiteTexture_ = 0;  // 1x1 white pixel for solid rects

    // gfx handles
    gfx::BufferHandle vboHandle_{};
    gfx::BufferHandle eboHandle_{};
    gfx::TextureHandle whiteTexHandle_{};
    gfx::PipelineHandle pipelineAlpha_{};
    gfx::PipelineHandle pipelineAdditive_{};
    gfx::PipelineHandle pipelineMultiplicative_{};
    gfx::PipelineHandle pipelineNone_{};
    gfx::CommandList* cmdList_ = nullptr;

#ifdef FATEMMO_METAL
    void* metalEncoder_ = nullptr;
#endif

    Mat4 viewProjection_;
    int cachedViewportHeight_ = 0;  // cached to avoid glGetIntegerv stalls in scissor
    std::vector<BatchEntry> entries_;
    std::vector<SpriteVertex> vertices_;

    int drawCallCount_ = 0;
    int spriteCount_ = 0;
    bool drawing_ = false;

    // Dirty flag: skip sort when draw order is unchanged from last frame
    bool sortDirty_ = true;
    uint32_t prevSortHash_ = 0;
    size_t prevEntryCount_ = 0;
    BlendMode blendMode_ = BlendMode::Alpha;

    void flush();
    void flushPending();           // sort + flush + clear entries mid-frame
    void applyScissorState();      // set GL/Metal scissor from stack
    void createWhiteTexture();

    std::vector<Rect> scissorStack_;

    bool hasRoundedRect_ = false;
    RoundedRectParams pendingRoundedRect_;

    // Get the pipeline handle for the current blend mode
    gfx::PipelineHandle currentPipeline() const;

    // Build the SpriteVertex layout descriptor
    static gfx::VertexLayout spriteVertexLayout();
};

} // namespace fate
