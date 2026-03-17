#include "engine/render/render_graph.h"
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
    for (auto& pass : passes_) {
        if (!pass.enabled) continue;
        pass.execute(ctx);
    }
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
