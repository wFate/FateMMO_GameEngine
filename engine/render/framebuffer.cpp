/**************************************************************************/
/*  framebuffer.cpp                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
#include "engine/render/framebuffer.h"
#include "engine/render/gfx/device.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#include "engine/core/logger.h"

namespace fate {

// Snap to 8-pixel boundary to avoid per-pixel FBO recreation during window resize
static int snapFBO(int v) { return (v + 7) & ~7; }

bool Framebuffer::create(int width, int height, bool withDepthStencil) {
    width = snapFBO(width);
    height = snapFBO(height);
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
    w = snapFBO(w);
    h = snapFBO(h);
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
