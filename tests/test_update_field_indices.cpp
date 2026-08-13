// The wire indices of the update fields, against an independent oracle.
//
// A field index is a slot number in the update block the server sends. Read
// the wrong slot and nothing fails: you get the value of whatever field sits
// there, which for a guid is usually zero, so the feature reads as "the server
// never sent it" rather than as a mistake.
//
// UNIT_FIELD_TARGET was 6 for WotLK, which is UNIT_FIELD_CHARM. Vanilla and
// TBC have it at 16 and those were right; WotLK inserts UNIT_FIELD_CRITTER
// among the earlier guid fields, which moves TARGET to 18. Unit frames, the
// target-of-target display and combat targeting all read it, and a charm guid
// is almost always zero, so they showed no target at all.
//
// The oracle is AzerothCore's UpdateFields.h, read once and recorded here.
// Comparing our whole table against it found that one field out of 75.
#include <catch_amalgamated.hpp>

#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace {

#ifdef WOWEE_SOURCE_DIR
const std::string kRoot = std::string(WOWEE_SOURCE_DIR) + "/";
#else
const std::string kRoot;
#endif

std::map<std::string, int> declaredFields(const std::string& expansion) {
    std::ifstream in(kRoot + "Data/expansions/" + expansion + "/update_fields.json");
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();
    std::map<std::string, int> out;
    const std::regex pair(R"RX("(\w+)"\s*:\s*(\d+))RX");
    for (std::sregex_iterator it(json.begin(), json.end(), pair), end; it != end; ++it) {
        out[(*it)[1].str()] = std::stoi((*it)[2].str());
    }
    return out;
}

}  // namespace

TEST_CASE("the WotLK field indices are what the server sends", "[update-fields]") {
    // Recorded from AzerothCore's UpdateFields.h. Every one of the 75 fields
    // in the table agreed once UNIT_FIELD_TARGET was corrected; these are the
    // ones whose being wrong would be felt.
    const std::map<std::string, int> oracle = {
        {"OBJECT_FIELD_ENTRY", 3},
        {"OBJECT_FIELD_SCALE_X", 4},
        {"UNIT_FIELD_TARGET_LO", 18},
        {"UNIT_FIELD_TARGET_HI", 19},
        {"UNIT_FIELD_HEALTH", 24},
        {"UNIT_FIELD_MAXHEALTH", 32},
        {"UNIT_FIELD_LEVEL", 54},
        {"UNIT_FIELD_FACTIONTEMPLATE", 55},
        {"UNIT_FIELD_FLAGS", 59},
        {"UNIT_FIELD_DISPLAYID", 67},
        {"UNIT_FIELD_MOUNTDISPLAYID", 69},
        {"UNIT_NPC_FLAGS", 82},
        {"PLAYER_EXPLORED_ZONES_START", 1041},
        {"PLAYER_QUEST_LOG_START", 158},
        {"PLAYER_SKILL_INFO_START", 636},
    };

    const auto ours = declaredFields("wotlk");
    REQUIRE(ours.size() > 50);
    for (const auto& [name, index] : oracle) {
        INFO(name);
        const auto found = ours.find(name);
        REQUIRE(found != ours.end());
        CHECK(found->second == index);
    }
}

TEST_CASE("the target field moved between vanilla and WotLK",
          "[update-fields]") {
    // The difference that was missed. WotLK inserts UNIT_FIELD_CRITTER among
    // the guid fields before TARGET, so a value correct for one is wrong for
    // the other - and both are plausible slot numbers.
    for (const char* expansion : {"classic", "tbc", "turtle"}) {
        const auto fields = declaredFields(expansion);
        INFO(expansion);
        REQUIRE(fields.count("UNIT_FIELD_TARGET_LO") == 1);
        CHECK(fields.at("UNIT_FIELD_TARGET_LO") == 16);
        CHECK(fields.at("UNIT_FIELD_TARGET_HI") == 17);
    }

    const auto wotlk = declaredFields("wotlk");
    CHECK(wotlk.at("UNIT_FIELD_TARGET_LO") == 18);
    CHECK(wotlk.at("UNIT_FIELD_TARGET_HI") == 19);
}

TEST_CASE("the two halves of a guid field are adjacent", "[update-fields]") {
    // A guid occupies two slots. Any pair that is not consecutive is reading
    // half of one field and half of another.
    for (const char* expansion : {"classic", "tbc", "wotlk", "turtle"}) {
        const auto fields = declaredFields(expansion);
        INFO(expansion);
        REQUIRE(fields.count("UNIT_FIELD_TARGET_LO") == 1);
        CHECK(fields.at("UNIT_FIELD_TARGET_HI") ==
              fields.at("UNIT_FIELD_TARGET_LO") + 1);
    }
}
