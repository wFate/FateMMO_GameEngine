#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "game/shared/game_types.h"
#include "game/shared/faction.h"

namespace fate {

// ============================================================================
// Chat Message
// ============================================================================
struct ChatMessage {
    ChatChannel channel{};
    std::string senderName;
    std::string senderId;
    std::string message;
    std::string targetName;
    int64_t timestamp = 0;
    Faction senderFaction = Faction::None;
};

// ============================================================================
// Chat Manager
// ============================================================================
class ChatManager {
public:
    // State
    float lastMessageTime = 0.0f;
    std::vector<ChatMessage> chatHistory;  // Client-side buffer
    std::string characterName;
    std::string characterId;

    // Callbacks
    std::function<void(const ChatMessage&)> onMessageReceived;
    std::function<void(const std::string&)> onChatError;

    // Faction (set after initialization from FactionComponent)
    Faction localFaction = Faction::None;

    // Initialization
    void initialize(const std::string& charId, const std::string& charName);

    // Validation
    [[nodiscard]] bool validateMessage(const std::string& message, ChatChannel channel) const;

    // History
    void addToHistory(const ChatMessage& msg);
    [[nodiscard]] const std::vector<ChatMessage>& getHistory() const;
    void clearHistory();
};

} // namespace fate
