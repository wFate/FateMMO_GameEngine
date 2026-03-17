#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"
#include "engine/ecs/reflect.h"
#include <string>
#include <vector>

namespace fate {

// Zone region — defines a named area within a scene
// Used for: camera bounds, zone transitions, PvP flags, spawn areas
// Place as an entity with Transform (position = center) + ZoneComponent
struct ZoneComponent {
    FATE_COMPONENT(ZoneComponent)

    std::string zoneName;          // e.g., "Lighthouse_F1", "Town_Market"
    Vec2 size = {480.0f, 270.0f};  // width/height of the zone region

    // Zone properties (mirrors the Unity 'scenes' table)
    std::string displayName;       // shown to player during transition
    std::string zoneType = "zone"; // town, zone, dungeon
    int minLevel = 1;
    int maxLevel = 99;
    bool pvpEnabled = false;

    // Get world-space bounds given entity position
    Rect getBounds(const Vec2& entityPos) const {
        return {
            entityPos.x - size.x * 0.5f,
            entityPos.y - size.y * 0.5f,
            size.x,
            size.y
        };
    }

    // Check if a world position is inside this zone
    bool contains(const Vec2& entityPos, const Vec2& point) const {
        Rect b = getBounds(entityPos);
        return b.contains(point);
    }
};

// Portal — triggers a transition when the player walks into it
// Can transition within the same scene (zone-to-zone) or to a different scene
struct PortalComponent {
    FATE_COMPONENT(PortalComponent)

    Vec2 triggerSize = {32.0f, 32.0f}; // collision area for the portal

    // Where does this portal go?
    std::string targetScene;       // empty = same scene (zone transition)
    std::string targetZone;        // target zone name within the scene
    Vec2 targetSpawnPos;           // where the player appears after transition

    // Visual
    bool showLabel = true;         // show zone name above portal
    std::string label;             // override label text (empty = use targetZone)

    // Transition type
    bool useFadeTransition = true; // fade to black during transition
    float fadeDuration = 0.3f;     // seconds for fade in/out

    Rect getTriggerBounds(const Vec2& entityPos) const {
        return {
            entityPos.x - triggerSize.x * 0.5f,
            entityPos.y - triggerSize.y * 0.5f,
            triggerSize.x,
            triggerSize.y
        };
    }
};

} // namespace fate

FATE_REFLECT(fate::ZoneComponent,
    FATE_FIELD(zoneName, String),
    FATE_FIELD(size, Vec2),
    FATE_FIELD(displayName, String),
    FATE_FIELD(zoneType, String),
    FATE_FIELD(minLevel, Int),
    FATE_FIELD(maxLevel, Int),
    FATE_FIELD(pvpEnabled, Bool)
)

FATE_REFLECT(fate::PortalComponent,
    FATE_FIELD(triggerSize, Vec2),
    FATE_FIELD(targetScene, String),
    FATE_FIELD(targetZone, String),
    FATE_FIELD(targetSpawnPos, Vec2),
    FATE_FIELD(showLabel, Bool),
    FATE_FIELD(label, String),
    FATE_FIELD(useFadeTransition, Bool),
    FATE_FIELD(fadeDuration, Float)
)
