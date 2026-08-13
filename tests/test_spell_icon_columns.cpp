// Which columns of SpellIcon.dbc hold the icon id and its path.
//
// Five places built the same iconId -> path table, each with its own copy of
// the two column numbers and the same fallback when no layout is loaded. A
// DBCFile asked for the wrong column does not fail: it answers zero, or an
// empty string, for every row forever. The icons simply do not appear, and
// nothing anywhere says why.
//
// The oracle is the shipped file. SpellIcon.dbc is unusually easy to check
// because it has exactly two columns and they hold different kinds of thing:
// one is a small integer, the other is a path under Interface. Neither can be
// mistaken for the other, so the values settle which column is which without
// any outside knowledge of the format.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "pipeline/spell_icon_paths.hpp"

namespace {

#ifdef WOWEE_SOURCE_DIR
const std::string kRoot = std::string(WOWEE_SOURCE_DIR) + "/";
#else
const std::string kRoot;
#endif

/// The shipped file, whatever case it is stored in. Data/db holds them
/// lower-cased, and a check that cannot open its subject passes vacuously.
std::filesystem::path dbcPathFor(const std::string& table) {
    std::string lower = table;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(kRoot + "Data/db", ec)) {
        if (entry.path().extension() != ".dbc") continue;
        std::string stem = entry.path().stem().string();
        for (char& c : stem) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        if (stem == lower) return entry.path();
    }
    return {};
}

struct Dbc {
    std::vector<char> bytes;
    uint32_t records = 0, fields = 0, recordSize = 0;
    size_t stringBase = 0;

    bool open(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        bytes.assign((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
        if (bytes.size() < 20 || std::memcmp(bytes.data(), "WDBC", 4) != 0) return false;
        std::memcpy(&records, bytes.data() + 4, 4);
        std::memcpy(&fields, bytes.data() + 8, 4);
        std::memcpy(&recordSize, bytes.data() + 12, 4);
        stringBase = 20 + static_cast<size_t>(records) * recordSize;
        return true;
    }

    uint32_t u32(uint32_t row, uint32_t field) const {
        const size_t at = 20 + static_cast<size_t>(row) * recordSize +
                          static_cast<size_t>(field) * 4;
        if (at + 4 > bytes.size()) return 0;
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    }

    std::string str(uint32_t row, uint32_t field) const {
        const uint32_t off = u32(row, field);
        if (off == 0 || stringBase + off >= bytes.size()) return {};
        const char* start = bytes.data() + stringBase + off;
        const size_t max = bytes.size() - (stringBase + off);
        return std::string(start, ::strnlen(start, max));
    }
};

bool looksLikeIconPath(const std::string& s) {
    if (s.size() < 10) return false;
    std::string head = s.substr(0, 9);
    for (char& c : head) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    return head == "interface";
}

}  // namespace

TEST_CASE("the path column holds icon paths", "[spell-icon]") {
    const auto path = dbcPathFor("SpellIcon");
    if (path.empty()) {
        WARN("SpellIcon.dbc is not here, skipping");
        return;
    }
    Dbc dbc;
    REQUIRE(dbc.open(path));
    REQUIRE(dbc.records > 100);

    uint32_t iconPaths = 0;
    for (uint32_t r = 0; r < dbc.records; ++r) {
        if (looksLikeIconPath(dbc.str(r, wowee::pipeline::kSpellIconPathColumn)))
            ++iconPaths;
    }
    INFO("rows whose path column is an Interface path: " << iconPaths << " of "
                                                         << dbc.records);
    // Not all of them: the table has a row or two with nothing in it, which is
    // what the empty-path guard in the loader is for.
    CHECK(iconPaths > dbc.records * 9 / 10);
}

TEST_CASE("the id column holds ids", "[spell-icon]") {
    const auto path = dbcPathFor("SpellIcon");
    if (path.empty()) {
        WARN("SpellIcon.dbc is not here, skipping");
        return;
    }
    Dbc dbc;
    REQUIRE(dbc.open(path));

    std::set<uint32_t> seen;
    uint32_t nonZero = 0;
    for (uint32_t r = 0; r < dbc.records; ++r) {
        const uint32_t id = dbc.u32(r, wowee::pipeline::kSpellIconIdColumn);
        if (id != 0) ++nonZero;
        seen.insert(id);
    }
    INFO("distinct ids: " << seen.size() << " over " << dbc.records << " rows");
    // An id column is unique per row; a data column repeats.
    CHECK(seen.size() == dbc.records);
    CHECK(nonZero == dbc.records);
}

TEST_CASE("the two columns are not interchangeable", "[spell-icon]") {
    // What makes this checkable at all. Reading the id column as a string
    // offset lands on whatever byte its value happens to point at, and reading
    // the path column as a number gives a file offset rather than an id. If
    // the two were swapped, both tests above would fail rather than quietly
    // passing on the wrong column.
    const auto path = dbcPathFor("SpellIcon");
    if (path.empty()) {
        WARN("SpellIcon.dbc is not here, skipping");
        return;
    }
    Dbc dbc;
    REQUIRE(dbc.open(path));

    uint32_t swappedLooksRight = 0;
    for (uint32_t r = 0; r < dbc.records; ++r) {
        if (looksLikeIconPath(dbc.str(r, wowee::pipeline::kSpellIconIdColumn)))
            ++swappedLooksRight;
    }
    INFO("rows that would still look like paths if the columns were swapped: "
         << swappedLooksRight);
    CHECK(swappedLooksRight < dbc.records / 10);
}

TEST_CASE("the fallback columns are the ones this file uses", "[spell-icon]") {
    // The loader uses the active DBC layout when there is one and these when
    // there is not, which is every path that runs before a layout is chosen.
    CHECK(wowee::pipeline::kSpellIconIdColumn == 0);
    CHECK(wowee::pipeline::kSpellIconPathColumn == 1);
}
