/**************************************************************************/
/*  camera_follow_system.h                                                */
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
// engine/gameplay2d/systems/camera_follow_system.h
//
// Picks the highest-priority enabled CameraFollow2D entity and writes the
// host App's Camera position from its Transform. The Camera pointer is
// installed by the host (demo_app::onInit calls setCamera(&camera())) so the
// system stays decoupled from App.
//
// Snap mode = camera = entity each frame. Smooth mode lerps with rate.
// Deadzone is checked in screen pixels; movement inside the deadzone is
// ignored (camera stays put).

#pragma once

#include "engine/ecs/world.h"

namespace fate {

class Camera;

class CameraFollow2DSystem : public System {
public:
    const char* name() const override { return "CameraFollow2DSystem"; }

    void setCamera(Camera* cam) { camera_ = cam; }
    void update(float dt) override;

private:
    Camera* camera_ = nullptr;
};

} // namespace fate
