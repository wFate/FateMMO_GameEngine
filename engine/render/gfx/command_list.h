#pragma once
#include "engine/render/gfx/types.h"
#include "engine/core/types.h"

namespace gfx {

class CommandList {
public:
    void begin();
    void end();

    // Render target (FramebufferHandle{0} = default backbuffer)
    void setFramebuffer(FramebufferHandle fb);
    void setViewport(int x, int y, int w, int h);
    void clear(float r, float g, float b, float a, bool clearDepth = false);

    // Pipeline and resources
    void bindPipeline(PipelineHandle pipeline);
    void bindTexture(int slot, TextureHandle texture);
    void bindVertexBuffer(BufferHandle buffer);
    void bindIndexBuffer(BufferHandle buffer);

    // Uniforms (cached location lookup via Device)
    void setUniform(const char* name, float value);
    void setUniform(const char* name, int value);
    void setUniform(const char* name, const fate::Vec2& value);
    void setUniform(const char* name, const fate::Vec3& value);
    void setUniform(const char* name, const fate::Color& value);
    void setUniform(const char* name, const fate::Mat4& value);

    // Draw
    void draw(PrimitiveType type, int vertexCount, int firstVertex = 0);
    void drawIndexed(PrimitiveType type, int indexCount, int firstIndex = 0);

    void submit();

#ifdef FATEMMO_METAL
    void* currentEncoder() const;       // returns id<MTLRenderCommandEncoder>
    void  setDrawable(void* drawable);  // sets the CAMetalDrawable for screen rendering
#endif

private:
    PipelineHandle currentPipeline_{};
};

} // namespace gfx
