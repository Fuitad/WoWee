// Reading a number out of one of the expansion table files.
//
// opcodes.json and update_fields.json are both flat "name": number objects,
// and each had its own scanner for them. The scanners were not the same: the
// opcode one understood hex and quoted values, the update-field one did not.
//
// The difference matters because of how it fails. std::stoul("0x12") in base
// ten does not throw. It reads the leading zero, stops at the 'x', and
// answers 0. A table that loads and is wrong is the worst of the three
// outcomes: an opcode 0 is a real opcode, and update field 0 is the object
// GUID, so the client goes on reading a value that is never the one meant.
//
// The oracle is the shipped data: opcodes are written in hex (1306 of them in
// the WotLK file), update field indices in decimal, and neither file has a
// leading-zero value that an octal-guessing parser would misread.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "game/json_table_scan.hpp"

using wowee::game::forEachJsonKeyValue;
using wowee::game::parseTableNumber;

namespace {

std::vector<std::pair<std::string, std::string>> pairsOf(const std::string& json) {
    std::vector<std::pair<std::string, std::string>> out;
    forEachJsonKeyValue(json, [&](const std::string& k, const std::string& v) {
        out.emplace_back(k, v);
    });
    return out;
}

}  // namespace

TEST_CASE("decimal and hex both parse", "[json-table]") {
    uint32_t v = 0xDEAD;
    CHECK(parseTableNumber("18", v));
    CHECK(v == 18);
    CHECK(parseTableNumber("0x12", v));
    CHECK(v == 18);
    CHECK(parseTableNumber("0X12", v));
    CHECK(v == 18);
    CHECK(parseTableNumber("0", v));
    CHECK(v == 0);
    CHECK(parseTableNumber("  42  ", v));
    CHECK(v == 42);
}

TEST_CASE("hex is not read as zero", "[json-table]") {
    // The whole reason this is one function. Every opcode in the WotLK file is
    // written this way, and a decimal-only parser answers 0 for all of them
    // without raising.
    uint32_t v = 0;
    REQUIRE(parseTableNumber("0x1A2", v));
    CHECK(v == 0x1A2);
    CHECK(v != 0);
}

TEST_CASE("a value that is not entirely a number is refused", "[json-table]") {
    // Refusing is what lets the caller skip the row and say so. Parsing the
    // leading digits and ignoring the rest is how a wrong table loads.
    uint32_t v = 0xDEAD;
    for (const char* bad : {"", "   ", "abc", "18abc", "0x", "0xZZ", "1.5", "-1",
                            "+", "0x1A2G"}) {
        INFO("input: '" << bad << "'");
        CHECK_FALSE(parseTableNumber(bad, v));
    }
    CHECK(v == 0xDEAD);  // left alone on failure
}

TEST_CASE("a value too large for the table is refused", "[json-table]") {
    uint32_t v = 0;
    CHECK_FALSE(parseTableNumber("99999999999999999999", v));
    CHECK(parseTableNumber("4294967295", v));
    CHECK(v == 4294967295u);
}

TEST_CASE("the scanner reads a flat object", "[json-table]") {
    const std::string json = R"({
        "CMSG_ONE": 1,
        "CMSG_TWO": 0x02,
        "CMSG_THREE": 3
    })";
    const auto pairs = pairsOf(json);
    REQUIRE(pairs.size() == 3);
    CHECK(pairs[0].first == "CMSG_ONE");
    CHECK(pairs[1].second == "0x02");
    CHECK(pairs[2].first == "CMSG_THREE");
}

TEST_CASE("a quoted value is read without its quotes", "[json-table]") {
    // The opcode files carry these; the update field files do not. One scanner
    // has to handle both, and reading the quote as part of the number would
    // fail every row.
    const auto pairs = pairsOf(R"({"A": "0x10", "B": "32"})");
    REQUIRE(pairs.size() == 2);
    CHECK(pairs[0].second == "0x10");
    CHECK(pairs[1].second == "32");
}

TEST_CASE("trailing whitespace before a comma is not part of the value",
          "[json-table]") {
    const auto pairs = pairsOf("{\"A\": 7 ,\n \"B\": 8\t}");
    REQUIRE(pairs.size() == 2);
    uint32_t v = 0;
    REQUIRE(parseTableNumber(pairs[0].second, v));
    CHECK(v == 7);
    REQUIRE(parseTableNumber(pairs[1].second, v));
    CHECK(v == 8);
}

TEST_CASE("a truncated file stops rather than running off the end",
          "[json-table]") {
    CHECK(pairsOf("").empty());
    CHECK(pairsOf("{").empty());
    CHECK(pairsOf("{\"A\"").empty());
    CHECK(pairsOf("{\"A\":").size() <= 1);
    CHECK(pairsOf("{\"A\": 1").size() == 1);
}
