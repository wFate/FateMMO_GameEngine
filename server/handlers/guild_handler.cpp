#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "engine/net/game_messages.h"
#include "game/shared/game_types.h"
#include "game/shared/faction.h"

namespace fate {

void ServerApp::processGuild(uint16_t clientId, ByteReader& payload) {
    uint8_t subAction = payload.readU8();
    if (!validatePayload(payload, clientId, PacketType::CmdGuild)) return;
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    switch (subAction) {
        case GuildAction::Create: {
            std::string guildName = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdGuild)) return;
            auto* inv = e->getComponent<InventoryComponent>();
            if (!inv || inv->inventory.getGold() < GuildConstants::CREATION_COST) {
                SvGuildUpdateMsg resp;
                resp.updateType = 5; resp.resultCode = 1;
                resp.message = "Not enough gold (need 100,000)";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
                break;
            }

            // Guilds must belong to a faction
            auto* factionComp = e->getComponent<FactionComponent>();
            if (!factionComp || factionComp->faction == Faction::None) {
                SvGuildUpdateMsg resp;
                resp.updateType = 5; resp.resultCode = 2;
                resp.message = "Must belong to a faction to create a guild";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
                break;
            }

            GuildDbResult dbResult;
            int guildId = guildRepo_->createGuild(guildName, client->character_id,
                                                   GuildConstants::DEFAULT_MAX_MEMBERS,
                                                   static_cast<int>(factionComp->faction), dbResult);
            SvGuildUpdateMsg resp;
            if (dbResult == GuildDbResult::Success) {
                // WAL: record gold deduction before mutating
                wal_.appendGoldChange(client->character_id, -static_cast<int64_t>(GuildConstants::CREATION_COST));
                inv->inventory.removeGold(GuildConstants::CREATION_COST);
                playerDirty_[clientId].inventory = true;
                playerDirty_[clientId].guild = true;
                enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
                auto* guildComp = e->getComponent<GuildComponent>();
                if (guildComp) {
                    guildComp->guild.setGuildData(guildId, guildName, {},
                                                   GuildRank::Owner, 1);
                }
                resp.updateType = 0; resp.resultCode = 0;
                resp.guildName = guildName;
                resp.message = "Guild created!";
                sendPlayerState(clientId);
            } else {
                resp.updateType = 5;
                resp.resultCode = static_cast<uint8_t>(dbResult);
                resp.message = dbResult == GuildDbResult::NameTaken ? "Guild name already taken" : "Failed to create guild";
            }
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
            break;
        }
        case GuildAction::Leave: {
            auto* guildComp = e->getComponent<GuildComponent>();
            if (!guildComp || !guildComp->guild.isInGuild()) break;

            GuildDbResult dbResult;
            guildRepo_->removeMember(guildComp->guild.guildId, client->character_id, dbResult);
            if (dbResult == GuildDbResult::Success) {
                guildComp->guild.clearGuildData();
                SvGuildUpdateMsg resp;
                resp.updateType = 2; resp.resultCode = 0;
                resp.message = "You left the guild";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
            }
            break;
        }
        default:
            LOG_INFO("Server", "Guild sub-action %d from client %d (not yet implemented)", subAction, clientId);
            break;
    }
}

} // namespace fate
