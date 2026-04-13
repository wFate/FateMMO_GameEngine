#include "engine/ecs/component_meta.h"

#include <cstring>

#include <nlohmann/json.hpp>

#include "engine/core/logger.h"
#include "engine/core/types.h"
#include "engine/ecs/entity_handle.h"

namespace fate {

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
ComponentMetaRegistry& ComponentMetaRegistry::instance() {
    static ComponentMetaRegistry reg;
    return reg;
}

// ---------------------------------------------------------------------------
// Alias support
// ---------------------------------------------------------------------------
void ComponentMetaRegistry::registerAlias(const std::string& alias,
                                          const std::string& canonical) {
    aliases_[alias] = canonical;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
const ComponentMeta* ComponentMetaRegistry::findByName(const std::string& name) const {
    // Direct lookup first
    auto it = byName_.find(name);
    if (it != byName_.end()) return it->second;

    // Try alias
    auto ait = aliases_.find(name);
    if (ait != aliases_.end()) {
        auto cit = byName_.find(ait->second);
        if (cit != byName_.end()) return cit->second;
    }
    return nullptr;
}

const ComponentMeta* ComponentMetaRegistry::findById(CompId id) const {
    auto it = byId_.find(id);
    return it != byId_.end() ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------
void ComponentMetaRegistry::forEachMeta(
    const std::function<void(const ComponentMeta&)>& fn) const {
    for (auto& ptr : storage_) {
        fn(*ptr);
    }
}

// ---------------------------------------------------------------------------
// Auto-generated JSON serialization from reflection metadata
// ---------------------------------------------------------------------------
void autoToJson(const void* data, nlohmann::json& j,
                std::span<const FieldInfo> fields) {
    const auto* base = static_cast<const uint8_t*>(data);

    for (const auto& f : fields) {
        const void* ptr = base + f.offset;

        switch (f.type) {
            case FieldType::Float:
                j[f.name] = *static_cast<const float*>(ptr);
                break;
            case FieldType::Int: {
                // Size-aware read to avoid reading past small fields
                if (f.size == 8)       j[f.name] = *static_cast<const int64_t*>(ptr);
                else if (f.size == 2)  j[f.name] = static_cast<int>(*static_cast<const int16_t*>(ptr));
                else if (f.size == 1)  j[f.name] = static_cast<int>(*static_cast<const int8_t*>(ptr));
                else                   j[f.name] = *static_cast<const int32_t*>(ptr);
                break;
            }
            case FieldType::UInt: {
                if (f.size == 8)       j[f.name] = *static_cast<const uint64_t*>(ptr);
                else if (f.size == 2)  j[f.name] = static_cast<uint32_t>(*static_cast<const uint16_t*>(ptr));
                else if (f.size == 1)  j[f.name] = static_cast<uint32_t>(*static_cast<const uint8_t*>(ptr));
                else                   j[f.name] = *static_cast<const uint32_t*>(ptr);
                break;
            }
            case FieldType::Bool:
                j[f.name] = *static_cast<const bool*>(ptr);
                break;
            case FieldType::Vec2: {
                const auto& v = *static_cast<const Vec2*>(ptr);
                j[f.name] = {v.x, v.y};
                break;
            }
            case FieldType::Vec3: {
                const auto& v = *static_cast<const Vec3*>(ptr);
                j[f.name] = {v.x, v.y, v.z};
                break;
            }
            case FieldType::Vec4: {
                const auto& v = *static_cast<const Vec4*>(ptr);
                j[f.name] = {v.x, v.y, v.z, v.w};
                break;
            }
            case FieldType::Color: {
                const auto& c = *static_cast<const Color*>(ptr);
                j[f.name] = {c.r, c.g, c.b, c.a};
                break;
            }
            case FieldType::Rect: {
                const auto& r = *static_cast<const Rect*>(ptr);
                j[f.name] = {r.x, r.y, r.w, r.h};
                break;
            }
            case FieldType::String:
                j[f.name] = *static_cast<const std::string*>(ptr);
                break;
            case FieldType::Enum: {
                // Read enum value based on stored size (1, 2, or 4 bytes)
                int32_t v = 0;
                std::memcpy(&v, ptr, f.size <= sizeof(v) ? f.size : sizeof(v));
                j[f.name] = v;
                break;
            }
            case FieldType::EntityHandle: {
                const auto& h = *static_cast<const EntityHandle*>(ptr);
                j[f.name] = h.value;
                break;
            }
            case FieldType::Direction: {
                j[f.name] = static_cast<int>(*static_cast<const Direction*>(ptr));
                break;
            }
            case FieldType::Custom:
                LOG_WARN("ComponentMeta",
                         "autoToJson: skipping Custom field '%s' (no serializer)",
                         f.name);
                break;
        }
    }
}

void autoFromJson(const nlohmann::json& j, void* data,
                  std::span<const FieldInfo> fields) {
    auto* base = static_cast<uint8_t*>(data);

    for (const auto& f : fields) {
        if (!j.contains(f.name)) continue; // forward compat: skip missing

        void* ptr = base + f.offset;
        const auto& val = j[f.name];

        switch (f.type) {
            case FieldType::Float:
                *static_cast<float*>(ptr) = val.get<float>();
                break;
            case FieldType::Int: {
                if (f.size == 8)       *static_cast<int64_t*>(ptr) = val.get<int64_t>();
                else if (f.size == 2)  *static_cast<int16_t*>(ptr) = static_cast<int16_t>(val.get<int>());
                else if (f.size == 1)  *static_cast<int8_t*>(ptr) = static_cast<int8_t>(val.get<int>());
                else                   *static_cast<int32_t*>(ptr) = val.get<int32_t>();
                break;
            }
            case FieldType::UInt: {
                if (f.size == 8)       *static_cast<uint64_t*>(ptr) = val.get<uint64_t>();
                else if (f.size == 2)  *static_cast<uint16_t*>(ptr) = static_cast<uint16_t>(val.get<uint32_t>());
                else if (f.size == 1)  *static_cast<uint8_t*>(ptr) = static_cast<uint8_t>(val.get<uint32_t>());
                else                   *static_cast<uint32_t*>(ptr) = val.get<uint32_t>();
                break;
            }
            case FieldType::Bool:
                *static_cast<bool*>(ptr) = val.get<bool>();
                break;
            case FieldType::Vec2: {
                auto& v = *static_cast<Vec2*>(ptr);
                v.x = val[0].get<float>();
                v.y = val[1].get<float>();
                break;
            }
            case FieldType::Vec3: {
                auto& v = *static_cast<Vec3*>(ptr);
                v.x = val[0].get<float>();
                v.y = val[1].get<float>();
                v.z = val[2].get<float>();
                break;
            }
            case FieldType::Vec4: {
                auto& v = *static_cast<Vec4*>(ptr);
                v.x = val[0].get<float>();
                v.y = val[1].get<float>();
                v.z = val[2].get<float>();
                v.w = val[3].get<float>();
                break;
            }
            case FieldType::Color: {
                auto& c = *static_cast<Color*>(ptr);
                c.r = val[0].get<float>();
                c.g = val[1].get<float>();
                c.b = val[2].get<float>();
                c.a = val[3].get<float>();
                break;
            }
            case FieldType::Rect: {
                auto& r = *static_cast<Rect*>(ptr);
                r.x = val[0].get<float>();
                r.y = val[1].get<float>();
                r.w = val[2].get<float>();
                r.h = val[3].get<float>();
                break;
            }
            case FieldType::String:
                *static_cast<std::string*>(ptr) = val.get<std::string>();
                break;
            case FieldType::Enum: {
                int32_t v = val.get<int32_t>();
                std::memcpy(ptr, &v, f.size <= sizeof(v) ? f.size : sizeof(v));
                break;
            }
            case FieldType::EntityHandle: {
                auto& h = *static_cast<EntityHandle*>(ptr);
                h.value = val.get<uint32_t>();
                break;
            }
            case FieldType::Direction: {
                *static_cast<Direction*>(ptr) =
                    static_cast<Direction>(val.get<uint8_t>());
                break;
            }
            case FieldType::Custom:
                LOG_WARN("ComponentMeta",
                         "autoFromJson: skipping Custom field '%s' (no deserializer)",
                         f.name);
                break;
        }
    }
}

} // namespace fate
