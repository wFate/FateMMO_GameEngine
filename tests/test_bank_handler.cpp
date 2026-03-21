#include <doctest/doctest.h>
#include "game/shared/bank_storage.h"
#include "engine/net/game_messages.h"

using namespace fate;

// ============================================================================
// BankStorage game-logic tests — handler-focused edge cases
// ============================================================================

TEST_CASE("BankHandler: deposit gold with 2% fee") {
    BankStorage bank;
    bank.initialize(30);

    CHECK(bank.depositGold(1000, 0.02f));
    // fee = floor(1000 * 0.02) = 20, deposited = 980
    CHECK(bank.getStoredGold() == 980);
}

TEST_CASE("BankHandler: deposit gold fee eats entire amount") {
    BankStorage bank;
    bank.initialize(30);

    // deposit 1 gold with 2% fee => fee = floor(1*0.02) = 0, deposited = 1
    CHECK(bank.depositGold(1, 0.02f));
    CHECK(bank.getStoredGold() == 1);

    // deposit 0 gold => rejected
    CHECK_FALSE(bank.depositGold(0, 0.02f));

    // 100% fee eats everything
    CHECK_FALSE(bank.depositGold(100, 1.0f));
}

TEST_CASE("BankHandler: withdraw gold success") {
    BankStorage bank;
    bank.initialize(30);

    bank.depositGold(5000, 0.0f);
    CHECK(bank.getStoredGold() == 5000);

    CHECK(bank.withdrawGold(3000));
    CHECK(bank.getStoredGold() == 2000);
}

TEST_CASE("BankHandler: withdraw more gold than stored fails") {
    BankStorage bank;
    bank.initialize(30);

    bank.depositGold(100, 0.0f);
    CHECK_FALSE(bank.withdrawGold(200));
    CHECK(bank.getStoredGold() == 100);
}

TEST_CASE("BankHandler: withdraw negative gold fails") {
    BankStorage bank;
    bank.initialize(30);

    bank.depositGold(100, 0.0f);
    CHECK_FALSE(bank.withdrawGold(-1));
    CHECK(bank.getStoredGold() == 100);
}

TEST_CASE("BankHandler: deposit item stacking") {
    BankStorage bank;
    bank.initialize(10);

    CHECK(bank.depositItem("potion_hp", 5));
    CHECK(bank.depositItem("potion_hp", 3));  // stacks onto existing
    CHECK(bank.getItems().size() == 1);
    CHECK(bank.getItems()[0].count == 8);
}

TEST_CASE("BankHandler: bank full rejects new item") {
    BankStorage bank;
    bank.initialize(2);

    CHECK(bank.depositItem("item_a", 1));
    CHECK(bank.depositItem("item_b", 1));
    CHECK(bank.isFull());

    // New distinct item rejected
    CHECK_FALSE(bank.depositItem("item_c", 1));

    // But stacking onto existing still works
    CHECK(bank.depositItem("item_a", 5));
    CHECK(bank.getItems()[0].count == 6);
}

TEST_CASE("BankHandler: withdraw item reduces count") {
    BankStorage bank;
    bank.initialize(10);

    bank.depositItem("potion_hp", 10);
    CHECK(bank.withdrawItem("potion_hp", 3));
    CHECK(bank.getItems().size() == 1);
    CHECK(bank.getItems()[0].count == 7);
}

TEST_CASE("BankHandler: withdraw item removes slot when count reaches zero") {
    BankStorage bank;
    bank.initialize(10);

    bank.depositItem("potion_hp", 5);
    CHECK(bank.withdrawItem("potion_hp", 5));
    CHECK(bank.getItems().empty());
}

TEST_CASE("BankHandler: withdraw nonexistent item fails") {
    BankStorage bank;
    bank.initialize(10);

    CHECK_FALSE(bank.withdrawItem("ghost_item", 1));
}

TEST_CASE("BankHandler: withdraw more items than stored fails") {
    BankStorage bank;
    bank.initialize(10);

    bank.depositItem("potion_hp", 3);
    CHECK_FALSE(bank.withdrawItem("potion_hp", 5));
    // Count unchanged
    CHECK(bank.getItems()[0].count == 3);
}

TEST_CASE("BankHandler: deposit zero items fails") {
    BankStorage bank;
    bank.initialize(10);

    CHECK_FALSE(bank.depositItem("potion_hp", 0));
    CHECK(bank.getItems().empty());
}

TEST_CASE("BankHandler: withdraw zero items fails") {
    BankStorage bank;
    bank.initialize(10);

    bank.depositItem("potion_hp", 5);
    CHECK_FALSE(bank.withdrawItem("potion_hp", 0));
    CHECK(bank.getItems()[0].count == 5);
}

TEST_CASE("BankHandler: CmdBankMsg serialization roundtrip") {
    CmdBankMsg orig;
    orig.action = 2;       // deposit gold
    orig.goldAmount = 12345;
    orig.itemId = "rare_sword";
    orig.itemCount = 3;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    orig.write(w);

    ByteReader r(buf, w.size());
    auto decoded = CmdBankMsg::read(r);

    CHECK(decoded.action == 2);
    CHECK(decoded.goldAmount == 12345);
    CHECK(decoded.itemId == "rare_sword");
    CHECK(decoded.itemCount == 3);
}

TEST_CASE("BankHandler: SvBankResultMsg serialization roundtrip") {
    SvBankResultMsg orig;
    orig.action = 3;
    orig.success = 1;
    orig.bankGold = 99999;
    orig.message = "Withdrawal successful";

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    orig.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvBankResultMsg::read(r);

    CHECK(decoded.action == 3);
    CHECK(decoded.success == 1);
    CHECK(decoded.bankGold == 99999);
    CHECK(decoded.message == "Withdrawal successful");
}

TEST_CASE("BankHandler: deposit gold negative amount rejected") {
    BankStorage bank;
    bank.initialize(30);

    CHECK_FALSE(bank.depositGold(-100, 0.02f));
    CHECK(bank.getStoredGold() == 0);
}
