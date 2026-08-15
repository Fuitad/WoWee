// Parallax quality is one scale, and everywhere it is offered has to agree.
//
// It did not. The login screen offered two entries named "Medium" and "High"
// and wrote the chosen index into pom_quality, which is the same config key the
// in-game panel reads off a three entry scale. So the login screen's "Medium"
// asked for the 16 steps the panel calls Low, its "High" asked for the 32 the
// panel calls Medium, and the 64 the panel calls High could not be picked there
// at all - choosing High at login quietly downgraded a player who had High set
// in game.
//
// The step counts were written out four separate times besides. The header is
// the one scale now; these check that the two things it cannot reach from C++ -
// the schema row's choice labels and its prose - still say what it says.
#include <catch_amalgamated.hpp>

#include "test_support.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "rendering/pom_quality.hpp"

using namespace wowee::rendering;

namespace {

std::string readSource(const std::string& relative) {
    std::ifstream in(std::string(WOWEE_SOURCE_DIR) + "/" + relative);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// The parallaxquality row of the settings schema, as written.
std::string schemaRow() {
    const std::string src = readSource("src/ui/settings_schema.cpp");
    const size_t at = src.find("{\"parallaxquality\"");
    REQUIRE(at != std::string::npos);
    const size_t end = src.find("\"parallax\"}", at);
    REQUIRE(end != std::string::npos);
    return src.substr(at, end - at);
}

}  // namespace

TEST_CASE("an out of range quality index still lands on the scale", "[settings][pom]") {
    // What a hand-edited config file can hand us.
    CHECK(pomSamplesFor(-1) == kPomSampleCounts[0]);
    CHECK(pomSamplesFor(0) == 16);
    CHECK(pomSamplesFor(1) == 32);
    CHECK(pomSamplesFor(2) == 64);
    CHECK(pomSamplesFor(3) == kPomSampleCounts[kPomQualityCount - 1]);
    CHECK(pomSamplesFor(9999) == kPomSampleCounts[kPomQualityCount - 1]);
}

TEST_CASE("the scale climbs", "[settings][pom]") {
    // A quality level that asked for fewer steps than the one below it would
    // read as the dropdown being backwards.
    for (int i = 1; i < kPomQualityCount; ++i) {
        CHECK(kPomSampleCounts[i] > kPomSampleCounts[i - 1]);
    }
}

TEST_CASE("the settings schema offers exactly the levels the scale has", "[settings][pom]") {
    std::string expected;
    for (int i = 0; i < kPomQualityCount; ++i) {
        if (i) expected += "|";
        expected += kPomQualityLabels[i];
    }

    const std::string row = schemaRow();
    INFO("schema row: " << row);
    CHECK(row.find("\"" + expected + "\"") != std::string::npos);

    // The row's max is an index into the scale, not a count.
    const std::string bounds = "SettingKind::Enum, 0, " + std::to_string(kPomQualityCount - 1);
    CHECK(row.find(bounds) != std::string::npos);
}

TEST_CASE("the settings schema's description names the real step counts", "[settings][pom]") {
    const std::string row = schemaRow();
    for (int i = 0; i < kPomQualityCount; ++i) {
        INFO("step count " << kPomSampleCounts[i] << " missing from the description");
        CHECK(row.find(std::to_string(kPomSampleCounts[i])) != std::string::npos);
    }
}

TEST_CASE("nothing keeps its own copy of the step counts", "[settings][pom]") {
    // The four copies this header replaced. A fifth would drift the same way
    // the login screen's did.
    const std::regex table(R"(\{\s*16\s*,\s*32\s*,\s*64\s*\})");
    for (const std::string& file : {std::string("src/rendering/wmo_renderer.cpp"),
                                    std::string("src/rendering/character_renderer.cpp"),
                                    std::string("src/ui/auth_screen.cpp"),
                                    std::string("src/ui/settings_panel.cpp")}) {
        INFO(file << " spells the parallax step counts out again");
        CHECK_FALSE(std::regex_search(readSource(file), table));
    }
}
