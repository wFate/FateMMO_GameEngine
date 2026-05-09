/**************************************************************************/
/*  lighting.h                                                            */
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
