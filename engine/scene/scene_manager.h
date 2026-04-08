#pragma once
#include "engine/scene/scene.h"
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

namespace fate {

// Singleton that owns the current Scene.
//
// In practice this is a scene *holder*, not a loader.  The game client
// (GameApp) drives scene loading through AsyncSceneLoader which does
// fiber-based background parsing + batched main-thread finalization.
// SceneManager just provides the canonical currentScene() accessor that
// the engine (App, Editor, render loop) queries every frame.
//
// The synchronous loadScene()/switchScene() helpers and the factory
// registry are only used by the examples/demo_app.  Real game scene
// transitions go through AsyncSceneLoader -> SceneManager::currentScene_.
class SceneManager {
public:
    static SceneManager& instance() {
        static SceneManager s_instance;
        return s_instance;
    }

    // --- Scene ownership (used by engine + game) ---

    // Get the active scene.  Returns nullptr before any scene is loaded.
    Scene* currentScene() { return currentScene_.get(); }
    const std::string& currentSceneName() const { return currentSceneName_; }

    // Replace the current scene with an already-constructed one.
    // Calls onExit on the old scene if present.  The caller is responsible
    // for calling onEnter on the new scene after any additional setup.
    void setCurrentScene(std::unique_ptr<Scene> scene, const std::string& name);

    // Destroy the current scene (calls onExit, resets pointer).
    // Called by App::shutdown() to ensure cleanup before static destructors.
    void unloadScene();

    // --- Synchronous helpers (demo/editor only) ---

    // Register a scene factory for programmatic scene creation.
    using SceneFactory = std::function<void(Scene&)>;
    void registerScene(const std::string& name, SceneFactory factory);

    // Synchronous load from JSON — blocks the main thread for the entire
    // parse + entity creation.  Not used by the game client; prefer
    // AsyncSceneLoader for real scene transitions.
    bool loadScene(const std::string& name, const std::string& jsonPath);

    // Switch to a registered factory scene (synchronous).
    bool switchScene(const std::string& name);

    // Callback fired after loadScene/switchScene complete.
    std::function<void(Scene&)> onSceneLoaded;

private:
    SceneManager() = default;

    std::unique_ptr<Scene> currentScene_;
    std::string currentSceneName_;
    std::unordered_map<std::string, SceneFactory> factories_;
};

} // namespace fate
