#include "server/db/definition_caches.h"
#include "engine/core/logger.h"

namespace fate {

// ============================================================================
// MobDefCache
// ============================================================================

bool MobDefCache::initialize(pqxx::connection& conn) {
    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT mob_def_id, COALESCE(display_name, mob_name) as display_name, "
            "base_hp, base_damage, COALESCE(base_armor,0) as base_armor, "
            "crit_rate, attack_speed, move_speed, "
            "hp_per_level, damage_per_level, COALESCE(armor_per_level,0.5) as armor_per_level, "
            "base_xp_reward, COALESCE(xp_per_level,2) as xp_per_level, "
            "aggro_range, attack_range, leash_radius, respawn_seconds, "
            "min_spawn_level, max_spawn_level, COALESCE(spawn_weight,10) as spawn_weight, "
            "COALESCE(is_aggressive,true) as is_aggressive, "
            "is_boss, COALESCE(is_elite,false) as is_elite, "
            "COALESCE(attack_style,'Melee') as attack_style, "
            "COALESCE(monster_type,'Normal') as monster_type, "
            "loot_table_id, "
            "COALESCE(min_gold_drop,0) as min_gold_drop, "
            "COALESCE(max_gold_drop,0) as max_gold_drop, "
            "COALESCE(gold_drop_chance,1.0) as gold_drop_chance, "
            "COALESCE(magic_resist,0) as magic_resist, "
            "COALESCE(deals_magic_damage,FALSE) as deals_magic_damage, "
            "COALESCE(mob_hit_rate,0) as mob_hit_rate, "
            "COALESCE(honor_reward,0) as honor_reward "
            "FROM mob_definitions");
        txn.commit();

        mobs_.clear();
        mobs_.reserve(result.size());
        for (const auto& row : result) {
            CachedMobDef d;
            d.mobDefId         = row["mob_def_id"].as<std::string>();
            d.displayName      = row["display_name"].as<std::string>();
            d.baseHP           = row["base_hp"].as<int>();
            d.baseDamage       = row["base_damage"].as<int>();
            d.baseArmor        = row["base_armor"].as<int>();
            d.critRate         = row["crit_rate"].as<float>();
            d.attackSpeed      = row["attack_speed"].as<float>();
            d.moveSpeed        = row["move_speed"].as<float>();
            d.hpPerLevel       = row["hp_per_level"].as<float>();
            d.damagePerLevel   = row["damage_per_level"].as<float>();
            d.armorPerLevel    = row["armor_per_level"].as<float>();
            d.baseXPReward     = row["base_xp_reward"].as<int>();
            d.xpPerLevel       = row["xp_per_level"].as<float>();
            d.aggroRange       = row["aggro_range"].as<float>();
            d.attackRange      = row["attack_range"].as<float>();
            d.leashRadius      = row["leash_radius"].as<float>();
            d.respawnSeconds   = row["respawn_seconds"].as<int>();
            d.minSpawnLevel    = row["min_spawn_level"].as<int>();
            d.maxSpawnLevel    = row["max_spawn_level"].as<int>();
            d.spawnWeight      = row["spawn_weight"].as<int>();
            d.isAggressive     = row["is_aggressive"].as<bool>();
            d.isBoss           = row["is_boss"].as<bool>();
            d.isElite          = row["is_elite"].as<bool>();
            d.attackStyle      = row["attack_style"].as<std::string>();
            d.monsterType      = row["monster_type"].as<std::string>();
            d.lootTableId      = row["loot_table_id"].is_null() ? "" : row["loot_table_id"].as<std::string>();
            d.minGoldDrop      = row["min_gold_drop"].as<int>();
            d.maxGoldDrop      = row["max_gold_drop"].as<int>();
            d.goldDropChance   = row["gold_drop_chance"].as<float>();
            d.magicResist      = row["magic_resist"].as<int>();
            d.dealsMagicDamage = row["deals_magic_damage"].as<bool>();
            d.mobHitRate       = row["mob_hit_rate"].as<int>();
            d.honorReward      = row["honor_reward"].as<int>();
            mobs_[d.mobDefId] = std::move(d);
        }

        LOG_INFO("MobCache", "Loaded %d mob definitions", static_cast<int>(mobs_.size()));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MobCache", "Failed to load mob definitions: %s", e.what());
        return false;
    }
}

void MobDefCache::reload(pqxx::connection& conn) { initialize(conn); }

const CachedMobDef* MobDefCache::get(const std::string& mobDefId) const {
    auto it = mobs_.find(mobDefId);
    return it != mobs_.end() ? &it->second : nullptr;
}

bool MobDefCache::has(const std::string& mobDefId) const {
    return mobs_.count(mobDefId) > 0;
}

// ============================================================================
// SkillDefCache
// ============================================================================

bool SkillDefCache::initialize(pqxx::connection& conn) {
    try {
        pqxx::work txn(conn);

        // Load skill definitions
        auto skillResult = txn.exec(
            "SELECT skill_id, skill_name, class_req, skill_type, level_required, "
            "resource_type, target_type, range_tiles, aoe_radius, damage_type, "
            "can_crit, uses_hit_rate, fury_on_hit, is_ultimate, cast_time, channel_time, "
            "applies_bleed, applies_burn, applies_poison, applies_slow, "
            "applies_taunt, applies_freeze, "
            "grants_invulnerability, grants_stun_immunity, grants_crit_guarantee, removes_debuffs, "
            "teleport_distance, dash_distance, "
            "is_resurrection, locks_movement, consumes_all_resource, scales_with_resource, "
            "description, animation_trigger "
            "FROM skill_definitions ORDER BY class_req, level_required");

        skills_.clear();
        skills_.reserve(skillResult.size());
        for (const auto& row : skillResult) {
            CachedSkillDef d;
            d.skillId       = row["skill_id"].as<std::string>();
            d.skillName     = row["skill_name"].as<std::string>();
            d.classRequired = row["class_req"].is_null() ? "" : row["class_req"].as<std::string>();
            d.skillType     = row["skill_type"].is_null() ? "Active" : row["skill_type"].as<std::string>();
            d.levelRequired = row["level_required"].is_null() ? 1 : row["level_required"].as<int>();
            d.resourceType  = row["resource_type"].is_null() ? "None" : row["resource_type"].as<std::string>();
            d.targetType    = row["target_type"].is_null() ? "Self" : row["target_type"].as<std::string>();
            d.range         = row["range_tiles"].is_null() ? 1.0f : row["range_tiles"].as<float>();
            d.aoeRadius     = row["aoe_radius"].is_null() ? 0.0f : row["aoe_radius"].as<float>();
            d.damageType    = row["damage_type"].is_null() ? "Physical" : row["damage_type"].as<std::string>();
            d.canCrit       = row["can_crit"].is_null() ? true : row["can_crit"].as<bool>();
            d.usesHitRate   = row["uses_hit_rate"].is_null() ? true : row["uses_hit_rate"].as<bool>();
            d.furyOnHit     = row["fury_on_hit"].is_null() ? 0.0f : row["fury_on_hit"].as<float>();
            d.isUltimate    = row["is_ultimate"].is_null() ? false : row["is_ultimate"].as<bool>();
            d.castTime      = row["cast_time"].is_null() ? 0.0f : row["cast_time"].as<float>();
            d.channelTime   = row["channel_time"].is_null() ? 0.0f : row["channel_time"].as<float>();
            d.appliesBleed  = row["applies_bleed"].is_null() ? false : row["applies_bleed"].as<bool>();
            d.appliesBurn   = row["applies_burn"].is_null() ? false : row["applies_burn"].as<bool>();
            d.appliesPoison = row["applies_poison"].is_null() ? false : row["applies_poison"].as<bool>();
            d.appliesSlow   = row["applies_slow"].is_null() ? false : row["applies_slow"].as<bool>();
            d.appliesTaunt  = row["applies_taunt"].is_null() ? false : row["applies_taunt"].as<bool>();
            d.appliesFreeze = row["applies_freeze"].is_null() ? false : row["applies_freeze"].as<bool>();
            d.grantsInvulnerability = row["grants_invulnerability"].is_null() ? false : row["grants_invulnerability"].as<bool>();
            d.grantsStunImmunity    = row["grants_stun_immunity"].is_null() ? false : row["grants_stun_immunity"].as<bool>();
            d.grantsCritGuarantee   = row["grants_crit_guarantee"].is_null() ? false : row["grants_crit_guarantee"].as<bool>();
            d.removesDebuffs        = row["removes_debuffs"].is_null() ? false : row["removes_debuffs"].as<bool>();
            d.teleportDistance = row["teleport_distance"].is_null() ? 0.0f : row["teleport_distance"].as<float>();
            d.dashDistance     = row["dash_distance"].is_null() ? 0.0f : row["dash_distance"].as<float>();
            d.isResurrection     = row["is_resurrection"].is_null() ? false : row["is_resurrection"].as<bool>();
            d.locksMovement      = row["locks_movement"].is_null() ? false : row["locks_movement"].as<bool>();
            d.consumesAllResource = row["consumes_all_resource"].is_null() ? false : row["consumes_all_resource"].as<bool>();
            d.scalesWithResource  = row["scales_with_resource"].is_null() ? false : row["scales_with_resource"].as<bool>();
            d.description      = row["description"].is_null() ? "" : row["description"].as<std::string>();
            d.animationTrigger = row["animation_trigger"].is_null() ? "" : row["animation_trigger"].as<std::string>();
            skills_[d.skillId] = std::move(d);
        }

        // Load skill ranks
        auto rankResult = txn.exec(
            "SELECT skill_id, rank, resource_cost, cooldown_seconds, "
            "damage_percent, max_targets, effect_duration, effect_value, effect_value_2, "
            "stun_duration, execute_threshold, "
            "passive_damage_reduction, passive_crit_bonus, passive_speed_bonus, "
            "passive_hp_bonus, passive_stat_bonus, passive_armor_bonus, passive_hit_rate_bonus, "
            "transform_damage_mult, transform_speed_bonus, resurrect_hp_percent "
            "FROM skill_ranks ORDER BY skill_id, rank");
        txn.commit();

        ranks_.clear();
        ranksBySkill_.clear();
        for (const auto& row : rankResult) {
            CachedSkillRank r;
            r.skillId       = row["skill_id"].as<std::string>();
            r.rank          = row["rank"].as<int>();
            r.resourceCost  = row["resource_cost"].is_null() ? 0 : row["resource_cost"].as<int>();
            r.cooldownSeconds = row["cooldown_seconds"].is_null() ? 1.0f : row["cooldown_seconds"].as<float>();
            r.damagePercent = row["damage_percent"].is_null() ? 100 : row["damage_percent"].as<int>();
            r.maxTargets    = row["max_targets"].is_null() ? 1 : row["max_targets"].as<int>();
            r.effectDuration = row["effect_duration"].is_null() ? 0.0f : row["effect_duration"].as<float>();
            r.effectValue   = row["effect_value"].is_null() ? 0.0f : row["effect_value"].as<float>();
            r.effectValue2  = row["effect_value_2"].is_null() ? 0.0f : row["effect_value_2"].as<float>();
            r.stunDuration  = row["stun_duration"].is_null() ? 0.0f : row["stun_duration"].as<float>();
            r.executeThreshold = row["execute_threshold"].is_null() ? 0.0f : row["execute_threshold"].as<float>();
            r.passiveDamageReduction = row["passive_damage_reduction"].is_null() ? 0.0f : row["passive_damage_reduction"].as<float>();
            r.passiveCritBonus  = row["passive_crit_bonus"].is_null() ? 0.0f : row["passive_crit_bonus"].as<float>();
            r.passiveSpeedBonus = row["passive_speed_bonus"].is_null() ? 0.0f : row["passive_speed_bonus"].as<float>();
            r.passiveHPBonus    = row["passive_hp_bonus"].is_null() ? 0.0f : row["passive_hp_bonus"].as<float>();
            r.passiveStatBonus  = row["passive_stat_bonus"].is_null() ? 0 : row["passive_stat_bonus"].as<int>();
            r.passiveArmorBonus = row["passive_armor_bonus"].is_null() ? 0.0f : row["passive_armor_bonus"].as<float>();
            r.passiveHitRateBonus = row["passive_hit_rate_bonus"].is_null() ? 0.0f : row["passive_hit_rate_bonus"].as<float>();
            r.transformDamageMult = row["transform_damage_mult"].is_null() ? 0.0f : row["transform_damage_mult"].as<float>();
            r.transformSpeedBonus = row["transform_speed_bonus"].is_null() ? 0.0f : row["transform_speed_bonus"].as<float>();
            r.resurrectHPPercent  = row["resurrect_hp_percent"].is_null() ? 0.0f : row["resurrect_hp_percent"].as<float>();

            ranks_[rankKey(r.skillId, r.rank)] = r;
            ranksBySkill_[r.skillId].push_back(r);
        }

        LOG_INFO("SkillCache", "Loaded %d skills, %d ranks",
                 static_cast<int>(skills_.size()), static_cast<int>(ranks_.size()));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SkillCache", "Failed to load skill definitions: %s", e.what());
        return false;
    }
}

void SkillDefCache::reload(pqxx::connection& conn) { initialize(conn); }

const CachedSkillDef* SkillDefCache::getSkill(const std::string& skillId) const {
    auto it = skills_.find(skillId);
    return it != skills_.end() ? &it->second : nullptr;
}

std::vector<CachedSkillRank> SkillDefCache::getRanks(const std::string& skillId) const {
    auto it = ranksBySkill_.find(skillId);
    return it != ranksBySkill_.end() ? it->second : std::vector<CachedSkillRank>{};
}

const CachedSkillRank* SkillDefCache::getRank(const std::string& skillId, int rank) const {
    auto it = ranks_.find(rankKey(skillId, rank));
    return it != ranks_.end() ? &it->second : nullptr;
}

std::vector<const CachedSkillDef*> SkillDefCache::getSkillsForClass(const std::string& className) const {
    std::vector<const CachedSkillDef*> result;
    for (const auto& [id, skill] : skills_) {
        if (skill.classRequired == className || skill.classRequired.empty()) {
            result.push_back(&skill);
        }
    }
    return result;
}

bool SkillDefCache::has(const std::string& skillId) const {
    return skills_.count(skillId) > 0;
}

// ============================================================================
// SceneCache
// ============================================================================

bool SceneCache::initialize(pqxx::connection& conn) {
    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT scene_id, scene_name, scene_type, min_level, is_dungeon, pvp_enabled "
            "FROM scenes");
        txn.commit();

        scenes_.clear();
        for (const auto& row : result) {
            SceneInfoRecord s;
            s.sceneId    = row["scene_id"].as<std::string>();
            s.sceneName  = row["scene_name"].is_null() ? s.sceneId : row["scene_name"].as<std::string>();
            s.sceneType  = row["scene_type"].is_null() ? "zone" : row["scene_type"].as<std::string>();
            s.minLevel   = row["min_level"].is_null() ? 1 : row["min_level"].as<int>();
            s.isDungeon  = row["is_dungeon"].is_null() ? false : row["is_dungeon"].as<bool>();
            s.pvpEnabled = row["pvp_enabled"].is_null() ? false : row["pvp_enabled"].as<bool>();
            scenes_[s.sceneId] = std::move(s);
        }

        LOG_INFO("SceneCache", "Loaded %d scene definitions", static_cast<int>(scenes_.size()));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SceneCache", "Failed to load scene definitions: %s", e.what());
        return false;
    }
}

const SceneInfoRecord* SceneCache::get(const std::string& sceneId) const {
    auto it = scenes_.find(sceneId);
    return it != scenes_.end() ? &it->second : nullptr;
}

bool SceneCache::isPvPEnabled(const std::string& sceneId) const {
    auto* s = get(sceneId);
    return s ? s->pvpEnabled : false;
}

} // namespace fate
