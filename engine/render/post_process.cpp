#include "engine/render/post_process.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/render/gl_loader.h"
#include "engine/core/logger.h"

namespace fate {

static Shader s_bloomExtractShader;
static Shader s_blurShader;
static Shader s_postProcessShader;
static bool s_shadersLoaded = false;

static void ensureShaders() {
    if (s_shadersLoaded) return;
    const char* vert = "assets/shaders/fullscreen_quad.vert";
    bool ok = true;
    ok &= s_bloomExtractShader.loadFromFile(vert, "assets/shaders/bloom_extract.frag");
    ok &= s_blurShader.loadFromFile(vert, "assets/shaders/blur.frag");
    ok &= s_postProcessShader.loadFromFile(vert, "assets/shaders/postprocess.frag");
    s_shadersLoaded = ok;
    if (!ok) LOG_ERROR("PostProcess", "Failed to load one or more post-process shaders");
}

void registerPostProcessPasses(RenderGraph& graph, PostProcessConfig& config) {
    // Pass: Bloom Extract
    graph.addPass({"BloomExtract", true, [&config](RenderPassContext& ctx) {
        if (!config.bloomEnabled) return;
        ensureShaders();
        if (!s_shadersLoaded) return;

        int halfW = ctx.viewportWidth / 2;
        int halfH = ctx.viewportHeight / 2;

        auto& scene = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        auto& bloomDS = ctx.graph->getFBO("BloomDownsample", halfW, halfH);

        bloomDS.bind();
        glClear(GL_COLOR_BUFFER_BIT);

        s_bloomExtractShader.bind();
        s_bloomExtractShader.setInt("u_scene", 0);
        s_bloomExtractShader.setFloat("u_threshold", config.bloomThreshold);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene.textureId());

        FullscreenQuad::instance().draw();

        s_bloomExtractShader.unbind();
        bloomDS.unbind();
    }});

    // Pass: Bloom Blur (horizontal + vertical ping-pong)
    graph.addPass({"BloomBlur", true, [&config](RenderPassContext& ctx) {
        if (!config.bloomEnabled) return;
        ensureShaders();
        if (!s_shadersLoaded) return;

        int halfW = ctx.viewportWidth / 2;
        int halfH = ctx.viewportHeight / 2;

        auto& bloomDS = ctx.graph->getFBO("BloomDownsample", halfW, halfH);
        auto& blurH = ctx.graph->getFBO("BloomBlurH", halfW, halfH);
        auto& blurV = ctx.graph->getFBO("BloomBlurV", halfW, halfH);

        s_blurShader.bind();
        s_blurShader.setInt("u_texture", 0);

        // Horizontal pass
        blurH.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        s_blurShader.setVec2("u_direction", {1.0f / (float)halfW, 0.0f});
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomDS.textureId());
        FullscreenQuad::instance().draw();
        blurH.unbind();

        // Vertical pass
        blurV.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        s_blurShader.setVec2("u_direction", {0.0f, 1.0f / (float)halfH});
        glBindTexture(GL_TEXTURE_2D, blurH.textureId());
        FullscreenQuad::instance().draw();
        blurV.unbind();

        s_blurShader.unbind();
    }});

    // Pass: Post-Process Composite
    graph.addPass({"PostProcess", true, [&config](RenderPassContext& ctx) {
        ensureShaders();
        if (!s_shadersLoaded) return;

        int w = ctx.viewportWidth;
        int h = ctx.viewportHeight;

        auto& scene = ctx.graph->getFBO("Scene", w, h, true);
        auto& blurV = ctx.graph->getFBO("BloomBlurV", w / 2, h / 2);
        auto& postProcess = ctx.graph->getFBO("PostProcess", w, h);

        postProcess.bind();
        glClear(GL_COLOR_BUFFER_BIT);

        s_postProcessShader.bind();
        s_postProcessShader.setInt("u_scene", 0);
        s_postProcessShader.setInt("u_bloom", 1);
        s_postProcessShader.setFloat("u_bloomStrength", config.bloomEnabled ? config.bloomStrength : 0.0f);
        s_postProcessShader.setFloat("u_vignetteRadius", config.vignetteEnabled ? config.vignetteRadius : 10.0f);
        s_postProcessShader.setFloat("u_vignetteSmooth", config.vignetteSmoothness);
        s_postProcessShader.setVec3("u_colorTint", {config.colorTint.r, config.colorTint.g, config.colorTint.b});
        s_postProcessShader.setFloat("u_brightness", config.brightness);
        s_postProcessShader.setFloat("u_contrast", config.contrast);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene.textureId());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, blurV.textureId());

        FullscreenQuad::instance().draw();

        glActiveTexture(GL_TEXTURE0); // reset active unit
        s_postProcessShader.unbind();
        postProcess.unbind();
    }});
}

} // namespace fate
