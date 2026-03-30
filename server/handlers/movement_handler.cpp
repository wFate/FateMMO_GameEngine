#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/box_collider.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::processMove(uint16_t clientId, const CmdMove& move) {
    // Rate limit check
    int maxPerTick = static_cast<int>(MAX_MOVES_PER_SEC / TICK_RATE);
    if (maxPerTick < 1) maxPerTick = 1;
    moveCountThisTick_[clientId]++;
    if (moveCountThisTick_[clientId] > maxPerTick) {
        // Silently drop excess moves (was spamming logs)
        return;
    }

    auto* client = server_.connections().findById(clientId);
    if (client && client->playerEntityId != 0) {
        PersistentId pid(client->playerEntityId);
        EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
        Entity* e = getWorldForClient(clientId).getEntity(h);
        if (!e) return;

        // Moving while casting interrupts the cast
        auto* charStats = e->getComponent<CharacterStatsComponent>();
        if (charStats && charStats->stats.isCasting()) {
            charStats->stats.interruptCast();
            LOG_INFO("Server", "Client %d cast interrupted by movement", clientId);
        }

        // Moving while channeling interrupts the channel
        if (charStats && charStats->stats.isChanneling()) {
            charStats->stats.interruptChannel();
            LOG_INFO("Server", "Client %d channel interrupted by movement", clientId);
        }

        // Steady Aim: reset timer on movement
        if (charStats && charStats->stats.steadyAimActive) {
            charStats->stats.steadyAimTimer = 0.0f;
            charStats->stats.steadyAimReady = false;
        }

        // First move after connect: accept unconditionally (position desync)
        if (needsFirstMoveSync_.count(clientId)) {
            needsFirstMoveSync_.erase(clientId);
            if (move.position.x < -32768.0f || move.position.x > 32768.0f ||
                move.position.y < -32768.0f || move.position.y > 32768.0f) {
                LOG_WARN("Server", "Client %d first move out of bounds (%.0f, %.0f)",
                         clientId, move.position.x, move.position.y);
                return;
            }
            auto* t = e->getComponent<Transform>();
            if (t) t->position = move.position;
            lastValidPositions_[clientId] = move.position;
            lastMoveTime_[clientId] = gameTime_;
            playerDirty_[clientId].position = true;
            enqueuePersist(clientId, PersistPriority::NORMAL, PersistType::Position);
            return;
        }

        // Compute time delta since last move
        float now = gameTime_;
        float timeDelta = now - lastMoveTime_[clientId];
        if (timeDelta < 0.001f) timeDelta = 0.001f;

        // Check distance against max allowed
        Vec2 lastPos = lastValidPositions_[clientId];
        float dist = lastPos.distance(move.position);
        float maxDist = MAX_MOVE_SPEED * timeDelta;

        if (dist > maxDist + RUBBER_BAND_THRESHOLD) {
            // Rubber-band: reject move and send correction
            LOG_WARN("Server", "Client %d moved too far (%.1f > %.1f), rubber-banding",
                     clientId, dist, maxDist);
            SvMovementCorrectionMsg correction;
            correction.correctedPosition = lastPos;
            correction.rubberBand = 1;
            uint8_t buf[32];
            ByteWriter w(buf, sizeof(buf));
            correction.write(w);
            server_.sendTo(clientId, Channel::Unreliable,
                           PacketType::SvMovementCorrection, buf, w.size());
        } else {
            // Reject moves to out-of-bounds positions (symmetric range allows negative coords)
            if (move.position.x < -32768.0f || move.position.x > 32768.0f ||
                move.position.y < -32768.0f || move.position.y > 32768.0f) {
                LOG_WARN("Server", "Client %d move destination out of bounds (%.0f, %.0f)",
                         clientId, move.position.x, move.position.y);
                return;
            }
            // Collision grid check (server-authoritative)
            // charStats already fetched above (cast interrupt check)
            if (charStats) {
                auto gridIt = collisionGrids_.find(charStats->stats.currentScene);
                if (gridIt != collisionGrids_.end()) {
                    float halfW = 12.0f, halfH = 12.0f;
                    auto* box = e->getComponent<BoxCollider>();
                    if (box) { halfW = box->size.x * 0.5f; halfH = box->size.y * 0.5f; }
                    if (gridIt->second.isBlockedRect(move.position.x, move.position.y, halfW, halfH)) {
                        SvMovementCorrectionMsg correction;
                        correction.correctedPosition = lastValidPositions_[clientId];
                        correction.rubberBand = 1;
                        uint8_t buf[32];
                        ByteWriter w(buf, sizeof(buf));
                        correction.write(w);
                        server_.sendTo(clientId, Channel::Unreliable,
                                       PacketType::SvMovementCorrection, buf, w.size());
                        return;
                    }
                }
            }
            auto* t = e->getComponent<Transform>();
            if (t) t->position = move.position;
            lastValidPositions_[clientId] = move.position;
            lastMoveTime_[clientId] = now;
            playerDirty_[clientId].position = true;
            enqueuePersist(clientId, PersistPriority::NORMAL, PersistType::Position);
        }
    }
}

} // namespace fate
