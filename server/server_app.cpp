#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/net/protocol.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#include <chrono>
#include <thread>

namespace fate {

bool ServerApp::init(uint16_t port) {
    if (!NetSocket::initPlatform()) {
        LOG_ERROR("Server", "Failed to init network platform");
        return false;
    }

    if (!server_.start(port)) {
        LOG_ERROR("Server", "Failed to start on port %d", port);
        return false;
    }

    // Set callbacks
    server_.onClientConnected = [this](uint16_t id) { onClientConnected(id); };
    server_.onClientDisconnected = [this](uint16_t id) { onClientDisconnected(id); };
    server_.onPacketReceived = [this](uint16_t id, uint8_t type, ByteReader& r) {
        onPacketReceived(id, type, r);
    };

    // Register gameplay systems (no render systems)
    // For now, empty world — systems will be added when we have entity replication

    LOG_INFO("Server", "Started on port %d at %.0f ticks/sec", port, TICK_RATE);
    return true;
}

void ServerApp::run() {
    running_ = true;
    auto lastTick = std::chrono::high_resolution_clock::now();

#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    while (running_) {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTick).count();

        if (elapsed >= TICK_INTERVAL) {
            lastTick = now;
            gameTime_ += TICK_INTERVAL;
            tick(TICK_INTERVAL);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void ServerApp::shutdown() {
    server_.stop();
    NetSocket::shutdownPlatform();
    LOG_INFO("Server", "Shutdown complete");
}

void ServerApp::tick(float dt) {
    // 1. Drain incoming packets
    server_.poll(gameTime_);

    // 2. World update (systems)
    world_.update(dt);

    // 3. Retransmit unacked reliable packets
    server_.processRetransmits(gameTime_);

    // 4. Check timeouts
    auto timedOut = server_.checkTimeouts(gameTime_);
    for (uint16_t id : timedOut) {
        LOG_INFO("Server", "Client %d timed out", id);
        if (server_.onClientDisconnected) server_.onClientDisconnected(id);
        server_.connections().removeClient(id);
    }
}

void ServerApp::onClientConnected(uint16_t clientId) {
    LOG_INFO("Server", "Client %d connected", clientId);
}

void ServerApp::onClientDisconnected(uint16_t clientId) {
    LOG_INFO("Server", "Client %d disconnected", clientId);
}

void ServerApp::onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload) {
    // Handle game packets — stub for now, will be implemented in Phase 6C/6D
    switch (type) {
        case PacketType::CmdMove: {
            auto move = CmdMove::read(payload);
            // TODO: validate and apply movement
            (void)move;
            break;
        }
        case PacketType::CmdChat: {
            auto chat = CmdChat::read(payload);
            // TODO: route chat message
            LOG_INFO("Server", "Chat from client %d: %s", clientId, chat.message.c_str());
            break;
        }
        default:
            LOG_WARN("Server", "Unknown packet type 0x%02X from client %d", type, clientId);
            break;
    }
}

} // namespace fate
