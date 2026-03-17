// ============================================================================
// NOTE: Networking & Database Integration Pending
// ============================================================================
// NETWORKING (ENet):
//   - Sync friends, incomingRequests, blockedPlayers lists to owning client
//   - Handle Cmds: CmdSendFriendRequest, CmdAcceptRequest, CmdDeclineRequest,
//     CmdRemoveFriend, CmdBlockPlayer, CmdUnblockPlayer, CmdViewFriendProfile
//   - TargetRpc for FriendDetailedInfo (stats + equipment inspection)
//   - OnlineStatusTracker: Track which players are connected for friend status
//   - Update friend online/offline status when players connect/disconnect
//
// DATABASE (libpqxx):
//   - friends table: bidirectional friendship records
//   - blocked_players table: one-directional block records
//   - Load on player login, save changes immediately
//   - Friend inspection queries character stats + equipment
//   - Block check used by Chat, Trade, Party, Guild systems
// ============================================================================

#include "game/shared/friends_manager.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

// ============================================================================
// Initialization
// ============================================================================
void FriendsManager::initialize(const std::string& charId) {
    characterId_ = charId;
    friends_.clear();
    incomingRequests_.clear();
    blockedPlayers_.clear();
    blockedIds_.clear();
}

// ============================================================================
// Queries
// ============================================================================
bool FriendsManager::isBlocked(const std::string& targetCharId) const {
    return blockedIds_.contains(targetCharId);
}

bool FriendsManager::isFriend(const std::string& targetCharId) const {
    return std::ranges::any_of(friends_, [&](const FriendInfo& f) {
        return f.characterId == targetCharId;
    });
}

int FriendsManager::friendCount() const {
    return static_cast<int>(friends_.size());
}

int FriendsManager::blockCount() const {
    return static_cast<int>(blockedPlayers_.size());
}

// ============================================================================
// Friend Management
// ============================================================================
bool FriendsManager::addFriend(const FriendInfo& info) {
    if (friendCount() >= MAX_FRIENDS) {
        return false;
    }

    if (isFriend(info.characterId)) {
        return false;
    }

    friends_.push_back(info);

    if (onFriendsChanged) {
        onFriendsChanged();
    }

    return true;
}

bool FriendsManager::removeFriend(const std::string& targetCharId) {
    auto it = std::ranges::find_if(friends_, [&](const FriendInfo& f) {
        return f.characterId == targetCharId;
    });

    if (it == friends_.end()) {
        return false;
    }

    friends_.erase(it);

    if (onFriendsChanged) {
        onFriendsChanged();
    }

    return true;
}

// ============================================================================
// Block Management
// ============================================================================
bool FriendsManager::blockPlayer(const BlockedPlayerInfo& info) {
    if (blockCount() >= MAX_BLOCKS) {
        return false;
    }

    if (isBlocked(info.characterId)) {
        return false;
    }

    // Blocking also removes from friends list
    removeFriend(info.characterId);

    blockedPlayers_.push_back(info);
    blockedIds_.insert(info.characterId);

    if (onFriendsChanged) {
        onFriendsChanged();
    }

    return true;
}

bool FriendsManager::unblockPlayer(const std::string& targetCharId) {
    auto it = std::ranges::find_if(blockedPlayers_, [&](const BlockedPlayerInfo& b) {
        return b.characterId == targetCharId;
    });

    if (it == blockedPlayers_.end()) {
        return false;
    }

    blockedPlayers_.erase(it);
    blockedIds_.erase(targetCharId);

    if (onFriendsChanged) {
        onFriendsChanged();
    }

    return true;
}

// ============================================================================
// Friend Requests
// ============================================================================
void FriendsManager::addIncomingRequest(const FriendRequestInfo& request) {
    // Ignore duplicate requests from the same character
    auto exists = std::ranges::any_of(incomingRequests_, [&](const FriendRequestInfo& r) {
        return r.fromCharacterId == request.fromCharacterId;
    });

    if (exists) {
        return;
    }

    incomingRequests_.push_back(request);

    if (onFriendRequestReceived) {
        onFriendRequestReceived(request);
    }
}

bool FriendsManager::acceptRequest(const std::string& fromCharId) {
    auto it = std::ranges::find_if(incomingRequests_, [&](const FriendRequestInfo& r) {
        return r.fromCharacterId == fromCharId;
    });

    if (it == incomingRequests_.end()) {
        return false;
    }

    // Build a FriendInfo from the request
    FriendInfo newFriend;
    newFriend.characterId = it->fromCharacterId;
    newFriend.characterName = it->fromCharacterName;
    newFriend.className = it->fromClassName;
    newFriend.level = it->fromLevel;

    incomingRequests_.erase(it);

    return addFriend(newFriend);
}

void FriendsManager::declineRequest(const std::string& fromCharId) {
    auto it = std::ranges::find_if(incomingRequests_, [&](const FriendRequestInfo& r) {
        return r.fromCharacterId == fromCharId;
    });

    if (it != incomingRequests_.end()) {
        incomingRequests_.erase(it);
    }
}

// ============================================================================
// Status Updates
// ============================================================================
void FriendsManager::updateFriendOnlineStatus(const std::string& charId, bool online,
                                               const std::string& scene) {
    auto it = std::ranges::find_if(friends_, [&](const FriendInfo& f) {
        return f.characterId == charId;
    });

    if (it == friends_.end()) {
        return;
    }

    it->isOnline = online;

    if (!scene.empty()) {
        it->currentScene = scene;
    }

    if (onFriendsChanged) {
        onFriendsChanged();
    }
}

} // namespace fate
