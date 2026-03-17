// ============================================================================
// NOTE: Networking & Database Integration Pending
// ============================================================================
// NETWORKING (ENet):
//   - Sync partyId, isLeader, lootMode to owning client
//   - Sync members list to all party members
//   - Sync pendingInvites to invited player
//   - Handle Cmds: CmdInvite, CmdAcceptInvite, CmdDeclineInvite, CmdKick,
//     CmdPromote, CmdLeave, CmdSetLootMode
//   - Broadcast member HP/MP updates periodically (every 1-2s)
//   - Cross-scene party support via server-side player lookup
//
// DATABASE (libpqxx):
//   - parties, party_members, party_invites tables
//   - Create/delete party records on form/disband
//   - Insert/delete party_members on join/leave
//   - Party persists across server restarts (load on player login)
//
// XP DISTRIBUTION:
//   - Party bonus (+10%/member) from TOTAL party size (cross-scene)
//   - XP only awarded to members in SAME scene as mob
//   - Gold split equally among same-scene members
// ============================================================================

#include "game/shared/party_manager.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <iterator>

namespace fate {

// ============================================================================
// Queries
// ============================================================================

bool PartyManager::isInParty() const {
    return partyId >= 0 && !members.empty();
}

int PartyManager::memberCount() const {
    return static_cast<int>(members.size());
}

float PartyManager::getXPBonus() const {
    if (memberCount() <= 1) return 0.0f;
    return XP_BONUS_PER_MEMBER * static_cast<float>(memberCount() - 1);
}

// ============================================================================
// Party Lifecycle
// ============================================================================

bool PartyManager::createParty(const std::string& leaderId, const std::string& leaderName,
                               const std::string& className, int level) {
    if (isInParty()) {
        if (onActionResult) onActionResult("Already in a party.");
        return false;
    }

    // Assign a local party ID; server will replace with authoritative ID
    partyId = 1;
    isLeader = true;
    lootMode = PartyLootMode::FreeForAll;

    PartyMemberInfo leader;
    leader.characterId = leaderId;
    leader.characterName = leaderName;
    leader.className = className;
    leader.level = level;
    leader.isOnline = true;
    leader.isLeader = true;

    members.clear();
    members.push_back(leader);

    if (onPartyChanged) onPartyChanged();
    if (onActionResult) onActionResult("Party created.");
    return true;
}

bool PartyManager::addMember(const PartyMemberInfo& info) {
    if (memberCount() >= MAX_PARTY_SIZE) {
        if (onActionResult) onActionResult("Party is full.");
        return false;
    }

    // Check for duplicate
    for (const auto& m : members) {
        if (m.characterId == info.characterId) {
            if (onActionResult) onActionResult("Player is already in the party.");
            return false;
        }
    }

    members.push_back(info);

    if (onPartyChanged) onPartyChanged();
    if (onActionResult) onActionResult(info.characterName + " joined the party.");
    return true;
}

bool PartyManager::removeMember(const std::string& characterId) {
    auto it = std::find_if(members.begin(), members.end(),
        [&](const PartyMemberInfo& m) { return m.characterId == characterId; });

    if (it == members.end()) {
        if (onActionResult) onActionResult("Player not found in party.");
        return false;
    }

    std::string name = it->characterName;
    bool wasLeader = it->isLeader;
    members.erase(it);

    // If the removed member was leader and members remain, promote next member
    if (wasLeader && !members.empty()) {
        members.front().isLeader = true;
        isLeader = false; // Will be updated by server for the correct client
    }

    // If party is empty or only one member left, disband
    if (members.size() <= 1) {
        disbandParty();
        return true;
    }

    if (onPartyChanged) onPartyChanged();
    if (onActionResult) onActionResult(name + " left the party.");
    return true;
}

bool PartyManager::promoteMember(const std::string& characterId) {
    if (!isLeader) {
        if (onActionResult) onActionResult("Only the party leader can promote.");
        return false;
    }

    auto it = std::find_if(members.begin(), members.end(),
        [&](const PartyMemberInfo& m) { return m.characterId == characterId; });

    if (it == members.end()) {
        if (onActionResult) onActionResult("Player not found in party.");
        return false;
    }

    if (it->isLeader) {
        if (onActionResult) onActionResult("Player is already the leader.");
        return false;
    }

    // Demote current leader
    for (auto& m : members) {
        if (m.isLeader) {
            m.isLeader = false;
            break;
        }
    }

    // Promote new leader
    it->isLeader = true;
    isLeader = false;

    if (onPartyChanged) onPartyChanged();
    if (onActionResult) onActionResult(it->characterName + " is now the party leader.");
    return true;
}

void PartyManager::leaveParty() {
    if (!isInParty()) return;

    members.clear();
    partyId = -1;
    isLeader = false;
    lootMode = PartyLootMode::FreeForAll;

    if (onPartyChanged) onPartyChanged();
    if (onActionResult) onActionResult("You left the party.");
}

void PartyManager::disbandParty() {
    members.clear();
    partyId = -1;
    isLeader = false;
    lootMode = PartyLootMode::FreeForAll;

    if (onPartyChanged) onPartyChanged();
    if (onActionResult) onActionResult("Party disbanded.");
}

// ============================================================================
// Loot
// ============================================================================

void PartyManager::setLootMode(PartyLootMode mode) {
    lootMode = mode;
    if (onPartyChanged) onPartyChanged();
}

// ============================================================================
// Invites
// ============================================================================

void PartyManager::addInvite(const PartyInviteInfo& invite) {
    // Replace existing invite from the same party
    for (auto& existing : pendingInvites) {
        if (existing.partyId == invite.partyId) {
            existing = invite;
            if (onInviteReceived) onInviteReceived(existing);
            return;
        }
    }

    pendingInvites.push_back(invite);
    if (onInviteReceived) onInviteReceived(invite);
}

bool PartyManager::acceptInvite(int inviteId) {
    auto it = std::find_if(pendingInvites.begin(), pendingInvites.end(),
        [&](const PartyInviteInfo& inv) { return inv.inviteId == inviteId; });

    if (it == pendingInvites.end()) {
        if (onActionResult) onActionResult("Invite not found.");
        return false;
    }

    if (it->isExpired()) {
        pendingInvites.erase(it);
        if (onActionResult) onActionResult("Invite has expired.");
        return false;
    }

    if (isInParty()) {
        if (onActionResult) onActionResult("Already in a party.");
        return false;
    }

    // Server handles the actual join; clear the invite locally
    pendingInvites.erase(it);
    if (onActionResult) onActionResult("Invite accepted.");
    return true;
}

void PartyManager::declineInvite(int inviteId) {
    auto it = std::find_if(pendingInvites.begin(), pendingInvites.end(),
        [&](const PartyInviteInfo& inv) { return inv.inviteId == inviteId; });

    if (it != pendingInvites.end()) {
        pendingInvites.erase(it);
    }

    if (onActionResult) onActionResult("Invite declined.");
}

// ============================================================================
// Member Updates
// ============================================================================

void PartyManager::updateMemberInfo(const std::string& characterId, int hp, int maxHp,
                                    int mp, int maxMp, const std::string& scene) {
    for (auto& m : members) {
        if (m.characterId == characterId) {
            m.currentHP = hp;
            m.maxHP = maxHp;
            m.currentMP = mp;
            m.maxMP = maxMp;
            m.sceneName = scene;
            if (onPartyChanged) onPartyChanged();
            return;
        }
    }
}

std::vector<std::string> PartyManager::getMembersInScene(const std::string& sceneName) const {
    std::vector<std::string> result;
    for (const auto& m : members) {
        if (m.sceneName == sceneName && m.isOnline) {
            result.push_back(m.characterId);
        }
    }
    return result;
}

// ============================================================================
// Tick
// ============================================================================

void PartyManager::tick(float deltaTime) {
    // Expire pending invites
    bool changed = false;
    for (auto& inv : pendingInvites) {
        inv.expiresInSeconds -= deltaTime;
    }

    auto it = std::remove_if(pendingInvites.begin(), pendingInvites.end(),
        [](const PartyInviteInfo& inv) { return inv.isExpired(); });

    if (it != pendingInvites.end()) {
        pendingInvites.erase(it, pendingInvites.end());
        changed = true;
    }

    if (changed && onPartyChanged) {
        onPartyChanged();
    }
}

} // namespace fate
