#include "doctest/doctest.h"
#include "engine/net/auth_protocol.h"

TEST_CASE("Auth input validation") {
    using namespace fate;

    SUBCASE("Username validation") {
        CHECK(AuthValidation::isValidUsername("player1") == true);
        CHECK(AuthValidation::isValidUsername("ab") == false);           // too short (< 3)
        CHECK(AuthValidation::isValidUsername("a]b") == false);          // invalid char
        CHECK(AuthValidation::isValidUsername("valid_name") == true);    // underscore ok
        CHECK(AuthValidation::isValidUsername("") == false);             // empty
        CHECK(AuthValidation::isValidUsername("abcdefghijklmnopqrstu") == false); // 21 chars (> 20)
        CHECK(AuthValidation::isValidUsername("abc") == true);           // exactly 3
    }

    SUBCASE("Password validation") {
        CHECK(AuthValidation::isValidPassword("password") == true);     // exactly 8
        CHECK(AuthValidation::isValidPassword("short") == false);       // too short (< 8)
        CHECK(AuthValidation::isValidPassword("longpassword123!@#") == true);
        CHECK(AuthValidation::isValidPassword("pass\x01word") == false); // non-printable
    }

    SUBCASE("Character name validation") {
        CHECK(AuthValidation::isValidCharacterName("Hero") == true);
        CHECK(AuthValidation::isValidCharacterName("My Hero") == true); // spaces ok
        CHECK(AuthValidation::isValidCharacterName("X") == false);      // too short (< 2)
        CHECK(AuthValidation::isValidCharacterName(" Hero") == false);  // leading space
        CHECK(AuthValidation::isValidCharacterName("Hero ") == false);  // trailing space
        CHECK(AuthValidation::isValidCharacterName("He") == true);      // exactly 2
        CHECK(AuthValidation::isValidCharacterName("Name!") == false);  // invalid char
    }
}

TEST_CASE("AuthToken generation") {
    using namespace fate;
    AuthToken t1 = generateAuthToken();
    AuthToken t2 = generateAuthToken();
    CHECK(t1 != t2); // statistically guaranteed unique

    // Verify not all zeros
    AuthToken zeros = {};
    CHECK(t1 != zeros);
}

TEST_CASE("Auth message serialization") {
    using namespace fate;

    SUBCASE("RegisterRequest round-trip") {
        RegisterRequest req;
        req.username = "testuser";
        req.password = "mypassword";
        req.characterName = "Hero";
        req.className = "Warrior";

        std::vector<uint8_t> buf(512);
        ByteWriter w(buf.data(), buf.size());
        req.write(w);

        ByteReader r(buf.data(), w.size());
        uint8_t type = r.readU8(); // consume the message type byte
        CHECK(type == static_cast<uint8_t>(AuthMessageType::RegisterRequest));
        auto decoded = RegisterRequest::read(r);
        CHECK(decoded.username == "testuser");
        CHECK(decoded.password == "mypassword");
        CHECK(decoded.characterName == "Hero");
        CHECK(decoded.className == "Warrior");
    }

    SUBCASE("LoginRequest round-trip") {
        LoginRequest req;
        req.username = "testuser";
        req.password = "mypassword";

        std::vector<uint8_t> buf(256);
        ByteWriter w(buf.data(), buf.size());
        req.write(w);

        ByteReader r(buf.data(), w.size());
        uint8_t type = r.readU8(); // consume the message type byte
        CHECK(type == static_cast<uint8_t>(AuthMessageType::LoginRequest));
        auto decoded = LoginRequest::read(r);
        CHECK(decoded.username == "testuser");
        CHECK(decoded.password == "mypassword");
    }

    SUBCASE("AuthResponse success round-trip") {
        AuthResponse resp;
        resp.success = true;
        resp.authToken = generateAuthToken();
        resp.errorReason = "";
        resp.characterName = "Hero";
        resp.className = "Warrior";
        resp.level = 42;

        std::vector<uint8_t> buf(512);
        ByteWriter w(buf.data(), buf.size());
        resp.write(w);

        ByteReader r(buf.data(), w.size());
        auto decoded = AuthResponse::read(r);
        CHECK(decoded.success == true);
        CHECK(decoded.authToken == resp.authToken);
        CHECK(decoded.characterName == "Hero");
        CHECK(decoded.className == "Warrior");
        CHECK(decoded.level == 42);
    }

    SUBCASE("AuthResponse failure round-trip") {
        AuthResponse resp;
        resp.success = false;
        resp.errorReason = "Invalid password";

        std::vector<uint8_t> buf(512);
        ByteWriter w(buf.data(), buf.size());
        resp.write(w);

        ByteReader r(buf.data(), w.size());
        auto decoded = AuthResponse::read(r);
        CHECK(decoded.success == false);
        CHECK(decoded.errorReason == "Invalid password");
        // Preview fields should be default (not serialized on failure)
        CHECK(decoded.characterName == "");
        CHECK(decoded.level == 0);
    }
}
