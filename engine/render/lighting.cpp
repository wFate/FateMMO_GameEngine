#include "engine/render/lighting.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/render/gl_loader.h"
#include "engine/ecs/world.h"
#include "engine/core/logger.h"

namespace fate {

static Shader s_lightShader;
static bool s_lightShaderLoaded = false;

static void ensureLightShader() {
    if (s_lightShaderLoaded) return;
    s_lightShaderLoaded = s_lightShader.loadFromFile(
        "assets/shaders/fullscreen_quad.vert",
        "assets/shaders/light.frag"
    );
    if (!s_lightShaderLoaded) {
        LOG_ERROR("Lighting", "Failed to load light shader");
    }
}

void registerLightingPass(RenderGraph& graph, LightingConfig& config) {
    graph.addPass({"Lighting", true, [&config](RenderPassContext& ctx) {
        if (!config.enabled || !ctx.world) return;

        ensureLightShader();
        if (!s_lightShaderLoaded) return;

        int w = ctx.viewportWidth;
        int h = ctx.viewportHeight;

        // Build light map
        auto& lightMap = ctx.graph->getFBO("LightMap", w, h);
        lightMap.bind();

        // Clear to ambient
        glClearColor(
            config.ambientColor.r * config.ambientIntensity,
            config.ambientColor.g * config.ambientIntensity,
            config.ambientColor.b * config.ambientIntensity,
            1.0f
        );
        glClear(GL_COLOR_BUFFER_BIT);

        // Additive blending for point lights
        glBlendFunc(GL_ONE, GL_ONE);

        s_lightShader.bind();
        s_lightShader.setVec2("u_resolution", {(float)w, (float)h});

        // Iterate PointLightComponents — this requires the component to be available
        // The actual forEach happens in the game layer; here we provide the rendering logic
        // For now, store lights in a temporary vector filled by the game before graph execution
        // This will be wired in Task 8 when we integrate with the game

        s_lightShader.unbind();
        lightMap.unbind();

        // Composite light map onto scene via multiplicative blend
        static Shader s_blitShader;
        static bool s_blitLoaded = false;
        if (!s_blitLoaded) {
            s_blitLoaded = s_blitShader.loadFromFile(
                "assets/shaders/fullscreen_quad.vert",
                "assets/shaders/blit.frag"
            );
        }

        auto& scene = ctx.graph->getFBO("Scene", w, h, true);
        scene.bind();
        glBlendFunc(GL_DST_COLOR, GL_ZERO); // multiplicative

        s_blitShader.bind();
        s_blitShader.setInt("u_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, lightMap.textureId());
        FullscreenQuad::instance().draw();
        s_blitShader.unbind();

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // restore
        scene.unbind();
    }});
}

} // namespace fate
