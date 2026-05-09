/**************************************************************************/
/*  render_graph.cpp                                                      */
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
#include "engine/render/render_graph.h"
#include "engine/render/sprite_batch.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

void RenderGraph::addPass(RenderPass pass) {
    passes_.push_back(std::move(pass));
}

void RenderGraph::removePass(const std::string& name) {
    passes_.erase(
        std::remove_if(passes_.begin(), passes_.end(),
            [&](const RenderPass& p) { return p.name == name; }),
        passes_.end()
    );
}

void RenderGraph::setPassEnabled(const std::string& name, bool enabled) {
    for (auto& pass : passes_) {
        if (pass.name == name) {
            pass.enabled = enabled;
            return;
        }
    }
}

void RenderGraph::execute(RenderPassContext& ctx) {
    ctx.graph = this;
    gfx::CommandList cmdList;
    ctx.commandList = &cmdList;

    // Wire up CommandList to SpriteBatch so it uses gfx-abstracted draws
    if (ctx.spriteBatch) {
        ctx.spriteBatch->setCommandList(&cmdList);
    }

    for (auto& pass : passes_) {
        if (!pass.enabled) continue;
        pass.execute(ctx);
    }

    // Disconnect CommandList (stack-local) so SpriteBatch doesn't dangle
    if (ctx.spriteBatch) {
        ctx.spriteBatch->setCommandList(nullptr);
    }
}

void RenderGraph::clearPasses() {
    passes_.clear();
}

void RenderGraph::clearFBOs() {
    for (auto& [name, fbo] : fboPool_) {
        fbo->destroy();
    }
    fboPool_.clear();
}

Framebuffer& RenderGraph::getFBO(const std::string& name, int width, int height, bool withDepthStencil) {
    auto it = fboPool_.find(name);
    if (it != fboPool_.end()) {
        auto& fbo = *it->second;
        if (fbo.width() != width || fbo.height() != height) {
            fbo.resize(width, height);
        }
        return fbo;
    }

    auto fbo = std::make_unique<Framebuffer>();
    fbo->create(width, height, withDepthStencil);
    auto& ref = *fbo;
    fboPool_[name] = std::move(fbo);
    return ref;
}

} // namespace fate
