#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>
#include "server/db/db_pool.h"

namespace fate {

enum class GuildDbResult : uint8_t {
    Success, NameTaken, NotFound, NotOwner, NotMember,
    GuildFull, AlreadyInGuild, MaxOfficers, ServerError
};

struct GuildInfoRecord {
    int guildId = 0;
    std::string guildName;
    std::string ownerCharacterId;
    std::string ownerName;
    int guildLevel = 1;
    int64_t guildXP = 0;
    int memberCount = 0;
    int maxMembers = 20;
    int factionId = 0;     // 0=None, 1=Xyros, 2=Fenor, 3=Zethos, 4=Solis
};

struct GuildMemberRecord {
    std::string characterId;
    std::string characterName;
    int rank = 0;        // 0=Member, 1=Officer, 2=Owner
    int level = 0;
    int64_t xpContributed = 0;
    int64_t joinedUnix = 0;
    bool isOnline = false;
};

struct GuildDisplayRecord {
    int guildId = 0;
    std::string guildName;
};

class GuildRepository {
public:
    // Legacy: direct connection (for temp repos in async fibers)
    explicit GuildRepository(pqxx::connection& conn) : connRef_(&conn), pool_(nullptr) {}
    // Pool-based: acquires connection per operation
    explicit GuildRepository(DbPool& pool) : connRef_(nullptr), pool_(&pool) {}

    // Guild lifecycle
    int createGuild(const std::string& guildName, const std::string& ownerCharId,
                    int maxMembers, int factionId, GuildDbResult& result);
    bool disbandGuild(int guildId, const std::string& requesterId, GuildDbResult& result);

    // Queries
    std::optional<GuildInfoRecord> getGuildInfo(int guildId);
    int getPlayerGuildId(const std::string& characterId);
    std::optional<GuildDisplayRecord> getPlayerGuildDisplay(const std::string& characterId);
    int getPlayerRank(const std::string& characterId);
    std::vector<GuildMemberRecord> getGuildMembers(int guildId);
    int getOfficerCount(int guildId);

    // Membership
    bool addMember(int guildId, const std::string& characterId, int rank, GuildDbResult& result);
    bool removeMember(int guildId, const std::string& characterId, GuildDbResult& result);
    bool setMemberRank(int guildId, const std::string& characterId, int newRank);
    bool transferOwnership(int guildId, const std::string& currentOwnerId,
                           const std::string& newOwnerId, GuildDbResult& result);

    // Guild XP
    int64_t addGuildXP(int guildId, const std::string& contributorId, int64_t xpAmount);

private:
    pqxx::connection* connRef_ = nullptr;
    DbPool* pool_ = nullptr;

    DbPool::Guard acquireConn() {
        if (pool_) return pool_->acquire_guard();
        return DbPool::Guard::wrap(*connRef_);
    }
};

} // namespace fate
