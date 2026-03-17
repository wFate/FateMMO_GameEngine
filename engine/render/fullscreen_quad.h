#pragma once

namespace fate {

// Manages an empty VAO for gl_VertexID-based fullscreen triangle rendering
class FullscreenQuad {
public:
    static FullscreenQuad& instance();

    void init();
    void shutdown();
    void draw(); // binds VAO, draws 3 vertices (oversized triangle clipped to viewport)

private:
    FullscreenQuad() = default;
    unsigned int vao_ = 0;
};

} // namespace fate
