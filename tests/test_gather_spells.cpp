// The gathering ranks, and the shape that made them worth having once.
//
// Mining and Herb Gathering have five ranks each, and every id was written out
// four times across two files - the two lists separately, the two joined, and
// mining again on its own. A list that gains a rank in one copy and not the
// others fails without a word: the player swings at the node with a rank the
// check does not know, so the client decides they cannot gather it and says
// nothing about why.
//
// The ids are the oracle here, in the sense that they are what all four copies
// already agreed on. What the tests are for is the next edit.
#include <catch_amalgamated.hpp>

#include <set>
#include <vector>

#include "game/gather_spells.hpp"

using namespace wowee::game;

TEST_CASE("the five mining ranks", "[gather]") {
    const std::vector<uint32_t> ranks(kMiningRanks, kMiningRanks + kMiningRankCount);
    CHECK(ranks == std::vector<uint32_t>{2575, 2576, 3564, 10248, 29354});
}

TEST_CASE("the five herbalism ranks", "[gather]") {
    const std::vector<uint32_t> ranks(kHerbRanks, kHerbRanks + kHerbRankCount);
    CHECK(ranks == std::vector<uint32_t>{2366, 2368, 3570, 11993, 28695});
}

TEST_CASE("every rank is recognised as its own profession", "[gather]") {
    for (uint32_t id : kMiningRanks) {
        INFO("mining rank " << id);
        CHECK(isMiningRank(id));
        CHECK_FALSE(isHerbRank(id));
        CHECK(isGatherRank(id));
    }
    for (uint32_t id : kHerbRanks) {
        INFO("herb rank " << id);
        CHECK(isHerbRank(id));
        CHECK_FALSE(isMiningRank(id));
        CHECK(isGatherRank(id));
    }
}

TEST_CASE("a spell that is not a gather is not one", "[gather]") {
    // 133 is Fireball, 2018 is Blacksmithing - a profession, but not a gather.
    for (uint32_t id : {uint32_t{0}, uint32_t{133}, uint32_t{2018}, uint32_t{29355}}) {
        INFO("spell " << id);
        CHECK_FALSE(isGatherRank(id));
        CHECK_FALSE(isMiningRank(id));
        CHECK_FALSE(isHerbRank(id));
    }
}

TEST_CASE("the base id is the first rank", "[gather]") {
    // The rest of the client names a gather by its base id, so this is the
    // link between "which profession" and "which ranks".
    CHECK(kMiningBaseSpellId == 2575);
    CHECK(kHerbBaseSpellId == 2366);
    CHECK(kMiningBaseSpellId == kMiningRanks[0]);
    CHECK(kHerbBaseSpellId == kHerbRanks[0]);
}

TEST_CASE("a base id selects its own profession's ranks", "[gather]") {
    size_t count = 0;
    const uint32_t* ranks = gatherRanksForBase(kMiningBaseSpellId, count);
    REQUIRE(ranks != nullptr);
    CHECK(count == kMiningRankCount);
    CHECK(ranks[0] == 2575);

    ranks = gatherRanksForBase(kHerbBaseSpellId, count);
    REQUIRE(ranks != nullptr);
    CHECK(count == kHerbRankCount);
    CHECK(ranks[0] == 2366);
}

TEST_CASE("anything else selects nothing", "[gather]") {
    // Including a higher rank: only the base names a profession, which is the
    // convention the callers already use.
    size_t count = 99;
    CHECK(gatherRanksForBase(0, count) == nullptr);
    CHECK(count == 0);
    count = 99;
    CHECK(gatherRanksForBase(2576, count) == nullptr);
    CHECK(count == 0);
}

TEST_CASE("the ranks ascend and none repeats", "[gather]") {
    // Ascending is what lets a caller walk backwards for the best rank known.
    // A repeat would make one tier unreachable and shorten the list silently.
    for (const auto* list : {&kMiningRanks, &kHerbRanks}) {
        const uint32_t* ranks = *list;
        const size_t count = (list == &kMiningRanks) ? kMiningRankCount : kHerbRankCount;
        std::set<uint32_t> seen;
        for (size_t i = 0; i < count; ++i) {
            INFO("index " << i << " id " << ranks[i]);
            CHECK(ranks[i] != 0);
            CHECK(seen.insert(ranks[i]).second);
            if (i > 0) CHECK(ranks[i] > ranks[i - 1]);
        }
    }
}

TEST_CASE("the two professions share no rank", "[gather]") {
    for (uint32_t m : kMiningRanks) {
        for (uint32_t h : kHerbRanks) {
            CHECK(m != h);
        }
    }
}
