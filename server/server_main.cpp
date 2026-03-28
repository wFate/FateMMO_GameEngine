#include "server/server_app.h"
#include "engine/core/logger.h"
#include <cstdlib>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#endif

static fate::ServerApp* g_server = nullptr;

#ifdef _WIN32
static BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        LOG_INFO("Server", "Shutdown signal received (type=%lu)", signal);
        if (g_server) g_server->requestShutdown();
        return TRUE;
    }
    return FALSE;
}
#else
static void signalHandler(int sig) {
    LOG_INFO("Server", "Shutdown signal received (sig=%d)", sig);
    if (g_server) g_server->requestShutdown();
}
#endif

int main(int argc, char* argv[]) {
    uint16_t port = 7777;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    fate::ServerApp server;
    g_server = &server;

#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

    if (!server.init(port)) {
        return 1;
    }

    server.run();
    server.shutdown();
    return 0;
}
