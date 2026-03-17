// ============================================================================
// NOTE: Networking & Database Integration Pending
// ============================================================================
// NETWORKING (ENet):
//   - Handle CmdSendMessage(channel, message) from client
//   - Server-side message routing by channel:
//     * Map: Broadcast to players within PROXIMITY_RANGE (15 units)
//     * Global: Broadcast to ALL connected clients
//     * Trade: Broadcast to ALL connected clients
//     * Party: Send to party members only (via PartyManager)
//     * Guild: Send to guild members only (via GuildManager)
//     * Private: "/PlayerName message" -> route to specific player
//     * System: Server-only announcements (legendary drops, events)
//   - Rate limiting: 0.5s between messages (server enforced)
//   - Message length: max 200 characters
//   - Profanity filter integration (ProfanityFilter equivalent)
//   - Block check: blocked players cannot send messages to each other
//
// DATABASE (libpqxx):
//   - No persistent chat storage (messages are transient)
//   - Block list checked via SocialRepository for private messages
//   - Guild membership checked via GuildRepository for guild chat
// ============================================================================

#include "game/shared/chat_manager.h"
#include "game/shared/faction.h"

#include <algorithm>

namespace fate {

// ============================================================================
// Initialization
// ============================================================================

void ChatManager::initialize(const std::string& charId, const std::string& charName) {
    characterId = charId;
    characterName = charName;
    lastMessageTime = 0.0f;
    chatHistory.clear();
}

// ============================================================================
// Validation
// ============================================================================

bool ChatManager::validateMessage(const std::string& message, ChatChannel /*channel*/) const {
    // Empty messages not allowed
    if (message.empty()) {
        return false;
    }

    // Length check
    if (static_cast<int>(message.size()) > ChatConstants::MAX_MESSAGE_LENGTH) {
        return false;
    }

    // Rate limit check (caller must supply currentTime via lastMessageTime comparison)
    // Note: Server-side enforcement will use real timestamps; this is a client-side guard
    // The actual elapsed time comparison happens at the call site where currentTime is known

    return true;
}

// ============================================================================
// History
// ============================================================================

void ChatManager::addToHistory(const ChatMessage& msg) {
    ChatMessage stored = msg;

    // Cross-faction garbling: garble public channel messages from other factions
    bool isPublicChannel = (stored.channel == ChatChannel::Map
                         || stored.channel == ChatChannel::Global
                         || stored.channel == ChatChannel::Trade);
    if (isPublicChannel
        && localFaction != Faction::None
        && stored.senderFaction != Faction::None
        && !FactionRegistry::isSameFaction(localFaction, stored.senderFaction))
    {
        stored.message = FactionChatGarbler::garble(stored.message);
    }

    chatHistory.push_back(stored);

    // Cap at MAX_CHAT_HISTORY
    if (static_cast<int>(chatHistory.size()) > ChatConstants::MAX_CHAT_HISTORY) {
        chatHistory.erase(chatHistory.begin());
    }

    if (onMessageReceived) {
        onMessageReceived(stored);
    }
}

const std::vector<ChatMessage>& ChatManager::getHistory() const {
    return chatHistory;
}

void ChatManager::clearHistory() {
    chatHistory.clear();
}

} // namespace fate
