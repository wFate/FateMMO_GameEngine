#include "engine/render/post_process.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/command_list.h"
#include "engine/core/logger.h"

namespace fate {

static Shader s_bloomExtractShader;
static Shader s_blurShader;
static Shader s_postProcessShader;
static bool s_shadersLoaded = false;

// Pipeline handles — one per shader+blend variant
static gfx::PipelineHandle s_bloomExtractPipeline;  // no blend
static gfx::PipelineHandle s_blurPipeline;           // no blend
static gfx::PipelineHandle s_postProcessPipeline;    // no blend

static void ensureShaders() {
    if (s_shadersLoaded) return;
    const char* vert = "assets/shaders/fullscreen_quad.vert";
    bool ok = true;
    ok &= s_bloomExtractShader.loadFromFile(vert, "assets/shaders/bloom_extract.frag");
    ok &= s_blurShader.loadFromFile(vert, "assets/shaders/blur.frag");
    ok &= s_postProcessShader.loadFromFile(vert, "assets/shaders/postprocess.frag");
    s_shadersLoaded = ok;
    if (!ok) {
        LOG_ERROR("PostProcess", "Failed to load one or more post-process shaders");
        return;
    }

    // Create pipelines with appropriate blend modes
    auto& device = gfx::Device::instance();

    gfx::PipelineDesc desc{};
    desc.blendMode = gfx::BlendMode::None;

    desc.shader = s_bloomExtractShader.gfxHandle();
    s_bloomExtractPipeline = device.createPipeline(desc);

    desc.shader = s_blurShader.gfxHandle();
    s_blurPipeline = device.createPipeline(desc);

    desc.shader = s_postProcessShader.gfxHandle();
    s_postProcessPipeline = device.createPipeline(desc);
}

void registerPostProcessPasses(RenderGraph& graph, PostProcessConfig& config) {
    // Pass: Bloom Extract
    graph.addPass({"BloomExtract", true, [&config](RenderPassContext& ctx) {
        if (!config.bloomEnabled) return;
        ensureShaders();
        if (!s_shadersLoaded) return;

        auto* cmd = ctx.commandList;
        auto& device = gfx::Device::instance();
        int halfW = ctx.viewportWidth / 2;
        int halfH = ctx.viewportHeight / 2;

        auto& scene = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        auto& bloomDS = ctx.graph->getFBO("BloomDownsample", halfW, halfH);

        cmd->setFramebuffer(bloomDS.gfxHandle());
        cmd->setViewport(0, 0, halfW, halfH);
        cmd->clear(0.0f, 0.0f, 0.0f, 1.0f);

        cmd->bindPipeline(s_bloomExtractPipeline);
        cmd->setUniform("u_scene", 0);
        cmd->setUniform("u_threshold", config.bloomThreshold);

        gfx::TextureHandle sceneTex = device.getFramebufferTexture(scene.gfxHandle());
        cmd->bindTexture(0, sceneTex);

        cmd->draw(gfx::PrimitiveType::Triangles, 3);
    }});

    // Pass: Bloom Blur (horizontal + vertical ping-pong)
    graph.addPass({"BloomBlur", true, [&config](RenderPassContext& ctx) {
        if (!config.bloomEnabled) return;
        ensureShaders();
        if (!s_shadersLoaded) return;

        auto* cmd = ctx.commandList;
        auto& device = gfx::Device::instance();
        int halfW = ctx.viewportWidth / 2;
        int halfH = ctx.viewportHeight / 2;

        auto& bloomDS = ctx.graph->getFBO("BloomDownsample", halfW, halfH);
        auto& blurH = ctx.graph->getFBO("BloomBlurH", halfW, halfH);
        auto& blurV = ctx.graph->getFBO("BloomBlurV", halfW, halfH);

        cmd->bindPipeline(s_blurPipeline);
        cmd->setUniform("u_texture", 0);

        // Horizontal pass
        cmd->setFramebuffer(blurH.gfxHandle());
        cmd->setViewport(0, 0, halfW, halfH);
        cmd->clear(0.0f, 0.0f, 0.0f, 1.0f);
        cmd->setUniform("u_direction", Vec2{1.0f / (float)halfW, 0.0f});
        gfx::TextureHandle bloomDSTex = device.getFramebufferTexture(bloomDS.gfxHandle());
        cmd->bindTexture(0, bloomDSTex);
        cmd->draw(gfx::PrimitiveType::Triangles, 3);

        // Vertical pass
        cmd->setFramebuffer(blurV.gfxHandle());
        cmd->setViewport(0, 0, halfW, halfH);
        cmd->clear(0.0f, 0.0f, 0.0f, 1.0f);
        cmd->setUniform("u_direction", Vec2{0.0f, 1.0f / (float)halfH});
        gfx::TextureHandle blurHTex = device.getFramebufferTexture(blurH.gfxHandle());
        cmd->bindTexture(0, blurHTex);
        cmd->draw(gfx::PrimitiveType::Triangles, 3);
    }});

    // Pass: Post-Process Composite
    graph.addPass({"PostProcess", true, [&config](RenderPassContext& ctx) {
        ensureShaders();
        if (!s_shadersLoaded) return;

        auto* cmd = ctx.commandList;
        auto& device = gfx::Device::instance();
        int w = ctx.viewportWidth;
        int h = ctx.viewportHeight;

        auto& scene = ctx.graph->getFBO("Scene", w, h, true);
        auto& blurV = ctx.graph->getFBO("BloomBlurV", w / 2, h / 2);
        auto& postProcess = ctx.graph->getFBO("PostProcess", w, h);

        cmd->setFramebuffer(postProcess.gfxHandle());
        cmd->setViewport(0, 0, w, h);
        cmd->clear(0.0f, 0.0f, 0.0f, 1.0f);

        cmd->bindPipeline(s_postProcessPipeline);
        cmd->setUniform("u_scene", 0);
        cmd->setUniform("u_bloom", 1);
        cmd->setUniform("u_bloomStrength", config.bloomEnabled ? config.bloomStrength : 0.0f);
        cmd->setUniform("u_vignetteRadius", config.vignetteEnabled ? config.vignetteRadius : 10.0f);
        cmd->setUniform("u_vignetteSmooth", config.vignetteSmoothness);
        cmd->setUniform("u_colorTint", Vec3{config.colorTint.r, config.colorTint.g, config.colorTint.b});
        cmd->setUniform("u_brightness", config.brightness);
        cmd->setUniform("u_contrast", config.contrast);

        gfx::TextureHandle sceneTex = device.getFramebufferTexture(scene.gfxHandle());
        cmd->bindTexture(0, sceneTex);
        gfx::TextureHandle blurVTex = device.getFramebufferTexture(blurV.gfxHandle());
        cmd->bindTexture(1, blurVTex);

        cmd->draw(gfx::PrimitiveType::Triangles, 3);
    }});
}

} // namespace fate
