#include "server/db/guild_repository.h"
#include "engine/core/logger.h"

namespace fate {

int GuildRepository::createGuild(const std::string& guildName, const std::string& ownerCharId,
                                  int maxMembers, int factionId, GuildDbResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        // Check name availability
        auto check = txn.exec_params(
            "SELECT guild_id FROM guilds WHERE LOWER(guild_name) = LOWER($1) AND is_disbanded = FALSE",
            guildName);
        if (!check.empty()) {
            result = GuildDbResult::NameTaken;
            return -1;
        }

        // Check player not already in guild
        auto inGuild = txn.exec_params(
            "SELECT guild_id FROM guild_members WHERE character_id = $1", ownerCharId);
        if (!inGuild.empty()) {
            result = GuildDbResult::AlreadyInGuild;
            return -1;
        }

        // Create guild
        auto ins = txn.exec_params(
            "INSERT INTO guilds (guild_name, owner_character_id, guild_level, guild_xp, "
            "member_count, max_members, faction_id, created_at) "
            "VALUES ($1, $2, 1, 0, 1, $3, $4, NOW()) RETURNING guild_id",
            guildName, ownerCharId, maxMembers, factionId);

        int guildId = ins[0][0].as<int>();

        // Add owner as member with rank 2 (Owner)
        txn.exec_params(
            "INSERT INTO guild_members (character_id, guild_id, rank, xp_contributed, joined_at) "
            "VALUES ($1, $2, 2, 0, NOW())",
            ownerCharId, guildId);

        // Clear guild_left_at
        txn.exec_params(
            "UPDATE characters SET guild_id = $2, guild_left_at = NULL WHERE character_id = $1",
            ownerCharId, guildId);

        txn.commit();
        result = GuildDbResult::Success;
        LOG_INFO("GuildRepo", "Created guild '%s' (id=%d) for %s",
                 guildName.c_str(), guildId, ownerCharId.c_str());
        return guildId;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "createGuild failed: %s", e.what());
        result = GuildDbResult::ServerError;
    }
    return -1;
}

bool GuildRepository::disbandGuild(int guildId, const std::string& requesterId, GuildDbResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        // Verify requester is owner
        auto check = txn.exec_params(
            "SELECT owner_character_id FROM guilds WHERE guild_id = $1", guildId);
        if (check.empty()) { result = GuildDbResult::NotFound; return false; }
        if (check[0][0].as<std::string>() != requesterId) { result = GuildDbResult::NotOwner; return false; }

        // Set guild_left_at for all members
        txn.exec_params(
            "UPDATE characters SET guild_id = NULL, guild_left_at = NOW() "
            "WHERE character_id IN (SELECT character_id FROM guild_members WHERE guild_id = $1)",
            guildId);

        // Delete members and invites
        txn.exec_params("DELETE FROM guild_members WHERE guild_id = $1", guildId);
        txn.exec_params("DELETE FROM guild_invites WHERE guild_id = $1", guildId);

        // Mark disbanded (soft delete)
        txn.exec_params(
            "UPDATE guilds SET is_disbanded = TRUE, member_count = 0 WHERE guild_id = $1", guildId);

        txn.commit();
        result = GuildDbResult::Success;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "disbandGuild failed: %s", e.what());
        result = GuildDbResult::ServerError;
    }
    return false;
}

std::optional<GuildInfoRecord> GuildRepository::getGuildInfo(int guildId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT g.guild_id, g.guild_name, g.guild_level, g.guild_xp, "
            "g.member_count, g.max_members, g.owner_character_id, g.faction_id, "
            "c.character_name AS owner_name "
            "FROM guilds g LEFT JOIN characters c ON c.character_id = g.owner_character_id "
            "WHERE g.guild_id = $1 AND g.is_disbanded = FALSE", guildId);
        txn.commit();
        if (result.empty()) return std::nullopt;

        GuildInfoRecord rec;
        rec.guildId       = result[0]["guild_id"].as<int>();
        rec.guildName     = result[0]["guild_name"].as<std::string>();
        rec.ownerCharacterId = result[0]["owner_character_id"].as<std::string>();
        rec.ownerName     = result[0]["owner_name"].is_null() ? "" : result[0]["owner_name"].as<std::string>();
        rec.guildLevel    = result[0]["guild_level"].as<int>();
        rec.guildXP       = result[0]["guild_xp"].as<int64_t>();
        rec.memberCount   = result[0]["member_count"].as<int>();
        rec.maxMembers    = result[0]["max_members"].as<int>();
        rec.factionId     = result[0]["faction_id"].as<int>();
        return rec;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "getGuildInfo failed: %s", e.what());
    }
    return std::nullopt;
}

int GuildRepository::getPlayerGuildId(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT guild_id FROM guild_members WHERE character_id = $1", characterId);
        txn.commit();
        if (!result.empty()) return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "getPlayerGuildId failed: %s", e.what());
    }
    return 0;
}

std::optional<GuildDisplayRecord> GuildRepository::getPlayerGuildDisplay(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT g.guild_id, g.guild_name "
            "FROM guild_members gm JOIN guilds g ON g.guild_id = gm.guild_id "
            "WHERE gm.character_id = $1 AND g.is_disbanded = FALSE", characterId);
        txn.commit();
        if (result.empty()) return std::nullopt;

        GuildDisplayRecord rec;
        rec.guildId   = result[0]["guild_id"].as<int>();
        rec.guildName = result[0]["guild_name"].as<std::string>();
        return rec;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "getPlayerGuildDisplay failed: %s", e.what());
    }
    return std::nullopt;
}

int GuildRepository::getPlayerRank(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT rank FROM guild_members WHERE character_id = $1", characterId);
        txn.commit();
        if (!result.empty()) return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "getPlayerRank failed: %s", e.what());
    }
    return -1;
}

std::vector<GuildMemberRecord> GuildRepository::getGuildMembers(int guildId) {
    std::vector<GuildMemberRecord> members;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT gm.character_id, c.character_name, gm.rank, c.level, "
            "gm.xp_contributed, EXTRACT(EPOCH FROM gm.joined_at)::BIGINT AS joined_unix "
            "FROM guild_members gm JOIN characters c ON c.character_id = gm.character_id "
            "WHERE gm.guild_id = $1 ORDER BY gm.rank DESC, c.level DESC", guildId);
        txn.commit();
        members.reserve(result.size());
        for (const auto& row : result) {
            GuildMemberRecord m;
            m.characterId   = row["character_id"].as<std::string>();
            m.characterName = row["character_name"].as<std::string>();
            m.rank          = row["rank"].as<int>();
            m.level         = row["level"].as<int>();
            m.xpContributed = row["xp_contributed"].as<int64_t>();
            m.joinedUnix    = row["joined_unix"].as<int64_t>();
            members.push_back(std::move(m));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "getGuildMembers failed: %s", e.what());
    }
    return members;
}

int GuildRepository::getOfficerCount(int guildId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT COUNT(*) FROM guild_members WHERE guild_id = $1 AND rank = 1", guildId);
        txn.commit();
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "getOfficerCount failed: %s", e.what());
    }
    return 0;
}

bool GuildRepository::addMember(int guildId, const std::string& characterId, int rank, GuildDbResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        // Check guild capacity
        auto cap = txn.exec_params(
            "SELECT member_count, max_members FROM guilds WHERE guild_id = $1 AND is_disbanded = FALSE",
            guildId);
        if (cap.empty()) { result = GuildDbResult::NotFound; return false; }
        if (cap[0]["member_count"].as<int>() >= cap[0]["max_members"].as<int>()) {
            result = GuildDbResult::GuildFull; return false;
        }

        // Check player not already in guild
        auto check = txn.exec_params(
            "SELECT guild_id FROM guild_members WHERE character_id = $1", characterId);
        if (!check.empty()) { result = GuildDbResult::AlreadyInGuild; return false; }

        // Insert member
        txn.exec_params(
            "INSERT INTO guild_members (character_id, guild_id, rank, xp_contributed, joined_at) "
            "VALUES ($1, $2, $3, 0, NOW())", characterId, guildId, rank);

        // Update member count and character
        txn.exec_params(
            "UPDATE guilds SET member_count = member_count + 1 WHERE guild_id = $1", guildId);
        txn.exec_params(
            "UPDATE characters SET guild_id = $2, guild_left_at = NULL WHERE character_id = $1",
            characterId, guildId);

        txn.commit();
        result = GuildDbResult::Success;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "addMember failed: %s", e.what());
        result = GuildDbResult::ServerError;
    }
    return false;
}

bool GuildRepository::removeMember(int guildId, const std::string& characterId, GuildDbResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        txn.exec_params(
            "DELETE FROM guild_members WHERE guild_id = $1 AND character_id = $2",
            guildId, characterId);
        txn.exec_params(
            "UPDATE guilds SET member_count = member_count - 1 WHERE guild_id = $1", guildId);
        txn.exec_params(
            "UPDATE characters SET guild_id = NULL, guild_left_at = NOW() WHERE character_id = $1",
            characterId);

        txn.commit();
        result = GuildDbResult::Success;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "removeMember failed: %s", e.what());
        result = GuildDbResult::ServerError;
    }
    return false;
}

bool GuildRepository::setMemberRank(int guildId, const std::string& characterId, int newRank) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "UPDATE guild_members SET rank = $3 WHERE guild_id = $1 AND character_id = $2",
            guildId, characterId, newRank);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "setMemberRank failed: %s", e.what());
    }
    return false;
}

bool GuildRepository::transferOwnership(int guildId, const std::string& currentOwnerId,
                                         const std::string& newOwnerId, GuildDbResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        // Demote current owner to officer
        txn.exec_params(
            "UPDATE guild_members SET rank = 1 WHERE character_id = $1 AND guild_id = $2",
            currentOwnerId, guildId);

        // Promote new owner
        txn.exec_params(
            "UPDATE guild_members SET rank = 2 WHERE character_id = $1 AND guild_id = $2",
            newOwnerId, guildId);

        // Update guild record
        txn.exec_params(
            "UPDATE guilds SET owner_character_id = $2 WHERE guild_id = $1",
            guildId, newOwnerId);

        txn.commit();
        result = GuildDbResult::Success;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "transferOwnership failed: %s", e.what());
        result = GuildDbResult::ServerError;
    }
    return false;
}

int64_t GuildRepository::addGuildXP(int guildId, const std::string& contributorId, int64_t xpAmount) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        auto result = txn.exec_params(
            "UPDATE guilds SET guild_xp = guild_xp + $2 WHERE guild_id = $1 "
            "RETURNING guild_xp", guildId, xpAmount);

        txn.exec_params(
            "UPDATE guild_members SET xp_contributed = xp_contributed + $3 "
            "WHERE character_id = $1 AND guild_id = $2",
            contributorId, guildId, xpAmount);

        txn.commit();
        return result.empty() ? 0 : result[0][0].as<int64_t>();
    } catch (const std::exception& e) {
        LOG_ERROR("GuildRepo", "addGuildXP failed: %s", e.what());
    }
    return 0;
}

} // namespace fate
