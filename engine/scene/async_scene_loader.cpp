#include "engine/scene/async_scene_loader.h"
#include "engine/asset/asset_registry.h"
#include "engine/ecs/world.h"
#include "engine/ecs/prefab.h"
#include "engine/job/job_system.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_set>
#include <chrono>

namespace fate {

float PendingSceneLoad::progress() const {
    if (!workerDone.load()) return workerProgress.load();
    if (totalEntities == 0 && totalTextures == 0) return 1.0f;
    float entityProg = (totalEntities > 0)
        ? static_cast<float>(createdEntities) / totalEntities : 1.0f;
    float texProg = (totalTextures > 0)
        ? static_cast<float>(loadedTextures) / totalTextures : 1.0f;
    return 0.4f + entityProg * 0.3f + texProg * 0.3f;
}

static void asyncSceneLoadJob(void* param) {
    auto* pending = static_cast<PendingSceneLoad*>(param);

    pending->workerProgress.store(0.0f);
    std::ifstream file(pending->jsonPath);
    if (!file.is_open()) {
        pending->workerFailed = true;
        pending->errorMessage = "Cannot open scene: " + pending->jsonPath;
        pending->workerDone.store(true);
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(file);
    } catch (const nlohmann::json::exception& e) {
        pending->workerFailed = true;
        pending->errorMessage = std::string("JSON parse error: ") + e.what();
        pending->workerDone.store(true);
        return;
    }
    file.close();

    if (root.contains("metadata")) {
        pending->sceneMetadata = root["metadata"];
    }
    pending->workerProgress.store(0.2f);

    std::unordered_set<std::string> uniqueTextures;
    if (root.contains("entities") && root["entities"].is_array()) {
        for (auto& entityJson : root["entities"]) {
            pending->prefabs.push_back(std::move(entityJson));
            auto& last = pending->prefabs.back();
            if (last.contains("SpriteComponent")) {
                auto& sc = last["SpriteComponent"];
                if (sc.contains("texturePath") && sc["texturePath"].is_string()) {
                    std::string path = sc["texturePath"].get<std::string>();
                    if (!path.empty()) uniqueTextures.insert(path);
                }
            }
        }
    }

    pending->texturePaths.assign(uniqueTextures.begin(), uniqueTextures.end());
    pending->totalEntities = static_cast<int>(pending->prefabs.size());
    pending->totalTextures = static_cast<int>(pending->texturePaths.size());
    pending->workerProgress.store(0.4f);
    pending->workerDone.store(true);

    LOG_INFO("AsyncLoader", "Scene '%s': %d entities, %d unique textures",
             pending->sceneName.c_str(), pending->totalEntities, pending->totalTextures);
}

void AsyncSceneLoader::startLoad(const std::string& sceneName, const std::string& jsonPath) {
    if (active_) {
        LOG_WARN("AsyncLoader", "startLoad called while already active -- ignoring");
        return;
    }
    pending_ = std::make_unique<PendingSceneLoad>();
    pending_->sceneName = sceneName;
    pending_->jsonPath = jsonPath;
    active_ = true;

    Job job;
    job.function = &asyncSceneLoadJob;
    job.param = pending_.get();
    JobSystem::instance().submitFireAndForget(&job, 1);

    LOG_INFO("AsyncLoader", "Started async load: %s", sceneName.c_str());
}

bool AsyncSceneLoader::tickFinalization(World& world) {
    if (!active_ || !pending_ || !pending_->workerDone.load()) return false;
    if (pending_->workerFailed) return true;

    // First frame: kick off texture async loads on main thread
    if (!pending_->texturesKicked) {
        for (auto& path : pending_->texturePaths) {
            AssetHandle h = AssetRegistry::instance().loadAsync(path);
            pending_->textureHandles.push_back(h);
        }
        pending_->texturesKicked = true;
    }

    // Clear old world (once)
    if (!pending_->worldCleared) {
        std::vector<EntityHandle> toDestroy;
        world.forEachEntity([&](Entity* e) {
            toDestroy.push_back(e->handle());
        });
        for (auto h : toDestroy) world.destroyEntity(h);
        world.processDestroyQueue();
        pending_->worldCleared = true;
    }

    // Batch-create entities (adaptive)
    auto start = std::chrono::high_resolution_clock::now();
    constexpr int MIN_BATCH = 10;
    constexpr int MAX_BATCH = 100;
    constexpr float TARGET_MS = 2.0f;

    int created = 0;
    while (pending_->createdEntities < pending_->totalEntities) {
        PrefabLibrary::jsonToEntity(pending_->prefabs[pending_->createdEntities], world);
        pending_->createdEntities++;
        created++;
        if (created >= MIN_BATCH) {
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            float ms = std::chrono::duration<float, std::milli>(elapsed).count();
            if (ms >= TARGET_MS || created >= MAX_BATCH) break;
        }
    }

    // Count loaded textures
    int loaded = 0;
    for (auto h : pending_->textureHandles) {
        if (AssetRegistry::instance().isReady(h)) loaded++;
    }
    pending_->loadedTextures = loaded;

    bool allDone = (pending_->createdEntities >= pending_->totalEntities) &&
                   (pending_->loadedTextures >= pending_->totalTextures);

    if (allDone) {
        active_ = false;
        LOG_INFO("AsyncLoader", "Scene '%s' fully loaded (%d entities, %d textures)",
                 pending_->sceneName.c_str(), pending_->totalEntities, pending_->totalTextures);
    }
    return allDone;
}

bool AsyncSceneLoader::isWorkerDone() const {
    return pending_ && pending_->workerDone.load();
}

bool AsyncSceneLoader::hasFailed() const {
    return pending_ && pending_->workerFailed;
}

const std::string& AsyncSceneLoader::errorMessage() const {
    static const std::string empty;
    return pending_ ? pending_->errorMessage : empty;
}

float AsyncSceneLoader::progress() const {
    return pending_ ? pending_->progress() : 0.0f;
}

const std::string& AsyncSceneLoader::sceneName() const {
    static const std::string empty;
    return pending_ ? pending_->sceneName : empty;
}

} // namespace fate
