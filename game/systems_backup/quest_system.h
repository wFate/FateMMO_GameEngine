#pragma once
#include "engine/ecs/world.h"
#include "game/components/game_components.h"
#include "game/components/player_controller.h"
#include "game/shared/quest_data.h"
#include "engine/core/logger.h"

#include <string>

namespace fate {

// Tracks quest progress via event methods and updates NPC quest markers.
// Other systems call onMobDeath / onItemPickup / onNPCInteraction / onPvPKill
// to route events to every player's QuestManager.
class QuestSystem : public System {
public:
    const char* name() const override { return "QuestSystem"; }

    void update(float /*dt*/) override {
        if (!world_) return;

        // On first frame, wire up callbacks for event-driven marker updates
        if (!callbacksWired_) {
            wireCallbacks();
        }
    }

    // ---- Event Methods (called by other systems) ----------------------------

    // Called by CombatActionSystem when a mob dies
    void onMobDeath(const std::string& mobId) {
        if (!world_) return;
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onMobKilled(mobId);
        });
    }

    // Called when a player picks up / receives an item
    void onItemPickup(const std::string& itemId) {
        if (!world_) return;
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onItemCollected(itemId);
        });
    }

    // Called by NPCInteractionSystem when player talks to an NPC
    void onNPCInteraction(uint32_t npcId) {
        if (!world_) return;
        std::string npcIdStr = std::to_string(npcId);
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onNPCTalkedTo(npcIdStr);
        });
    }

    // Called by combat system on PvP kill
    void onPvPKill() {
        if (!world_) return;
        world_->forEach<QuestComponent>([&](Entity*, QuestComponent* qc) {
            qc->quests.onPvPKill();
        });
    }

    // ---- Quest Marker Updates -----------------------------------------------

    // Update quest markers on all NPC entities (event-driven via callbacks)
    void refreshQuestMarkers() {
        if (!world_) return;

        // Find local player's quest state and level
        QuestManager* playerQuests = nullptr;
        int playerLevel = 0;
        world_->forEach<QuestComponent, PlayerController>(
            [&](Entity*, QuestComponent* qc, PlayerController* pc) {
                if (pc->isLocalPlayer) {
                    playerQuests = &qc->quests;
                }
            }
        );
        if (!playerQuests) return;

        // Get player level from CharacterStatsComponent
        world_->forEach<CharacterStatsComponent, PlayerController>(
            [&](Entity*, CharacterStatsComponent* sc, PlayerController* pc) {
                if (pc->isLocalPlayer) {
                    playerLevel = sc->stats.level;
                }
            }
        );

        // Update each quest-giving NPC's marker
        // World only has 1 and 2-component forEach, so iterate QuestGiverComponent
        // and fetch the others via getComponent.
        world_->forEach<QuestGiverComponent>(
            [&](Entity* entity, QuestGiverComponent* qg) {
                auto* npcComp = entity->getComponent<NPCComponent>();
                auto* marker = entity->getComponent<QuestMarkerComponent>();
                if (!npcComp || !marker) return;

                marker->currentState = MarkerState::None;
                QuestTier bestTier = QuestTier::Starter;
                bool hasAvailable = false;

                std::string npcIdStr = std::to_string(npcComp->npcId);

                for (uint32_t questId : qg->questIds) {
                    const auto* def = QuestData::getQuest(questId);
                    if (!def) continue;

                    // Check turn-in first (highest priority)
                    if (playerQuests->isQuestComplete(questId)
                        && def->turnInNpcId == npcIdStr) {
                        marker->currentState = MarkerState::TurnIn;
                        return; // TurnIn takes priority, stop checking
                    }

                    // Check if quest is available to accept
                    if (playerQuests->canAcceptQuest(questId, playerLevel)) {
                        hasAvailable = true;
                        if (def->tier > bestTier) bestTier = def->tier;
                    }
                }

                if (hasAvailable) {
                    marker->currentState = MarkerState::Available;
                    marker->highestTier = bestTier;
                }
            }
        );
    }

private:
    bool callbacksWired_ = false;

    void wireCallbacks() {
        world_->forEach<QuestComponent, PlayerController>(
            [&](Entity*, QuestComponent* qc, PlayerController* pc) {
                if (!pc->isLocalPlayer) return;
                qc->quests.onQuestAccepted = [this](uint32_t) { refreshQuestMarkers(); };
                qc->quests.onQuestCompleted = [this](uint32_t) { refreshQuestMarkers(); };
            }
        );
        callbacksWired_ = true;
        refreshQuestMarkers(); // Initial marker state
    }
};

} // namespace fate
