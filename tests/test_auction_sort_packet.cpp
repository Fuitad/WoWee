// The sort block on the end of CMSG_AUCTION_LIST_ITEMS.
//
// It used to be a hardcoded zero — no columns, no ordering — and the browse
// tab's column headers did nothing at all as a result. Not "sorted only the
// page on hand": nothing. The browse tab does not reorder what it already has,
// it re-asks with the ordering attached (AuctionFrame_OnClickSortColumn calls
// AuctionFrameBrowse_Search for "list" rather than SortAuctionApplySort), so
// an empty sort block is the whole answer.
//
// Two orderings have to agree for this to work and they run opposite ways:
//
//   * The interface pushes keys least significant first. GetAuctionSort(table,
//     1) returns the *primary*, and it reads keys.size() - index for exactly
//     that reason.
//   * AzerothCore's AuctionSorter walks its vector from begin() and returns on
//     the first column that separates two rows, so its front entry is primary.
//
// So the client's list is reversed on the way out. Reversed twice, or not at
// all, and a two-column sort silently orders by the tiebreaker — which looks
// almost right, which is what makes it worth pinning.
//
// Checked against WorldSession::HandleAuctionListItems, which reads guid,
// listfrom, name, levelmin, levelmax, auctionSlotID, auctionMainCategory,
// auctionSubCategory, quality, usable, getAll, then a uint8 count and that
// many (sortMode, isDesc) pairs.
//
// This covers the block, not its position in the packet. The builder's
// translation unit reaches the splines and the crypto through a shared facade,
// so linking it here would mean pulling in half the client to check six bytes
// — and building the packet by hand instead would test a copy of the code
// rather than the code. The block is the part with a convention to get wrong;
// where it sits is one line above it in the builder and covered by the
// layout sweeps.

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "game/world_packets.hpp"
#include "network/packet.hpp"

using wowee::game::AuctionSortKey;
using wowee::game::writeAuctionSortBlock;
using wowee::network::Packet;

namespace {

struct Tail {
    uint8_t count = 0xFF;
    std::vector<std::pair<uint8_t, uint8_t>> keys;
};

Tail buildAndRead(const std::vector<AuctionSortKey>& sort) {
    Packet p(0);
    // Where the block starts is whatever the packet already holds, so the
    // reader never has to guess at a header size.
    const size_t begin = p.getData().size();
    writeAuctionSortBlock(p, sort);
    const auto& body = p.getData();

    Tail t;
    REQUIRE(begin < body.size());
    t.count = body[begin];
    for (uint8_t i = 0; i < t.count; ++i) {
        const size_t o = begin + 1 + static_cast<size_t>(i) * 2;
        REQUIRE(o + 1 < body.size());
        t.keys.emplace_back(body[o], body[o + 1]);
    }
    // Nothing beyond the pairs the count promised, and nothing short of them.
    CHECK(begin + 1 + static_cast<size_t>(t.count) * 2 == body.size());
    return t;
}

}  // namespace

TEST_CASE("no sort writes a zero count and nothing after it", "[auction]") {
    const Tail t = buildAndRead({});
    CHECK(t.count == 0);
    CHECK(t.keys.empty());
}

TEST_CASE("each sort column becomes a mode and a direction", "[auction]") {
    // AUCTION_SORT_BID descending, then AUCTION_SORT_ITEM ascending.
    const Tail t = buildAndRead({{8, true}, {5, false}});
    REQUIRE(t.count == 2);
    CHECK(t.keys[0] == std::make_pair<uint8_t, uint8_t>(8, 1));
    CHECK(t.keys[1] == std::make_pair<uint8_t, uint8_t>(5, 0));
}

TEST_CASE("the order given is the order sent", "[auction]") {
    // The caller hands this over primary first, because that is the end the
    // server reads from. Nothing here re-orders it — the one reversal happens
    // where the interface's least-significant-first list is translated, and
    // doing it again here would undo it.
    const Tail t = buildAndRead({{1, false}, {0, true}, {9, false}});
    REQUIRE(t.count == 3);
    CHECK(t.keys[0].first == 1);
    CHECK(t.keys[1].first == 0);
    CHECK(t.keys[2].first == 9);
}

TEST_CASE("a direction is one byte per column, not one for the block", "[auction]") {
    // Every column carries its own flag: the interface's own tables mix them
    // within a single sort, and a block-wide direction would collapse them.
    const Tail t = buildAndRead({{0, true}, {1, false}, {3, true}, {7, false}});
    REQUIRE(t.count == 4);
    CHECK(t.keys[0].second == 1);
    CHECK(t.keys[1].second == 0);
    CHECK(t.keys[2].second == 1);
    CHECK(t.keys[3].second == 0);
}

TEST_CASE("too many columns are dropped rather than sent", "[auction]") {
    // AUCTION_SORT_MAX is 11 and the server abandons the whole request when the
    // count exceeds it — returning nothing at all, not an unsorted list. A
    // request that cannot be honoured is worse than one that is not sorted.
    std::vector<AuctionSortKey> many;
    for (int i = 0; i < 20; ++i) many.push_back({static_cast<uint8_t>(i % 10), false});
    const Tail t = buildAndRead(many);
    CHECK(t.count == 11);
    CHECK(t.keys.size() == 11u);
}

