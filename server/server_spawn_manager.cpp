#include "server/server_spawn_manager.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "engine/ecs/persistent_id.h"
#include "engine/core/logger.h"
#include "game/shared/mob_ai.h"
#include <random>

namespace fate {

static thread_local std::mt19937 s_rng{std::random_device{}()};

// ---------------------------------------------------------------------------
// initialize — spawn all mobs for the given scene from DB data
// ---------------------------------------------------------------------------
void ServerSpawnManager::initialize(
    const std::string& sceneId,
    World& world,
    ReplicationManager& replication,
    const SpawnZoneCache& spawnZoneCache,
    const MobDefCache& mobDefCache,
    const CollisionGrid* /*collisionGrid*/,
    ZoneMobStateRepository* mobStateRepo)
{
    zoneRows_.clear();
    mobs_.clear();
    totalMobs_ = 0;
    mobStateRepo_ = mobStateRepo;
    sceneId_ = sceneId;
    initializedTime_ = false;

    const auto& zones = spawnZoneCache.getZonesForScene(sceneId);
    for (const auto& zone : zones) {
        const CachedMobDef* def = mobDefCache.get(zone.mobDefId);
        if (!def) {
            LOG_WARN("SpawnManager", "Unknown mob_def_id '%s' in zone '%s' — skipping",
                     zone.mobDefId.c_str(), zone.zoneName.c_str());
            continue;
        }

        int idx = static_cast<int>(zoneRows_.size());
        ZoneRowState state;
        state.config = zone;
        state.def = def;
        state.respawnSeconds = (zone.respawnOverrideSeconds >= 0)
            ? zone.respawnOverrideSeconds : def->respawnSeconds;
        zoneRows_.push_back(std::move(state));

        // Load persisted death state for this zone
        std::vector<DeadMobRecord> persistedDeaths;
        if (mobStateRepo_) {
            persistedDeaths = mobStateRepo_->loadZoneDeaths(sceneId, zone.zoneName);
        }

        std::unordered_set<int> deadIndices;
        std::unordered_map<int, float> remainingTimes;
        for (const auto& rec : persistedDeaths) {
            if (!rec.hasRespawned()) {
                deadIndices.insert(rec.mobIndex);
                remainingTimes[rec.mobIndex] = rec.getRemainingRespawnTime();
            }
        }

        // Spawn target_count mobs for this zone row (skip persisted dead mobs)
        for (int i = 0; i < zone.targetCount; ++i) {
            if (deadIndices.count(i)) {
                TrackedMob tracked;
                tracked.handle = EntityHandle{};
                tracked.zoneRowIndex = idx;
                tracked.alive = false;
                tracked.respawnAt = remainingTimes[i]; // remaining seconds, converted in tick
                mobs_.push_back(tracked);
                ++totalMobs_;
                continue;
            }
            createMob(world, replication, idx, 0.0f);
        }
    }

    LOG_INFO("SpawnManager", "Spawned %d mobs across %d zone rules for scene '%s'",
             totalMobs_, static_cast<int>(zoneRows_.size()), sceneId.c_str());
}

// ---------------------------------------------------------------------------
// createMobEntity — static, single source of truth for server-side mob creation
// ---------------------------------------------------------------------------
EntityHandle ServerSpawnManager::createMobEntity(
    World& world, ReplicationManager& replication,
    const MobCreateParams& params)
{
    const auto* def = params.def;

    // Create entity
    Entity* mob = world.createEntity(def->displayName);
    mob->setTag("mob");

    // Transform
    auto* t = mob->addComponent<Transform>(params.position);
    t->depth = 1.0f;

    // EnemyStatsComponent — fill all stats from CachedMobDef
    auto* esComp = mob->addComponent<EnemyStatsComponent>();
    EnemyStats& es = esComp->stats;
    es.enemyId          = def->mobDefId;
    es.enemyName        = def->displayName;
    es.sceneId          = params.sceneId;
    es.level            = params.level;
    es.baseDamage       = def->getDamageForLevel(params.level);
    es.maxHP            = def->getHPForLevel(params.level);
    es.currentHP        = es.maxHP;
    es.armor            = def->getArmorForLevel(params.level);
    es.magicResist      = def->magicResist;
    es.critRate         = def->critRate;
    es.attackSpeed      = def->attackSpeed;
    es.moveSpeed        = def->moveSpeed;
    es.mobHitRate       = def->mobHitRate;
    es.xpReward         = def->getXPRewardForLevel(params.level);
    es.dealsMagicDamage = def->dealsMagicDamage;
    es.isAggressive     = def->isAggressive;
    es.isBoss           = def->isBoss;
    es.monsterType      = def->monsterType;
    es.lootTableId      = def->lootTableId;
    es.minGoldDrop      = def->minGoldDrop;
    es.maxGoldDrop      = def->maxGoldDrop;
    es.goldDropChance   = def->goldDropChance;
    es.honorReward      = def->honorReward;
    es.isAlive          = true;

    // MobAIComponent — initialize AI with home position and ranges from def
    auto* aiComp = mob->addComponent<MobAIComponent>();
    // Convert tile ranges to pixel ranges (32px per tile)
    aiComp->ai.acquireRadius   = def->aggroRange * 32.0f;
    aiComp->ai.attackRange     = def->attackRange * 32.0f;
    aiComp->ai.contactRadius   = def->leashRadius * 32.0f;
    aiComp->ai.attackCooldown  = def->attackSpeed;  // direct seconds, NOT inverted
    aiComp->ai.isPassive       = !def->isAggressive;
    // moveSpeed: convert tiles/sec to px/sec (32px per tile)
    aiComp->ai.baseChaseSpeed  = def->moveSpeed * 32.0f;
    aiComp->ai.baseReturnSpeed = def->moveSpeed * 32.0f;
    aiComp->ai.baseRoamSpeed   = def->moveSpeed * 32.0f * 0.6f;
    // roamRadius: 40% of zone radius
    aiComp->ai.roamRadius      = params.zoneRadius * 0.4f;
    aiComp->ai.initialize(params.position);  // sets homePos and initial position

    // MobNameplateComponent — for replication buildEnterMessage
    auto* np = mob->addComponent<MobNameplateComponent>();
    np->displayName = def->displayName;
    np->level       = params.level;
    np->isBoss      = def->isBoss;
    np->visible     = true;

    // Register with replication manager
    PersistentId pid = PersistentId::generate(1);
    replication.registerEntity(mob->handle(), pid);

    return mob->handle();
}

// ---------------------------------------------------------------------------
// createMob — thin wrapper: rolls level/position, delegates to createMobEntity
// ---------------------------------------------------------------------------
EntityHandle ServerSpawnManager::createMob(
    World& world, ReplicationManager& replication,
    int zoneRowIndex, float /*gameTime*/)
{
    auto& row = zoneRows_[zoneRowIndex];
    const auto* def = row.def;

    // Random level within [minSpawnLevel, maxSpawnLevel]
    int level = def->minSpawnLevel;
    if (def->maxSpawnLevel > def->minSpawnLevel) {
        std::uniform_int_distribution<int> levelDist(def->minSpawnLevel, def->maxSpawnLevel);
        level = levelDist(s_rng);
    }

    // Random position within zone bounds
    Vec2 pos = randomPositionInZone(row.config);

    // Build params and delegate to the static factory
    MobCreateParams params;
    params.def        = def;
    params.position   = pos;
    params.level      = level;
    params.sceneId    = row.config.sceneId;
    params.zoneRadius = row.config.radius;

    EntityHandle handle = createMobEntity(world, replication, params);

    // Track this mob
    TrackedMob tracked;
    tracked.handle       = handle;
    tracked.zoneRowIndex = zoneRowIndex;
    tracked.alive        = true;
    tracked.respawnAt    = 0.0f;
    mobs_.push_back(tracked);
    ++totalMobs_;

    return handle;
}

// ---------------------------------------------------------------------------
// tick — check for deaths, process respawn timers
// ---------------------------------------------------------------------------
void ServerSpawnManager::tick(float /*dt*/, float gameTime, World& world, ReplicationManager& replication) {
    if (!initializedTime_) {
        for (auto& mob : mobs_) {
            if (!mob.alive && mob.respawnAt > 0.0f) {
                mob.respawnAt = gameTime + mob.respawnAt;
            }
        }
        initializedTime_ = true;
    }

    for (auto& mob : mobs_) {
        if (mob.alive) {
            // Check if entity was destroyed outside our knowledge
            Entity* e = world.getEntity(mob.handle);
            if (!e) {
                mob.alive = false;
                mob.respawnAt = gameTime + static_cast<float>(zoneRows_[mob.zoneRowIndex].respawnSeconds);
                continue;
            }

            // Check if EnemyStats marks mob as dead
            auto* esComp = e->getComponent<EnemyStatsComponent>();
            if (esComp && !esComp->stats.isAlive) {
                mob.alive     = false;
                mob.respawnAt = gameTime + static_cast<float>(zoneRows_[mob.zoneRowIndex].respawnSeconds);

                // Unregister from replication (sends SvEntityLeave to clients)
                replication.unregisterEntity(mob.handle);
                world.destroyEntity(mob.handle);
            }
        } else {
            // Dead — check respawn timer
            if (gameTime >= mob.respawnAt) {
                // createMob pushes a new TrackedMob onto the back of mobs_.
                // We are reusing this existing slot, so we pop the duplicate
                // and undo its totalMobs_ increment.
                EntityHandle newHandle = createMob(world, replication, mob.zoneRowIndex, gameTime);
                mobs_.pop_back();   // remove the entry createMob just pushed
                --totalMobs_;       // createMob incremented, reverse it for the slot we're reusing

                // Reuse this slot
                mob.handle = newHandle;
                mob.alive  = true;
            }
        }
    }

    world.processDestroyQueue();
}

// ---------------------------------------------------------------------------
// onMobDeath — external callback (MobAISystem combat result)
// ---------------------------------------------------------------------------
void ServerSpawnManager::onMobDeath(EntityHandle handle, float gameTime) {
    for (int i = 0; i < static_cast<int>(mobs_.size()); ++i) {
        auto& mob = mobs_[i];
        if (mob.handle == handle && mob.alive) {
            auto& row = zoneRows_[mob.zoneRowIndex];
            mob.alive = false;
            mob.respawnAt = gameTime + static_cast<float>(row.respawnSeconds);

            if (mobStateRepo_) {
                DeadMobRecord rec;
                rec.enemyId = row.def->mobDefId;
                rec.mobIndex = i;
                rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr));
                rec.respawnSeconds = row.respawnSeconds;
                mobStateRepo_->saveZoneDeaths(sceneId_, row.config.zoneName, {rec});
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// shutdown — persist all dead mobs to DB before server shutdown
// ---------------------------------------------------------------------------
void ServerSpawnManager::shutdown() {
    if (!mobStateRepo_) return;

    std::unordered_map<std::string, std::vector<DeadMobRecord>> deathsByZone;
    for (int i = 0; i < static_cast<int>(mobs_.size()); ++i) {
        const auto& mob = mobs_[i];
        if (mob.alive) continue;

        auto& row = zoneRows_[mob.zoneRowIndex];
        DeadMobRecord rec;
        rec.enemyId = row.def->mobDefId;
        rec.mobIndex = i;
        rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr));
        rec.respawnSeconds = row.respawnSeconds;
        deathsByZone[row.config.zoneName].push_back(rec);
    }

    for (const auto& [zoneName, deaths] : deathsByZone) {
        mobStateRepo_->saveZoneDeaths(sceneId_, zoneName, deaths);
    }

    LOG_INFO("SpawnManager", "Saved death state for scene '%s' (%zu zones with dead mobs)",
             sceneId_.c_str(), deathsByZone.size());
}

// ---------------------------------------------------------------------------
// randomPositionInZone — uniform random point within axis-aligned zone square
// ---------------------------------------------------------------------------
Vec2 ServerSpawnManager::randomPositionInZone(const SpawnZoneRow& zone) {
    std::uniform_real_distribution<float> xDist(zone.centerX - zone.radius, zone.centerX + zone.radius);
    std::uniform_real_distribution<float> yDist(zone.centerY - zone.radius, zone.centerY + zone.radius);
    return {xDist(s_rng), yDist(s_rng)};
}

} // namespace fate

