/**************************************************************************/
/*  framebuffer.h                                                         */
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
