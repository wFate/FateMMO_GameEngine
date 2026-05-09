/**************************************************************************/
/*  render_graph.h                                                        */
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
#include "engine/render/framebuffer.h"
#include "engine/render/gfx/command_list.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace fate {

class SpriteBatch;
class Camera;
class World;
class RenderGraph;

struct RenderPassContext {
    SpriteBatch* spriteBatch = nullptr;
    Camera* camera = nullptr;
    World* world = nullptr;
    RenderGraph* graph = nullptr;
    gfx::CommandList* commandList = nullptr;
    int viewportWidth = 0;
    int viewportHeight = 0;
};

struct RenderPass {
    std::string name;
    bool enabled = true;
    std::function<void(RenderPassContext& ctx)> execute;
};

class RenderGraph {
public:
    void addPass(RenderPass pass);
    void removePass(const std::string& name);
    void setPassEnabled(const std::string& name, bool enabled);

    void execute(RenderPassContext& ctx);

    // FBO pool — get or create by name
    Framebuffer& getFBO(const std::string& name, int width, int height, bool withDepthStencil = false);

    // Destroy all pooled FBOs (call before GL context teardown)
    void clearFBOs();

    // Destroy all passes (call before tearing down objects captured by lambdas)
    void clearPasses();

    const std::vector<RenderPass>& passes() const { return passes_; }

private:
    std::vector<RenderPass> passes_;
    std::unordered_map<std::string, std::unique_ptr<Framebuffer>> fboPool_;
};

} // namespace fate
