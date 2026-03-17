#include "engine/render/sprite_batch.h"
#include "engine/render/gl_loader.h"
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

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
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

bool SpriteBatch::init() {
    // Try loading shader from files first, fall back to embedded
    if (!shader_.loadFromFile("assets/shaders/sprite.vert", "assets/shaders/sprite.frag")) {
        LOG_WARN("SpriteBatch", "External shaders not found, using embedded shaders");
        if (!shader_.loadFromSource(SPRITE_VERT_SRC, SPRITE_FRAG_SRC)) {
            LOG_ERROR("SpriteBatch", "Failed to compile embedded shaders");
            return false;
        }
    }

    // Create VAO/VBO/EBO
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    // VBO - dynamic, updated each frame
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * sizeof(SpriteVertex), nullptr, GL_DYNAMIC_DRAW);

    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, x));
    // TexCoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, u));
    // Color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, r));
    // RenderType
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, renderType));

    // EBO - static index pattern for quads
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
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    createWhiteTexture();

    entries_.reserve(1000);
    vertices_.reserve(4000);

    LOG_INFO("SpriteBatch", "Initialized (max %d sprites per batch)", MAX_SPRITES);
    return true;
}

void SpriteBatch::shutdown() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (whiteTexture_) { glDeleteTextures(1, &whiteTexture_); whiteTexture_ = 0; }
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
        // Skip sort — same order as last frame
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

void SpriteBatch::createWhiteTexture() {
    unsigned char white[] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTexture_);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace fate
