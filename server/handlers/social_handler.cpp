#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/net/game_messages.h"
#include "game/shared/game_types.h"

namespace fate {

void ServerApp::processSocial(uint16_t clientId, ByteReader& payload) {
    uint8_t subAction = payload.readU8();
    if (!validatePayload(payload, clientId, PacketType::CmdSocial)) return;
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    switch (subAction) {
        case SocialAction::SendFriendRequest: {
            std::string targetCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdSocial)) return;
            if (socialRepo_->isBlocked(targetCharId, client->character_id)) {
                // Target has blocked us — silently fail
                break;
            }
            socialRepo_->sendFriendRequest(client->character_id, targetCharId);
            SvSocialUpdateMsg resp;
            resp.updateType = 0; resp.resultCode = 0;
            resp.message = "Friend request sent";
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvSocialUpdate, buf, w.size());
            break;
        }
        case SocialAction::AcceptFriend: {
            std::string fromCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdSocial)) return;
            socialRepo_->acceptFriendRequest(client->character_id, fromCharId);
            SvSocialUpdateMsg resp;
            resp.updateType = 1; resp.resultCode = 0;
            resp.message = "Friend request accepted";
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvSocialUpdate, buf, w.size());
            break;
        }
        case SocialAction::RemoveFriend: {
            std::string friendCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdSocial)) return;
            socialRepo_->removeFriend(client->character_id, friendCharId);
            break;
        }
        case SocialAction::BlockPlayer: {
            std::string targetCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdSocial)) return;
            socialRepo_->blockPlayer(client->character_id, targetCharId);
            break;
        }
        case SocialAction::UnblockPlayer: {
            std::string targetCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdSocial)) return;
            socialRepo_->unblockPlayer(client->character_id, targetCharId);
            break;
        }
        default:
            LOG_INFO("Server", "Social sub-action %d from client %d (not yet implemented)", subAction, clientId);
            break;
    }
}

} // namespace fate
