#include <doctest/doctest.h>
#include "engine/net/socket.h"
#include <thread>
#include <chrono>

using namespace fate;

TEST_CASE("NetSocket: create and bind") {
    NetSocket::initPlatform();
    NetSocket sock;
    CHECK(sock.open(0));
    CHECK(sock.port() > 0);
    sock.close();
    NetSocket::shutdownPlatform();
}

TEST_CASE("NetSocket: send and receive loopback") {
    NetSocket::initPlatform();
    NetSocket sender, receiver;
    CHECK(sender.open(0));
    CHECK(receiver.open(0));

    uint8_t sendBuf[] = {0xDE, 0xAD, 0xBE, 0xEF};
    NetAddress loopback{0x7F000001, receiver.port()};

    int sent = sender.sendTo(sendBuf, 4, loopback);
    CHECK(sent == 4);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint8_t recvBuf[64];
    NetAddress from;
    int received = receiver.recvFrom(recvBuf, sizeof(recvBuf), from);
    CHECK(received == 4);
    CHECK(recvBuf[0] == 0xDE);
    CHECK(recvBuf[3] == 0xEF);

    sender.close();
    receiver.close();
    NetSocket::shutdownPlatform();
}
