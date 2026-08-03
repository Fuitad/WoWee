#pragma once

// Where the bank, its bags and the keyring sit in the player's inventory.
//
// These numbers were written out in ten places across two files — the client's
// own bank window computed 39 + index, 67 + index and 86 + index in each of its
// two drop paths, and the interface bindings arrived at the same figures by a
// different route. They agreed, and nothing made them agree.
//
// That matters more here than it usually would. A slot number is what a swap
// request names, so a mismatch does not draw something wrong or answer nil: it
// **moves an item somewhere nobody asked for**, and the wrong place is a real
// place, so it looks like the player did it.
//
// The wire counts from zero and the interface counts from one, which is the
// other half of what went wrong every time this was rewritten by hand.

namespace wowee::game::slots {

/// The 28 general bank slots begin here, straight after the worn equipment.
inline constexpr int kBankGeneralFirst = 39;
inline constexpr int kBankGeneralCount = 28;

/// The 7 bank bag slots follow the general ones.
inline constexpr int kBankBagFirst  = kBankGeneralFirst + kBankGeneralCount;  // 67
inline constexpr int kBankBagCount  = 7;

/// The keyring is further along again, past the bags.
inline constexpr int kKeyringFirst = 86;

/// Wire slot for the nth general bank slot, counting from zero.
inline constexpr int bankGeneralWireSlot(int index) {
    return kBankGeneralFirst + index;
}

/// Wire slot for the nth bank bag, counting from zero.
inline constexpr int bankBagWireSlot(int index) {
    return kBankBagFirst + index;
}

/// Wire slot for the nth keyring slot, counting from zero.
inline constexpr int keyringWireSlot(int index) {
    return kKeyringFirst + index;
}

/// The interface numbers inventory slots from one; the wire numbers them from
/// zero. Every place that takes a slot from Lua and sends it has to cross this,
/// and every place that got it wrong was off by exactly this.
inline constexpr int toInventorySlot(int wireSlot)      { return wireSlot + 1; }
inline constexpr int toWireSlot(int inventorySlot)      { return inventorySlot - 1; }

/// The inventory slot the first bank bag occupies, as Lua counts.
inline constexpr int kFirstBankBagInventorySlot = toInventorySlot(bankBagWireSlot(0));

} // namespace wowee::game::slots
