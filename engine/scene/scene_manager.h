#pragma once
#include "engine/scene/scene.h"
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

namespace fate {

// Manages scene transitions (load, unload, switch)
// Future: integrates with server for networked scene loading
class SceneManager {
public:
    static SceneManager& instance() {
        static SceneManager s_instance;
        return s_instance;
    }

    // Register a scene factory (for programmatic scene creation)
    using SceneFactory = std::function<void(Scene&)>;
    void registerScene(const std::string& name, SceneFactory factory);

    // Load and activate a scene (from JSON file)
    bool loadScene(const std::string& name, const std::string& jsonPath);

    // Switch to a registered scene (calls factory)
    bool switchScene(const std::string& name);

    // Get current active scene
    Scene* currentScene() { return currentScene_.get(); }
    const std::string& currentSceneName() const { return currentSceneName_; }

    // Loading state — true while a scene transition is in progress
    bool isLoading() const { return isLoading_; }
    float loadProgress() const { return loadProgress_; }

    // Callback when scene is loaded (game code hooks into this)
    std::function<void(Scene&)> onSceneLoaded;

private:
    SceneManager() = default;

    std::unique_ptr<Scene> currentScene_;
    std::string currentSceneName_;
    std::unordered_map<std::string, SceneFactory> factories_;
    bool isLoading_ = false;
    float loadProgress_ = 0.0f;
};

} // namespace fate
