#pragma once
#include "engine/core/types.h"
#include "engine/render/shader.h"
#include "engine/render/texture.h"
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

    // Draw a quad with a raw GL texture ID (for font atlas, custom textures)
    void drawTexturedQuad(unsigned int glTexId, const SpriteDrawParams& params, float renderType = 0.0f);
    // Draw a quad with a gfx::TextureHandle (preferred — works with CommandList path)
    void drawTexturedQuad(gfx::TextureHandle gfxTex, unsigned int glTexId, const SpriteDrawParams& params, float renderType = 0.0f);

    int drawCallCount() const { return drawCallCount_; }
    int spriteCount() const { return spriteCount_; }

    void setBlendMode(BlendMode mode);

    // Palette swap support (RenderType 5)
    void setPalette(const Color* colors, int count);
    void clearPalette();
    void drawPaletteSwapped(std::shared_ptr<Texture>& texture,
                            const SpriteDrawParams& params,
                            const Color* palette, int paletteSize);

    // Set the CommandList for gfx-abstracted drawing; nullptr = direct GL fallback
    void setCommandList(gfx::CommandList* cmd) { cmdList_ = cmd; }

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

    Mat4 viewProjection_;
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
    void createWhiteTexture();

    // Get the pipeline handle for the current blend mode
    gfx::PipelineHandle currentPipeline() const;

    // Build the SpriteVertex layout descriptor
    static gfx::VertexLayout spriteVertexLayout();
};

} // namespace fate
