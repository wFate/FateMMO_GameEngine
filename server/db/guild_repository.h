#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>

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
    explicit GuildRepository(pqxx::connection& conn) : conn_(conn) {}

    // Guild lifecycle
    int createGuild(const std::string& guildName, const std::string& ownerCharId,
                    int maxMembers, GuildDbResult& result);
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
    pqxx::connection& conn_;
};

} // namespace fate
