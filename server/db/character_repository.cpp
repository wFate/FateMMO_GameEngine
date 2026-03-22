#include "server/db/character_repository.h"
#include "engine/core/logger.h"
#include <chrono>

namespace fate {

std::string CharacterRepository::createDefaultCharacter(int accountId, const std::string& characterName,
                                                         const std::string& className) {
    try {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::string charId = "chr_" + std::to_string(accountId) + "_" + std::to_string(now);

        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "INSERT INTO characters (character_id, account_id, character_name, class_name) "
            "VALUES ($1, $2, $3, $4) RETURNING character_id",
            charId, accountId, characterName, className);
        txn.commit();
        if (!result.empty()) return result[0][0].as<std::string>();
    } catch (const pqxx::unique_violation&) {
        LOG_WARN("CharRepo", "Character name already exists: %s", characterName.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("CharRepo", "createDefaultCharacter failed: %s", e.what());
    }
    return "";
}

CharacterRecord CharacterRepository::rowToRecord(const pqxx::row& row) {
    CharacterRecord rec;
    rec.character_id       = row["character_id"].as<std::string>();
    rec.account_id         = row["account_id"].as<int>();
    rec.character_name     = row["character_name"].as<std::string>();
    rec.class_name         = row["class_name"].is_null() ? "Warrior" : row["class_name"].as<std::string>();

    rec.level              = row["level"].is_null() ? 1 : row["level"].as<int>();
    rec.current_xp         = row["current_xp"].is_null() ? 0 : row["current_xp"].as<int64_t>();
    rec.xp_to_next_level   = row["xp_to_next_level"].is_null() ? 100 : row["xp_to_next_level"].as<int>();

    rec.current_scene      = row["current_scene"].is_null() ? "WhisperingWoods" : row["current_scene"].as<std::string>();
    rec.position_x         = row["position_x"].is_null() ? 0.0f : row["position_x"].as<float>();
    rec.position_y         = row["position_y"].is_null() ? 0.0f : row["position_y"].as<float>();

    rec.current_hp         = row["current_hp"].is_null() ? 100 : row["current_hp"].as<int>();
    rec.max_hp             = row["max_hp"].is_null() ? 100 : row["max_hp"].as<int>();
    rec.current_mp         = row["current_mp"].is_null() ? 50 : row["current_mp"].as<int>();
    rec.max_mp             = row["max_mp"].is_null() ? 50 : row["max_mp"].as<int>();
    rec.current_fury       = row["current_fury"].is_null() ? 0.0f : row["current_fury"].as<float>();

    rec.base_strength      = row["base_strength"].is_null() ? 10 : row["base_strength"].as<int>();
    rec.base_vitality      = row["base_vitality"].is_null() ? 10 : row["base_vitality"].as<int>();
    rec.base_intelligence  = row["base_intelligence"].is_null() ? 10 : row["base_intelligence"].as<int>();
    rec.base_dexterity     = row["base_dexterity"].is_null() ? 10 : row["base_dexterity"].as<int>();
    rec.base_wisdom        = row["base_wisdom"].is_null() ? 10 : row["base_wisdom"].as<int>();

    rec.gold               = row["gold"].is_null() ? 0 : row["gold"].as<int64_t>();

    rec.honor              = row["honor"].is_null() ? 0 : row["honor"].as<int>();
    rec.pvp_kills          = row["pvp_kills"].is_null() ? 0 : row["pvp_kills"].as<int>();
    rec.pvp_deaths         = row["pvp_deaths"].is_null() ? 0 : row["pvp_deaths"].as<int>();

    rec.is_dead            = row["is_dead"].as<bool>();
    rec.death_timestamp    = row["death_timestamp"].is_null() ? 0 : row["death_timestamp"].as<int64_t>();

    rec.gender             = row["gender"].is_null() ? 0 : row["gender"].as<int>();
    rec.hairstyle          = row["hairstyle"].is_null() ? 0 : row["hairstyle"].as<int>();
    rec.hair_color         = row["hair_color"].is_null() ? 0 : row["hair_color"].as<int>();

    rec.total_playtime_seconds = row["total_playtime_seconds"].is_null() ? 0 : row["total_playtime_seconds"].as<int64_t>();

    rec.pk_status              = row["pk_status"].is_null() ? 0 : row["pk_status"].as<int>();
    rec.faction                = row["faction"].is_null() ? 0 : row["faction"].as<int>();

    return rec;
}

std::optional<CharacterRecord> CharacterRepository::loadCharacter(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT character_id, account_id, character_name, class_name, "
            "level, current_xp, xp_to_next_level, "
            "current_scene, position_x, position_y, "
            "current_hp, max_hp, current_mp, max_mp, current_fury, "
            "base_strength, base_vitality, base_intelligence, base_dexterity, base_wisdom, "
            "gold, honor, pvp_kills, pvp_deaths, "
            "is_dead, death_timestamp, "
            "gender, hairstyle, hair_color, "
            "total_playtime_seconds, "
            "pk_status, faction "
            "FROM characters WHERE character_id = $1",
            characterId);
        txn.commit();
        if (result.empty()) return std::nullopt;
        return rowToRecord(result[0]);
    } catch (const std::exception& e) {
        LOG_ERROR("CharRepo", "loadCharacter failed: %s", e.what());
    }
    return std::nullopt;
}

std::optional<CharacterRecord> CharacterRepository::loadCharacterByAccount(int accountId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT character_id, account_id, character_name, class_name, "
            "level, current_xp, xp_to_next_level, "
            "current_scene, position_x, position_y, "
            "current_hp, max_hp, current_mp, max_mp, current_fury, "
            "base_strength, base_vitality, base_intelligence, base_dexterity, base_wisdom, "
            "gold, honor, pvp_kills, pvp_deaths, "
            "is_dead, death_timestamp, "
            "gender, hairstyle, hair_color, "
            "total_playtime_seconds, "
            "pk_status, faction "
            "FROM characters WHERE account_id = $1 LIMIT 1",
            accountId);
        txn.commit();
        if (result.empty()) return std::nullopt;
        return rowToRecord(result[0]);
    } catch (const std::exception& e) {
        LOG_ERROR("CharRepo", "loadCharacterByAccount failed: %s", e.what());
    }
    return std::nullopt;
}

bool CharacterRepository::saveCharacter(const CharacterRecord& rec) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "UPDATE characters SET "
            "level = $2, current_xp = $3, xp_to_next_level = $4, "
            "current_scene = $5, position_x = $6, position_y = $7, "
            "current_hp = $8, max_hp = $9, current_mp = $10, max_mp = $11, current_fury = $12, "
            "base_strength = $13, base_vitality = $14, base_intelligence = $15, "
            "base_dexterity = $16, base_wisdom = $17, "
            "gold = $18, "
            "honor = $19, pvp_kills = $20, pvp_deaths = $21, "
            "is_dead = $22, death_timestamp = $23, "
            "total_playtime_seconds = $24, "
            "pk_status = $25, faction = $26, "
            "last_saved_at = NOW() "
            "WHERE character_id = $1",
            rec.character_id,
            rec.level, rec.current_xp, rec.xp_to_next_level,
            rec.current_scene, rec.position_x, rec.position_y,
            rec.current_hp, rec.max_hp, rec.current_mp, rec.max_mp, rec.current_fury,
            rec.base_strength, rec.base_vitality, rec.base_intelligence,
            rec.base_dexterity, rec.base_wisdom,
            rec.gold,
            rec.honor, rec.pvp_kills, rec.pvp_deaths,
            rec.is_dead, rec.death_timestamp,
            rec.total_playtime_seconds,
            rec.pk_status, rec.faction);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("CharRepo", "saveCharacter failed: %s", e.what());
    }
    return false;
}

} // namespace fate
