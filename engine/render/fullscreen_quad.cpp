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
