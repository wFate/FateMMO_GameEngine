/**************************************************************************/
/*  fullscreen_quad.cpp                                                   */
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
#include "engine/render/fullscreen_quad.h"
#include "engine/render/gfx/device.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif

namespace fate {

FullscreenQuad& FullscreenQuad::instance() {
    static FullscreenQuad s_instance;
    return s_instance;
}

void FullscreenQuad::init() {
#ifndef FATEMMO_METAL
    // On Metal: nothing to init — draw(encoder) just issues drawPrimitives;
    // the caller (blit pass / render graph) binds the appropriate pipeline before calling draw.
    auto& device = gfx::Device::instance();
    gfx::PipelineDesc desc{};
    pipelineHandle_ = device.createPipeline(desc);
    vao_ = device.resolveGLPipelineVAO(pipelineHandle_);
#endif
}

void FullscreenQuad::shutdown() {
    if (pipelineHandle_.valid()) {
        gfx::Device::instance().destroy(pipelineHandle_);
        pipelineHandle_ = {};
#ifndef FATEMMO_METAL
        vao_ = 0;
#endif
    }
}

void FullscreenQuad::draw() {
#ifndef FATEMMO_METAL
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
#endif
}

#ifdef FATEMMO_METAL
#import <Metal/Metal.h>
void FullscreenQuad::draw(void* encoder) {
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)encoder;
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}
#endif

} // namespace fate
