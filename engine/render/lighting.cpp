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

        // Count point lights so we know whether the light map is trivially white
        int lightCount = 0;

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
                ++lightCount;

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

        // Optimization: if the light map is solid white (ambient is white and
        // no point lights exist), multiplying the scene by it is a no-op.
        // Skip the composite draw entirely to save GPU time.
        float ar = config.ambientColor.r * config.ambientIntensity;
        float ag = config.ambientColor.g * config.ambientIntensity;
        float ab = config.ambientColor.b * config.ambientIntensity;
        bool ambientIsWhite = (ar >= 1.0f && ag >= 1.0f && ab >= 1.0f);

        if (ambientIsWhite && lightCount == 0) {
            // Restore standard alpha blending before early exit
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            return; // light map is solid white — composite is a no-op
        }

        // Composite light map onto scene via multiplicative blend
        // (Scene *= LightMap): use DST_COLOR * SRC_COLOR + 0 * DST_COLOR
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

        // Multiplicative blend: existing scene color * light map color
        glEnable(GL_BLEND);
        glBlendFunc(GL_DST_COLOR, GL_ZERO);

        s_blitShader.bind();
        s_blitShader.setInt("u_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, lightMap.textureId());
        FullscreenQuad::instance().draw();
        s_blitShader.unbind();

        // Restore standard alpha blending and clear color
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        scene.unbind();
    }});
}

} // namespace fate
