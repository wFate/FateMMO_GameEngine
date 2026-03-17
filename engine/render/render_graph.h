#pragma once
#include "engine/render/framebuffer.h"
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

    const std::vector<RenderPass>& passes() const { return passes_; }

private:
    std::vector<RenderPass> passes_;
    std::unordered_map<std::string, std::unique_ptr<Framebuffer>> fboPool_;
};

} // namespace fate
