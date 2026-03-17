#pragma once
#include "engine/render/gl_loader.h"

namespace fate {

class Framebuffer {
public:
    Framebuffer() = default;
    // No auto-cleanup in destructor (matches SpriteBatch::init/shutdown pattern)
    ~Framebuffer() = default;

    bool create(int width, int height);
    void destroy();
    void resize(int width, int height);
    void bind();
    void unbind();

    unsigned int textureId() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool isValid() const { return fbo_ != 0; }

private:
    unsigned int fbo_ = 0;
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace fate
