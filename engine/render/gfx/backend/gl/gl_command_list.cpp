#include "engine/render/gfx/command_list.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"

namespace gfx {

// Helper: convert PrimitiveType to GL enum
static GLenum toGLPrimitive(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Triangles: return GL_TRIANGLES;
        case PrimitiveType::Lines:     return GL_LINES;
        case PrimitiveType::Points:    return GL_POINTS;
    }
    return GL_TRIANGLES;
}

void CommandList::begin() {
    currentPipeline_ = {};
}

void CommandList::end() {
    // No-op for immediate mode
}

void CommandList::setFramebuffer(FramebufferHandle fb) {
    if (fb.id == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        GLuint glFb = Device::instance().resolveGLFramebuffer(fb);
        glBindFramebuffer(GL_FRAMEBUFFER, glFb);
    }
}

void CommandList::setViewport(int x, int y, int w, int h) {
    glViewport(x, y, w, h);
}

void CommandList::clear(float r, float g, float b, float a, bool clearDepth) {
    glClearColor(r, g, b, a);
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (clearDepth) {
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
}

void CommandList::bindPipeline(PipelineHandle pipeline) {
    currentPipeline_ = pipeline;
    auto& dev = Device::instance();

    // Bind shader program
    GLuint program = dev.resolveGLShader(dev.resolvePipelineDesc(pipeline)->shader);
    glUseProgram(program);

    // Bind VAO
    GLuint vao = dev.resolveGLPipelineVAO(pipeline);
    glBindVertexArray(vao);

    // Blend mode
    const PipelineDesc* desc = dev.resolvePipelineDesc(pipeline);
    switch (desc->blendMode) {
        case BlendMode::None:
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BlendMode::Multiplicative:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
    }

    // Depth state
    if (desc->depthTest) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(desc->depthWrite ? GL_TRUE : GL_FALSE);
}

void CommandList::bindTexture(int slot, TextureHandle texture) {
    glActiveTexture(GL_TEXTURE0 + slot);
    GLuint glTex = Device::instance().resolveGLTexture(texture);
    glBindTexture(GL_TEXTURE_2D, glTex);
}

void CommandList::bindVertexBuffer(BufferHandle buffer) {
    GLuint glBuf = Device::instance().resolveGLBuffer(buffer);
    glBindBuffer(GL_ARRAY_BUFFER, glBuf);

    // Set up vertex attributes from the current pipeline's VertexLayout
    if (currentPipeline_.valid()) {
        const PipelineDesc* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
        if (desc) {
            for (const auto& attr : desc->vertexLayout.attributes) {
                glEnableVertexAttribArray(attr.location);
                glVertexAttribPointer(
                    attr.location,
                    attr.components,
                    GL_FLOAT,
                    attr.normalized ? GL_TRUE : GL_FALSE,
                    static_cast<GLsizei>(desc->vertexLayout.stride),
                    reinterpret_cast<const void*>(attr.offset)
                );
            }
        }
    }
}

void CommandList::bindIndexBuffer(BufferHandle buffer) {
    GLuint glBuf = Device::instance().resolveGLBuffer(buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuf);
}

void CommandList::setUniform(const char* name, float value) {
    if (!currentPipeline_.valid()) return;
    const PipelineDesc* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform1f(loc, value);
}

void CommandList::setUniform(const char* name, int value) {
    if (!currentPipeline_.valid()) return;
    const PipelineDesc* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform1i(loc, value);
}

void CommandList::setUniform(const char* name, const fate::Vec2& value) {
    if (!currentPipeline_.valid()) return;
    const PipelineDesc* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform2f(loc, value.x, value.y);
}

void CommandList::setUniform(const char* name, const fate::Vec3& value) {
    if (!currentPipeline_.valid()) return;
    const PipelineDesc* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform3f(loc, value.x, value.y, value.z);
}

void CommandList::setUniform(const char* name, const fate::Color& value) {
    if (!currentPipeline_.valid()) return;
    const PipelineDesc* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniform4f(loc, value.r, value.g, value.b, value.a);
}

void CommandList::setUniform(const char* name, const fate::Mat4& value) {
    if (!currentPipeline_.valid()) return;
    const PipelineDesc* desc = Device::instance().resolvePipelineDesc(currentPipeline_);
    int loc = Device::instance().getUniformLocation(desc->shader, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, value.data());
}

void CommandList::setUniformBlock(const void* /*data*/, std::size_t /*bytes*/) {
    // GL: no-op. The sprite shader is wired through named per-field setUniform
    // calls; a future UBO migration can revisit this. Kept on the API so
    // SpriteBatch can call it unconditionally and let Metal pick it up.
}

void CommandList::draw(PrimitiveType type, int vertexCount, int firstVertex) {
    glDrawArrays(toGLPrimitive(type), firstVertex, vertexCount);
}

void CommandList::drawIndexed(PrimitiveType type, int indexCount, int firstIndex) {
    const void* offset = reinterpret_cast<const void*>(
        static_cast<size_t>(firstIndex) * sizeof(unsigned int)
    );
    glDrawElements(toGLPrimitive(type), indexCount, GL_UNSIGNED_INT, offset);
}

void CommandList::submit() {
    // No-op for immediate mode
}

} // namespace gfx
