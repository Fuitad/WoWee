// Where the strings begin in SMSG_QUEST_QUERY_RESPONSE.
//
// The body is a fixed block of four-byte fields and then five strings - Title,
// Objectives, Details, AreaDescription, CompletedText. Reading from the wrong
// offset does not fail: it returns a different string, and every string after
// it shifts by one. A quest then shows its objectives where its name belongs.
//
// The block is built here exactly as AzerothCore's Quest::BuildQuestData writes
// it, field for field, so the count in quest_query_layout.hpp is checked against
// the server rather than remembered.
#include <catch_amalgamated.hpp>

#include "game/quest_query_layout.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace wowee;

namespace {

// The three array sizes from the server's QuestDef.h.
constexpr int kRewards = 4;
constexpr int kRewardChoices = 6;
constexpr int kReputations = 5;

struct Body {
    std::vector<uint8_t> bytes;
    void u32(uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16));
        bytes.push_back(static_cast<uint8_t>(v >> 24));
    }
    void str(const std::string& s) {
        bytes.insert(bytes.end(), s.begin(), s.end());
        bytes.push_back(0);
    }
};

/// The numeric block, in the server's order.
Body buildWotlkQuestQueryBody() {
    Body b;
    b.u32(1712);      // questId
    b.u32(2);         // method
    b.u32(50);        // level
    b.u32(45);        // minLevel
    b.u32(38);        // zoneOrSort

    b.u32(0);         // type
    b.u32(0);         // suggestedPlayers

    b.u32(0); b.u32(0);   // reputation objective 1: faction, value
    b.u32(0); b.u32(0);   // reputation objective 2: faction, value

    b.u32(0);         // nextQuestInChain
    b.u32(0);         // xpId

    b.u32(1000);      // money - one field either way

    b.u32(0);         // moneyMaxLevel
    b.u32(0);         // rewSpell
    b.u32(0);         // rewSpellCast

    b.u32(0);         // honorAddition
    b.u32(0);         // honorMultiplier (a float, still four bytes)

    b.u32(0);         // srcItemId
    b.u32(0);         // flags
    b.u32(0);         // charTitleId
    b.u32(0);         // playersSlain
    b.u32(0);         // bonusTalents
    b.u32(0);         // rewArenaPoints
    b.u32(0);         // reviewRepShowMask

    for (int i = 0; i < kRewards; ++i) { b.u32(0); b.u32(0); }
    for (int i = 0; i < kRewardChoices; ++i) { b.u32(0); b.u32(0); }

    for (int i = 0; i < kReputations; ++i) b.u32(0);   // faction ids
    for (int i = 0; i < kReputations; ++i) b.u32(0);   // faction values
    for (int i = 0; i < kReputations; ++i) b.u32(0);   // value overrides

    b.u32(0);         // POI continent
    b.u32(0);         // POI x
    b.u32(0);         // POI y
    b.u32(0);         // pointOpt
    return b;
}

std::string cStringAt(const std::vector<uint8_t>& data, size_t at) {
    std::string out;
    for (size_t i = at; i < data.size() && data[i] != 0; ++i) {
        out.push_back(static_cast<char>(data[i]));
    }
    return out;
}

}  // namespace

TEST_CASE("the numeric block is sixty-five fields on WotLK", "[quest][layout]") {
    const Body b = buildWotlkQuestQueryBody();
    CHECK(b.bytes.size() == game::kWotlkQuestQueryFields * 4);
    CHECK(game::kWotlkQuestQueryStringsOffset == 260);
}

TEST_CASE("the title is the first string after it", "[quest][layout]") {
    Body b = buildWotlkQuestQueryBody();
    b.str("Bath'rah the Windwatcher");
    b.str("Bring Bath'rah's Parchment to Bath'rah the Windwatcher.");
    b.str("The parchment is old and the ink has faded.");
    b.str("");                       // AreaDescription, usually empty
    b.str("Return to Bath'rah.");

    size_t at = game::kWotlkQuestQueryStringsOffset;
    const std::string title = cStringAt(b.bytes, at);
    CHECK(title == "Bath'rah the Windwatcher");

    SECTION("and the rest follow it in order") {
        at += title.size() + 1;
        const std::string objectives = cStringAt(b.bytes, at);
        CHECK(objectives == "Bring Bath'rah's Parchment to Bath'rah the Windwatcher.");
        at += objectives.size() + 1;
        const std::string details = cStringAt(b.bytes, at);
        CHECK(details == "The parchment is old and the ink has faded.");
        at += details.size() + 1;
        const std::string areaDescription = cStringAt(b.bytes, at);
        CHECK(areaDescription.empty());
        at += areaDescription.size() + 1;
        CHECK(cStringAt(b.bytes, at) == "Return to Bath'rah.");
    }

    SECTION("the offset this used to seed lands inside the reward block") {
        // Fifty-seven fields, which is thirty-two bytes short. It is not a
        // string there - it is zeroes - so the read came back empty and the
        // quest fell through to the scan that guesses a title by its shape.
        const size_t wrong = 8 + 55 * 4;
        CHECK(wrong == 228);
        CHECK(wrong < game::kWotlkQuestQueryStringsOffset);
        CHECK(cStringAt(b.bytes, wrong).empty());
    }
}
