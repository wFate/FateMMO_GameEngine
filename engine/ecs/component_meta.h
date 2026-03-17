#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "engine/ecs/component_registry.h"
#include "engine/ecs/component_traits.h"
#include "engine/ecs/reflect.h"

namespace fate {

struct ComponentMeta {
    const char* name;
    CompId id;
    size_t size;
    size_t alignment;
    ComponentFlags flags;
    std::span<const FieldInfo> fields;

    std::function<void(void*)> construct;
    std::function<void(void*)> destroy;
    std::function<void(const void*, nlohmann::json&)> toJson;
    std::function<void(const nlohmann::json&, void*)> fromJson;
};

// Auto-generate serialization functions from reflection metadata
void autoToJson(const void* data, nlohmann::json& j, std::span<const FieldInfo> fields);
void autoFromJson(const nlohmann::json& j, void* data, std::span<const FieldInfo> fields);

class ComponentMetaRegistry {
public:
    static ComponentMetaRegistry& instance();

    template<typename T>
    void registerComponent(
        std::function<void(const void*, nlohmann::json&)> customToJson = nullptr,
        std::function<void(const nlohmann::json&, void*)> customFromJson = nullptr);

    void registerAlias(const std::string& alias, const std::string& canonical);

    const ComponentMeta* findByName(const std::string& name) const;
    const ComponentMeta* findById(CompId id) const;

    void forEachMeta(const std::function<void(const ComponentMeta&)>& fn) const;

private:
    ComponentMetaRegistry() = default;

    // Stable storage: unique_ptr ensures pointers remain valid on container growth
    std::vector<std::unique_ptr<ComponentMeta>> storage_;
    std::unordered_map<std::string, ComponentMeta*> byName_;
    std::unordered_map<CompId, ComponentMeta*> byId_;
    std::unordered_map<std::string, std::string> aliases_;
};

// ---------------------------------------------------------------------------
// Template implementation
// ---------------------------------------------------------------------------

template<typename T>
void ComponentMetaRegistry::registerComponent(
    std::function<void(const void*, nlohmann::json&)> customToJson,
    std::function<void(const nlohmann::json&, void*)> customFromJson)
{
    auto meta = std::make_unique<ComponentMeta>();
    meta->name      = T::COMPONENT_NAME;
    meta->id        = componentId<T>();
    meta->size      = sizeof(T);
    meta->alignment = alignof(T);
    meta->flags     = component_traits<T>::flags;
    meta->fields    = Reflection<T>::fields();

    // Construct: placement-new default
    meta->construct = [](void* ptr) { new (ptr) T(); };

    // Destroy: call destructor (only meaningful for non-trivially-destructible)
    if constexpr (!std::is_trivially_destructible_v<T>) {
        meta->destroy = [](void* ptr) { static_cast<T*>(ptr)->~T(); };
    } else {
        meta->destroy = nullptr;
    }

    // Serialization
    if (customToJson) {
        meta->toJson = std::move(customToJson);
    } else if (!meta->fields.empty()) {
        auto fields = meta->fields;
        meta->toJson = [fields](const void* data, nlohmann::json& j) {
            autoToJson(data, j, fields);
        };
    }

    if (customFromJson) {
        meta->fromJson = std::move(customFromJson);
    } else if (!meta->fields.empty()) {
        auto fields = meta->fields;
        meta->fromJson = [fields](const nlohmann::json& j, void* data) {
            autoFromJson(j, data, fields);
        };
    }

    ComponentMeta* rawPtr = meta.get();
    storage_.push_back(std::move(meta));
    byName_[rawPtr->name] = rawPtr;
    byId_[rawPtr->id]     = rawPtr;
}

} // namespace fate
