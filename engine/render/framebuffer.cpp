#include "engine/render/framebuffer.h"
#include "engine/core/logger.h"

namespace fate {

bool Framebuffer::create(int width, int height) {
    if (width <= 0 || height <= 0) return false;

    width_ = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Framebuffer", "FBO incomplete: 0x%x", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOG_INFO("Framebuffer", "Created FBO %u (%dx%d)", fbo_, width_, height_);
    return true;
}

void Framebuffer::destroy() {
    if (texture_) { glDeleteTextures(1, &texture_); texture_ = 0; }
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    width_ = 0;
    height_ = 0;
}

void Framebuffer::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;
    destroy();
    create(width, height);
}

void Framebuffer::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void Framebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace fate
