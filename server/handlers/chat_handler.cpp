#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/shared/profanity_filter.h"
#include "engine/net/game_messages.h"
#include "server/gm_commands.h"
#include <chrono>

namespace fate {

void ServerApp::processChat(uint16_t clientId, const CmdChat& chat_in) {
    CmdChat chat = chat_in;

    // GM command intercept — check before profanity filter and broadcast
    {
        auto parsed = GMCommandParser::parse(chat.message);
        if (parsed.isCommand) {
            auto sendSystemMsg = [this](uint16_t targetClientId, const std::string& text) {
                SvChatMessageMsg sysMsg;
                sysMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
                sysMsg.senderName = "System";
                sysMsg.message    = text;
                sysMsg.faction    = 0;
                uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
                sysMsg.write(w);
                server_.sendTo(targetClientId, Channel::ReliableOrdered,
                               PacketType::SvChatMessage, buf, w.size());
            };

            auto* cmd = gmCommands_.findCommand(parsed.commandName);
            if (!cmd) {
                sendSystemMsg(clientId, "Unknown command: /" + parsed.commandName);
                return;
            }
            AdminRole role = clientAdminRoles_.count(clientId) ? clientAdminRoles_[clientId] : AdminRole::Player;
            if (!GMCommandRegistry::hasPermission(role, cmd->minRole)) {
                sendSystemMsg(clientId, "Insufficient permission.");
                return;
            }
            cmd->handler(clientId, parsed.args);
            return; // don't broadcast GM commands as chat
        }
    }

    // Mute check — block chat from muted players (GM commands above still work)
    {
        auto muteIt = clientMutes_.find(clientId);
        if (muteIt != clientMutes_.end()) {
            if (std::chrono::steady_clock::now() >= muteIt->second.expiresAt) {
                clientMutes_.erase(muteIt); // Mute expired
            } else {
                auto remaining = std::chrono::duration_cast<std::chrono::minutes>(
                    muteIt->second.expiresAt - std::chrono::steady_clock::now()).count();
                SvChatMessageMsg sysMsg;
                sysMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
                sysMsg.senderName = "System";
                sysMsg.message    = "You are muted for " + std::to_string(remaining + 1) + " more minute(s).";
                sysMsg.faction    = 0;
                uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
                sysMsg.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered,
                               PacketType::SvChatMessage, buf, w.size());
                return;
            }
        }
    }

    // Server-side profanity filter
    if (chat.message.empty() || chat.message.size() > 200) return;

    auto filterResult = ProfanityFilter::filterChatMessage(chat.message, FilterMode::Censor);
    chat.message = filterResult.filteredText;

    LOG_INFO("Server", "Chat from client %d (ch=%d): %s",
             clientId, chat.channel, chat.message.c_str());

    // Get sender info
    std::string senderName = "Unknown";
    uint8_t senderFaction = 0;
    auto* client = server_.connections().findById(clientId);
    if (client && client->playerEntityId != 0) {
        PersistentId pid(client->playerEntityId);
        EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
        Entity* e = getWorldForClient(clientId).getEntity(h);
        if (e) {
            auto* nameplate = e->getComponent<NameplateComponent>();
            if (nameplate) senderName = nameplate->displayName;
            auto* factionComp = e->getComponent<FactionComponent>();
            if (factionComp) senderFaction = static_cast<uint8_t>(factionComp->faction);
        }
    }

    // Validate channel: players can only send on Map, Global, Trade, Party, Guild, Private
    uint8_t ch = chat.channel;
    if (ch > static_cast<uint8_t>(ChatChannel::Private)) {
        ch = static_cast<uint8_t>(ChatChannel::Map); // clamp invalid/reserved channels
    }

    // Build and broadcast chat message
    SvChatMessageMsg msg;
    msg.channel    = ch;
    msg.senderName = senderName;
    msg.message    = chat.message;
    msg.faction    = senderFaction;

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
}

} // namespace fate
