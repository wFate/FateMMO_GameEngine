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
        case GuildAction::Invite: {
            std::string targetCharId = payload.readString();
            if (!validatePayload(payload, clientId, PacketType::CmdGuild)) return;

            auto sendGuildResult = [&](uint8_t code, const std::string& msg) {
                SvGuildUpdateMsg resp;
                resp.updateType = 5; resp.resultCode = code;
                resp.message = msg;
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
            };

            // Must be in a guild
            auto* guildComp = e->getComponent<GuildComponent>();
            if (!guildComp || !guildComp->guild.isInGuild()) {
                sendGuildResult(1, "You are not in a guild");
                break;
            }

            // Must be officer or owner to invite
            if (!guildComp->guild.canInvite()) {
                sendGuildResult(1, "Only officers and the guild owner can invite");
                break;
            }

            // Faction check: target must match guild's faction
            auto guildInfo = guildRepo_->getGuildInfo(guildComp->guild.guildId);
            if (!guildInfo) {
                sendGuildResult(1, "Guild not found");
                break;
            }

            Entity* targetEntity = nullptr;
            uint16_t targetClientId = 0;
            server_.connections().forEach([&](ClientConnection& c) {
                if (c.character_id == targetCharId && c.playerEntityId != 0) {
                    PersistentId tp(c.playerEntityId);
                    EntityHandle th = getReplicationForClient(c.clientId).getEntityHandle(tp);
                    targetEntity = getWorldForClient(c.clientId).getEntity(th);
                    targetClientId = c.clientId;
                }
            });
            if (!targetEntity) {
                sendGuildResult(1, "Player not found or offline");
                break;
            }

            auto* targetFaction = targetEntity->getComponent<FactionComponent>();
            if (!targetFaction || static_cast<int>(targetFaction->faction) != guildInfo->factionId) {
                sendGuildResult(1, "Player is not in your faction");
                break;
            }

            // Target must not already be in a guild
            auto* targetGuild = targetEntity->getComponent<GuildComponent>();
            if (targetGuild && targetGuild->guild.isInGuild()) {
                sendGuildResult(1, "Player is already in a guild");
                break;
            }

            // Check guild is not full
            if (guildInfo->memberCount >= guildInfo->maxMembers) {
                sendGuildResult(1, "Guild is full");
                break;
            }

            // Add member to guild in DB
            GuildDbResult dbResult;
            if (guildRepo_->addMember(guildComp->guild.guildId, targetCharId, 0, dbResult)) {
                // Update target's GuildComponent
                if (targetGuild) {
                    targetGuild->guild.setGuildData(guildComp->guild.guildId, guildInfo->guildName,
                                                     {}, GuildRank::Member, guildInfo->guildLevel);
                }
                // Notify inviter
                sendGuildResult(0, targetCharId + " joined the guild");
                // Notify target
                SvGuildUpdateMsg targetResp;
                targetResp.updateType = 1; // joined
                targetResp.resultCode = 0;
                targetResp.guildName = guildInfo->guildName;
                targetResp.message = "You joined " + guildInfo->guildName;
                uint8_t tbuf[256]; ByteWriter tw(tbuf, sizeof(tbuf));
                targetResp.write(tw);
                server_.sendTo(targetClientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, tbuf, tw.size());
            } else {
                sendGuildResult(static_cast<uint8_t>(dbResult), "Failed to add member");
            }
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
