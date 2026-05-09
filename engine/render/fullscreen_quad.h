/**************************************************************************/
/*  fullscreen_quad.h                                                     */
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

// Manages an empty VAO for gl_VertexID-based fullscreen triangle rendering
class FullscreenQuad {
public:
    static FullscreenQuad& instance();

    void init();
    void shutdown();
    void draw(); // binds VAO, draws 3 vertices (oversized triangle clipped to viewport)
#ifdef FATEMMO_METAL
    void draw(void* encoder); // id<MTLRenderCommandEncoder>
#endif

private:
    FullscreenQuad() = default;
#ifndef FATEMMO_METAL
    unsigned int vao_ = 0;
#endif
    gfx::PipelineHandle pipelineHandle_{};
};

} // namespace fate
