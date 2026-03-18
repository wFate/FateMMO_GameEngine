#pragma once
#include "engine/net/auth_protocol.h"
#include <string>

namespace fate {

enum class LoginScreenState {
    Login,
    Register,
};

class LoginScreen {
public:
    void draw();
    void reset();

    LoginScreenState state = LoginScreenState::Login;

    // Input fields
    char username[21] = {};      // max 20 chars + null
    char password[129] = {};     // max 128 chars + null
    char confirmPassword[129] = {};
    char email[129] = {};        // max 128 chars + null
    char characterName[17] = {}; // max 16 chars + null
    int selectedClass = 0;       // 0=Warrior, 1=Mage, 2=Archer

    // Server connection info
    char serverHost[64] = "127.0.0.1";
    int serverPort = 7778;       // Auth port

    // Status
    std::string statusMessage;
    bool isError = false;
    bool loginSubmitted = false;
    bool registerSubmitted = false;
};

} // namespace fate
