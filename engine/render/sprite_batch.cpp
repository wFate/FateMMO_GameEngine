#include "engine/render/sprite_batch.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fate {

// Embedded shader sources (no external file needed for fallback)
static const char* SPRITE_VERT_SRC = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;
layout (location = 3) in float aRenderType;

out vec2 v_uv;
out vec4 v_color;
out float v_renderType;

uniform mat4 uViewProjection;

void main() {
    gl_Position = uViewProjection * vec4(aPos, 0.0, 1.0);
    v_uv = aTexCoord;
    v_color = aColor;
    v_renderType = aRenderType;
}
)";

static const char* SPRITE_FRAG_SRC = R"(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
in float v_renderType;

out vec4 fragColor;

uniform sampler2D uTexture;
uniform float u_pxRange;
uniform vec2 u_atlasSize;
uniform vec2 u_shadowOffset;
uniform vec4 u_palette[16];
uniform float u_paletteSize;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // RenderType 5: Palette swap -- grayscale index maps to palette color
    if (v_renderType > 4.5 && v_renderType < 5.5) {
        vec4 texel = texture(uTexture, v_uv);
        if (texel.a < 0.01) discard;
        int index = int(texel.r * 15.0 + 0.5); // 16-color palette (0-15)
        index = clamp(index, 0, int(u_paletteSize) - 1);
        fragColor = u_palette[index];
        fragColor.a *= texel.a * v_color.a;
        return;
    }

    if (v_renderType < 0.5) {
        fragColor = texture(uTexture, v_uv) * v_color;
        if (fragColor.a < 0.01) discard;
    } else {
        vec4 sdf = texture(uTexture, v_uv);
        float sd = median(sdf.r, sdf.g, sdf.b);
        vec2 unitRange = vec2(u_pxRange) / u_atlasSize;
        vec2 screenTexSize = vec2(1.0) / fwidth(v_uv);
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float screenPxDist = screenPxRange * (sd - 0.5);
        float opacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

        if (v_renderType < 1.5) {
            fragColor = vec4(v_color.rgb, v_color.a * opacity);
        } else if (v_renderType < 2.5) {
            float outlineDist = screenPxRange * (sd - 0.35);
            float outlineOp = clamp(outlineDist + 0.5, 0.0, 1.0);
            vec4 outline = vec4(0.0, 0.0, 0.0, v_color.a * outlineOp);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(outline, fill, opacity);
        } else if (v_renderType < 3.5) {
            float glowOp = smoothstep(0.0, 0.5, sdf.a);
            vec4 glow = vec4(v_color.rgb, v_color.a * glowOp * 0.6);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(glow, fill, opacity);
        } else {
            vec2 shadowUV = v_uv - u_shadowOffset;
            vec4 shadowSdf = texture(uTexture, shadowUV);
            float shadowSd = median(shadowSdf.r, shadowSdf.g, shadowSdf.b);
            float shadowDist = screenPxRange * (shadowSd - 0.5);
            float shadowOp = clamp(shadowDist + 0.5, 0.0, 1.0) * 0.5;
            vec4 shadow = vec4(0.0, 0.0, 0.0, v_color.a * shadowOp);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(shadow, fill, opacity);
        }

        if (fragColor.a < 0.01) discard;
    }
}
)";

SpriteBatch::SpriteBatch() {}

SpriteBatch::~SpriteBatch() {
    shutdown();
}

gfx::VertexLayout SpriteBatch::spriteVertexLayout() {
    gfx::VertexLayout layout;
    layout.stride = sizeof(SpriteVertex);
    layout.attributes = {
        {0, 2, offsetof(SpriteVertex, x),          false}, // aPos
        {1, 2, offsetof(SpriteVertex, u),          false}, // aTexCoord
        {2, 4, offsetof(SpriteVertex, r),          false}, // aColor
        {3, 1, offsetof(SpriteVertex, renderType), false}, // aRenderType
    };
    return layout;
}

gfx::PipelineHandle SpriteBatch::currentPipeline() const {
    switch (blendMode_) {
        case BlendMode::None:           return pipelineNone_;
        case BlendMode::Alpha:          return pipelineAlpha_;
        case BlendMode::Additive:       return pipelineAdditive_;
        case BlendMode::Multiplicative: return pipelineMultiplicative_;
    }
    return pipelineAlpha_;
}

bool SpriteBatch::init() {
    // Try loading shader from files first, fall back to embedded
    if (!shader_.loadFromFile("assets/shaders/sprite.vert", "assets/shaders/sprite.frag")) {
        LOG_WARN("SpriteBatch", "External shaders not found, using embedded shaders");
        if (!shader_.loadFromSource(SPRITE_VERT_SRC, SPRITE_FRAG_SRC)) {
            LOG_ERROR("SpriteBatch", "Failed to compile embedded shaders");
            return false;
        }
    }

    auto& device = gfx::Device::instance();

    // Create VBO (dynamic, updated each frame)
    vboHandle_ = device.createBuffer(gfx::BufferType::Vertex, gfx::BufferUsage::Dynamic,
                                     MAX_VERTICES * sizeof(SpriteVertex), nullptr);

    // Create EBO (static index pattern for quads)
    std::vector<unsigned int> indices(MAX_INDICES);
    for (int i = 0; i < MAX_SPRITES; i++) {
        int vi = i * 4;
        int ii = i * 6;
        indices[ii + 0] = vi + 0;
        indices[ii + 1] = vi + 1;
        indices[ii + 2] = vi + 2;
        indices[ii + 3] = vi + 2;
        indices[ii + 4] = vi + 3;
        indices[ii + 5] = vi + 0;
    }
    eboHandle_ = device.createBuffer(gfx::BufferType::Index, gfx::BufferUsage::Static,
                                     indices.size() * sizeof(unsigned int), indices.data());

    // Resolve raw GL names for fallback path
    vbo_ = device.resolveGLBuffer(vboHandle_);
    ebo_ = device.resolveGLBuffer(eboHandle_);

    // Create a pipeline for each blend mode
    auto layout = spriteVertexLayout();
    auto shaderH = shader_.gfxHandle();

    auto makePipeline = [&](gfx::BlendMode bm) {
        gfx::PipelineDesc desc;
        desc.shader = shaderH;
        desc.vertexLayout = layout;
        desc.blendMode = bm;
        desc.depthTest = false;
        desc.depthWrite = false;
        return device.createPipeline(desc);
    };

    pipelineAlpha_          = makePipeline(gfx::BlendMode::Alpha);
    pipelineAdditive_       = makePipeline(gfx::BlendMode::Additive);
    pipelineMultiplicative_ = makePipeline(gfx::BlendMode::Multiplicative);
    pipelineNone_           = makePipeline(gfx::BlendMode::None);

    // Resolve the VAO from the alpha pipeline for the GL fallback path
    // (all pipelines share the same vertex layout, so any VAO will do)
    vao_ = device.resolveGLPipelineVAO(pipelineAlpha_);

    createWhiteTexture();

    entries_.reserve(1000);
    vertices_.reserve(4000);

    LOG_INFO("SpriteBatch", "Initialized (max %d sprites per batch)", MAX_SPRITES);
    return true;
}

void SpriteBatch::shutdown() {
    auto& device = gfx::Device::instance();

    if (pipelineAlpha_.valid())          { device.destroy(pipelineAlpha_);          pipelineAlpha_ = {}; }
    if (pipelineAdditive_.valid())       { device.destroy(pipelineAdditive_);       pipelineAdditive_ = {}; }
    if (pipelineMultiplicative_.valid()) { device.destroy(pipelineMultiplicative_); pipelineMultiplicative_ = {}; }
    if (pipelineNone_.valid())           { device.destroy(pipelineNone_);           pipelineNone_ = {}; }
    if (vboHandle_.valid()) { device.destroy(vboHandle_); vboHandle_ = {}; }
    if (eboHandle_.valid()) { device.destroy(eboHandle_); eboHandle_ = {}; }
    if (whiteTexHandle_.valid()) { device.destroy(whiteTexHandle_); whiteTexHandle_ = {}; }

    vao_ = 0;
    vbo_ = 0;
    ebo_ = 0;
    whiteTexture_ = 0;
}

void SpriteBatch::begin(const Mat4& viewProjection) {
    viewProjection_ = viewProjection;
    entries_.clear();
    drawCallCount_ = 0;
    spriteCount_ = 0;
    drawing_ = true;
    sortDirty_ = true;
}

void SpriteBatch::draw(const std::shared_ptr<Texture>& texture, const SpriteDrawParams& params) {
    if (!drawing_) return;
    entries_.push_back({texture, 0, params});
}

void SpriteBatch::drawRect(const Vec2& position, const Vec2& size, const Color& color, float depth) {
    SpriteDrawParams params;
    params.position = position;
    params.size = size;
    params.color = color;
    params.depth = depth;
    params.sourceRect = {0, 0, 1, 1};
    entries_.push_back({nullptr, 0, params});
}

void SpriteBatch::drawTexturedQuad(unsigned int glTexId, const SpriteDrawParams& params, float renderType) {
    if (!drawing_) return;
    entries_.push_back({nullptr, glTexId, params, renderType});
}

void SpriteBatch::drawTexturedQuad(gfx::TextureHandle gfxTex, unsigned int glTexId, const SpriteDrawParams& params, float renderType) {
    if (!drawing_) return;
    entries_.push_back({nullptr, glTexId, params, renderType, gfxTex});
}

void SpriteBatch::end() {
    if (!drawing_) return;
    drawing_ = false;

    if (entries_.empty()) return;

    // Compute FNV-1a hash over sort keys (texture id + depth) to detect order changes
    uint32_t hash = 2166136261u;
    for (const auto& entry : entries_) {
        uint32_t texId = entry.texture ? entry.texture->id() : entry.rawTexId;
        uint32_t depthBits;
        std::memcpy(&depthBits, &entry.params.depth, sizeof(depthBits));
        hash ^= texId;     hash *= 16777619u;
        hash ^= depthBits; hash *= 16777619u;
    }

    if (hash == prevSortHash_ && entries_.size() == prevEntryCount_ && !sortDirty_) {
        // Skip sort -- same order as last frame
    } else {
        // Sort by depth (back to front), then by texture to minimize state changes
        auto texKey = [](const BatchEntry& e) -> uintptr_t {
            if (e.texture) return (uintptr_t)e.texture.get();
            if (e.rawTexId) return (uintptr_t)e.rawTexId;
            return 0;
        };

        std::sort(entries_.begin(), entries_.end(),
            [&texKey](const BatchEntry& a, const BatchEntry& b) {
                if (a.params.depth != b.params.depth)
                    return a.params.depth < b.params.depth;
                return texKey(a) < texKey(b);
            });

        prevSortHash_ = hash;
        prevEntryCount_ = entries_.size();
        sortDirty_ = false;
    }

    flush();
}

void SpriteBatch::flush() {
    // ------------------------------------------------------------------
    // CommandList path (gfx-abstracted)
    // ------------------------------------------------------------------
    if (cmdList_) {
        auto& device = gfx::Device::instance();

        cmdList_->bindPipeline(currentPipeline());
        cmdList_->setUniform("uViewProjection", viewProjection_);
        cmdList_->setUniform("uTexture", 0);
        cmdList_->setUniform("u_pxRange", 4.0f);
        cmdList_->setUniform("u_atlasSize", Vec2{512.0f, 512.0f});
        cmdList_->setUniform("u_shadowOffset", Vec2{0.002f, 0.002f});

        cmdList_->bindVertexBuffer(vboHandle_);
        cmdList_->bindIndexBuffer(eboHandle_);

        uintptr_t currentTexKey = ~(uintptr_t)0;
        vertices_.clear();

        auto flushBatch = [&]() {
            if (vertices_.empty()) return;

            device.updateBuffer(vboHandle_, vertices_.data(),
                                vertices_.size() * sizeof(SpriteVertex));

            int quadCount = (int)vertices_.size() / 4;
            cmdList_->drawIndexed(gfx::PrimitiveType::Triangles, quadCount * 6);
            drawCallCount_++;
            vertices_.clear();
        };

        for (auto& entry : entries_) {
            // Determine texture key for batching
            uintptr_t texKey;
            if (entry.texture)       texKey = (uintptr_t)entry.texture.get();
            else if (entry.rawTexId) texKey = (uintptr_t)entry.rawTexId;
            else                     texKey = 0;

            // If texture changed, flush current batch and bind new texture
            if (texKey != currentTexKey) {
                flushBatch();
                currentTexKey = texKey;

                if (entry.texture) {
                    cmdList_->bindTexture(0, entry.texture->gfxHandle());
                } else if (entry.gfxTexHandle.valid()) {
                    cmdList_->bindTexture(0, entry.gfxTexHandle);
                } else if (entry.rawTexId) {
                    // Raw GL texture ID -- fall back to direct GL bind
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, entry.rawTexId);
                } else {
                    cmdList_->bindTexture(0, whiteTexHandle_);
                }
            }

            // If batch is full, flush
            if ((int)vertices_.size() >= MAX_VERTICES) {
                flushBatch();
            }

            // Build quad vertices
            const auto& p = entry.params;
            float hw = p.size.x * 0.5f;
            float hh = p.size.y * 0.5f;

            float u0 = p.sourceRect.x;
            float v0 = p.sourceRect.y;
            float u1 = p.sourceRect.x + p.sourceRect.w;
            float v1 = p.sourceRect.y + p.sourceRect.h;

            if (p.flipX) std::swap(u0, u1);
            if (p.flipY) std::swap(v0, v1);

            float x0 = -hw, y0 = -hh;
            float x1 =  hw, y1 = -hh;
            float x2 =  hw, y2 =  hh;
            float x3 = -hw, y3 =  hh;

            if (std::abs(p.rotation) > 0.001f) {
                float c = std::cos(p.rotation);
                float s = std::sin(p.rotation);
                auto rot = [c, s](float& px, float& py) {
                    float nx = px * c - py * s;
                    float ny = px * s + py * c;
                    px = nx; py = ny;
                };
                rot(x0, y0); rot(x1, y1); rot(x2, y2); rot(x3, y3);
            }

            float cx = p.position.x;
            float cy = p.position.y;

            vertices_.push_back({cx + x0, cy + y0, u0, v0, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});
            vertices_.push_back({cx + x1, cy + y1, u1, v0, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});
            vertices_.push_back({cx + x2, cy + y2, u1, v1, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});
            vertices_.push_back({cx + x3, cy + y3, u0, v1, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});

            spriteCount_++;
        }

        flushBatch();
        return;
    }

    // ------------------------------------------------------------------
    // Direct GL fallback path (editor/ImGui or when no CommandList is set)
    // ------------------------------------------------------------------
    shader_.bind();
    shader_.setMat4("uViewProjection", viewProjection_);
    shader_.setInt("uTexture", 0);
    shader_.setFloat("u_pxRange", 4.0f);
    shader_.setVec2("u_atlasSize", {512.0f, 512.0f});
    shader_.setVec2("u_shadowOffset", {0.002f, 0.002f});

    glBindVertexArray(vao_);

    uintptr_t currentTexKey = ~(uintptr_t)0; // invalid initial key
    vertices_.clear();

    auto flushBatch = [&]() {
        if (vertices_.empty()) return;

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        vertices_.size() * sizeof(SpriteVertex), vertices_.data());

        int quadCount = (int)vertices_.size() / 4;
        glDrawElements(GL_TRIANGLES, quadCount * 6, GL_UNSIGNED_INT, nullptr);
        drawCallCount_++;
        vertices_.clear();
    };

    for (auto& entry : entries_) {
        // Determine texture key for batching
        uintptr_t texKey;
        if (entry.texture)       texKey = (uintptr_t)entry.texture.get();
        else if (entry.rawTexId) texKey = (uintptr_t)entry.rawTexId;
        else                     texKey = 0;

        // If texture changed, flush current batch and bind new texture
        if (texKey != currentTexKey) {
            flushBatch();
            currentTexKey = texKey;

            if (entry.texture) {
                entry.texture->bind(0);
            } else if (entry.rawTexId) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, entry.rawTexId);
            } else {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, whiteTexture_);
            }
        }

        // If batch is full, flush
        if ((int)vertices_.size() >= MAX_VERTICES) {
            flushBatch();
        }

        // Build quad vertices
        const auto& p = entry.params;
        float hw = p.size.x * 0.5f;
        float hh = p.size.y * 0.5f;

        // UV coordinates
        float u0 = p.sourceRect.x;
        float v0 = p.sourceRect.y;
        float u1 = p.sourceRect.x + p.sourceRect.w;
        float v1 = p.sourceRect.y + p.sourceRect.h;

        if (p.flipX) std::swap(u0, u1);
        if (p.flipY) std::swap(v0, v1);

        // Corner positions (before rotation)
        float x0 = -hw, y0 = -hh; // bottom-left
        float x1 =  hw, y1 = -hh; // bottom-right
        float x2 =  hw, y2 =  hh; // top-right
        float x3 = -hw, y3 =  hh; // top-left

        // Apply rotation if needed
        if (std::abs(p.rotation) > 0.001f) {
            float c = std::cos(p.rotation);
            float s = std::sin(p.rotation);
            auto rot = [c, s](float& px, float& py) {
                float nx = px * c - py * s;
                float ny = px * s + py * c;
                px = nx; py = ny;
            };
            rot(x0, y0); rot(x1, y1); rot(x2, y2); rot(x3, y3);
        }

        float cx = p.position.x;
        float cy = p.position.y;

        vertices_.push_back({cx + x0, cy + y0, u0, v0, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});
        vertices_.push_back({cx + x1, cy + y1, u1, v0, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});
        vertices_.push_back({cx + x2, cy + y2, u1, v1, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});
        vertices_.push_back({cx + x3, cy + y3, u0, v1, p.color.r, p.color.g, p.color.b, p.color.a, entry.renderType});

        spriteCount_++;
    }

    // Flush remaining
    flushBatch();

    glBindVertexArray(0);
    shader_.unbind();
}

void SpriteBatch::setBlendMode(BlendMode mode) {
    if (mode == blendMode_) return;
    flush(); // must flush before changing blend state
    blendMode_ = mode;

    if (cmdList_) {
        // CommandList path: just switch pipeline on next flush
        // (pipeline is selected in flush() via currentPipeline())
        return;
    }

    // Direct GL fallback
    switch (mode) {
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
}

void SpriteBatch::setPalette(const Color* colors, int count) {
    count = std::min(count, 16);
    shader_.bind();
    shader_.setFloat("u_paletteSize", static_cast<float>(count));
    for (int i = 0; i < count; ++i) {
        std::string name = "u_palette[" + std::to_string(i) + "]";
        shader_.setVec4(name, colors[i].r, colors[i].g, colors[i].b, colors[i].a);
    }
}

void SpriteBatch::clearPalette() {
    shader_.bind();
    shader_.setFloat("u_paletteSize", 0.0f);
}

void SpriteBatch::drawPaletteSwapped(std::shared_ptr<Texture>& texture,
                                      const SpriteDrawParams& params,
                                      const Color* palette, int paletteSize) {
    setPalette(palette, paletteSize);
    drawTexturedQuad(texture->gfxHandle(), texture->id(), params, 5.0f);
    clearPalette();
}

void SpriteBatch::createWhiteTexture() {
    unsigned char white[] = {255, 255, 255, 255};
    auto& device = gfx::Device::instance();
    whiteTexHandle_ = device.createTexture(1, 1, gfx::TextureFormat::RGBA8, white);
    whiteTexture_ = device.resolveGLTexture(whiteTexHandle_);
}

} // namespace fate
