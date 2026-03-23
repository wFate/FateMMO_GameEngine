#pragma once
// ============================================================================
// Editor Shim — provides no-op Editor::instance() for FATE_SHIPPING builds
// so game code compiles without #ifdef guards at every call site.
// In shipping builds, loadScene actually loads via PrefabLibrary (no editor UI).
// ============================================================================

#ifdef FATE_SHIPPING

#include "engine/core/types.h"
#include "engine/core/logger.h"
#include "engine/ecs/world.h"
#include "engine/ecs/prefab.h"
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fate {

class Editor {
public:
    static Editor& instance() {
        static Editor s;
        return s;
    }

    // All editor queries return safe defaults in shipping builds
    bool isPaused() const { return false; }
    bool isOpen() const { return false; }
    bool wantsKeyboard() const { return false; }
    bool showCollisionDebug() const { return false; }
    bool isTilePaintMode() const { return false; }
    bool isEraseMode() const { return false; }
    bool inPlayMode() const { return false; }

    Vec2 viewportPos() const { return {0, 0}; }
    Vec2 viewportSize() const {
        // Shipping has no editor viewport — callers use windowWidth/Height directly.
        return {0, 0};
    }
    const std::string& currentScenePath() const { return currentScene_; }

    void setAssetRoot(const std::string&) {}
    void setSourceDir(const std::string&) {}

    void loadScene(World* world, const std::string& path) {
        if (!world) return;
        // Destroy existing entities
        std::vector<EntityHandle> toDestroy;
        world->forEachEntity([&](Entity* e) { toDestroy.push_back(e->handle()); });
        for (auto h : toDestroy) world->destroyEntity(h);
        world->processDestroyQueue();

        // Load from JSON
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("Game", "Cannot open scene: %s", path.c_str());
            return;
        }
        nlohmann::json root = nlohmann::json::parse(file);
        if (root.contains("entities")) {
            for (auto& entityJson : root["entities"]) {
                PrefabLibrary::jsonToEntity(entityJson, *world);
            }
        }
        currentScene_ = path;
        LOG_INFO("Game", "Scene loaded from %s", path.c_str());
    }

private:
    Editor() = default;
    std::string currentScene_;
};

} // namespace fate

#else
#include "engine/editor/editor.h"
#endif
