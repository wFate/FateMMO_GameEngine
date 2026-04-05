#include "engine/editor/animation_preview.h"
#include "engine/render/texture.h"
#include "engine/core/logger.h"
#ifdef FATE_HAS_GAME
#include "game/data/paper_doll_catalog.h"
#endif // FATE_HAS_GAME
#include <imgui.h>
#include <algorithm>

namespace fate {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void AnimationPreview::init() {}

void AnimationPreview::shutdown() {}

// ---------------------------------------------------------------------------
// Layer configuration
// ---------------------------------------------------------------------------
void AnimationPreview::setLayerVisible(int layer, bool visible) {
    if (layer >= 0 && layer < (int)layerVisible_.size())
        layerVisible_[layer] = visible;
}

void AnimationPreview::setClassPreset(const std::string& preset) {
    classPreset_ = preset;
}

void AnimationPreview::setLayerStyle(int layer, const std::string& style) {
    if (layer >= 0 && layer < (int)layerStyles_.size())
        layerStyles_[layer] = style;
}

void AnimationPreview::setPrimarySheet(const std::string& sheetPath,
                                       int frameW, int frameH, int columns) {
    sheetPath_ = sheetPath;
    frameW_ = frameW;
    frameH_ = frameH;
    columns_ = std::max(1, columns);
}

// ---------------------------------------------------------------------------
// Checkerboard background (transparency indicator)
// ---------------------------------------------------------------------------
void AnimationPreview::drawCheckerboard(ImDrawList* drawList,
                                        float x, float y,
                                        float w, float h) const {
    constexpr int checkSize = 8;
    ImU32 c1 = IM_COL32(40, 40, 40, 255);
    ImU32 c2 = IM_COL32(50, 50, 50, 255);
    for (int cy = 0; cy < (int)h; cy += checkSize) {
        for (int cx = 0; cx < (int)w; cx += checkSize) {
            ImU32 c = ((cx / checkSize + cy / checkSize) % 2 == 0) ? c1 : c2;
            float x1 = x + (float)cx;
            float y1 = y + (float)cy;
            float x2 = x1 + fminf((float)checkSize, w - (float)cx);
            float y2 = y1 + fminf((float)checkSize, h - (float)cy);
            drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), c);
        }
    }
}

// ---------------------------------------------------------------------------
// Draw a single frame from a sprite sheet texture
// ---------------------------------------------------------------------------
void AnimationPreview::drawLayerFrame(ImDrawList* drawList, unsigned int texId,
                                      int frame, float x, float y,
                                      float zoom, float alpha,
                                      float offsetX, float offsetY) const {
    if (texId == 0 || frameW_ <= 0 || frameH_ <= 0 || columns_ <= 0)
        return;

    // Look up actual texture dimensions via TextureCache
    // We need them for UV computation — the texture stores the full sheet
    auto tex = TextureCache::instance().get(sheetPath_);
    int texW = 0, texH = 0;
    if (tex) {
        texW = tex->width();
        texH = tex->height();
    }
    // If no cached sheet, try to infer from the GL texture ID we were given
    // by assuming the texture was loaded elsewhere; fall back to frame-based guess
    if (texW <= 0 || texH <= 0) {
        texW = frameW_ * columns_;
        int rows = std::max(1, (frame + columns_) / columns_);
        texH = frameH_ * rows;
    }

    int col = frame % columns_;
    int row = frame / columns_;

    float u0 = (float)(col * frameW_) / (float)texW;
    float v0 = (float)((row + 1) * frameH_) / (float)texH;  // GL: bottom-up, flip V
    float u1 = (float)((col + 1) * frameW_) / (float)texW;
    float v1 = (float)(row * frameH_) / (float)texH;

    float drawX = x + offsetX * zoom;
    float drawY = y + offsetY * zoom;
    float drawW = (float)frameW_ * zoom;
    float drawH = (float)frameH_ * zoom;

    // Alpha tint: white with variable alpha
    ImU32 tint = IM_COL32(255, 255, 255, (int)(alpha * 255.0f));

    drawList->AddImage((ImTextureID)(intptr_t)texId, ImVec2(drawX, drawY),
                       ImVec2(drawX + drawW, drawY + drawH),
                       ImVec2(u0, v0), ImVec2(u1, v1), tint);
}

// ---------------------------------------------------------------------------
// Main draw — multi-layer composite preview
// ---------------------------------------------------------------------------
void AnimationPreview::draw(int currentFrame, int direction, float zoom) {
    float displayW = (float)frameW_ * zoom;
    float displayH = (float)frameH_ * zoom;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    float baseX = cursorPos.x;
    float baseY = cursorPos.y;

    // Reserve space in ImGui layout
    ImGui::Dummy(ImVec2(displayW, displayH));

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Checkerboard background
    drawCheckerboard(dl, baseX, baseY, displayW, displayH);

#ifdef FATE_HAS_GAME
    // Multi-layer composite mode using PaperDollCatalog
    auto& catalog = PaperDollCatalog::instance();
    if (!catalog.isLoaded()) {
        // Fall back to primary sheet only
        if (!sheetPath_.empty()) {
            auto tex = TextureCache::instance().get(sheetPath_);
            if (!tex) tex = TextureCache::instance().load(sheetPath_);
            if (tex && tex->id() != 0) {
                drawLayerFrame(dl, tex->id(), currentFrame,
                               baseX, baseY, zoom, 1.0f);
            }
        }
        return;
    }

    // Gender string: use "Male" as default for preview
    std::string gender = "Male";

    // Helper: pick directional texture from a SpriteSet
    auto pickDir = [&](const SpriteSet& ss) -> std::shared_ptr<Texture> {
        switch (direction) {
            case 0: return ss.front;
            case 1: return ss.back;
            case 2: return ss.side;
            default: return ss.front;
        }
    };

    // Use catalog frame dimensions for UV computation
    int catFrameW = catalog.frameWidth();
    int catFrameH = catalog.frameHeight();

    // Temporarily override frameW_/frameH_ for drawLayerFrame UV math
    // (save and restore since drawLayerFrame uses member fields)
    int savedFrameW = frameW_;
    int savedFrameH = frameH_;
    int savedColumns = columns_;
    std::string savedSheetPath = sheetPath_;

    // Helper to draw a catalog layer at a specific frame
    auto drawCatalogLayer = [&](const std::shared_ptr<Texture>& tex, int frame,
                                float alpha,
                                float offsetX = 0.0f, float offsetY = 0.0f) {
        if (!tex || tex->id() == 0) return;

        // Compute columns from texture width / frame width
        int texW = tex->width();
        int cols = (catFrameW > 0) ? std::max(1, texW / catFrameW) : 1;

        // Point member fields at this layer's texture for UV computation
        frameW_ = catFrameW;
        frameH_ = catFrameH;
        columns_ = cols;
        sheetPath_ = tex->path();

        drawLayerFrame(dl, tex->id(), frame,
                       baseX, baseY, zoom, alpha, offsetX, offsetY);
    };

    // Animation name for offset lookups (empty string if no current animation)
    // The caller (AnimationEditor) would set this; for now use empty which
    // returns 0 offsets from catalog
    std::string animName;

    // -- Onion skin: previous frame at 30% alpha --
    if (onionSkin_ && currentFrame > 0) {
        int prevFrame = currentFrame - 1;
        // Body
        if (layerVisible_[0]) {
            auto tex = pickDir(catalog.getBody(gender));
            drawCatalogLayer(tex, prevFrame, 0.3f);
        }
        // Hair
        if (layerVisible_[1] && !layerStyles_[1].empty()) {
            auto tex = pickDir(catalog.getHairstyle(gender, layerStyles_[1]));
            float ox = catalog.getLayerOffsetX(animName, "hair", prevFrame);
            float oy = catalog.getLayerOffsetY(animName, "hair", prevFrame);
            drawCatalogLayer(tex, prevFrame, 0.3f, ox, oy);
        }
        // Armor
        if (layerVisible_[2] && !layerStyles_[2].empty()) {
            auto tex = pickDir(catalog.getEquipment("armor", layerStyles_[2]));
            float ox = catalog.getLayerOffsetX(animName, "armor", prevFrame);
            float oy = catalog.getLayerOffsetY(animName, "armor", prevFrame);
            drawCatalogLayer(tex, prevFrame, 0.3f, ox, oy);
        }
        // Hat
        if (layerVisible_[3] && !layerStyles_[3].empty()) {
            auto tex = pickDir(catalog.getEquipment("hat", layerStyles_[3]));
            float ox = catalog.getLayerOffsetX(animName, "hat", prevFrame);
            float oy = catalog.getLayerOffsetY(animName, "hat", prevFrame);
            drawCatalogLayer(tex, prevFrame, 0.3f, ox, oy);
        }
        // Weapon (ignore depth ordering for onion skin ghost)
        if (layerVisible_[4] && !layerStyles_[4].empty()) {
            auto tex = pickDir(catalog.getEquipment("weapon", layerStyles_[4]));
            float ox = catalog.getLayerOffsetX(animName, "weapon", prevFrame);
            float oy = catalog.getLayerOffsetY(animName, "weapon", prevFrame);
            drawCatalogLayer(tex, prevFrame, 0.3f, ox, oy);
        }
    }

    // -- Current frame: full alpha, proper depth ordering --
    // Determine weapon depth for current frame
    WeaponLayer weaponDepth = catalog.getWeaponLayer(animName, currentFrame);
    float weaponRotation = catalog.getWeaponRotation(animName, currentFrame);
    (void)weaponRotation; // Rotation applied via ImGui transform would need custom
                          // vertex manipulation; stored for future use

    // Draw weapon behind body if BehindBody
    if (weaponDepth == WeaponLayer::BehindBody && layerVisible_[4] && !layerStyles_[4].empty()) {
        auto tex = pickDir(catalog.getEquipment("weapon", layerStyles_[4]));
        float ox = catalog.getLayerOffsetX(animName, "weapon", currentFrame);
        float oy = catalog.getLayerOffsetY(animName, "weapon", currentFrame);
        drawCatalogLayer(tex, currentFrame, 1.0f, ox, oy);
    }

    // Body (layer 0)
    if (layerVisible_[0]) {
        auto tex = pickDir(catalog.getBody(gender));
        float ox = catalog.getLayerOffsetX(animName, "body", currentFrame);
        float oy = catalog.getLayerOffsetY(animName, "body", currentFrame);
        drawCatalogLayer(tex, currentFrame, 1.0f, ox, oy);
    }

    // Hair (layer 1)
    if (layerVisible_[1] && !layerStyles_[1].empty()) {
        auto tex = pickDir(catalog.getHairstyle(gender, layerStyles_[1]));
        float ox = catalog.getLayerOffsetX(animName, "hair", currentFrame);
        float oy = catalog.getLayerOffsetY(animName, "hair", currentFrame);
        drawCatalogLayer(tex, currentFrame, 1.0f, ox, oy);
    }

    // Armor (layer 2)
    if (layerVisible_[2] && !layerStyles_[2].empty()) {
        auto tex = pickDir(catalog.getEquipment("armor", layerStyles_[2]));
        float ox = catalog.getLayerOffsetX(animName, "armor", currentFrame);
        float oy = catalog.getLayerOffsetY(animName, "armor", currentFrame);
        drawCatalogLayer(tex, currentFrame, 1.0f, ox, oy);
    }

    // Hat (layer 3)
    if (layerVisible_[3] && !layerStyles_[3].empty()) {
        auto tex = pickDir(catalog.getEquipment("hat", layerStyles_[3]));
        float ox = catalog.getLayerOffsetX(animName, "hat", currentFrame);
        float oy = catalog.getLayerOffsetY(animName, "hat", currentFrame);
        drawCatalogLayer(tex, currentFrame, 1.0f, ox, oy);
    }

    // Draw weapon in front if InFront (default)
    if (weaponDepth == WeaponLayer::InFront && layerVisible_[4] && !layerStyles_[4].empty()) {
        auto tex = pickDir(catalog.getEquipment("weapon", layerStyles_[4]));
        float ox = catalog.getLayerOffsetX(animName, "weapon", currentFrame);
        float oy = catalog.getLayerOffsetY(animName, "weapon", currentFrame);
        drawCatalogLayer(tex, currentFrame, 1.0f, ox, oy);
    }

    // -- Onion skin: next frame at 30% alpha --
    if (onionSkin_) {
        // We need to know total frame count; infer from body texture columns
        auto bodyTex = pickDir(catalog.getBody(gender));
        int totalFrames = 1;
        if (bodyTex && catFrameW > 0) {
            int cols = std::max(1, bodyTex->width() / catFrameW);
            int rows = std::max(1, bodyTex->height() / catFrameH);
            totalFrames = cols * rows;
        }

        int nextFrame = currentFrame + 1;
        if (nextFrame < totalFrames) {
            if (layerVisible_[0]) {
                auto tex = pickDir(catalog.getBody(gender));
                drawCatalogLayer(tex, nextFrame, 0.3f);
            }
            if (layerVisible_[1] && !layerStyles_[1].empty()) {
                auto tex = pickDir(catalog.getHairstyle(gender, layerStyles_[1]));
                float ox = catalog.getLayerOffsetX(animName, "hair", nextFrame);
                float oy = catalog.getLayerOffsetY(animName, "hair", nextFrame);
                drawCatalogLayer(tex, nextFrame, 0.3f, ox, oy);
            }
            if (layerVisible_[2] && !layerStyles_[2].empty()) {
                auto tex = pickDir(catalog.getEquipment("armor", layerStyles_[2]));
                float ox = catalog.getLayerOffsetX(animName, "armor", nextFrame);
                float oy = catalog.getLayerOffsetY(animName, "armor", nextFrame);
                drawCatalogLayer(tex, nextFrame, 0.3f, ox, oy);
            }
            if (layerVisible_[3] && !layerStyles_[3].empty()) {
                auto tex = pickDir(catalog.getEquipment("hat", layerStyles_[3]));
                float ox = catalog.getLayerOffsetX(animName, "hat", nextFrame);
                float oy = catalog.getLayerOffsetY(animName, "hat", nextFrame);
                drawCatalogLayer(tex, nextFrame, 0.3f, ox, oy);
            }
            if (layerVisible_[4] && !layerStyles_[4].empty()) {
                auto tex = pickDir(catalog.getEquipment("weapon", layerStyles_[4]));
                float ox = catalog.getLayerOffsetX(animName, "weapon", nextFrame);
                float oy = catalog.getLayerOffsetY(animName, "weapon", nextFrame);
                drawCatalogLayer(tex, nextFrame, 0.3f, ox, oy);
            }
        }
    }

    // Restore member fields
    frameW_ = savedFrameW;
    frameH_ = savedFrameH;
    columns_ = savedColumns;
    sheetPath_ = savedSheetPath;

#else
    // No game module — single-sheet fallback
    if (!sheetPath_.empty()) {
        auto tex = TextureCache::instance().get(sheetPath_);
        if (!tex) tex = TextureCache::instance().load(sheetPath_);
        if (tex && tex->id() != 0) {
            // Onion skin: previous frame
            if (onionSkin_ && currentFrame > 0) {
                drawLayerFrame(dl, tex->id(), currentFrame - 1,
                               baseX, baseY, zoom, 0.3f);
            }

            // Current frame
            drawLayerFrame(dl, tex->id(), currentFrame,
                           baseX, baseY, zoom, 1.0f);

            // Onion skin: next frame
            if (onionSkin_) {
                int cols = columns_;
                int texW = tex->width();
                int texH = tex->height();
                int rows = (frameH_ > 0) ? std::max(1, texH / frameH_) : 1;
                int totalFrames = cols * rows;
                if (currentFrame + 1 < totalFrames) {
                    drawLayerFrame(dl, tex->id(), currentFrame + 1,
                                   baseX, baseY, zoom, 0.3f);
                }
            }
        }
    }
#endif // FATE_HAS_GAME
}

} // namespace fate
