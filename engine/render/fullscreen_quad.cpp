#include "engine/render/fullscreen_quad.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"

namespace fate {

FullscreenQuad& FullscreenQuad::instance() {
    static FullscreenQuad s_instance;
    return s_instance;
}

void FullscreenQuad::init() {
    auto& device = gfx::Device::instance();
    gfx::PipelineDesc desc{};
    pipelineHandle_ = device.createPipeline(desc);
    vao_ = device.resolveGLPipelineVAO(pipelineHandle_);
}

void FullscreenQuad::shutdown() {
    if (pipelineHandle_.valid()) {
        gfx::Device::instance().destroy(pipelineHandle_);
        pipelineHandle_ = {};
        vao_ = 0;
    }
}

void FullscreenQuad::draw() {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace fate
