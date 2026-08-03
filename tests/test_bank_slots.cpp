#include <catch_amalgamated.hpp>

#include "game/bank_slots.hpp"

using namespace wowee::game::slots;

// A slot number is what a swap request names. Getting one wrong does not draw
// something odd or answer nil — it moves an item to a real place nobody asked
// for, which looks like the player did it. These pin the figures the client's
// own bank window has been sending, so the interface bindings and that window
// cannot drift apart.

TEST_CASE("The bank's three regions do not overlap", "[bank][slots]") {
    // The general slots run out exactly where the bags begin.
    REQUIRE(bankGeneralWireSlot(kBankGeneralCount - 1) + 1 == bankBagWireSlot(0));
    // And the bags end before the keyring starts.
    REQUIRE(bankBagWireSlot(kBankBagCount - 1) < keyringWireSlot(0));
}

TEST_CASE("The wire slots are the ones this client already sends",
          "[bank][slots]") {
    // Read out of inventory_screen.cpp, which has been sending these all along.
    REQUIRE(bankGeneralWireSlot(0) == 39);
    REQUIRE(bankBagWireSlot(0) == 67);
    REQUIRE(keyringWireSlot(0) == 86);
}

TEST_CASE("The interface's first bank bag is the wire's, one higher",
          "[bank][slots]") {
    // BankButtonIDToInvSlotID(1, isBag) answers this, and PickupBagFromSlot is
    // handed it back. The two have to meet exactly or a bag is picked up from
    // one slot and put down in another.
    REQUIRE(kFirstBankBagInventorySlot == 68);
    REQUIRE(toWireSlot(kFirstBankBagInventorySlot) == bankBagWireSlot(0));
}

TEST_CASE("Crossing between the two numberings round-trips", "[bank][slots]") {
    for (int wire = 0; wire < 100; ++wire) {
        REQUIRE(toWireSlot(toInventorySlot(wire)) == wire);
    }
}

TEST_CASE("Every bank bag maps to a distinct slot in both numberings",
          "[bank][slots]") {
    for (int i = 0; i < kBankBagCount; ++i) {
        const int inv = toInventorySlot(bankBagWireSlot(i));
        REQUIRE(inv == kFirstBankBagInventorySlot + i);
        REQUIRE(toWireSlot(inv) == bankBagWireSlot(i));
    }
}
