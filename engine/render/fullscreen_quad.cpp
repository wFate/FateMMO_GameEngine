#include "engine/render/fullscreen_quad.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"

namespace fate {

FullscreenQuad& FullscreenQuad::instance() {
    static FullscreenQuad s_instance;
    return s_instance;
}

void FullscreenQuad::init() {
    glGenVertexArrays(1, &vao_);
}

void FullscreenQuad::shutdown() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void FullscreenQuad::draw() {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace fate
