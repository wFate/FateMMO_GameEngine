#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/net/admin_messages.h"
#include "engine/net/packet.h"
#include "game/shared/game_types.h"
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>

namespace fate {

// ============================================================================
// Helpers
// ============================================================================

void ServerApp::sendAdminResult(uint16_t clientId, uint8_t requestType,
                                 bool success, const std::string& message) {
    SvAdminResultMsg res;
    res.requestType = requestType;
    res.success     = success ? 1 : 0;
    res.message     = message;
    uint8_t buf[1024];
    ByteWriter w(buf, sizeof(buf));
    res.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered,
                   PacketType::SvAdminResult, buf, w.size());
}

void ServerApp::reloadCacheByType(uint8_t cacheType) {
    auto& conn = gameDbConn_.connection();
    switch (cacheType) {
        case AdminCacheType::MobDefs:
            mobDefCache_.reload(conn);
            break;
        case AdminCacheType::ItemDefs:
            itemDefCache_.reload(conn);
            break;
        case AdminCacheType::LootTables:
            lootTableCache_.reload(conn);
            break;
        case AdminCacheType::SpawnZones:
            spawnZoneCache_.reload(conn);
            break;
        case AdminCacheType::SkillDefs:
            skillDefCache_.reload(conn);
            break;
        case AdminCacheType::Recipes:
            recipeCache_.reload(conn);
            break;
        case AdminCacheType::Pets:
            petDefCache_.reload(conn);
            break;
        case AdminCacheType::Collections:
            collectionCache_.reload(conn);
            break;
        case AdminCacheType::Costumes:
            costumeCache_.reload(conn);
            break;
        case AdminCacheType::All:
            itemDefCache_.reload(conn);
            lootTableCache_.reload(conn);
            mobDefCache_.reload(conn);
            skillDefCache_.reload(conn);
            spawnZoneCache_.reload(conn);
            recipeCache_.reload(conn);
            petDefCache_.reload(conn);
            collectionCache_.reload(conn);
            costumeCache_.reload(conn);
            break;
        default:
            LOG_WARN("Admin", "Unknown cache type %d for reload", cacheType);
            break;
    }
}

// Map AdminContentType to AdminCacheType for auto-reload after save/delete
static uint8_t contentTypeToCacheType(uint8_t contentType) {
    switch (contentType) {
        case AdminContentType::Mob:       return AdminCacheType::MobDefs;
        case AdminContentType::Item:      return AdminCacheType::ItemDefs;
        case AdminContentType::LootDrop:  return AdminCacheType::LootTables;
        case AdminContentType::SpawnZone: return AdminCacheType::SpawnZones;
        default: return AdminCacheType::All;
    }
}

// ============================================================================
// DB insert/update helpers
// ============================================================================

static void insertMob(pqxx::work& txn, const nlohmann::json& j) {
    txn.exec_params(
        "INSERT INTO mob_definitions ("
        "mob_def_id, mob_name, display_name, base_hp, base_damage, base_armor, "
        "crit_rate, attack_speed, move_speed, magic_resist, deals_magic_damage, mob_hit_rate, "
        "hp_per_level, damage_per_level, armor_per_level, "
        "base_xp_reward, xp_per_level, "
        "aggro_range, attack_range, leash_radius, respawn_seconds, "
        "min_spawn_level, max_spawn_level, spawn_weight, "
        "is_aggressive, is_boss, is_elite, "
        "attack_style, monster_type, loot_table_id, "
        "min_gold_drop, max_gold_drop, gold_drop_chance, honor_reward"
        ") VALUES ("
        "$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,"
        "$16,$17,$18,$19,$20,$21,$22,$23,$24,$25,$26,$27,$28,$29,$30,$31,$32,$33,$34"
        ")",
        j.at("mob_def_id").get<std::string>(),
        j.value("mob_name", j.at("mob_def_id").get<std::string>()),
        j.value("display_name", j.value("mob_name", j.at("mob_def_id").get<std::string>())),
        j.value("base_hp", 100),
        j.value("base_damage", 10),
        j.value("base_armor", 0),
        j.value("crit_rate", 0.05),
        j.value("attack_speed", 1.5),
        j.value("move_speed", 1.8),
        j.value("magic_resist", 0),
        j.value("deals_magic_damage", false),
        j.value("mob_hit_rate", 0),
        j.value("hp_per_level", 20.0),
        j.value("damage_per_level", 2.0),
        j.value("armor_per_level", 0.5),
        j.value("base_xp_reward", 10),
        j.value("xp_per_level", 2.0),
        j.value("aggro_range", 4.0),
        j.value("attack_range", 1.0),
        j.value("leash_radius", 6.0),
        j.value("respawn_seconds", 30),
        j.value("min_spawn_level", 1),
        j.value("max_spawn_level", 100),
        j.value("spawn_weight", 10),
        j.value("is_aggressive", true),
        j.value("is_boss", false),
        j.value("is_elite", false),
        j.value("attack_style", std::string("Melee")),
        j.value("monster_type", std::string("Normal")),
        j.value("loot_table_id", std::string("")),
        j.value("min_gold_drop", 0),
        j.value("max_gold_drop", 0),
        j.value("gold_drop_chance", 1.0),
        j.value("honor_reward", 0)
    );
}

static void updateMob(pqxx::work& txn, const nlohmann::json& j) {
    std::string id = j.at("mob_def_id").get<std::string>();
    txn.exec_params(
        "UPDATE mob_definitions SET "
        "mob_name=$2, display_name=$3, base_hp=$4, base_damage=$5, base_armor=$6, "
        "crit_rate=$7, attack_speed=$8, move_speed=$9, magic_resist=$10, "
        "deals_magic_damage=$11, mob_hit_rate=$12, "
        "hp_per_level=$13, damage_per_level=$14, armor_per_level=$15, "
        "base_xp_reward=$16, xp_per_level=$17, "
        "aggro_range=$18, attack_range=$19, leash_radius=$20, respawn_seconds=$21, "
        "min_spawn_level=$22, max_spawn_level=$23, spawn_weight=$24, "
        "is_aggressive=$25, is_boss=$26, is_elite=$27, "
        "attack_style=$28, monster_type=$29, loot_table_id=$30, "
        "min_gold_drop=$31, max_gold_drop=$32, gold_drop_chance=$33, honor_reward=$34 "
        "WHERE mob_def_id=$1",
        id,
        j.value("mob_name", id),
        j.value("display_name", j.value("mob_name", id)),
        j.value("base_hp", 100),
        j.value("base_damage", 10),
        j.value("base_armor", 0),
        j.value("crit_rate", 0.05),
        j.value("attack_speed", 1.5),
        j.value("move_speed", 1.8),
        j.value("magic_resist", 0),
        j.value("deals_magic_damage", false),
        j.value("mob_hit_rate", 0),
        j.value("hp_per_level", 20.0),
        j.value("damage_per_level", 2.0),
        j.value("armor_per_level", 0.5),
        j.value("base_xp_reward", 10),
        j.value("xp_per_level", 2.0),
        j.value("aggro_range", 4.0),
        j.value("attack_range", 1.0),
        j.value("leash_radius", 6.0),
        j.value("respawn_seconds", 30),
        j.value("min_spawn_level", 1),
        j.value("max_spawn_level", 100),
        j.value("spawn_weight", 10),
        j.value("is_aggressive", true),
        j.value("is_boss", false),
        j.value("is_elite", false),
        j.value("attack_style", std::string("Melee")),
        j.value("monster_type", std::string("Normal")),
        j.value("loot_table_id", std::string("")),
        j.value("min_gold_drop", 0),
        j.value("max_gold_drop", 0),
        j.value("gold_drop_chance", 1.0),
        j.value("honor_reward", 0)
    );
}

static void insertItem(pqxx::work& txn, const nlohmann::json& j) {
    txn.exec_params(
        "INSERT INTO item_definitions ("
        "item_id, name, type, subtype, class_req, level_req, "
        "damage_min, damage_max, armor, description, "
        "gold_value, max_stack, icon_path, "
        "is_socketable, is_soulbound, rarity, max_enchant, visual_style, "
        "possible_stats, attributes"
        ") VALUES ("
        "$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20"
        ")",
        j.at("item_id").get<std::string>(),
        j.value("name", j.at("item_id").get<std::string>()),
        j.value("type", std::string("Consumable")),
        j.value("subtype", std::string("")),
        j.value("class_req", std::string("All")),
        j.value("level_req", 1),
        j.value("damage_min", 0),
        j.value("damage_max", 0),
        j.value("armor", 0),
        j.value("description", std::string("")),
        j.value("gold_value", 0),
        j.value("max_stack", 1),
        j.value("icon_path", std::string("")),
        j.value("is_socketable", false),
        j.value("is_soulbound", false),
        j.value("rarity", std::string("Common")),
        j.value("max_enchant", 12),
        j.value("visual_style", std::string("")),
        j.value("possible_stats", std::string("[]")),
        j.value("attributes", std::string("{}"))
    );
}

static void updateItem(pqxx::work& txn, const nlohmann::json& j) {
    std::string id = j.at("item_id").get<std::string>();
    txn.exec_params(
        "UPDATE item_definitions SET "
        "name=$2, type=$3, subtype=$4, class_req=$5, level_req=$6, "
        "damage_min=$7, damage_max=$8, armor=$9, description=$10, "
        "gold_value=$11, max_stack=$12, icon_path=$13, "
        "is_socketable=$14, is_soulbound=$15, rarity=$16, max_enchant=$17, visual_style=$18, "
        "possible_stats=$19, attributes=$20 "
        "WHERE item_id=$1",
        id,
        j.value("name", id),
        j.value("type", std::string("Consumable")),
        j.value("subtype", std::string("")),
        j.value("class_req", std::string("All")),
        j.value("level_req", 1),
        j.value("damage_min", 0),
        j.value("damage_max", 0),
        j.value("armor", 0),
        j.value("description", std::string("")),
        j.value("gold_value", 0),
        j.value("max_stack", 1),
        j.value("icon_path", std::string("")),
        j.value("is_socketable", false),
        j.value("is_soulbound", false),
        j.value("rarity", std::string("Common")),
        j.value("max_enchant", 12),
        j.value("visual_style", std::string("")),
        j.value("possible_stats", std::string("[]")),
        j.value("attributes", std::string("{}"))
    );
}

static void insertLootDrop(pqxx::work& txn, const nlohmann::json& j) {
    txn.exec_params(
        "INSERT INTO loot_drops (loot_table_id, item_id, drop_chance, min_quantity, max_quantity) "
        "VALUES ($1,$2,$3,$4,$5)",
        j.at("loot_table_id").get<std::string>(),
        j.at("item_id").get<std::string>(),
        j.value("drop_chance", 0.1),
        j.value("min_quantity", 1),
        j.value("max_quantity", 1)
    );
}

static void updateLootDrop(pqxx::work& txn, const nlohmann::json& j) {
    txn.exec_params(
        "UPDATE loot_drops SET "
        "drop_chance=$3, min_quantity=$4, max_quantity=$5 "
        "WHERE loot_table_id=$1 AND item_id=$2",
        j.at("loot_table_id").get<std::string>(),
        j.at("item_id").get<std::string>(),
        j.value("drop_chance", 0.1),
        j.value("min_quantity", 1),
        j.value("max_quantity", 1)
    );
}

static void insertSpawnZone(pqxx::work& txn, const nlohmann::json& j) {
    // zone_id is auto-generated by DB sequence if not provided
    if (j.contains("zone_id")) {
        txn.exec_params(
            "INSERT INTO spawn_zones "
            "(zone_id, scene_id, zone_name, mob_def_id, center_x, center_y, radius, zone_shape, target_count, respawn_override_seconds) "
            "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)",
            j.at("zone_id").get<int>(),
            j.at("scene_id").get<std::string>(),
            j.value("zone_name", std::string("")),
            j.at("mob_def_id").get<std::string>(),
            j.value("center_x", 0.0),
            j.value("center_y", 0.0),
            j.value("radius", 100.0),
            j.value("zone_shape", std::string("circle")),
            j.value("target_count", 3),
            j.value("respawn_override_seconds", -1)
        );
    } else {
        txn.exec_params(
            "INSERT INTO spawn_zones "
            "(scene_id, zone_name, mob_def_id, center_x, center_y, radius, zone_shape, target_count, respawn_override_seconds) "
            "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9)",
            j.at("scene_id").get<std::string>(),
            j.value("zone_name", std::string("")),
            j.at("mob_def_id").get<std::string>(),
            j.value("center_x", 0.0),
            j.value("center_y", 0.0),
            j.value("radius", 100.0),
            j.value("zone_shape", std::string("circle")),
            j.value("target_count", 3),
            j.value("respawn_override_seconds", -1)
        );
    }
}

static void updateSpawnZone(pqxx::work& txn, const nlohmann::json& j) {
    txn.exec_params(
        "UPDATE spawn_zones SET "
        "scene_id=$2, zone_name=$3, mob_def_id=$4, "
        "center_x=$5, center_y=$6, radius=$7, zone_shape=$8, "
        "target_count=$9, respawn_override_seconds=$10 "
        "WHERE zone_id=$1",
        j.at("zone_id").get<int>(),
        j.at("scene_id").get<std::string>(),
        j.value("zone_name", std::string("")),
        j.at("mob_def_id").get<std::string>(),
        j.value("center_x", 0.0),
        j.value("center_y", 0.0),
        j.value("radius", 100.0),
        j.value("zone_shape", std::string("circle")),
        j.value("target_count", 3),
        j.value("respawn_override_seconds", -1)
    );
}

// ============================================================================
// Cache -> JSON serialization
// ============================================================================

static nlohmann::json mobsToJson(const MobDefCache& cache) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [id, m] : cache.allMobs()) {
        nlohmann::json j;
        j["mob_def_id"]       = m.mobDefId;
        j["mob_name"]         = m.mobDefId;  // mob_name defaults to id (COALESCE in DB)
        j["display_name"]     = m.displayName;
        j["base_hp"]          = m.baseHP;
        j["base_damage"]      = m.baseDamage;
        j["base_armor"]       = m.baseArmor;
        j["crit_rate"]        = m.critRate;
        j["attack_speed"]     = m.attackSpeed;
        j["move_speed"]       = m.moveSpeed;
        j["magic_resist"]     = m.magicResist;
        j["deals_magic_damage"] = m.dealsMagicDamage;
        j["mob_hit_rate"]     = m.mobHitRate;
        j["hp_per_level"]     = m.hpPerLevel;
        j["damage_per_level"] = m.damagePerLevel;
        j["armor_per_level"]  = m.armorPerLevel;
        j["base_xp_reward"]   = m.baseXPReward;
        j["xp_per_level"]     = m.xpPerLevel;
        j["aggro_range"]      = m.aggroRange;
        j["attack_range"]     = m.attackRange;
        j["leash_radius"]     = m.leashRadius;
        j["respawn_seconds"]  = m.respawnSeconds;
        j["min_spawn_level"]  = m.minSpawnLevel;
        j["max_spawn_level"]  = m.maxSpawnLevel;
        j["spawn_weight"]     = m.spawnWeight;
        j["is_aggressive"]    = m.isAggressive;
        j["is_boss"]          = m.isBoss;
        j["is_elite"]         = m.isElite;
        j["attack_style"]     = m.attackStyle;
        j["monster_type"]     = m.monsterType;
        j["loot_table_id"]    = m.lootTableId;
        j["min_gold_drop"]    = m.minGoldDrop;
        j["max_gold_drop"]    = m.maxGoldDrop;
        j["gold_drop_chance"] = m.goldDropChance;
        j["honor_reward"]     = m.honorReward;
        arr.push_back(std::move(j));
    }
    return arr;
}

static nlohmann::json itemsToJson(const ItemDefinitionCache& cache) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [id, d] : cache.allItems()) {
        nlohmann::json j;
        j["item_id"]      = d.itemId;
        j["name"]         = d.displayName;
        j["type"]         = d.itemType;
        j["subtype"]      = d.subtype;
        j["class_req"]    = d.classReq;
        j["level_req"]    = d.levelReq;
        j["damage_min"]   = d.damageMin;
        j["damage_max"]   = d.damageMax;
        j["armor"]        = d.armor;
        j["description"]  = d.description;
        j["gold_value"]   = d.goldValue;
        j["max_stack"]    = d.maxStack;
        j["icon_path"]    = d.iconPath;
        j["is_socketable"] = d.isSocketable;
        j["is_soulbound"] = d.isSoulbound;
        j["rarity"]       = d.rarity;
        j["max_enchant"]  = d.maxEnchant;
        j["visual_style"] = d.visualStyle;
        j["attributes"]   = d.attributes;
        // possible_stats stored as parsed vector in cache, serialize back to JSON string
        {
            nlohmann::json stats = nlohmann::json::array();
            for (const auto& ps : d.possibleStats) {
                stats.push_back({{"stat", ps.stat}, {"min", ps.min}, {"max", ps.max}});
            }
            j["possible_stats"] = stats.dump();
        }
        arr.push_back(std::move(j));
    }
    return arr;
}

static nlohmann::json lootToJson(const LootTableCache& cache) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [tableId, entries] : cache.allTables()) {
        for (const auto& e : entries) {
            nlohmann::json j;
            j["loot_table_id"] = tableId;
            j["item_id"]       = e.itemId;
            j["drop_chance"]   = e.dropChance;
            j["min_quantity"]  = e.minQuantity;
            j["max_quantity"]  = e.maxQuantity;
            arr.push_back(std::move(j));
        }
    }
    return arr;
}

static nlohmann::json spawnsToJson(const SpawnZoneCache& cache) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [sceneId, zones] : cache.allZones()) {
        for (const auto& z : zones) {
            nlohmann::json j;
            j["zone_id"]    = z.zoneId;
            j["scene_id"]   = z.sceneId;
            j["zone_name"]  = z.zoneName;
            j["mob_def_id"] = z.mobDefId;
            j["center_x"]   = z.centerX;
            j["center_y"]   = z.centerY;
            j["radius"]     = z.radius;
            j["zone_shape"] = z.zoneShape;
            j["target_count"] = z.targetCount;
            j["respawn_override_seconds"] = z.respawnOverrideSeconds;
            arr.push_back(std::move(j));
        }
    }
    return arr;
}

// ============================================================================
// processAdminSaveContent
// ============================================================================

void ServerApp::processAdminSaveContent(uint16_t clientId,
                                         const CmdAdminSaveContentMsg& msg) {
    // Admin role check
    auto roleIt = clientAdminRoles_.find(clientId);
    if (roleIt == clientAdminRoles_.end() || roleIt->second < AdminRole::Admin) {
        sendAdminResult(clientId, PacketType::CmdAdminSaveContent, false, "Insufficient permissions");
        return;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(msg.jsonPayload);
    } catch (const std::exception& e) {
        sendAdminResult(clientId, PacketType::CmdAdminSaveContent, false,
                        std::string("Invalid JSON: ") + e.what());
        return;
    }

    try {
        pqxx::work txn(gameDbConn_.connection());

        switch (msg.contentType) {
            case AdminContentType::Mob:
                if (msg.isNew) insertMob(txn, j);
                else           updateMob(txn, j);
                break;
            case AdminContentType::Item:
                if (msg.isNew) insertItem(txn, j);
                else           updateItem(txn, j);
                break;
            case AdminContentType::LootDrop:
                if (msg.isNew) insertLootDrop(txn, j);
                else           updateLootDrop(txn, j);
                break;
            case AdminContentType::SpawnZone:
                if (msg.isNew) insertSpawnZone(txn, j);
                else           updateSpawnZone(txn, j);
                break;
            default:
                sendAdminResult(clientId, PacketType::CmdAdminSaveContent, false,
                                "Unknown content type");
                return;
        }

        txn.commit();

        // Reload the relevant cache
        reloadCacheByType(contentTypeToCacheType(msg.contentType));

        std::string action = msg.isNew ? "Created" : "Updated";
        LOG_INFO("Admin", "Client %d %s content type %d", clientId, action.c_str(), msg.contentType);
        sendAdminResult(clientId, PacketType::CmdAdminSaveContent, true,
                        action + " successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Admin", "Save content failed: %s", e.what());
        sendAdminResult(clientId, PacketType::CmdAdminSaveContent, false,
                        std::string("DB error: ") + e.what());
    }
}

// ============================================================================
// processAdminDeleteContent
// ============================================================================

void ServerApp::processAdminDeleteContent(uint16_t clientId,
                                           const CmdAdminDeleteContentMsg& msg) {
    auto roleIt = clientAdminRoles_.find(clientId);
    if (roleIt == clientAdminRoles_.end() || roleIt->second < AdminRole::Admin) {
        sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, false, "Insufficient permissions");
        return;
    }

    if (msg.contentId.empty()) {
        sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, false, "Empty content ID");
        return;
    }

    try {
        pqxx::work txn(gameDbConn_.connection());

        switch (msg.contentType) {
            case AdminContentType::Mob: {
                // Reference check: ensure no spawn_zones reference this mob
                auto refCheck = txn.exec_params(
                    "SELECT COUNT(*) FROM spawn_zones WHERE mob_def_id = $1",
                    msg.contentId);
                if (refCheck[0][0].as<int>() > 0) {
                    sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, false,
                                    "Cannot delete: mob is referenced by spawn zones");
                    return;
                }
                txn.exec_params("DELETE FROM mob_definitions WHERE mob_def_id = $1",
                                msg.contentId);
                break;
            }
            case AdminContentType::Item: {
                // Reference check: ensure no loot_drops reference this item
                auto refCheck = txn.exec_params(
                    "SELECT COUNT(*) FROM loot_drops WHERE item_id = $1",
                    msg.contentId);
                if (refCheck[0][0].as<int>() > 0) {
                    sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, false,
                                    "Cannot delete: item is referenced by loot tables");
                    return;
                }
                txn.exec_params("DELETE FROM item_definitions WHERE item_id = $1",
                                msg.contentId);
                break;
            }
            case AdminContentType::LootDrop: {
                // contentId format: "loot_table_id:item_id"
                auto sep = msg.contentId.find(':');
                if (sep == std::string::npos) {
                    sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, false,
                                    "Invalid loot drop ID (expected table_id:item_id)");
                    return;
                }
                std::string tableId = msg.contentId.substr(0, sep);
                std::string itemId  = msg.contentId.substr(sep + 1);
                txn.exec_params(
                    "DELETE FROM loot_drops WHERE loot_table_id = $1 AND item_id = $2",
                    tableId, itemId);
                break;
            }
            case AdminContentType::SpawnZone: {
                int zoneId = std::stoi(msg.contentId);
                txn.exec_params("DELETE FROM spawn_zones WHERE zone_id = $1", zoneId);
                break;
            }
            default:
                sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, false,
                                "Unknown content type");
                return;
        }

        txn.commit();
        reloadCacheByType(contentTypeToCacheType(msg.contentType));

        LOG_INFO("Admin", "Client %d deleted content type %d id '%s'",
                 clientId, msg.contentType, msg.contentId.c_str());
        sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, true, "Deleted successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Admin", "Delete content failed: %s", e.what());
        sendAdminResult(clientId, PacketType::CmdAdminDeleteContent, false,
                        std::string("DB error: ") + e.what());
    }
}

// ============================================================================
// processAdminReloadCache
// ============================================================================

void ServerApp::processAdminReloadCache(uint16_t clientId,
                                         const CmdAdminReloadCacheMsg& msg) {
    auto roleIt = clientAdminRoles_.find(clientId);
    if (roleIt == clientAdminRoles_.end() || roleIt->second < AdminRole::Admin) {
        sendAdminResult(clientId, PacketType::CmdAdminReloadCache, false, "Insufficient permissions");
        return;
    }

    try {
        reloadCacheByType(msg.cacheType);
        LOG_INFO("Admin", "Client %d reloaded cache type %d", clientId, msg.cacheType);
        sendAdminResult(clientId, PacketType::CmdAdminReloadCache, true, "Cache reloaded");
    } catch (const std::exception& e) {
        LOG_ERROR("Admin", "Cache reload failed: %s", e.what());
        sendAdminResult(clientId, PacketType::CmdAdminReloadCache, false,
                        std::string("Reload error: ") + e.what());
    }
}

// ============================================================================
// processAdminValidate
// ============================================================================

void ServerApp::processAdminValidate(uint16_t clientId) {
    auto roleIt = clientAdminRoles_.find(clientId);
    if (roleIt == clientAdminRoles_.end() || roleIt->second < AdminRole::Admin) {
        sendAdminResult(clientId, PacketType::CmdAdminValidate, false, "Insufficient permissions");
        return;
    }

    auto issues = contentValidator_.runAll();

    SvValidationReportMsg report;
    report.issues.reserve(issues.size());
    for (const auto& issue : issues) {
        SvValidationReportMsg::ValidationIssueNet net;
        net.severity = issue.severity;
        net.message  = issue.message;
        report.issues.push_back(std::move(net));
    }

    // Use heap-allocated buffer since the report can be large
    std::vector<uint8_t> buf(65536);
    ByteWriter w(buf.data(), buf.size());
    report.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered,
                   PacketType::SvValidationReport, buf.data(), w.size());

    LOG_INFO("Admin", "Client %d requested validation: %zu issues", clientId, issues.size());
}

// ============================================================================
// processAdminRequestContentList
// ============================================================================

void ServerApp::processAdminRequestContentList(uint16_t clientId,
                                                const CmdAdminRequestContentListMsg& msg) {
    auto roleIt = clientAdminRoles_.find(clientId);
    if (roleIt == clientAdminRoles_.end() || roleIt->second < AdminRole::Admin) {
        sendAdminResult(clientId, PacketType::CmdAdminRequestContentList, false,
                        "Insufficient permissions");
        return;
    }

    nlohmann::json arr;
    switch (msg.contentType) {
        case AdminContentType::Mob:       arr = mobsToJson(mobDefCache_); break;
        case AdminContentType::Item:      arr = itemsToJson(itemDefCache_); break;
        case AdminContentType::LootDrop:  arr = lootToJson(lootTableCache_); break;
        case AdminContentType::SpawnZone: arr = spawnsToJson(spawnZoneCache_); break;
        default:
            sendAdminResult(clientId, PacketType::CmdAdminRequestContentList, false,
                            "Unknown content type");
            return;
    }

    // Chunk into pages that fit within packet limits (~3KB per page to stay under 4096 with header+encryption overhead)
    constexpr size_t MAX_PAGE_BYTES = 3000;
    std::vector<nlohmann::json> pages;
    nlohmann::json currentPage = nlohmann::json::array();
    size_t currentSize = 2; // "[]"

    for (const auto& entry : arr) {
        std::string entryStr = entry.dump();
        size_t entrySize = entryStr.size() + 1; // +1 for comma
        if (currentSize + entrySize > MAX_PAGE_BYTES && !currentPage.empty()) {
            pages.push_back(std::move(currentPage));
            currentPage = nlohmann::json::array();
            currentSize = 2;
        }
        currentPage.push_back(entry);
        currentSize += entrySize;
    }
    if (!currentPage.empty()) {
        pages.push_back(std::move(currentPage));
    }
    if (pages.empty()) {
        pages.push_back(nlohmann::json::array()); // at least one empty page
    }

    uint16_t totalPages = static_cast<uint16_t>(pages.size());
    for (uint16_t i = 0; i < totalPages; ++i) {
        SvAdminContentListMsg listMsg;
        listMsg.contentType = msg.contentType;
        listMsg.pageIndex = i;
        listMsg.totalPages = totalPages;
        listMsg.jsonPayload = pages[i].dump();

        uint8_t buf[4096];  // pages are ~3KB, need more than MAX_PACKET_SIZE (1200)
        ByteWriter w(buf, sizeof(buf));
        listMsg.write(w);
        if (!w.overflowed()) {
            server_.sendTo(clientId, Channel::ReliableOrdered,
                           PacketType::SvAdminContentList, buf, w.size());
        }
    }

    LOG_INFO("Admin", "Client %d requested content list type %d (%zu entries, %d pages)",
             clientId, msg.contentType, arr.size(), totalPages);
}

} // namespace fate
