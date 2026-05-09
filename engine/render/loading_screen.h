/**************************************************************************/
/*  loading_screen.h                                                      */
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
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include "engine/render/texture.h"
#include <string>
#include <memory>

namespace fate {

class LoadingScreen {
public:
    void begin(const std::string& sceneName, int screenWidth, int screenHeight);
    void render(SpriteBatch& batch, SDFText& sdf, float progress,
                int screenWidth, int screenHeight);
    void end();

private:
    std::string sceneName_;
    std::string displayName_;
    std::shared_ptr<Texture> backgroundTex_;
};

} // namespace fate
