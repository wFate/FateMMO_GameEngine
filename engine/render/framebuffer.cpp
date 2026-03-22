#include "engine/render/framebuffer.h"
#include "engine/render/gfx/device.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#include "engine/core/logger.h"

namespace fate {

bool Framebuffer::create(int width, int height, bool withDepthStencil) {
    if (width <= 0 || height <= 0) return false;

    width_ = width;
    height_ = height;

    auto& device = gfx::Device::instance();
    gfxHandle_ = device.createFramebuffer(width, height, gfx::TextureFormat::RGBA8, withDepthStencil);
    if (!gfxHandle_.valid()) {
        LOG_ERROR("Framebuffer", "Device::createFramebuffer failed");
        width_ = 0;
        height_ = 0;
        return false;
    }

#ifndef FATEMMO_METAL
    fbo_ = device.resolveGLFramebuffer(gfxHandle_);

    // Resolve the color attachment texture ID for backward compat
    gfx::TextureHandle texHandle = device.getFramebufferTexture(gfxHandle_);
    texture_ = device.resolveGLTexture(texHandle);
#endif

    hasDepthStencil_ = withDepthStencil;

#ifndef FATEMMO_METAL
    LOG_DEBUG("Framebuffer", "Created FBO %u (%dx%d)", fbo_, width_, height_);
#else
    LOG_DEBUG("Framebuffer", "Created Metal framebuffer (%dx%d)", width_, height_);
#endif
    return true;
}

void Framebuffer::destroy() {
    if (gfxHandle_.valid()) {
        gfx::Device::instance().destroy(gfxHandle_);
        gfxHandle_ = {};
    }
#ifndef FATEMMO_METAL
    fbo_ = 0;
    texture_ = 0;
    rbo_ = 0;
#endif
    width_ = 0;
    height_ = 0;
    hasDepthStencil_ = false;
}

void Framebuffer::resize(int w, int h) {
    if (w == width_ && h == height_) return;
    bool ds = hasDepthStencil_;
    destroy();
    create(w, h, ds);
}

void Framebuffer::bind() {
#ifndef FATEMMO_METAL
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
#endif
}

void Framebuffer::unbind() {
#ifndef FATEMMO_METAL
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
}

} // namespace fate
