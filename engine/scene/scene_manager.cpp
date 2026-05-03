#include "engine/scene/scene_manager.h"
#include "engine/core/logger.h"
#if FATE_ENABLE_HOT_RELOAD
#include "engine/module/hot_reload_manager.h"
#endif

namespace fate {

// ---------------------------------------------------------------------------
// Scene ownership
// ---------------------------------------------------------------------------

void SceneManager::setCurrentScene(std::unique_ptr<Scene> scene, const std::string& name) {
    if (currentScene_) {
#if FATE_ENABLE_HOT_RELOAD
        // Tear down any behaviors bound to the outgoing world BEFORE the
        // unique_ptr destroys it. Otherwise the next reload's roster sweep
        // would dereference dead World pointers.
        HotReloadManager::instance().onWorldUnload(currentScene_->world());
#endif
        currentScene_->onExit();
    }
    currentScene_ = std::move(scene);
    currentSceneName_ = name;
}

void SceneManager::unloadScene() {
    if (currentScene_) {
#if FATE_ENABLE_HOT_RELOAD
        HotReloadManager::instance().onWorldUnload(currentScene_->world());
#endif
        currentScene_->onExit();
        currentScene_.reset();
        currentSceneName_.clear();
        LOG_INFO("SceneManager", "Scene unloaded");
    }
}

// ---------------------------------------------------------------------------
// Synchronous helpers — used by demo_app / editor, not by GameApp.
// The game client uses AsyncSceneLoader for non-blocking scene transitions.
// ---------------------------------------------------------------------------

void SceneManager::registerScene(const std::string& name, SceneFactory factory) {
    factories_[name] = std::move(factory);
    LOG_DEBUG("SceneManager", "Registered scene factory: %s", name.c_str());
}

bool SceneManager::loadScene(const std::string& name, const std::string& jsonPath) {
    // Build + validate the replacement BEFORE exiting the current scene. If
    // the load fails we leave the existing scene intact — otherwise a bad
    // file path / malformed JSON would leave us with no active scene.
    auto scene = std::make_unique<Scene>(name);
    if (!scene->loadFromFile(jsonPath)) {
        LOG_ERROR("SceneManager", "Failed to load scene '%s' from %s (current scene preserved)",
                  name.c_str(), jsonPath.c_str());
        return false;
    }

    if (currentScene_) {
#if FATE_ENABLE_HOT_RELOAD
        // Roster has raw World* entries for the outgoing scene; tear them
        // down before the unique_ptr destroys the World, otherwise a later
        // tick or reload would dereference a freed pointer.
        HotReloadManager::instance().onWorldUnload(currentScene_->world());
#endif
        currentScene_->onExit();
    }

    currentScene_ = std::move(scene);
    currentSceneName_ = name;
    currentScene_->onEnter();

    if (onSceneLoaded) {
        onSceneLoaded(*currentScene_);
    }

    return true;
}

bool SceneManager::switchScene(const std::string& name) {
    auto it = factories_.find(name);
    if (it == factories_.end()) {
        LOG_ERROR("SceneManager", "No factory registered for scene: %s", name.c_str());
        return false;
    }

    // Exit old scene
    if (currentScene_) {
#if FATE_ENABLE_HOT_RELOAD
        // Same dangling-World guard as loadScene() / setCurrentScene().
        HotReloadManager::instance().onWorldUnload(currentScene_->world());
#endif
        currentScene_->onExit();
    }

    // Create and populate new scene via factory
    auto scene = std::make_unique<Scene>(name);
    it->second(*scene);

    // Activate
    currentScene_ = std::move(scene);
    currentSceneName_ = name;
    currentScene_->onEnter();

    if (onSceneLoaded) {
        onSceneLoaded(*currentScene_);
    }

    return true;
}

} // namespace fate
