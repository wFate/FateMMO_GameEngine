#include "engine/scene/async_scene_loader.h"
#include "engine/asset/asset_registry.h"
#include "engine/asset/asset_source.h"
#include "engine/ecs/world.h"
#include "engine/ecs/prefab.h"
#include "engine/job/job_system.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <chrono>

namespace fate {

float PendingSceneLoad::progress() const {
    if (!workerDone.load(std::memory_order_acquire)) return workerProgress.load(std::memory_order_relaxed);
    if (totalEntities == 0 && totalTextures == 0) return 1.0f;
    float entityProg = (totalEntities > 0)
        ? static_cast<float>(createdEntities) / totalEntities : 1.0f;
    // Failed textures count toward "resolved" so a single missing asset can't
    // pin the bar below 100%. Entities still spawn; missing-texture sprites
    // render with the loader's placeholder.
    float texProg = (totalTextures > 0)
        ? static_cast<float>(loadedTextures + failedTextures) / totalTextures : 1.0f;
    return 0.4f + entityProg * 0.3f + texProg * 0.3f;
}

static void asyncSceneLoadJob(void* param) {
    auto* pending = static_cast<PendingSceneLoad*>(param);

    pending->workerProgress.store(0.0f, std::memory_order_relaxed);
    // Phase 3 of VFS migration: read scene JSON via the installed
    // IAssetSource so packaged .pak builds work. Source readText is
    // documented thread-safe; this runs on a fiber-job worker.
    auto sceneText = AssetRegistry::readText(pending->jsonPath);
    if (!sceneText) {
        pending->errorMessage = "Cannot open scene: " + pending->jsonPath;
        pending->workerFailed.store(true, std::memory_order_relaxed);
        pending->workerDone.store(true, std::memory_order_release);
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(*sceneText);
    } catch (const nlohmann::json::exception& e) {
        pending->errorMessage = std::string("JSON parse error: ") + e.what();
        pending->workerFailed.store(true, std::memory_order_relaxed);
        pending->workerDone.store(true, std::memory_order_release);
        return;
    }

    if (root.contains("metadata")) {
        pending->sceneMetadata = root["metadata"];
    }
    pending->workerProgress.store(0.2f, std::memory_order_relaxed);

    std::unordered_set<std::string> uniqueTextures;
    if (root.contains("entities") && root["entities"].is_array()) {
        for (auto& entityJson : root["entities"]) {
            pending->prefabs.push_back(std::move(entityJson));
            auto& last = pending->prefabs.back();
            if (last.contains("components")) {
                auto& comps = last["components"];
                // Check canonical name and backward-compat alias
                const char* spriteKey = nullptr;
                if (comps.contains("SpriteComponent")) spriteKey = "SpriteComponent";
                else if (comps.contains("Sprite"))     spriteKey = "Sprite";
                if (spriteKey) {
                    auto& sc = comps[spriteKey];
                    // Support both current ("texturePath") and legacy ("texture") key
                    std::string path;
                    if (sc.contains("texturePath") && sc["texturePath"].is_string())
                        path = sc["texturePath"].get<std::string>();
                    else if (sc.contains("texture") && sc["texture"].is_string())
                        path = sc["texture"].get<std::string>();
                    if (!path.empty()) uniqueTextures.insert(path);
                }
            }
        }
    }

    pending->texturePaths.assign(uniqueTextures.begin(), uniqueTextures.end());
    pending->totalEntities = static_cast<int>(pending->prefabs.size());
    pending->totalTextures = static_cast<int>(pending->texturePaths.size());
    pending->workerProgress.store(0.4f, std::memory_order_relaxed);
    pending->workerDone.store(true, std::memory_order_release);  // Publishes all non-atomic writes above

    LOG_INFO("AsyncLoader", "Scene '%s': %d entities, %d unique textures",
             pending->sceneName.c_str(), pending->totalEntities, pending->totalTextures);
}

void AsyncSceneLoader::startLoad(const std::string& sceneName, const std::string& jsonPath) {
    if (active_) {
        LOG_WARN("AsyncLoader", "startLoad('%s') called while '%s' is already active -- ignoring",
                 sceneName.c_str(),
                 (pending_ ? pending_->sceneName.c_str() : "<unknown>"));
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
    if (!active_ || !pending_ || !pending_->workerDone.load(std::memory_order_acquire)) return false;
    if (pending_->workerFailed.load(std::memory_order_relaxed)) return true; // caller must call reset() after handling error

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
        // Scope: async scene loader wipes the old scene's world wholesale
        // before populating the new one. Client-side scene swap only —
        // never runs on the authoritative server.
        world.processDestroyQueue("scene_unload");
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

    // Count textures by terminal state. A failed async load frees its slot and
    // bumps generation past the held handle — isResolved() returns true for
    // both success and failure, so a missing asset can't permanently hang the
    // load. Entities referencing a failed texture still spawn (placeholder).
    int loaded = 0;
    int failed = 0;
    for (auto h : pending_->textureHandles) {
        auto& reg = AssetRegistry::instance();
        if (reg.isReady(h)) {
            loaded++;
        } else if (reg.isResolved(h)) {
            failed++;
        }
    }
    pending_->loadedTextures = loaded;
    pending_->failedTextures = failed;

    bool allDone = (pending_->createdEntities >= pending_->totalEntities) &&
                   (pending_->loadedTextures + pending_->failedTextures >= pending_->totalTextures);

    if (allDone) {
        active_ = false;
        if (pending_->failedTextures > 0) {
            LOG_WARN("AsyncLoader", "Scene '%s' loaded with %d entities, %d textures (%d failed)",
                     pending_->sceneName.c_str(), pending_->totalEntities,
                     pending_->totalTextures, pending_->failedTextures);
        } else {
            LOG_INFO("AsyncLoader", "Scene '%s' fully loaded (%d entities, %d textures)",
                     pending_->sceneName.c_str(), pending_->totalEntities, pending_->totalTextures);
        }
    }
    return allDone;
}

void AsyncSceneLoader::reset() {
    active_ = false;
    pending_.reset();
}

bool AsyncSceneLoader::isWorkerDone() const {
    return pending_ && pending_->workerDone.load(std::memory_order_acquire);
}

bool AsyncSceneLoader::hasFailed() const {
    // Must check workerDone (acquire) first to guarantee visibility of workerFailed
    return pending_ && pending_->workerDone.load(std::memory_order_acquire)
                     && pending_->workerFailed.load(std::memory_order_relaxed);
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

const nlohmann::json& AsyncSceneLoader::sceneMetadata() const {
    static const nlohmann::json empty = nlohmann::json::object();
    return pending_ ? pending_->sceneMetadata : empty;
}

} // namespace fate
