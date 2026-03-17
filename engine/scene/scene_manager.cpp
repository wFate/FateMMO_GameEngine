#include "engine/scene/scene_manager.h"
#include "engine/core/logger.h"

namespace fate {

void SceneManager::registerScene(const std::string& name, SceneFactory factory) {
    factories_[name] = std::move(factory);
    LOG_DEBUG("SceneManager", "Registered scene factory: %s", name.c_str());
}

bool SceneManager::loadScene(const std::string& name, const std::string& jsonPath) {
    isLoading_ = true;
    loadProgress_ = 0.0f;

    // Exit current scene
    if (currentScene_) {
        currentScene_->onExit();
    }
    loadProgress_ = 0.25f;

    // Create new scene
    auto scene = std::make_unique<Scene>(name);
    if (!scene->loadFromFile(jsonPath)) {
        LOG_ERROR("SceneManager", "Failed to load scene '%s' from %s", name.c_str(), jsonPath.c_str());
        isLoading_ = false;
        loadProgress_ = 0.0f;
        return false;
    }
    loadProgress_ = 0.75f;

    currentScene_ = std::move(scene);
    currentSceneName_ = name;
    currentScene_->onEnter();
    loadProgress_ = 1.0f;

    if (onSceneLoaded) {
        onSceneLoaded(*currentScene_);
    }

    isLoading_ = false;
    return true;
}

bool SceneManager::switchScene(const std::string& name) {
    auto it = factories_.find(name);
    if (it == factories_.end()) {
        LOG_ERROR("SceneManager", "No factory registered for scene: %s", name.c_str());
        return false;
    }

    isLoading_ = true;
    loadProgress_ = 0.0f;

    // Exit old scene
    if (currentScene_) {
        currentScene_->onExit();
    }
    loadProgress_ = 0.25f;

    // Create and populate new scene
    auto scene = std::make_unique<Scene>(name);
    it->second(*scene);
    loadProgress_ = 0.75f;

    // Activate new scene
    currentScene_ = std::move(scene);
    currentSceneName_ = name;
    currentScene_->onEnter();
    loadProgress_ = 1.0f;

    if (onSceneLoaded) {
        onSceneLoaded(*currentScene_);
    }

    isLoading_ = false;
    return true;
}

} // namespace fate
