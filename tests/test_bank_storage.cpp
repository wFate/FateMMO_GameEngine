#include <doctest/doctest.h>
#include "game/shared/bank_storage.h"

using namespace fate;

TEST_CASE("BankStorage: initialize with max slots") {
    BankStorage bank;
    bank.initialize(30);
    CHECK(bank.getStoredGold() == 0);
    CHECK(bank.getItems().empty());
    CHECK_FALSE(bank.isFull());
}

TEST_CASE("BankStorage: deposit and withdraw gold") {
    BankStorage bank;
    bank.initialize(30);

    CHECK(bank.depositGold(1000, 0.0f));
    CHECK(bank.getStoredGold() == 1000);

    CHECK(bank.withdrawGold(500));
    CHECK(bank.getStoredGold() == 500);

    CHECK_FALSE(bank.withdrawGold(9999));
    CHECK(bank.getStoredGold() == 500);
}

TEST_CASE("BankStorage: deposit gold with fee") {
    BankStorage bank;
    bank.initialize(30);

    CHECK(bank.depositGold(1000, 0.05f));
    CHECK(bank.getStoredGold() == 950);
}

TEST_CASE("BankStorage: deposit and withdraw items") {
    BankStorage bank;
    bank.initialize(3);

    CHECK(bank.depositItem("potion_hp", 5));
    CHECK(bank.depositItem("potion_mp", 3));
    CHECK(bank.depositItem("scroll_tp", 1));
    CHECK(bank.isFull());

    CHECK_FALSE(bank.depositItem("sword_iron", 1));
    CHECK(bank.depositItem("potion_hp", 2));

    CHECK(bank.withdrawItem("potion_hp", 3));
    auto& items = bank.getItems();
    bool found = false;
    for (auto& item : items) {
        if (item.itemId == "potion_hp") {
            CHECK(item.count == 4);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("BankStorage: withdraw more items than stored") {
    BankStorage bank;
    bank.initialize(10);
    bank.depositItem("potion_hp", 3);
    CHECK_FALSE(bank.withdrawItem("potion_hp", 5));
}

TEST_CASE("BankStorage: withdraw all of an item removes the slot") {
    BankStorage bank;
    bank.initialize(10);
    bank.depositItem("potion_hp", 3);
    CHECK(bank.getItems().size() == 1);

    CHECK(bank.withdrawItem("potion_hp", 3));
    CHECK(bank.getItems().empty());
    CHECK_FALSE(bank.isFull());
}

TEST_CASE("BankStorage: withdraw item that doesn't exist") {
    BankStorage bank;
    bank.initialize(10);
    CHECK_FALSE(bank.withdrawItem("nonexistent", 1));
}

TEST_CASE("BankStorage: zero gold deposit") {
    BankStorage bank;
    bank.initialize(10);
    CHECK_FALSE(bank.depositGold(0, 0.0f));
    CHECK(bank.getStoredGold() == 0);
}
