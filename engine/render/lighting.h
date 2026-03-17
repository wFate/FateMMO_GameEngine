#pragma once
#include "engine/core/types.h"
#include "engine/render/shader.h"
#include "engine/render/render_graph.h"

namespace fate {

struct PointLight {
    Vec2 position;
    Color color = {1.0f, 0.9f, 0.7f, 1.0f};
    float radius = 128.0f;
    float intensity = 1.0f;
    float falloff = 2.0f;
};

struct LightingConfig {
    Color ambientColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float ambientIntensity = 1.0f;
    bool enabled = true;
};

// Registers the lighting pass with the render graph
void registerLightingPass(RenderGraph& graph, LightingConfig& config);

} // namespace fate
