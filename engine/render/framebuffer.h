#pragma once
#include "engine/render/gfx/types.h"

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

    unsigned int textureId() const {
#ifdef FATEMMO_METAL
        return 0;
#else
        return texture_;
#endif
    }
    int width() const { return width_; }
    int height() const { return height_; }
    bool isValid() const {
#ifdef FATEMMO_METAL
        return gfxHandle_.valid();
#else
        return fbo_ != 0;
#endif
    }
    bool hasDepthStencil() const { return hasDepthStencil_; }
    gfx::FramebufferHandle gfxHandle() const { return gfxHandle_; }

private:
#ifndef FATEMMO_METAL
    unsigned int fbo_ = 0;
#endif
    gfx::FramebufferHandle gfxHandle_{};
#ifndef FATEMMO_METAL
    unsigned int texture_ = 0;
    unsigned int rbo_ = 0;
#endif
    int width_ = 0;
    int height_ = 0;
    bool hasDepthStencil_ = false;
};

} // namespace fate
