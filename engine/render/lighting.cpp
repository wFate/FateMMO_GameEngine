#include "engine/render/lighting.h"
#include "engine/render/point_light_component.h"
#include "engine/render/fullscreen_quad.h"
#include "engine/render/gl_loader.h"
#include "engine/ecs/world.h"
#include "engine/render/camera.h"
#include "game/components/transform.h"
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

        // Iterate all entities with PointLightComponent + Transform
        Mat4 vp = ctx.camera->getViewProjection();
        float cameraZoom = ctx.camera->zoom();

        ctx.world->forEach<PointLightComponent, Transform>(
            [&](Entity*, PointLightComponent* plc, Transform* t) {
                const PointLight& light = plc->light;

                // Convert world position to NDC via view-projection matrix
                float px = t->position.x, py = t->position.y;
                float ndcX = vp.m[0]*px + vp.m[4]*py + vp.m[12];
                float ndcY = vp.m[1]*px + vp.m[5]*py + vp.m[13];
                // w component (for ortho projection w==1, but be safe)
                float ndcW = vp.m[3]*px + vp.m[7]*py + vp.m[15];
                if (ndcW != 0.0f) { ndcX /= ndcW; ndcY /= ndcW; }

                Vec2 screenPos = {
                    (ndcX * 0.5f + 0.5f) * (float)w,
                    (ndcY * 0.5f + 0.5f) * (float)h
                };

                // Convert world-space radius to screen-space using camera zoom
                float screenRadius = light.radius * cameraZoom;

                s_lightShader.setVec2("u_lightPos", screenPos);
                s_lightShader.setVec3("u_lightColor", {light.color.r, light.color.g, light.color.b});
                s_lightShader.setFloat("u_lightRadius", screenRadius);
                s_lightShader.setFloat("u_lightIntensity", light.intensity);
                s_lightShader.setFloat("u_lightFalloff", light.falloff);

                FullscreenQuad::instance().draw();
            });

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
        glDisable(GL_BLEND);

        s_blitShader.bind();
        s_blitShader.setInt("u_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, lightMap.textureId());
        FullscreenQuad::instance().draw();
        s_blitShader.unbind();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // restore
        scene.unbind();
    }});
}

} // namespace fate
