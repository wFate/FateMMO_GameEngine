#pragma once

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

    unsigned int textureId() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool isValid() const { return fbo_ != 0; }
    bool hasDepthStencil() const { return hasDepthStencil_; }

private:
    unsigned int fbo_ = 0;
    unsigned int texture_ = 0;
    unsigned int rbo_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool hasDepthStencil_ = false;
};

} // namespace fate
