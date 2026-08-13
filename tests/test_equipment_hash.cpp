// The fingerprint a cache redraws on when the outfit changes.
//
// Two of these existed, one in the character screen and one in the unit
// portrait, each with its own FNV-1a over the same three fields. Neither was
// wrong, but they were two answers to one question: which parts of an equipped
// item change how it looks. Add a field that does, update one, and the other
// cache keeps showing the previous outfit - a portrait wearing what the
// character took off, with nothing anywhere to say so.
#include <catch_amalgamated.hpp>

#include <vector>

#include "game/equipment_hash.hpp"

using wowee::game::EquipmentItem;
using wowee::game::hashEquipmentAppearance;

namespace {

EquipmentItem piece(uint32_t model, uint8_t slot, uint32_t enchant) {
    EquipmentItem e{};
    e.displayModel = model;
    e.inventoryType = slot;
    e.enchantment = enchant;
    return e;
}

}  // namespace

TEST_CASE("the same outfit hashes the same", "[equip-hash]") {
    const std::vector<EquipmentItem> a{piece(1234, 1, 0), piece(5678, 5, 9)};
    const std::vector<EquipmentItem> b{piece(1234, 1, 0), piece(5678, 5, 9)};
    CHECK(hashEquipmentAppearance(a) == hashEquipmentAppearance(b));
}

TEST_CASE("each visible field changes the fingerprint", "[equip-hash]") {
    // The three that decide how an item looks. A field that did not move the
    // hash would leave the cache showing the old picture.
    const std::vector<EquipmentItem> base{piece(1234, 1, 0)};
    CHECK(hashEquipmentAppearance(base) !=
          hashEquipmentAppearance({piece(1235, 1, 0)}));   // model
    CHECK(hashEquipmentAppearance(base) !=
          hashEquipmentAppearance({piece(1234, 2, 0)}));   // slot
    CHECK(hashEquipmentAppearance(base) !=
          hashEquipmentAppearance({piece(1234, 1, 7)}));   // enchant visual
}

TEST_CASE("the same items in different slots are a different outfit",
          "[equip-hash]") {
    const std::vector<EquipmentItem> a{piece(11, 1, 0), piece(22, 2, 0)};
    const std::vector<EquipmentItem> b{piece(22, 2, 0), piece(11, 1, 0)};
    CHECK(hashEquipmentAppearance(a) != hashEquipmentAppearance(b));
}

TEST_CASE("taking a piece off changes the fingerprint", "[equip-hash]") {
    const std::vector<EquipmentItem> dressed{piece(11, 1, 0), piece(22, 2, 0)};
    const std::vector<EquipmentItem> fewer{piece(11, 1, 0)};
    CHECK(hashEquipmentAppearance(dressed) != hashEquipmentAppearance(fewer));
}

TEST_CASE("wearing nothing is stable", "[equip-hash]") {
    // Two naked characters look alike, and the empty case must not depend on
    // whatever the vector happened to hold before.
    CHECK(hashEquipmentAppearance({}) == hashEquipmentAppearance({}));
}

TEST_CASE("a high slot number is not truncated away", "[equip-hash]") {
    // inventoryType is one byte; mixing it as four would pad with zeros and
    // mixing it as one must still tell the slots apart.
    CHECK(hashEquipmentAppearance({piece(1, 200, 0)}) !=
          hashEquipmentAppearance({piece(1, 201, 0)}));
}
