/**************************************************************************/
/*  nameplate_render_system.h                                             */
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
// engine/gameplay2d/systems/nameplate_render_system.h
//
// Render-only helper. NOT a regular System — the demo's onRender calls
// render(world, batch, camera) inside its own batch.begin/.end pair so the
// nameplates participate in the existing 2D draw order.
//
// Output is colored quads only: backing rect + HP bar fill. Text is rendered
// via SDFText when an atlas is initialized; if the demo never installed a
// font, the nameplates degrade gracefully to bar-only.

#pragma once

#include "engine/core/types.h"

namespace fate {

class World;
class SpriteBatch;
class Camera;

class NameplateRenderSystem {
public:
    void render(World& world, SpriteBatch& batch, Camera& camera);

    // Optional: cull nameplates whose entity is farther than this from the
    // camera center (in world pixels). 0 = no culling.
    void setMaxDistance(float pixels) { maxDistance_ = pixels; }

private:
    float maxDistance_ = 0.0f;
};

} // namespace fate
