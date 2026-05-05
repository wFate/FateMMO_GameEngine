#include "engine/module/behavior_component.h"
#include "engine/ecs/component_meta.h"

namespace fate {

void registerBehaviorComponent() {
    auto& reg = ComponentMetaRegistry::instance();
    reg.registerComponent<BehaviorComponent>(
        // toJson — authoring fields ONLY. runtimeFields, state, and
        // boundGeneration are deliberately excluded so per-frame scratch
        // (theta accumulators, captured anchors, module pointers) never
        // leaks into saved scene .json.
        [](const void* data, nlohmann::json& j) {
            const auto* bc = static_cast<const BehaviorComponent*>(data);
            j["behavior"] = bc->behavior;
            j["protocol"] = bc->payloadProtocolVersion;
            j["fields"]   = bc->fields;
            j["enabled"]  = bc->enabled;
        },
        // fromJson — defensive about malformed input. A scene with a stale
        // behavior name is a valid runtime state (the registry simply has no
        // binding); we keep the name so a future reload that registers it
        // takes effect without re-saving the scene.
        [](const nlohmann::json& j, void* data) {
            auto* bc = static_cast<BehaviorComponent*>(data);
            if (j.contains("behavior") && j["behavior"].is_string()) {
                bc->behavior = j["behavior"].get<std::string>();
            }
            // Protocol stamp: missing or non-numeric → 1 (pre-stamp scenes
            // wrote the v1 payload shape; 0 would imply something even
            // older which doesn't exist). Future protocol bumps will
            // trigger the module's migrate callback at next reload.
            if (j.contains("protocol") && j["protocol"].is_number_unsigned()) {
                bc->payloadProtocolVersion = j["protocol"].get<uint32_t>();
                if (bc->payloadProtocolVersion == 0) bc->payloadProtocolVersion = 1;
            } else {
                bc->payloadProtocolVersion = 1;
            }
            if (j.contains("fields") && j["fields"].is_object()) {
                bc->fields = j["fields"];
            } else {
                bc->fields = nlohmann::json::object();
            }
            if (j.contains("enabled") && j["enabled"].is_boolean()) {
                bc->enabled = j["enabled"].get<bool>();
            }
            // Defensive: even if a malformed scene somehow contains a
            // "runtimeFields" or "state" entry, do not honor them. Runtime
            // state is rebuilt by the next onStart.
            bc->runtimeFields = nlohmann::json::object();
            bc->state = nullptr;
            bc->boundGeneration = 0;
        }
    );
}

} // namespace fate
