#include "engine/render/lighting.h"
#include "engine/render/point_light_component.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/command_list.h"
#include "engine/ecs/world.h"
#include "engine/render/camera.h"
#include "game/components/transform.h"
#include "engine/core/logger.h"
#include <mutex>

namespace fate {

static Shader s_lightShader;
static Shader s_blitShader;
static std::once_flag s_lightShaderFlag;
static std::once_flag s_blitShaderFlag;
static bool s_lightShaderLoaded = false;
static bool s_blitShaderLoaded = false;

// Pipeline handles for each blend mode variant
static gfx::PipelineHandle s_lightAdditivePipeline;
static gfx::PipelineHandle s_blitMultiplicativePipeline;
static gfx::PipelineHandle s_blitAlphaPipeline;  // for restoring standard alpha state

static void ensureLightShader() {
    std::call_once(s_lightShaderFlag, []() {
        if (!s_lightShader.loadFromFile(
                "assets/shaders/fullscreen_quad.vert",
                "assets/shaders/light.frag")) {
            LOG_ERROR("Lighting", "Failed to load light shader");
            return;
        }
        auto& device = gfx::Device::instance();
        gfx::PipelineDesc desc{};
        desc.shader = s_lightShader.gfxHandle();
        desc.blendMode = gfx::BlendMode::Additive;
        s_lightAdditivePipeline = device.createPipeline(desc);
        s_lightShaderLoaded = true;
    });
}

static void ensureBlitShader() {
    std::call_once(s_blitShaderFlag, []() {
        if (!s_blitShader.loadFromFile(
                "assets/shaders/fullscreen_quad.vert",
                "assets/shaders/blit.frag")) {
            LOG_ERROR("Lighting", "Failed to load blit shader");
            return;
        }
        auto& device = gfx::Device::instance();
        gfx::PipelineDesc desc{};
        desc.shader = s_blitShader.gfxHandle();
        desc.blendMode = gfx::BlendMode::Multiplicative;
        s_blitMultiplicativePipeline = device.createPipeline(desc);
        s_blitShaderLoaded = true;
    });
}

void registerLightingPass(RenderGraph& graph, LightingConfig& config) {
    graph.addPass({"Lighting", true, [&config](RenderPassContext& ctx) {
        if (!config.enabled || !ctx.world) return;

        ensureLightShader();
        if (!s_lightShaderLoaded) return;
        ensureBlitShader();
        if (!s_blitShaderLoaded) return;

        auto* cmd = ctx.commandList;
        auto& device = gfx::Device::instance();
        int w = ctx.viewportWidth;
        int h = ctx.viewportHeight;

        // Build light map
        auto& lightMap = ctx.graph->getFBO("LightMap", w, h);
        cmd->setFramebuffer(lightMap.gfxHandle());
        cmd->setViewport(0, 0, w, h);

        // Clear to ambient
        cmd->clear(
            config.ambientColor.r * config.ambientIntensity,
            config.ambientColor.g * config.ambientIntensity,
            config.ambientColor.b * config.ambientIntensity,
            1.0f
        );

        // Count point lights so we know whether the light map is trivially white
        int lightCount = 0;

        // Bind additive pipeline for point lights
        cmd->bindPipeline(s_lightAdditivePipeline);

        // Iterate all entities with PointLightComponent + Transform
        Mat4 vp = ctx.camera->getViewProjection();
        float cameraZoom = ctx.camera->zoom();
        Vec2 resolution{(float)w, (float)h};

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

                // Uniform order must match LightUniforms MSL struct field order:
                // {u_lightPos, u_lightColor, u_lightRadius, u_lightIntensity,
                //  u_lightFalloff, u_resolution}
                // The scratch buffer resets after each draw, so all uniforms
                // for this draw must be set here.
                cmd->setUniform("u_lightPos", screenPos);
                cmd->setUniform("u_lightColor", Vec3{light.color.r, light.color.g, light.color.b});
                cmd->setUniform("u_lightRadius", screenRadius);
                cmd->setUniform("u_lightIntensity", light.intensity);
                cmd->setUniform("u_lightFalloff", light.falloff);
                cmd->setUniform("u_resolution", resolution);

                cmd->draw(gfx::PrimitiveType::Triangles, 3);
            });

        // Optimization: if the light map is solid white (ambient is white and
        // no point lights exist), multiplying the scene by it is a no-op.
        // Skip the composite draw entirely to save GPU time.
        float ar = config.ambientColor.r * config.ambientIntensity;
        float ag = config.ambientColor.g * config.ambientIntensity;
        float ab = config.ambientColor.b * config.ambientIntensity;
        bool ambientIsWhite = (ar >= 1.0f && ag >= 1.0f && ab >= 1.0f);

        if (ambientIsWhite && lightCount == 0) {
            return; // light map is solid white — composite is a no-op
        }

        // Composite light map onto scene via multiplicative blend
        auto& scene = ctx.graph->getFBO("Scene", w, h, true);
        cmd->setFramebuffer(scene.gfxHandle());
        cmd->setViewport(0, 0, w, h);

        cmd->bindPipeline(s_blitMultiplicativePipeline);
        cmd->setUniform("u_texture", 0);

        gfx::TextureHandle lightMapTex = device.getFramebufferTexture(lightMap.gfxHandle());
        cmd->bindTexture(0, lightMapTex);
        cmd->draw(gfx::PrimitiveType::Triangles, 3);
    }});
}

} // namespace fate
