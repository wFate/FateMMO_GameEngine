#include "engine/scene/scene_manager.h"
#include "engine/core/logger.h"

namespace fate {

// ---------------------------------------------------------------------------
// Scene ownership
// ---------------------------------------------------------------------------

void SceneManager::setCurrentScene(std::unique_ptr<Scene> scene, const std::string& name) {
    if (currentScene_) {
        currentScene_->onExit();
    }
    currentScene_ = std::move(scene);
    currentSceneName_ = name;
}

void SceneManager::unloadScene() {
    if (currentScene_) {
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
