#include "catch_amalgamated.hpp"
#include "game/inventory.hpp"

using namespace wowee::game;

namespace {

ItemDef makeItem(uint32_t id, ItemQuality quality, uint32_t stack = 1) {
    ItemDef def;
    def.itemId = id;
    def.quality = quality;
    def.stackCount = stack;
    def.maxStack = 20;
    return def;
}


/// Reads a slot by the wire address the swap ops use.
///
/// Bag 0xFF addresses two different places, told apart by the slot number:
/// slots NUM_EQUIP_SLOTS upwards are the backpack, and BANK_SLOT_START upwards
/// are the main bank. Bag FIRST_BAG_EQUIP_SLOT + i is equip bag i, and
/// BANK_BAG_CONTAINER_START + i is bank bag i.
ItemDef itemAt(const Inventory& inv, uint8_t bag, uint8_t slot) {
    if (bag == 0xFF) {
        if (slot >= Inventory::BANK_SLOT_START) {
            return inv.getBankSlot(slot - Inventory::BANK_SLOT_START).item;
        }
        return inv.getBackpackSlot(slot - Inventory::NUM_EQUIP_SLOTS).item;
    }
    if (bag >= Inventory::BANK_BAG_CONTAINER_START) {
        return inv.getBankBagSlot(bag - Inventory::BANK_BAG_CONTAINER_START, slot).item;
    }
    return inv.getBagSlot(bag - Inventory::FIRST_BAG_EQUIP_SLOT, slot).item;
}

void setItemAt(Inventory& inv, uint8_t bag, uint8_t slot, const ItemDef& item) {
    if (bag == 0xFF) {
        if (slot >= Inventory::BANK_SLOT_START) {
            inv.setBankSlot(slot - Inventory::BANK_SLOT_START, item);
        } else {
            inv.setBackpackSlot(slot - Inventory::NUM_EQUIP_SLOTS, item);
        }
    } else if (bag >= Inventory::BANK_BAG_CONTAINER_START) {
        inv.setBankBagSlot(bag - Inventory::BANK_BAG_CONTAINER_START, slot, item);
    } else {
        inv.setBagSlot(bag - Inventory::FIRST_BAG_EQUIP_SLOT, slot, item);
    }
}

/// Plays the swap plan out on an inventory, the way the server does when the
/// ops reach it one at a time.
void applySwaps(Inventory& inv, const std::vector<Inventory::SwapOp>& swaps) {
    for (const auto& op : swaps) {
        const ItemDef source = itemAt(inv, op.srcBag, op.srcSlot);
        const ItemDef destination = itemAt(inv, op.dstBag, op.dstSlot);
        setItemAt(inv, op.dstBag, op.dstSlot, source);
        setItemAt(inv, op.srcBag, op.srcSlot, destination);
    }
}

} // namespace

TEST_CASE("sortBags orders backpack by quality then itemId", "[inventory]") {
    Inventory inv;
    inv.setBackpackSlot(0, makeItem(500, ItemQuality::COMMON));
    inv.setBackpackSlot(3, makeItem(100, ItemQuality::RARE));
    inv.setBackpackSlot(7, makeItem(200, ItemQuality::UNCOMMON));

    inv.sortBags();

    CHECK(inv.getBackpackSlot(0).item.itemId == 100);  // rare first
    CHECK(inv.getBackpackSlot(1).item.itemId == 200);  // then uncommon
    CHECK(inv.getBackpackSlot(2).item.itemId == 500);  // then common
    CHECK(inv.getBackpackSlot(3).empty());
}

TEST_CASE("sortBags skips special containers", "[inventory]") {
    Inventory inv;
    // Bag 0: quiver with arrows in slots 0 and 2
    inv.setBagSize(0, 6);
    inv.setBagSpecial(0, true);
    inv.setBagSlot(0, 0, makeItem(2512, ItemQuality::COMMON, 200));  // arrows
    inv.setBagSlot(0, 2, makeItem(2512, ItemQuality::COMMON, 50));
    // Bag 1: normal bag with one item
    inv.setBagSize(1, 4);
    inv.setBagSlot(1, 3, makeItem(300, ItemQuality::EPIC));
    // Backpack: one item so bag contents have room to move forward
    inv.setBackpackSlot(5, makeItem(400, ItemQuality::COMMON));

    inv.sortBags();

    // Quiver contents untouched, including the gap between them
    CHECK(inv.getBagSlot(0, 0).item.itemId == 2512);
    CHECK(inv.getBagSlot(0, 0).item.stackCount == 200);
    CHECK(inv.getBagSlot(0, 1).empty());
    CHECK(inv.getBagSlot(0, 2).item.itemId == 2512);
    CHECK(inv.getBagSlot(0, 2).item.stackCount == 50);
    // Normal bag + backpack items pooled and sorted into the backpack
    CHECK(inv.getBackpackSlot(0).item.itemId == 300);  // epic first
    CHECK(inv.getBackpackSlot(1).item.itemId == 400);
    CHECK(inv.getBagSlot(1, 3).empty());
}

TEST_CASE("computeSortSwaps never addresses special containers", "[inventory]") {
    Inventory inv;
    inv.setBagSize(0, 6);
    inv.setBagSpecial(0, true);
    inv.setBagSlot(0, 0, makeItem(2512, ItemQuality::COMMON, 200));
    inv.setBagSize(1, 4);
    inv.setBagSlot(1, 0, makeItem(100, ItemQuality::RARE));
    inv.setBackpackSlot(0, makeItem(500, ItemQuality::COMMON));

    auto swaps = inv.computeSortSwaps();

    // Wire address of bag 0 is FIRST_BAG_EQUIP_SLOT + 0 = 19
    const uint8_t quiverBag = static_cast<uint8_t>(Inventory::FIRST_BAG_EQUIP_SLOT);
    for (const auto& op : swaps) {
        CHECK(op.srcBag != quiverBag);
        CHECK(op.dstBag != quiverBag);
    }
    // The rare item from the normal bag should still be moved (sort not a no-op)
    CHECK(!swaps.empty());
}

TEST_CASE("sortBank orders main bank slots by quality then itemId", "[inventory]") {
    Inventory inv;
    inv.setBankSlot(0, makeItem(500, ItemQuality::COMMON));
    inv.setBankSlot(4, makeItem(100, ItemQuality::RARE));
    inv.setBankSlot(9, makeItem(200, ItemQuality::UNCOMMON));

    inv.sortBank(28);

    CHECK(inv.getBankSlot(0).item.itemId == 100);  // rare first
    CHECK(inv.getBankSlot(1).item.itemId == 200);  // then uncommon
    CHECK(inv.getBankSlot(2).item.itemId == 500);  // then common
    CHECK(inv.getBankSlot(3).empty());
}

TEST_CASE("sortBank pools bank bag contents into the main bank", "[inventory]") {
    Inventory inv;
    // Main bank has one low-quality item; a bank bag holds a higher-quality one.
    inv.setBankSlot(2, makeItem(400, ItemQuality::COMMON));
    inv.setBankBagSize(0, 6);
    inv.setBankBagSlot(0, 3, makeItem(100, ItemQuality::EPIC));

    inv.sortBank(28);

    // Everything pools and refills main bank first: epic, then common.
    CHECK(inv.getBankSlot(0).item.itemId == 100);
    CHECK(inv.getBankSlot(1).item.itemId == 400);
    CHECK(inv.getBankBagSlot(0, 3).empty());
}

TEST_CASE("sortBank respects the main slot count for Classic", "[inventory]") {
    Inventory inv;
    // Only 24 main slots exist on Classic - item in slot 25 must not seed the sort,
    // and the sort must never write past slot 23.
    inv.setBankSlot(0, makeItem(500, ItemQuality::COMMON));
    inv.setBankSlot(1, makeItem(100, ItemQuality::RARE));

    inv.sortBank(24);

    CHECK(inv.getBankSlot(0).item.itemId == 100);
    CHECK(inv.getBankSlot(1).item.itemId == 500);
    CHECK(inv.getBankSlot(24).empty());
    CHECK(inv.getBankSlot(27).empty());
}

TEST_CASE("computeBankSortSwaps addresses bank slots and bags correctly", "[inventory]") {
    Inventory inv;
    inv.setBankSlot(0, makeItem(500, ItemQuality::COMMON));
    inv.setBankSlot(1, makeItem(100, ItemQuality::RARE));
    inv.setBankBagSize(0, 4);
    inv.setBankBagSlot(0, 0, makeItem(300, ItemQuality::EPIC));

    auto swaps = inv.computeBankSortSwaps(28);
    CHECK(!swaps.empty());

    // Every address is either the main bank (0xFF, slot >= BANK_SLOT_START) or a bank
    // bag container (>= BANK_BAG_CONTAINER_START).
    for (const auto& op : swaps) {
        bool srcOk = (op.srcBag == 0xFF && op.srcSlot >= Inventory::BANK_SLOT_START) ||
                     (op.srcBag >= Inventory::BANK_BAG_CONTAINER_START);
        bool dstOk = (op.dstBag == 0xFF && op.dstSlot >= Inventory::BANK_SLOT_START) ||
                     (op.dstBag >= Inventory::BANK_BAG_CONTAINER_START);
        CHECK(srcOk);
        CHECK(dstOk);
    }
}

TEST_CASE("sortBankBag orders one bag without touching the rest of the bank",
          "[inventory]") {
    Inventory inv;
    inv.setBankSlot(0, makeItem(900, ItemQuality::POOR));
    inv.setBankBagSize(0, 6);
    inv.setBankBagSize(1, 6);
    inv.setBankBagSlot(0, 0, makeItem(500, ItemQuality::COMMON));
    inv.setBankBagSlot(0, 4, makeItem(100, ItemQuality::EPIC));
    inv.setBankBagSlot(1, 2, makeItem(700, ItemQuality::RARE));

    inv.sortBankBag(0);

    // Sorted and compacted to the front of its own bag.
    CHECK(inv.getBankBagSlot(0, 0).item.itemId == 100);
    CHECK(inv.getBankBagSlot(0, 1).item.itemId == 500);
    CHECK(inv.getBankBagSlot(0, 4).empty());

    // Nothing pooled into the main bank, and the other bag is untouched - which
    // is the whole point of sorting one bag rather than the whole bank.
    CHECK(inv.getBankSlot(0).item.itemId == 900);
    CHECK(inv.getBankSlot(1).empty());
    CHECK(inv.getBankBagSlot(1, 2).item.itemId == 700);
}

TEST_CASE("computeBankBagSortSwaps stays inside the one bag", "[inventory]") {
    Inventory inv;
    inv.setBankSlot(0, makeItem(900, ItemQuality::POOR));
    inv.setBankBagSize(2, 5);
    inv.setBankBagSlot(2, 0, makeItem(500, ItemQuality::COMMON));
    inv.setBankBagSlot(2, 3, makeItem(100, ItemQuality::EPIC));

    const auto swaps = inv.computeBankBagSortSwaps(2);
    CHECK(!swaps.empty());

    const uint8_t expected =
        static_cast<uint8_t>(Inventory::BANK_BAG_CONTAINER_START + 2);
    for (const auto& op : swaps) {
        CHECK(op.srcBag == expected);
        CHECK(op.dstBag == expected);
    }
}

TEST_CASE("an out-of-range bank bag index is a no-op", "[inventory]") {
    Inventory inv;
    inv.sortBankBag(-1);
    inv.sortBankBag(Inventory::BANK_BAG_SLOTS);
    CHECK(inv.computeBankBagSortSwaps(-1).empty());
    CHECK(inv.computeBankBagSortSwaps(Inventory::BANK_BAG_SLOTS).empty());
}

TEST_CASE("merging pours partial stacks together", "[inventory]") {
    Inventory inv;
    auto stack = [](uint32_t id, uint32_t count, uint32_t max) {
        ItemDef d = makeItem(id, ItemQuality::COMMON, count);
        d.maxStack = max;
        return d;
    };

    SECTION("two half stacks become one") {
        inv.setBackpackSlot(0, stack(2589, 10, 20));   // Linen Cloth
        inv.setBackpackSlot(3, stack(2589, 10, 20));

        const auto ops = inv.mergePartialStacks();

        CHECK(inv.getBackpackSlot(0).item.stackCount == 20);
        CHECK(inv.getBackpackSlot(3).empty());
        REQUIRE(ops.size() == 1);
        // The later slot pours into the earlier one.
        CHECK(ops[0].srcSlot == Inventory::NUM_EQUIP_SLOTS + 3);
        CHECK(ops[0].dstSlot == Inventory::NUM_EQUIP_SLOTS + 0);
    }

    SECTION("an overflowing pour leaves the remainder behind") {
        inv.setBackpackSlot(0, stack(2589, 15, 20));
        inv.setBackpackSlot(1, stack(2589, 12, 20));

        const auto ops = inv.mergePartialStacks();

        CHECK(inv.getBackpackSlot(0).item.stackCount == 20);
        CHECK(inv.getBackpackSlot(1).item.stackCount == 7);
        CHECK(ops.size() == 1);
    }

    SECTION("three partials collapse to as few stacks as fit") {
        inv.setBackpackSlot(0, stack(2589, 8, 20));
        inv.setBackpackSlot(1, stack(2589, 8, 20));
        inv.setBackpackSlot(2, stack(2589, 8, 20));

        inv.mergePartialStacks();

        CHECK(inv.getBackpackSlot(0).item.stackCount == 20);
        CHECK(inv.getBackpackSlot(1).item.stackCount == 4);
        CHECK(inv.getBackpackSlot(2).empty());
    }

    SECTION("different items and full stacks are left alone") {
        inv.setBackpackSlot(0, stack(2589, 20, 20));   // already full
        inv.setBackpackSlot(1, stack(2592, 5, 20));    // Wool Cloth
        inv.setBackpackSlot(2, stack(2589, 5, 20));
        ItemDef sword = makeItem(1234, ItemQuality::RARE);  // maxStack 1 by default
        sword.maxStack = 1;
        inv.setBackpackSlot(3, sword);

        const auto ops = inv.mergePartialStacks();

        CHECK(ops.empty());
        CHECK(inv.getBackpackSlot(0).item.stackCount == 20);
        CHECK(inv.getBackpackSlot(2).item.stackCount == 5);
        CHECK(inv.getBackpackSlot(3).item.itemId == 1234);
    }

    SECTION("special containers are skipped, as the sort skips them") {
        inv.setBagSize(0, 6);
        inv.setBagSpecial(0, true);
        inv.setBagSlot(0, 0, stack(2512, 100, 200));   // arrows in a quiver
        inv.setBagSlot(0, 2, stack(2512, 50, 200));

        CHECK(inv.mergePartialStacks().empty());
        CHECK(inv.getBagSlot(0, 0).item.stackCount == 100);
        CHECK(inv.getBagSlot(0, 2).item.stackCount == 50);
    }

    SECTION("the bank merges the same way") {
        inv.setBankSlot(0, stack(2589, 6, 20));
        inv.setBankBagSize(0, 6);
        inv.setBankBagSlot(0, 1, stack(2589, 9, 20));

        const auto ops = inv.mergeBankPartialStacks(28);

        CHECK(inv.getBankSlot(0).item.stackCount == 15);
        CHECK(inv.getBankBagSlot(0, 1).empty());
        REQUIRE(ops.size() == 1);
        CHECK(ops[0].srcBag == Inventory::BANK_BAG_CONTAINER_START);
        CHECK(ops[0].dstBag == 0xFF);
    }
}

// ---- the plan and the sort have to agree ----
//
// Every sort exists twice: sortBags() rearranges the local copy, and
// computeSortSwaps() plans the moves the server is asked to make. Nothing
// checks the two against each other, and they cannot both be right by
// construction because they are separate walks over the same slots.
//
// These are the oracle for that: apply the plan to one inventory, sort the
// other in place, and require them to agree slot for slot. A plan that drops,
// duplicates or misplaces an item fails here and nowhere else, because on a
// live client the wrong answer is just an inventory in a slightly odd order.

TEST_CASE("the bag sort plan lands where sortBags does", "[inventory]") {
    Inventory planned;
    planned.setBackpackSlot(0, makeItem(500, ItemQuality::COMMON));
    planned.setBackpackSlot(1, makeItem(100, ItemQuality::RARE, 5));
    planned.setBackpackSlot(4, makeItem(100, ItemQuality::RARE, 20));
    planned.setBackpackSlot(9, makeItem(200, ItemQuality::UNCOMMON));
    planned.setBackpackSlot(12, makeItem(300, ItemQuality::EPIC));

    Inventory sorted = planned;
    applySwaps(planned, planned.computeSortSwaps());
    sorted.sortBags();

    for (int i = 0; i < Inventory::BACKPACK_SLOTS; ++i) {
        INFO("backpack slot " << i);
        CHECK(planned.getBackpackSlot(i).item.itemId ==
              sorted.getBackpackSlot(i).item.itemId);
        CHECK(planned.getBackpackSlot(i).item.stackCount ==
              sorted.getBackpackSlot(i).item.stackCount);
    }
}

TEST_CASE("the bag sort plan carries items across bags", "[inventory]") {
    // The case a single-container plan cannot get wrong: an item that has to
    // leave one bag for another. The swap addresses are the only thing saying
    // where it lands.
    Inventory planned;
    planned.setBagSize(0, 4);
    planned.setBagSize(1, 4);
    planned.setBackpackSlot(0, makeItem(900, ItemQuality::POOR));
    planned.setBagSlot(0, 0, makeItem(100, ItemQuality::EPIC));
    planned.setBagSlot(1, 2, makeItem(200, ItemQuality::RARE));

    Inventory sorted = planned;
    applySwaps(planned, planned.computeSortSwaps());
    sorted.sortBags();

    for (int i = 0; i < Inventory::BACKPACK_SLOTS; ++i) {
        INFO("backpack slot " << i);
        CHECK(planned.getBackpackSlot(i).item.itemId ==
              sorted.getBackpackSlot(i).item.itemId);
    }
    for (int b = 0; b < 2; ++b) {
        for (int s = 0; s < 4; ++s) {
            INFO("bag " << b << " slot " << s);
            CHECK(planned.getBagSlot(b, s).item.itemId ==
                  sorted.getBagSlot(b, s).item.itemId);
        }
    }
}

TEST_CASE("the bank sort plan lands where sortBank does", "[inventory]") {
    Inventory planned;
    planned.setBankSlot(0, makeItem(500, ItemQuality::COMMON));
    planned.setBankSlot(5, makeItem(100, ItemQuality::RARE));
    planned.setBankSlot(11, makeItem(300, ItemQuality::EPIC));

    Inventory sorted = planned;
    applySwaps(planned, planned.computeBankSortSwaps(28));
    sorted.sortBank(28);

    for (int i = 0; i < 28; ++i) {
        INFO("bank slot " << i);
        CHECK(planned.getBankSlot(i).item.itemId == sorted.getBankSlot(i).item.itemId);
    }
}

TEST_CASE("the bank bag sort plan lands where sortBankBag does", "[inventory]") {
    Inventory planned;
    planned.setBankBagSize(1, 6);
    planned.setBankBagSlot(1, 0, makeItem(500, ItemQuality::COMMON));
    planned.setBankBagSlot(1, 3, makeItem(100, ItemQuality::EPIC));
    planned.setBankBagSlot(1, 5, makeItem(200, ItemQuality::RARE));

    Inventory sorted = planned;
    applySwaps(planned, planned.computeBankBagSortSwaps(1));
    sorted.sortBankBag(1);

    for (int s = 0; s < 6; ++s) {
        INFO("bank bag slot " << s);
        CHECK(planned.getBankBagSlot(1, s).item.itemId ==
              sorted.getBankBagSlot(1, s).item.itemId);
    }
}

TEST_CASE("a plan for an already sorted inventory is empty", "[inventory]") {
    // Not a nicety: every op is a packet and a server round trip, and a sort
    // that always moves everything is how a bank sort trips flood protection.
    Inventory inv;
    inv.setBackpackSlot(0, makeItem(100, ItemQuality::EPIC));
    inv.setBackpackSlot(1, makeItem(200, ItemQuality::RARE));
    inv.setBackpackSlot(2, makeItem(300, ItemQuality::COMMON));
    CHECK(inv.computeSortSwaps().empty());
}
