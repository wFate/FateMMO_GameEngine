#pragma once
#include "engine/core/types.h"
#include "engine/render/shader.h"
#include "engine/render/render_graph.h"

namespace fate {

struct PostProcessConfig {
    bool bloomEnabled = true;
    float bloomThreshold = 0.8f;
    float bloomStrength = 0.3f;

    bool vignetteEnabled = false;
    float vignetteRadius = 0.85f;
    float vignetteSmoothness = 0.25f;

    Color colorTint = Color::white();
    float brightness = 1.0f;
    float contrast = 1.0f;
};

// Registers bloom extract, blur, and composite passes with the render graph
void registerPostProcessPasses(RenderGraph& graph, PostProcessConfig& config);

} // namespace fate
