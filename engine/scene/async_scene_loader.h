#pragma once
#include "engine/asset/asset_registry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <memory>

namespace fate {

class World;

struct PendingSceneLoad {
    std::string sceneName;
    std::string jsonPath;

    nlohmann::json sceneMetadata;
    std::vector<nlohmann::json> prefabs;
    std::vector<std::string> texturePaths;
    std::vector<AssetHandle> textureHandles;
    std::atomic<bool> workerFailed{false};
    std::string errorMessage;        // Written BEFORE workerDone store(release); read AFTER workerDone load(acquire)

    std::atomic<float> workerProgress{0.0f};
    std::atomic<bool> workerDone{false};   // Release/acquire gate for all non-atomic fields above

    int totalEntities = 0;
    int createdEntities = 0;
    int totalTextures = 0;
    int loadedTextures = 0;
    bool worldCleared = false;
    bool texturesKicked = false;

    float progress() const;
};

class AsyncSceneLoader {
public:
    void startLoad(const std::string& sceneName, const std::string& jsonPath);
    bool tickFinalization(World& world);

    // Clear active/pending state so startLoad can run again. Call after
    // observing hasFailed() and reporting the error — otherwise the loader
    // stays "active" and all future startLoad() calls are silently ignored.
    void reset();

    bool isWorkerDone() const;
    bool hasFailed() const;
    const std::string& errorMessage() const;
    float progress() const;
    const std::string& sceneName() const;
    const nlohmann::json& sceneMetadata() const;
    bool isActive() const { return active_; }
    PendingSceneLoad* pendingLoad() { return pending_.get(); }

private:
    std::unique_ptr<PendingSceneLoad> pending_;
    bool active_ = false;
};

} // namespace fate
