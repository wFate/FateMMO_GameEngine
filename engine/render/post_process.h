/**************************************************************************/
/*  post_process.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
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
