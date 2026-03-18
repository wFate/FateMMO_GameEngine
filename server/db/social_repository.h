#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <pqxx/pqxx>

namespace fate {

struct FriendRecord {
    std::string friendCharacterId;
    std::string status;     // "pending" or "accepted"
    std::string note;
    int64_t lastOnlineUnix = 0;
};

struct FriendRequestRecord {
    std::string fromCharacterId;
    int64_t createdAtUnix = 0;
};

struct BlockRecord {
    std::string blockedCharacterId;
    int64_t blockedAtUnix = 0;
};

class SocialRepository {
public:
    explicit SocialRepository(pqxx::connection& conn) : conn_(conn) {}

    // Friends
    bool sendFriendRequest(const std::string& fromId, const std::string& toId);
    bool acceptFriendRequest(const std::string& characterId, const std::string& fromId);
    bool declineFriendRequest(const std::string& characterId, const std::string& fromId);
    bool removeFriend(const std::string& charId, const std::string& friendId);
    std::vector<FriendRecord> getFriends(const std::string& characterId);
    std::vector<FriendRequestRecord> getIncomingRequests(const std::string& characterId);
    int getFriendCount(const std::string& characterId);
    bool areFriends(const std::string& char1, const std::string& char2);

    // Blocks
    bool blockPlayer(const std::string& blockerId, const std::string& blockedId,
                     const std::string& reason = "");
    bool unblockPlayer(const std::string& blockerId, const std::string& blockedId);
    std::vector<BlockRecord> getBlockedPlayers(const std::string& characterId);
    bool isBlocked(const std::string& blockerId, const std::string& blockedId);
    int getBlockCount(const std::string& characterId);

    // Notes
    bool setFriendNote(const std::string& charId, const std::string& friendId,
                       const std::string& note);

    // Online status
    bool updateLastOnline(const std::string& characterId);

private:
    pqxx::connection& conn_;
};

} // namespace fate
