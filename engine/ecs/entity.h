#pragma once
#include "engine/ecs/component.h"
#include "engine/core/types.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

namespace fate {

class World;

class Entity {
public:
    Entity(EntityId id, const std::string& name = "Entity");
    ~Entity();

    EntityId id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

    const std::string& tag() const { return tag_; }
    void setTag(const std::string& tag) { tag_ = tag; }

    // Component management
    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = comp.get();
        components_[getComponentTypeId<T>()] = std::move(comp);
        return ptr;
    }

    template<typename T>
    T* getComponent() const {
        auto it = components_.find(getComponentTypeId<T>());
        if (it == components_.end()) return nullptr;
        return static_cast<T*>(it->second.get());
    }

    template<typename T>
    bool hasComponent() const {
        return components_.find(getComponentTypeId<T>()) != components_.end();
    }

    template<typename T>
    void removeComponent() {
        components_.erase(getComponentTypeId<T>());
    }

    // Iterate all components
    void forEachComponent(const std::function<void(Component*)>& fn) {
        for (auto& [id, comp] : components_) {
            fn(comp.get());
        }
    }

    size_t componentCount() const { return components_.size(); }

private:
    EntityId id_;
    std::string name_;
    std::string tag_;
    bool active_ = true;
    std::unordered_map<ComponentTypeId, std::unique_ptr<Component>> components_;
};

} // namespace fate
