#pragma once
#include "engine/ecs/world.h"
#include "engine/render/texture.h"
#include "stb_image_write.h"
#include <filesystem>
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/box_collider.h"
#include "game/components/animator.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/pet_component.h"
#include "game/shared/game_types.h"
#include "game/shared/cached_mob_def.h"

namespace fate {

struct NPCTemplate;

class EntityFactory {
public:

    /// Create a fully-assembled player entity with all game components.
    /// Mirrors the Unity PlayerScene2 prefab (24 MonoBehaviours).
    static Entity* createPlayer(World& world, const std::string& name, ClassType classType, bool isLocal = false, Faction faction = Faction::None);

    /// Create a mob from a database-backed CachedMobDef (73 mob definitions).
    /// Uses all stats from the definition (HP/damage/armor scaled by level, AI ranges, loot, etc).
    static Entity* createMobFromDef(World& world, const CachedMobDef& def, int level, Vec2 spawnPos);

    /// Create a mob entity with AI and stats (hardcoded fallback).
    /// Used when no MobDefCache is available (client, tests, legacy).
    static Entity* createMob(World& world, const std::string& mobName, int level,
                             int baseHP, int baseDamage, Vec2 spawnPos,
                             bool aggressive = true, bool isBoss = false);

    /// Create an NPC entity from an NPCTemplate.
    /// Adds role-specific components based on template flags.
    static Entity* createNPC(World& world, const NPCTemplate& tmpl);

    /// Create a ghost (remote) player entity — minimal visual representation.
    /// Includes CharacterStatsComponent, FactionComponent, and DamageableComponent
    /// so the entity is targetable for PvP. Server syncs actual values via replication.
    static Entity* createGhostPlayer(World& world, const std::string& name, Vec2 position);

    /// Create a ghost (remote) mob entity — full visual representation from server data.
    // Ensures a mob sprite PNG exists — if not, creates a temporary local-only
    // mob entity to trigger procedural generation, then discards it.
    static void ensureMobSprite(World& world, const std::string& mobId, bool isBoss);

    static Entity* createGhostMob(World& world, const std::string& name, Vec2 position,
                                   const std::string& mobDefId = "", int level = 1,
                                   int currentHP = 100, int maxHP = 100,
                                   bool isBoss = false);

    // Creates a dropped item entity on the ground (server-side)
    static Entity* createDroppedItem(World& world, Vec2 position, bool isGold);

    // Creates a ghost dropped item (client-side, for rendering)
    static Entity* createGhostDroppedItem(World& world, const std::string& name, Vec2 position,
                                           bool isGold = false, const std::string& rarity = "Common");

};

} // namespace fate
