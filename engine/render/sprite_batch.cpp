#include "engine/render/sprite_batch.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#ifdef FATEMMO_METAL
#import <Metal/Metal.h>
#endif
#include "engine/core/logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fate {

// Embedded shader sources (no external file needed for fallback)
// Note: #version preamble injected by Shader::loadFromSource() at compile time
static const char* SPRITE_VERT_SRC = R"(
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

// Configurable text effect uniforms (mirror assets/shaders/sprite.frag).
uniform vec4 u_outlineColor;
uniform float u_outlineThickness;
uniform vec2 u_textShadowOffset;
uniform vec4 u_textShadowColor;
uniform vec4 u_textGlowColor;
uniform float u_textGlowIntensity;

// Rounded rect uniforms (renderType 7)
uniform vec2 u_rectSize;
uniform float u_cornerRadius;
uniform float u_rrBorderWidth;
uniform vec4 u_rrBorderColor;
uniform vec4 u_gradientTop;
uniform vec4 u_gradientBottom;
uniform vec2 u_rrShadowOffset;
uniform float u_rrShadowBlur;
uniform vec4 u_rrShadowColor;

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

    // RenderType 7: SDF rounded rectangle
    if (v_renderType > 6.5 && v_renderType < 7.5) {
        vec2 p = (v_uv - 0.5) * u_rectSize;
        vec2 q = abs(p) - u_rectSize * 0.5 + u_cornerRadius;
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_cornerRadius;

        vec2 pShadow = p - u_rrShadowOffset;
        vec2 qShadow = abs(pShadow) - u_rectSize * 0.5 + u_cornerRadius;
        float shadowDist = length(max(qShadow, 0.0)) + min(max(qShadow.x, qShadow.y), 0.0) - u_cornerRadius;
        float shadowAlpha = 1.0 - smoothstep(-u_rrShadowBlur, u_rrShadowBlur, shadowDist);
        vec4 shadow = vec4(u_rrShadowColor.rgb, u_rrShadowColor.a * shadowAlpha);

        vec4 fill = mix(u_gradientTop, u_gradientBottom, v_uv.y);
        float fillAlpha = 1.0 - smoothstep(-1.0, 0.0, dist);
        float borderOuter = 1.0 - smoothstep(-1.0, 0.0, dist);
        float borderInner = 1.0 - smoothstep(-1.0, 0.0, dist + u_rrBorderWidth);
        float borderAlpha = borderOuter - borderInner;
        vec4 border = vec4(u_rrBorderColor.rgb, u_rrBorderColor.a * borderAlpha);
        fill.a *= (fillAlpha - borderAlpha);

        fragColor = shadow;
        fragColor = mix(fragColor, fill, fill.a);
        fragColor = mix(fragColor, border, border.a);
        fragColor.a *= v_color.a;

        if (fragColor.a < 0.01) discard;
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
            float outlineDist = screenPxRange * (sd - u_outlineThickness);
            float outlineOp = clamp(outlineDist + 0.5, 0.0, 1.0);
            vec4 outline = vec4(u_outlineColor.rgb, v_color.a * outlineOp * u_outlineColor.a);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(outline, fill, opacity);
        } else if (v_renderType < 3.5) {
            float glowOp = smoothstep(0.0, 0.5, sdf.a);
            vec4 glow = vec4(u_textGlowColor.rgb, v_color.a * glowOp * u_textGlowIntensity);
            vec4 fill = vec4(v_color.rgb, v_color.a * opacity);
            fragColor = mix(glow, fill, opacity);
        } else {
            vec2 shadowUV = v_uv - u_textShadowOffset;
            vec4 shadowSdf = texture(uTexture, shadowUV);
            float shadowSd = median(shadowSdf.r, shadowSdf.g, shadowSdf.b);
            float shadowDist = screenPxRange * (shadowSd - 0.5);
            float shadowOp = clamp(shadowDist + 0.5, 0.0, 1.0) * u_textShadowColor.a;
            vec4 shadow = vec4(u_textShadowColor.rgb, v_color.a * shadowOp);
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

    // Configure vertex attributes on the VAO now so the direct-GL fallback
    // path works even if the CommandList path never runs first (e.g. the
    // open-source demo build where render-graph passes return early).
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    for (const auto& attr : layout.attributes) {
        glEnableVertexAttribArray(attr.location);
        glVertexAttribPointer(
            attr.location,
            attr.components,
            GL_FLOAT,
            attr.normalized ? GL_TRUE : GL_FALSE,
            static_cast<GLsizei>(layout.stride),
            reinterpret_cast<const void*>(attr.offset)
        );
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBindVertexArray(0);

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
#ifndef FATEMMO_METAL
    // Cache viewport height once per frame to avoid glGetIntegerv stalls in scissor
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    cachedViewportHeight_ = vp[3];
#endif
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

void SpriteBatch::drawRoundedRect(const RoundedRectParams& params) {
    if (!drawing_) return;

    // Flush any pending normal entries (without rounded rect uniforms)
    if (!entries_.empty()) {
        hasRoundedRect_ = false;
        flushPending();
    }

    // Store rounded rect params for uniform setup during flush
    hasRoundedRect_ = true;
    pendingRoundedRect_ = params;

    // Expand quad to accommodate shadow bleed
    float expandX = params.shadowBlur + std::abs(params.shadowOffset.x);
    float expandY = params.shadowBlur + std::abs(params.shadowOffset.y);
    Vec2 expandedSize = {params.size.x + expandX * 2.0f, params.size.y + expandY * 2.0f};

    // Create a single BatchEntry with renderType=7 (SDF rounded rect)
    SpriteDrawParams sp;
    sp.position = params.position;
    sp.size = expandedSize;
    sp.color = Color::white();
    sp.depth = params.depth;
    sp.sourceRect = {0, 0, 1, 1};
    entries_.push_back({nullptr, 0, sp, 7.0f});

    // Flush immediately (this sets rounded rect uniforms and draws the quad)
    flushPending();

    // Reset so subsequent flushes don't set rounded rect uniforms
    hasRoundedRect_ = false;
}

#ifndef FATEMMO_METAL
void SpriteBatch::drawTexturedQuad(unsigned int glTexId, const SpriteDrawParams& params, float renderType) {
    if (!drawing_) return;
    entries_.push_back({nullptr, glTexId, params, renderType});
}
#endif

void SpriteBatch::drawTexturedQuad(gfx::TextureHandle gfxTex, unsigned int glTexId, const SpriteDrawParams& params, float renderType) {
    if (!drawing_) return;
    entries_.push_back({nullptr, glTexId, params, renderType, gfxTex});
}

#ifdef FATEMMO_METAL
void SpriteBatch::setMetalEncoder(void* encoder) { metalEncoder_ = encoder; }
#endif

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

// ---------------------------------------------------------------------------
// Scissor clipping
// ---------------------------------------------------------------------------

void SpriteBatch::flushPending() {
    if (entries_.empty()) return;

    // Same sort logic as end()
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

    flush();
    entries_.clear();
    sortDirty_ = true;
}

void SpriteBatch::applyScissorState() {
    if (scissorStack_.empty()) {
#ifndef FATEMMO_METAL
        glDisable(GL_SCISSOR_TEST);
#else
        // Reset to full viewport (Metal has no "disable scissor" — set to max)
        if (metalEncoder_) {
            id<MTLRenderCommandEncoder> enc =
                (__bridge id<MTLRenderCommandEncoder>)metalEncoder_;
            MTLScissorRect full = {0, 0, 16384, 16384};
            [enc setScissorRect:full];
        }
#endif
        return;
    }

    // Intersect all rects in the stack
    // Note: (std::max)(...) parenthesization avoids Windows min/max macro clash
    Rect clip = scissorStack_[0];
    for (size_t i = 1; i < scissorStack_.size(); ++i) {
        float x1 = (std::max)(clip.x, scissorStack_[i].x);
        float y1 = (std::max)(clip.y, scissorStack_[i].y);
        float x2 = (std::min)(clip.x + clip.w, scissorStack_[i].x + scissorStack_[i].w);
        float y2 = (std::min)(clip.y + clip.h, scissorStack_[i].y + scissorStack_[i].h);
        float rw = x2 - x1; if (rw < 0.0f) rw = 0.0f;
        float rh = y2 - y1; if (rh < 0.0f) rh = 0.0f;
        clip = {x1, y1, rw, rh};
    }

    int cx = static_cast<int>(clip.x);
    int cy = static_cast<int>(clip.y);
    int cw = static_cast<int>(clip.w);
    int ch = static_cast<int>(clip.h);

#ifndef FATEMMO_METAL
    // GL scissor uses bottom-left origin — flip Y (use cached viewport height)
    glEnable(GL_SCISSOR_TEST);
    glScissor(cx, cachedViewportHeight_ - cy - ch, cw, ch);
#else
    // Metal uses top-left origin (same as screen space)
    if (metalEncoder_) {
        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)metalEncoder_;
        MTLScissorRect sr;
        sr.x      = static_cast<NSUInteger>(cx > 0 ? cx : 0);
        sr.y      = static_cast<NSUInteger>(cy > 0 ? cy : 0);
        sr.width  = static_cast<NSUInteger>(cw > 0 ? cw : 0);
        sr.height = static_cast<NSUInteger>(ch > 0 ? ch : 0);
        [enc setScissorRect:sr];
    }
#endif
}

void SpriteBatch::pushScissorRect(const Rect& rect) {
    flushPending();
    scissorStack_.push_back(rect);
    applyScissorState();
}

void SpriteBatch::popScissorRect() {
    flushPending();
    if (!scissorStack_.empty()) scissorStack_.pop_back();
    applyScissorState();
}

// ---------------------------------------------------------------------------
// flush
// ---------------------------------------------------------------------------

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
        cmdList_->setUniform("u_shadowOffset", textEffectUniforms_.shadowOffsetUv);

        // Text effect uniforms — driven by setTextEffectUniforms()
        cmdList_->setUniform("u_outlineColor",      textEffectUniforms_.outlineColor);
        cmdList_->setUniform("u_outlineThickness",  textEffectUniforms_.outlineThickness);
        cmdList_->setUniform("u_textShadowOffset",  textEffectUniforms_.shadowOffsetUv);
        cmdList_->setUniform("u_textShadowColor",   textEffectUniforms_.shadowColor);
        cmdList_->setUniform("u_textGlowColor",     textEffectUniforms_.glowColor);
        cmdList_->setUniform("u_textGlowIntensity", textEffectUniforms_.glowIntensity);

        if (hasRoundedRect_) {
            auto& rr = pendingRoundedRect_;
            cmdList_->setUniform("u_rectSize", rr.size);
            cmdList_->setUniform("u_cornerRadius", rr.cornerRadius);
            cmdList_->setUniform("u_rrBorderWidth", rr.borderWidth);
            cmdList_->setUniform("u_rrBorderColor", rr.borderColor);
            cmdList_->setUniform("u_gradientTop", rr.fillTop);
            cmdList_->setUniform("u_gradientBottom", rr.fillBottom);
            cmdList_->setUniform("u_rrShadowOffset", rr.shadowOffset);
            cmdList_->setUniform("u_rrShadowBlur", rr.shadowBlur);
            cmdList_->setUniform("u_rrShadowColor", rr.shadowColor);
        }

        cmdList_->bindVertexBuffer(vboHandle_);
        cmdList_->bindIndexBuffer(eboHandle_);

        uintptr_t currentTexKey = ~(uintptr_t)0;
        vertices_.clear();

        auto flushBatch = [&]() {
            if (vertices_.empty()) return;

            int vertexCount = (int)vertices_.size();
            int quadCount = vertexCount / 4;

#ifdef FATEMMO_METAL
            // Metal direct draw path
            id<MTLBuffer> vbo = (__bridge id<MTLBuffer>)device.resolveMetalBuffer(vboHandle_);
            memcpy(vbo.contents, vertices_.data(), vertexCount * sizeof(SpriteVertex));

            id<MTLRenderCommandEncoder> enc =
                (__bridge id<MTLRenderCommandEncoder>)metalEncoder_;
            [enc setRenderPipelineState:
                (__bridge id<MTLRenderPipelineState>)device.resolveMetalPipelineState(currentPipeline())];
            [enc setVertexBuffer:vbo offset:0 atIndex:0];

            // Upload view-projection matrix via setVertexBytes at index 1
            [enc setVertexBytes:&viewProjection_ length:sizeof(viewProjection_) atIndex:1];

            // Index buffer
            id<MTLBuffer> ebo = (__bridge id<MTLBuffer>)device.resolveMetalBuffer(eboHandle_);
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:(NSUInteger)(quadCount * 6)
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:ebo
                     indexBufferOffset:0];
#else
            device.updateBuffer(vboHandle_, vertices_.data(),
                                vertexCount * sizeof(SpriteVertex));
            cmdList_->drawIndexed(gfx::PrimitiveType::Triangles, quadCount * 6);
#endif
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
#ifdef FATEMMO_METAL
                    id<MTLRenderCommandEncoder> enc =
                        (__bridge id<MTLRenderCommandEncoder>)metalEncoder_;
                    [enc setFragmentTexture:
                        (__bridge id<MTLTexture>)device.resolveMetalTexture(entry.texture->gfxHandle())
                                   atIndex:0];
#else
                    cmdList_->bindTexture(0, entry.texture->gfxHandle());
#endif
                } else if (entry.gfxTexHandle.valid()) {
#ifdef FATEMMO_METAL
                    id<MTLRenderCommandEncoder> enc =
                        (__bridge id<MTLRenderCommandEncoder>)metalEncoder_;
                    [enc setFragmentTexture:
                        (__bridge id<MTLTexture>)device.resolveMetalTexture(entry.gfxTexHandle)
                                   atIndex:0];
#else
                    cmdList_->bindTexture(0, entry.gfxTexHandle);
#endif
                } else if (entry.rawTexId) {
#ifdef FATEMMO_METAL
                    // rawTexId is a GL name — not usable on Metal; use whiteTexHandle_ as fallback
                    id<MTLRenderCommandEncoder> enc =
                        (__bridge id<MTLRenderCommandEncoder>)metalEncoder_;
                    [enc setFragmentTexture:
                        (__bridge id<MTLTexture>)device.resolveMetalTexture(whiteTexHandle_)
                                   atIndex:0];
#else
                    // Raw GL texture ID -- fall back to direct GL bind
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, entry.rawTexId);
#endif
                } else {
#ifdef FATEMMO_METAL
                    id<MTLRenderCommandEncoder> enc =
                        (__bridge id<MTLRenderCommandEncoder>)metalEncoder_;
                    [enc setFragmentTexture:
                        (__bridge id<MTLTexture>)device.resolveMetalTexture(whiteTexHandle_)
                                   atIndex:0];
#else
                    cmdList_->bindTexture(0, whiteTexHandle_);
#endif
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

#ifndef FATEMMO_METAL
    // ------------------------------------------------------------------
    // Direct GL fallback path (editor/ImGui or when no CommandList is set)
    // ------------------------------------------------------------------
    shader_.bind();
    shader_.setMat4("uViewProjection", viewProjection_);
    shader_.setInt("uTexture", 0);
    shader_.setFloat("u_pxRange", 4.0f);
    shader_.setVec2("u_atlasSize", {512.0f, 512.0f});
    shader_.setVec2("u_shadowOffset", textEffectUniforms_.shadowOffsetUv);

    // Text effect uniforms — driven by setTextEffectUniforms()
    {
        const auto& tfx = textEffectUniforms_;
        shader_.setVec4("u_outlineColor",      tfx.outlineColor.r, tfx.outlineColor.g, tfx.outlineColor.b, tfx.outlineColor.a);
        shader_.setFloat("u_outlineThickness", tfx.outlineThickness);
        shader_.setVec2("u_textShadowOffset",  tfx.shadowOffsetUv);
        shader_.setVec4("u_textShadowColor",   tfx.shadowColor.r,  tfx.shadowColor.g,  tfx.shadowColor.b,  tfx.shadowColor.a);
        shader_.setVec4("u_textGlowColor",     tfx.glowColor.r,    tfx.glowColor.g,    tfx.glowColor.b,    tfx.glowColor.a);
        shader_.setFloat("u_textGlowIntensity", tfx.glowIntensity);
    }

    if (hasRoundedRect_) {
        auto& rr = pendingRoundedRect_;
        shader_.setVec2("u_rectSize", {rr.size.x, rr.size.y});
        shader_.setFloat("u_cornerRadius", rr.cornerRadius);
        shader_.setFloat("u_rrBorderWidth", rr.borderWidth);
        shader_.setVec4("u_rrBorderColor", rr.borderColor.r, rr.borderColor.g, rr.borderColor.b, rr.borderColor.a);
        shader_.setVec4("u_gradientTop", rr.fillTop.r, rr.fillTop.g, rr.fillTop.b, rr.fillTop.a);
        shader_.setVec4("u_gradientBottom", rr.fillBottom.r, rr.fillBottom.g, rr.fillBottom.b, rr.fillBottom.a);
        shader_.setVec2("u_rrShadowOffset", {rr.shadowOffset.x, rr.shadowOffset.y});
        shader_.setFloat("u_rrShadowBlur", rr.shadowBlur);
        shader_.setVec4("u_rrShadowColor", rr.shadowColor.r, rr.shadowColor.g, rr.shadowColor.b, rr.shadowColor.a);
    }

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
#endif // !FATEMMO_METAL
}

void SpriteBatch::setTextEffectUniforms(const TextEffectUniforms& fx) {
    // Pending sprites must flush before the new uniforms apply, otherwise the
    // active batch would render with whichever uniforms happen to win at flush.
    flushPending();
    textEffectUniforms_ = fx;
}

void SpriteBatch::resetTextEffectUniforms() {
    flushPending();
    textEffectUniforms_ = TextEffectUniforms{};
}

void SpriteBatch::setBlendMode(BlendMode mode) {
    if (mode == blendMode_) return;
    flushPending(); // must flush AND clear before changing blend state
    blendMode_ = mode;

    if (cmdList_) {
        // CommandList path: just switch pipeline on next flush
        // (pipeline is selected in flush() via currentPipeline())
        return;
    }

#ifndef FATEMMO_METAL
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
#endif
}

void SpriteBatch::setPalette(const Color* colors, int count) {
    count = (std::min)(count, 16);
    if (cmdList_) {
        cmdList_->setUniform("u_paletteSize", static_cast<float>(count));
        for (int i = 0; i < count; ++i) {
            std::string name = "u_palette[" + std::to_string(i) + "]";
            cmdList_->setUniform(name.c_str(), colors[i]);
        }
    } else {
        shader_.bind();
        shader_.setFloat("u_paletteSize", static_cast<float>(count));
        for (int i = 0; i < count; ++i) {
            std::string name = "u_palette[" + std::to_string(i) + "]";
            shader_.setVec4(name, colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        }
    }
}

void SpriteBatch::clearPalette() {
    if (cmdList_) {
        cmdList_->setUniform("u_paletteSize", 0.0f);
    } else {
        shader_.bind();
        shader_.setFloat("u_paletteSize", 0.0f);
    }
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

void SpriteBatch::drawEllipse(const Vec2& center, float radiusX, float radiusY,
                               const Color& color, float depth, int segments) {
    if (!drawing_ || radiusX <= 0 || radiusY <= 0) return;
    float angleStep = 2.0f * 3.14159265f / static_cast<float>(segments);

    for (int i = 0; i < segments; i++) {
        float a0 = i * angleStep;
        float a1 = (i + 1) * angleStep;
        float midAngle = (a0 + a1) * 0.5f;

        // Ellipse point at midAngle
        float ex = std::cos(midAngle) * radiusX;
        float ey = std::sin(midAngle) * radiusY;

        // Chord length between edge points a0 and a1
        float x0 = std::cos(a0) * radiusX, y0 = std::sin(a0) * radiusY;
        float x1 = std::cos(a1) * radiusX, y1 = std::sin(a1) * radiusY;
        float chordLen = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));

        // Radial length from center to midpoint
        float radLen = std::sqrt(ex * ex + ey * ey);

        // Rotation angle of the chord
        float chordAngle = std::atan2(y1 - y0, x1 - x0);

        SpriteDrawParams params;
        params.position = {center.x + ex * 0.5f, center.y + ey * 0.5f};
        params.size = {chordLen * 1.15f, radLen * 1.05f};
        params.color = color;
        params.depth = depth;
        params.rotation = chordAngle - 1.5707963f;
        params.sourceRect = {0, 0, 1, 1};
        entries_.push_back({nullptr, 0, params});
    }
}

void SpriteBatch::drawEllipseRing(const Vec2& center, float radiusX, float radiusY,
                                    float thickness, const Color& color, float depth, int segments) {
    if (!drawing_ || radiusX <= 0 || radiusY <= 0) return;
    // Scale down for inner edge proportionally
    float scaleX = (radiusX - thickness) / radiusX;
    float scaleY = (radiusY - thickness) / radiusY;
    if (scaleX < 0.0f) scaleX = 0.0f;
    if (scaleY < 0.0f) scaleY = 0.0f;
    float midScaleX = (1.0f + scaleX) * 0.5f;
    float midScaleY = (1.0f + scaleY) * 0.5f;

    float angleStep = 2.0f * 3.14159265f / static_cast<float>(segments);

    for (int i = 0; i < segments; i++) {
        float a0 = i * angleStep;
        float a1 = (i + 1) * angleStep;
        float midAngle = (a0 + a1) * 0.5f;

        // Mid-ring position
        float mx = std::cos(midAngle) * radiusX * midScaleX;
        float my = std::sin(midAngle) * radiusY * midScaleY;

        // Chord endpoints on the mid-ring
        float x0 = std::cos(a0) * radiusX * midScaleX;
        float y0 = std::sin(a0) * radiusY * midScaleY;
        float x1 = std::cos(a1) * radiusX * midScaleX;
        float y1 = std::sin(a1) * radiusY * midScaleY;
        float chordLen = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        float chordAngle = std::atan2(y1 - y0, x1 - x0);

        // Thickness along the radial direction at this angle
        float outerR = std::sqrt(std::cos(midAngle) * radiusX * std::cos(midAngle) * radiusX +
                                  std::sin(midAngle) * radiusY * std::sin(midAngle) * radiusY);
        float innerR = outerR * ((scaleX + scaleY) * 0.5f);
        float localThick = outerR - innerR;
        if (localThick < thickness * 0.5f) localThick = thickness;

        SpriteDrawParams params;
        params.position = {center.x + mx, center.y + my};
        params.size = {chordLen * 1.15f, localThick * 1.02f};
        params.color = color;
        params.depth = depth;
        params.rotation = chordAngle - 1.5707963f;
        params.sourceRect = {0, 0, 1, 1};
        entries_.push_back({nullptr, 0, params});
    }
}

void SpriteBatch::drawEllipseRingArc(const Vec2& center, float radiusX, float radiusY,
                                      float thickness, float startAngle, float endAngle,
                                      const Color& color, float depth, int segments) {
    if (!drawing_ || radiusX <= 0 || radiusY <= 0 || segments < 1) return;
    float scaleX = (radiusX - thickness) / radiusX;
    float scaleY = (radiusY - thickness) / radiusY;
    if (scaleX < 0.0f) scaleX = 0.0f;
    if (scaleY < 0.0f) scaleY = 0.0f;
    float midScaleX = (1.0f + scaleX) * 0.5f;
    float midScaleY = (1.0f + scaleY) * 0.5f;

    float totalAngle = endAngle - startAngle;
    float angleStep = totalAngle / static_cast<float>(segments);

    for (int i = 0; i < segments; i++) {
        float a0 = startAngle + i * angleStep;
        float a1 = startAngle + (i + 1) * angleStep;
        float midAngle = (a0 + a1) * 0.5f;

        float mx = std::cos(midAngle) * radiusX * midScaleX;
        float my = std::sin(midAngle) * radiusY * midScaleY;

        float x0 = std::cos(a0) * radiusX * midScaleX;
        float y0 = std::sin(a0) * radiusY * midScaleY;
        float x1 = std::cos(a1) * radiusX * midScaleX;
        float y1 = std::sin(a1) * radiusY * midScaleY;
        float chordLen = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        float chordAngle = std::atan2(y1 - y0, x1 - x0);

        float outerR = std::sqrt(std::cos(midAngle) * radiusX * std::cos(midAngle) * radiusX +
                                  std::sin(midAngle) * radiusY * std::sin(midAngle) * radiusY);
        float innerR = outerR * ((scaleX + scaleY) * 0.5f);
        float localThick = outerR - innerR;
        if (localThick < thickness * 0.5f) localThick = thickness;

        SpriteDrawParams params;
        params.position = {center.x + mx, center.y + my};
        params.size = {chordLen * 1.15f, localThick * 1.02f};
        params.color = color;
        params.depth = depth;
        params.rotation = chordAngle - 1.5707963f;
        params.sourceRect = {0, 0, 1, 1};
        entries_.push_back({nullptr, 0, params});
    }
}

void SpriteBatch::drawCircle(const Vec2& center, float radius, const Color& color,
                              float depth, int segments) {
    if (!drawing_ || radius <= 0) return;
    float angleStep = 2.0f * 3.14159265f / static_cast<float>(segments);
    float halfChord = radius * std::sin(angleStep * 0.5f);

    for (int i = 0; i < segments; i++) {
        float midAngle = (i + 0.5f) * angleStep;
        float cx = center.x + std::cos(midAngle) * radius * 0.5f;
        float cy = center.y + std::sin(midAngle) * radius * 0.5f;

        SpriteDrawParams params;
        params.position  = {cx, cy};
        params.size      = {halfChord * 2.2f, radius * 1.05f};
        params.color     = color;
        params.depth     = depth;
        params.rotation  = midAngle - 1.5707963f; // midAngle - PI/2
        params.sourceRect = {0, 0, 1, 1};
        entries_.push_back({nullptr, 0, params});
    }
}

void SpriteBatch::drawRing(const Vec2& center, float radius, float thickness,
                            const Color& color, float depth, int segments) {
    if (!drawing_ || radius <= 0) return;
    float innerR = radius - thickness;
    if (innerR < 0.0f) innerR = 0.0f;
    float midR = (radius + innerR) * 0.5f;
    float angleStep = 2.0f * 3.14159265f / static_cast<float>(segments);

    for (int i = 0; i < segments; i++) {
        float midAngle = (i + 0.5f) * angleStep;

        SpriteDrawParams params;
        params.position  = {center.x + std::cos(midAngle) * midR,
                            center.y + std::sin(midAngle) * midR};
        params.size      = {2.0f * midR * std::sin(angleStep * 0.5f) * 1.15f, thickness * 1.02f};
        params.color     = color;
        params.depth     = depth;
        params.rotation  = midAngle - 1.5707963f;
        params.sourceRect = {0, 0, 1, 1};
        entries_.push_back({nullptr, 0, params});
    }
}

void SpriteBatch::drawArc(const Vec2& center, float radius, float startAngle,
                           float endAngle, const Color& color, float depth, int segments) {
    if (!drawing_ || radius <= 0) return;
    float totalAngle = endAngle - startAngle;
    float angleStep = totalAngle / static_cast<float>(segments);
    float halfChord = radius * std::sin(std::abs(angleStep) * 0.5f);

    for (int i = 0; i < segments; i++) {
        float midAngle = startAngle + (i + 0.5f) * angleStep;

        SpriteDrawParams params;
        params.position  = {center.x + std::cos(midAngle) * radius * 0.5f,
                            center.y + std::sin(midAngle) * radius * 0.5f};
        params.size      = {halfChord * 2.2f, radius * 1.05f};
        params.color     = color;
        params.depth     = depth;
        params.rotation  = midAngle - 1.5707963f;
        params.sourceRect = {0, 0, 1, 1};
        entries_.push_back({nullptr, 0, params});
    }
}

void SpriteBatch::drawNineSlice(const std::shared_ptr<Texture>& texture,
                                const Rect& dest,
                                const NineSlice& s,
                                const Color& tint,
                                float depth) {
    if (!texture || !drawing_) return;

    float texW = static_cast<float>(texture->width());
    float texH = static_cast<float>(texture->height());
    if (texW <= 0 || texH <= 0) return;

    // UV insets (normalized 0-1)
    float uLeft   = s.left / texW;
    float uRight  = s.right / texW;
    float vTop    = s.top / texH;
    float vBottom = s.bottom / texH;

    // Destination regions
    float dl = dest.x;
    float dt = dest.y;
    float dr = dest.x + dest.w;
    float db = dest.y + dest.h;
    float dml = dl + s.left;
    float dmr = dr - s.right;
    float dmt = dt + s.top;
    float dmb = db - s.bottom;

    // Clamp: if dest is smaller than slice insets, degenerate gracefully
    if (dml > dmr) { dml = dmr = (dl + dr) * 0.5f; }
    if (dmt > dmb) { dmt = dmb = (dt + db) * 0.5f; }

    struct SliceRegion {
        Rect destRect;
        Rect uvRect;
    };

    SliceRegion regions[9] = {
        // Top row
        {{dl,  dt,  s.left,        s.top},         {0,           0,           uLeft,          vTop}},
        {{dml, dt,  dmr - dml,     s.top},         {uLeft,       0,           1-uLeft-uRight, vTop}},
        {{dmr, dt,  s.right,       s.top},         {1 - uRight,  0,           uRight,         vTop}},
        // Middle row
        {{dl,  dmt, s.left,        dmb - dmt},     {0,           vTop,        uLeft,          1-vTop-vBottom}},
        {{dml, dmt, dmr - dml,     dmb - dmt},     {uLeft,       vTop,        1-uLeft-uRight, 1-vTop-vBottom}},
        {{dmr, dmt, s.right,       dmb - dmt},     {1 - uRight,  vTop,        uRight,         1-vTop-vBottom}},
        // Bottom row
        {{dl,  dmb, s.left,        s.bottom},      {0,           1 - vBottom, uLeft,          vBottom}},
        {{dml, dmb, dmr - dml,     s.bottom},      {uLeft,       1 - vBottom, 1-uLeft-uRight, vBottom}},
        {{dmr, dmb, s.right,       s.bottom},      {1 - uRight,  1 - vBottom, uRight,         vBottom}},
    };

    for (auto& r : regions) {
        if (r.destRect.w <= 0 || r.destRect.h <= 0) continue;

        SpriteDrawParams params;
        params.position = {r.destRect.x + r.destRect.w * 0.5f,
                          r.destRect.y + r.destRect.h * 0.5f};
        params.size = {r.destRect.w, r.destRect.h};
        params.sourceRect = r.uvRect;
        params.color = tint;
        params.depth = depth;

        draw(texture, params);
    }
}

} // namespace fate
