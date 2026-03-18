#include "server/db/social_repository.h"
#include "engine/core/logger.h"

namespace fate {

// ---- Friends ----

bool SocialRepository::sendFriendRequest(const std::string& fromId, const std::string& toId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "INSERT INTO friends (character_id, friend_character_id, status, created_at) "
            "VALUES ($1, $2, 'pending', NOW()) ON CONFLICT DO NOTHING",
            fromId, toId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "sendFriendRequest failed: %s", e.what());
    }
    return false;
}

bool SocialRepository::acceptFriendRequest(const std::string& characterId, const std::string& fromId) {
    try {
        pqxx::work txn(conn_);
        // Update existing request
        txn.exec_params(
            "UPDATE friends SET status = 'accepted', accepted_at = NOW() "
            "WHERE character_id = $1 AND friend_character_id = $2 AND status = 'pending'",
            fromId, characterId);
        // Create reverse record
        txn.exec_params(
            "INSERT INTO friends (character_id, friend_character_id, status, created_at, accepted_at) "
            "VALUES ($1, $2, 'accepted', NOW(), NOW()) ON CONFLICT DO NOTHING",
            characterId, fromId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "acceptFriendRequest failed: %s", e.what());
    }
    return false;
}

bool SocialRepository::declineFriendRequest(const std::string& characterId, const std::string& fromId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM friends WHERE character_id = $1 AND friend_character_id = $2 AND status = 'pending'",
            fromId, characterId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "declineFriendRequest failed: %s", e.what());
    }
    return false;
}

bool SocialRepository::removeFriend(const std::string& charId, const std::string& friendId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM friends WHERE "
            "(character_id = $1 AND friend_character_id = $2) OR "
            "(character_id = $2 AND friend_character_id = $1)",
            charId, friendId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "removeFriend failed: %s", e.what());
    }
    return false;
}

std::vector<FriendRecord> SocialRepository::getFriends(const std::string& characterId) {
    std::vector<FriendRecord> friends;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT f.friend_character_id, f.status, f.note, "
            "EXTRACT(EPOCH FROM c.last_online)::BIGINT AS last_online "
            "FROM friends f LEFT JOIN characters c ON f.friend_character_id = c.character_id "
            "WHERE f.character_id = $1", characterId);
        txn.commit();
        friends.reserve(result.size());
        for (const auto& row : result) {
            FriendRecord r;
            r.friendCharacterId = row["friend_character_id"].as<std::string>();
            r.status = row["status"].as<std::string>();
            r.note = row["note"].is_null() ? "" : row["note"].as<std::string>();
            r.lastOnlineUnix = row["last_online"].is_null() ? 0 : row["last_online"].as<int64_t>();
            friends.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "getFriends failed: %s", e.what());
    }
    return friends;
}

std::vector<FriendRequestRecord> SocialRepository::getIncomingRequests(const std::string& characterId) {
    std::vector<FriendRequestRecord> requests;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT character_id, EXTRACT(EPOCH FROM created_at)::BIGINT AS created_unix "
            "FROM friends WHERE friend_character_id = $1 AND status = 'pending'", characterId);
        txn.commit();
        for (const auto& row : result) {
            FriendRequestRecord r;
            r.fromCharacterId = row["character_id"].as<std::string>();
            r.createdAtUnix = row["created_unix"].as<int64_t>();
            requests.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "getIncomingRequests failed: %s", e.what());
    }
    return requests;
}

int SocialRepository::getFriendCount(const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT COUNT(*) FROM friends WHERE character_id = $1 AND status = 'accepted'",
            characterId);
        txn.commit();
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "getFriendCount failed: %s", e.what());
    }
    return 0;
}

bool SocialRepository::areFriends(const std::string& char1, const std::string& char2) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT 1 FROM friends "
            "WHERE character_id = $1 AND friend_character_id = $2 AND status = 'accepted' LIMIT 1",
            char1, char2);
        txn.commit();
        return !result.empty();
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "areFriends failed: %s", e.what());
    }
    return false;
}

// ---- Blocks ----

bool SocialRepository::blockPlayer(const std::string& blockerId, const std::string& blockedId,
                                    const std::string& reason) {
    try {
        pqxx::work txn(conn_);
        // Remove friendship first
        txn.exec_params(
            "DELETE FROM friends WHERE "
            "(character_id = $1 AND friend_character_id = $2) OR "
            "(character_id = $2 AND friend_character_id = $1)",
            blockerId, blockedId);
        // Insert block
        txn.exec_params(
            "INSERT INTO blocked_players (blocker_character_id, blocked_character_id, blocked_at, reason) "
            "VALUES ($1, $2, NOW(), $3) ON CONFLICT DO NOTHING",
            blockerId, blockedId, reason.empty() ? std::optional<std::string>(std::nullopt)
                                                  : std::optional<std::string>(reason));
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "blockPlayer failed: %s", e.what());
    }
    return false;
}

bool SocialRepository::unblockPlayer(const std::string& blockerId, const std::string& blockedId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM blocked_players WHERE blocker_character_id = $1 AND blocked_character_id = $2",
            blockerId, blockedId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "unblockPlayer failed: %s", e.what());
    }
    return false;
}

std::vector<BlockRecord> SocialRepository::getBlockedPlayers(const std::string& characterId) {
    std::vector<BlockRecord> blocks;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT blocked_character_id, EXTRACT(EPOCH FROM blocked_at)::BIGINT AS blocked_unix "
            "FROM blocked_players WHERE blocker_character_id = $1", characterId);
        txn.commit();
        for (const auto& row : result) {
            BlockRecord b;
            b.blockedCharacterId = row["blocked_character_id"].as<std::string>();
            b.blockedAtUnix = row["blocked_unix"].as<int64_t>();
            blocks.push_back(std::move(b));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "getBlockedPlayers failed: %s", e.what());
    }
    return blocks;
}

bool SocialRepository::isBlocked(const std::string& blockerId, const std::string& blockedId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT 1 FROM blocked_players "
            "WHERE blocker_character_id = $1 AND blocked_character_id = $2 LIMIT 1",
            blockerId, blockedId);
        txn.commit();
        return !result.empty();
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "isBlocked failed: %s", e.what());
    }
    return false;
}

int SocialRepository::getBlockCount(const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT COUNT(*) FROM blocked_players WHERE blocker_character_id = $1", characterId);
        txn.commit();
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "getBlockCount failed: %s", e.what());
    }
    return 0;
}

// ---- Notes ----

bool SocialRepository::setFriendNote(const std::string& charId, const std::string& friendId,
                                      const std::string& note) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE friends SET note = $3 WHERE character_id = $1 AND friend_character_id = $2",
            charId, friendId, note);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "setFriendNote failed: %s", e.what());
    }
    return false;
}

// ---- Online Status ----

bool SocialRepository::updateLastOnline(const std::string& characterId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params("UPDATE characters SET last_online = NOW() WHERE character_id = $1",
                         characterId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SocialRepo", "updateLastOnline failed: %s", e.what());
    }
    return false;
}

} // namespace fate
