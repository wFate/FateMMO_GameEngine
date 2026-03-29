#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "engine/net/game_messages.h"
#include "game/shared/game_types.h"
#include "game/shared/quest_data.h"
#include <string>

namespace fate {

void ServerApp::processQuestAction(uint16_t clientId, ByteReader& payload) {
    uint8_t subAction = payload.readU8();
    std::string questIdStr = payload.readString();
    if (!validatePayload(payload, clientId, PacketType::CmdQuestAction)) return;
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto* questComp = e->getComponent<QuestComponent>();
    auto* charStats = e->getComponent<CharacterStatsComponent>();
    if (!questComp || !charStats) return;

    uint32_t questId = 0;
    try { questId = static_cast<uint32_t>(std::stoul(questIdStr)); }
    catch (...) { return; }

    switch (subAction) {
        case QuestAction::Accept: {
            bool accepted = questComp->quests.acceptQuest(questId, charStats->stats.level);
            if (accepted) {
                playerDirty_[clientId].quests = true;
                questRepo_->saveQuestProgress(client->character_id, questIdStr, "active", 0, 1);
                SvQuestUpdateMsg resp;
                resp.updateType = 0;
                resp.questId = questIdStr;
                resp.message = "Quest accepted";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestUpdate, buf, w.size());
            }
            break;
        }
        case QuestAction::Abandon: {
            questComp->quests.abandonQuest(questId);
            playerDirty_[clientId].quests = true;
            questRepo_->abandonQuest(client->character_id, questIdStr);
            SvQuestUpdateMsg resp;
            resp.updateType = 3;
            resp.questId = questIdStr;
            resp.message = "Quest abandoned";
            uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
            resp.write(w);
            server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestUpdate, buf, w.size());
            break;
        }
        case QuestAction::TurnIn: {
            auto* inv = e->getComponent<InventoryComponent>();
            if (!inv) break;
            bool turnedIn = questComp->quests.turnInQuest(questId, charStats->stats, inv->inventory);
            if (turnedIn) {
                playerDirty_[clientId].quests = true;
                playerDirty_[clientId].stats = true;
                playerDirty_[clientId].inventory = true;
                enqueuePersist(clientId, PersistPriority::HIGH, PersistType::Quests);
                questRepo_->completeQuest(client->character_id, questIdStr);
                SvQuestUpdateMsg resp;
                resp.updateType = 2;
                resp.questId = questIdStr;
                resp.message = "Quest completed!";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestUpdate, buf, w.size());
                sendPlayerState(clientId);
                checkPlayerCollections(clientId, "OwnItem");

                // Auto-accept chained quest if configured
                const auto* completedDef = QuestData::getQuest(questId);
                if (completedDef && completedDef->autoAcceptNextQuestId != 0) {
                    uint32_t nextId = completedDef->autoAcceptNextQuestId;
                    bool autoAccepted = questComp->quests.acceptQuest(nextId, charStats->stats.level);
                    if (autoAccepted) {
                        playerDirty_[clientId].quests = true;
                        std::string nextIdStr = std::to_string(nextId);
                        questRepo_->saveQuestProgress(client->character_id, nextIdStr, "active", 0, 1);
                        SvQuestUpdateMsg autoResp;
                        autoResp.updateType = 0;
                        autoResp.questId = nextIdStr;
                        autoResp.message = "Quest accepted";
                        uint8_t autoBuf[256]; ByteWriter aw(autoBuf, sizeof(autoBuf));
                        autoResp.write(aw);
                        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvQuestUpdate, autoBuf, aw.size());
                    }
                }
            }
            break;
        }
        default: break;
    }
}

} // namespace fate
