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
