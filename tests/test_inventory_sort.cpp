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
    // Only 24 main slots exist on Classic — item in slot 25 must not seed the sort,
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
