// Every column a layout names has to exist in the file it describes.
//
// The layouts in Data/expansions/*/dbc_layouts.json say which field of a DBC
// holds what. A DBCFile asked for a field past the end of a record does not
// fail: it answers zero, for every row, forever. A layout that names column 19
// of an 18-column file is a value that is always zero and a feature that never
// works, with nothing anywhere to say so.
//
// The oracle is the DBC files themselves. A WDBC header states its field count
// in bytes 8..11, so the range check is exact and needs no knowledge of any
// particular table.
//
// A range check alone would not have caught the bug that prompted it.
// LightIntBand had its columns declared one to the right, and 19 is as valid
// an index as 18 in a 34-field file - the count was read from the first time
// key, 12024 of 15300 bands came back with zero keyframes, and every zone fell
// back to a flat grey. Catching that needs the values, not the indices, which
// is what the second test does: a keyframe count is at most sixteen and a time
// of day is under 2880, and neither can be mistaken for the other.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

#ifdef WOWEE_SOURCE_DIR
const std::string kRoot = std::string(WOWEE_SOURCE_DIR) + "/";
#else
const std::string kRoot;
#endif

std::string slurp(const std::string& relative) {
    std::ifstream in(kRoot + relative);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// The shipped file for a table, whatever case it is stored in.
///
/// Data/db holds them lower-cased. Opening "LightIntBand.dbc" by name finds
/// nothing on a case-sensitive filesystem, and a check that cannot open its
/// subject passes without testing anything.
inline std::filesystem::path dbcPathFor(const std::string& table) {
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

/// Field count from a WDBC header, or 0 if the file is not one.
uint32_t dbcFieldCount(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    char head[12] = {};
    in.read(head, sizeof(head));
    if (in.gcount() != sizeof(head)) return 0;
    if (std::memcmp(head, "WDBC", 4) != 0) return 0;
    uint32_t fields = 0;
    std::memcpy(&fields, head + 8, sizeof(fields));
    return fields;
}

/// Every .dbc under Data/db, keyed by its lower-case stem.
std::map<std::string, uint32_t> shippedDbcFieldCounts() {
    std::map<std::string, uint32_t> out;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(kRoot + "Data/db", ec)) {
        if (entry.path().extension() != ".dbc") continue;
        std::string stem = entry.path().stem().string();
        for (char& c : stem) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        const uint32_t fields = dbcFieldCount(entry.path());
        if (fields > 0) out[stem] = fields;
    }
    return out;
}

/// The columns one layout names, as {name, index}.
std::vector<std::pair<std::string, int>> columnsOf(const std::string& json,
                                                   size_t at) {
    std::vector<std::pair<std::string, int>> out;
    const size_t open = json.find('{', at);
    const size_t close = json.find('}', open);
    if (open == std::string::npos || close == std::string::npos) return out;
    const std::string body = json.substr(open, close - open);
    const std::regex pair(R"RX("(\w+)"\s*:\s*(\d+))RX");
    for (std::sregex_iterator it(body.begin(), body.end(), pair), end;
         it != end; ++it) {
        out.emplace_back((*it)[1].str(), std::stoi((*it)[2].str()));
    }
    return out;
}

}  // namespace

TEST_CASE("no layout names a column its DBC does not have", "[dbc-layout]") {
    const auto shipped = shippedDbcFieldCounts();
    if (shipped.empty()) {
        WARN("Data/db has no readable DBC files here, skipping");
        return;
    }

    const std::string json = slurp("Data/expansions/wotlk/dbc_layouts.json");
    REQUIRE(json.size() > 100);

    // CharacterFacialHairStyles is declared for the nine-column file and the
    // shipped one has eight. detectFacialHairFields reads the field count and
    // moves the three geoset columns to 3, 4 and 5 when it is short, so the
    // layout's own numbers are the long-file case rather than a mistake. It is
    // named here so the exception is visible rather than silently tolerated.
    const std::string kKnownShortFile = "CharacterFacialHairStyles";

    const std::regex table(R"RX("(\w+)"\s*:\s*\{)RX");
    std::vector<std::string> problems;
    size_t checked = 0;

    for (std::sregex_iterator it(json.begin(), json.end(), table), end;
         it != end; ++it) {
        const std::string name = (*it)[1].str();
        if (name == kKnownShortFile) continue;

        std::string key = name;
        for (char& c : key) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        const auto found = shipped.find(key);
        if (found == shipped.end()) continue;  // that table is not shipped here
        ++checked;

        for (const auto& [column, index] :
             columnsOf(json, static_cast<size_t>(it->position()))) {
            if (index >= static_cast<int>(found->second)) {
                problems.push_back(name + "." + column + " = " +
                                   std::to_string(index) + ", but " + key +
                                   ".dbc has " + std::to_string(found->second) +
                                   " fields");
            }
        }
    }

    INFO("layouts checked against a shipped DBC: " << checked);
    for (const auto& p : problems) INFO("  " << p);
    CHECK(checked > 20);
    CHECK(problems.empty());
}

TEST_CASE("the light bands' count column holds counts", "[dbc-layout]") {
    // What the range check cannot see. Both band files are the id, a keyframe
    // count, sixteen times and sixteen values, and the count was declared at
    // field 2 - which is the first time key. Every index involved is inside
    // the file, so only the values give it away: a count is at most sixteen,
    // and the numbers in that column are 1440, 2160, 360 and 720, which are
    // noon, six in the evening, three and six in the morning.
    const std::string json = slurp("Data/expansions/wotlk/dbc_layouts.json");
    REQUIRE(json.size() > 100);

    for (const char* band : {"LightIntBand", "LightFloatBand"}) {
        // The column the layout declares, not a constant - otherwise this
        // passes no matter what the layout says.
        const size_t at = json.find(std::string("\"") + band + "\"");
        REQUIRE(at != std::string::npos);
        int countColumn = -1;
        for (const auto& [column, index] : columnsOf(json, at)) {
            if (column == "NumKeyframes") countColumn = index;
        }
        INFO(band << " declares NumKeyframes at column " << countColumn);
        REQUIRE(countColumn >= 0);

        const std::filesystem::path path = dbcPathFor(band);
        if (path.empty()) {
            WARN(std::string(band) + ".dbc is not here, skipping");
            continue;
        }
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        REQUIRE(bytes.size() > 20);
        REQUIRE(std::memcmp(bytes.data(), "WDBC", 4) == 0);

        uint32_t records = 0, fields = 0, recordSize = 0;
        std::memcpy(&records, bytes.data() + 4, 4);
        std::memcpy(&fields, bytes.data() + 8, 4);
        std::memcpy(&recordSize, bytes.data() + 12, 4);
        INFO(band << ": " << records << " records of " << fields << " fields");
        REQUIRE(fields == 34);

        const size_t base = 20;
        size_t overSixteen = 0;
        for (uint32_t r = 0; r < records; ++r) {
            const size_t at = base + static_cast<size_t>(r) * recordSize +
                              static_cast<size_t>(countColumn) * sizeof(uint32_t);
            if (at + 4 > bytes.size()) break;
            uint32_t count = 0;
            std::memcpy(&count, bytes.data() + at, 4);
            if (count > 16) ++overSixteen;
        }
        INFO("rows whose count column exceeds sixteen: " << overSixteen);
        CHECK(overSixteen == 0);
    }
}

// ---------------------------------------------------------------------------
// The spell effect block, checked by value across the expansions.
//
// The range check above passes a column that is inside the file and wrong, and
// the effect block is exactly that: it starts at 71 on WotLK, 65 on TBC and 61
// on vanilla, because TBC and vanilla carry an EffectBaseDice and an
// EffectDicePerLevel that WotLK dropped. All four layouts said 71, and 71 is a
// perfectly valid index in a 173-field file.
//
// Nothing raises. The client read some neighbouring column, no spell's effect
// ever equalled CREATE_ITEM, so no spell had a created item or reagents, and
// getCraftingRecipes filtered every recipe out - a trade skill window that came
// up empty, and then did not come up at all, because nothing opens a window
// with nothing in it. It was reported as smelting not working on Classic.
//
// The oracle needs no table of expected columns: a spell id means the same
// spell in every expansion that has it, so its effect, the item it creates and
// the reagent it consumes have to agree across the files. A column that is off
// by six agrees with nothing.
namespace {

/// One numeric field of one record, or nullopt if the file cannot be read.
struct Wdbc {
    std::vector<char> bytes;
    uint32_t records = 0, fields = 0, recordSize = 0;

    bool open(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        bytes.assign(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
        if (bytes.size() < 20 || std::memcmp(bytes.data(), "WDBC", 4) != 0)
            return false;
        std::memcpy(&records, bytes.data() + 4, 4);
        std::memcpy(&fields, bytes.data() + 8, 4);
        std::memcpy(&recordSize, bytes.data() + 12, 4);
        return recordSize >= fields * 4 &&
               bytes.size() >= 20 + size_t(records) * recordSize;
    }

    uint32_t at(uint32_t record, uint32_t field) const {
        if (record >= records || field >= fields) return 0;
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + 20 + size_t(record) * recordSize + field * 4, 4);
        return v;
    }
};

/// spell id -> the value in `column`, for every record.
std::map<uint32_t, uint32_t> columnById(const Wdbc& file, uint32_t column) {
    std::map<uint32_t, uint32_t> out;
    for (uint32_t r = 0; r < file.records; ++r) out[file.at(r, 0)] = file.at(r, column);
    return out;
}

int layoutColumn(const std::string& expansion, const std::string& field) {
    const std::string json =
        slurp("Data/expansions/" + expansion + "/dbc_layouts.json");
    const std::regex table(R"RX("Spell"\s*:\s*\{)RX");
    std::smatch m;
    if (!std::regex_search(json, m, table)) return -1;
    for (const auto& [name, index] :
         columnsOf(json, static_cast<size_t>(m.position()))) {
        if (name == field) return index;
    }
    return -1;
}

}  // namespace

TEST_CASE("the spell effect columns are the best match across expansions",
          "[dbc-layout]") {
    // WotLK's file is the shipped one; the other two ship as overlays. Classic
    // has no Spell.dbc of its own here and its layout is Turtle's, which is
    // checked - so a wrong column there shows up as this failing.
    const std::vector<std::pair<std::string, std::string>> files = {
        {"wotlk",  "Data/db/spell.dbc"},
        {"tbc",    "Data/expansions/tbc/overlay/db/Spell.dbc"},
        {"turtle", "Data/expansions/turtle/overlay/db/Spell.dbc"},
    };

    std::vector<std::pair<std::string, Wdbc>> opened;
    for (const auto& [expansion, path] : files) {
        Wdbc file;
        if (file.open(kRoot + path)) opened.emplace_back(expansion, std::move(file));
    }
    if (opened.size() < 2) {
        WARN("fewer than two Spell.dbc files readable here, skipping");
        return;
    }

    // Not "how often do they agree" - a private server's Spell.dbc is edited,
    // and Turtle's is edited enough that the right column agrees on four
    // spells in five rather than on all of them. What separates right from
    // wrong is that the right column agrees far more than any other column
    // does: 77% against 11% for the one beside it. So the test is that the
    // declared column wins its neighbourhood, which needs no threshold tuned
    // to how modded any particular file is.
    for (const char* field : {"Effect0", "EffectItemType0", "Reagent0",
                              "EffectMiscValue0", "EffectApplyAuraName0",
                              "EffectBasePoints0"}) {
        std::vector<std::pair<std::string, int>> declared;
        for (const auto& [expansion, file] : opened) {
            const int column = layoutColumn(expansion, field);
            INFO(expansion << " names no " << field);
            REQUIRE(column >= 0);
            REQUIRE(column < static_cast<int>(file.fields));
            declared.emplace_back(expansion, column);
        }

        for (size_t a = 0; a + 1 < opened.size(); ++a) {
            for (size_t b = a + 1; b < opened.size(); ++b) {
                const auto& left = opened[a].second;
                const auto& right = opened[b].second;
                const auto reference = columnById(left, static_cast<uint32_t>(declared[a].second));

                int bestColumn = -1;
                size_t best = 0, declaredAgreement = 0, runnerUp = 0;
                const int centre = declared[b].second;
                for (int column = std::max(0, centre - 12);
                     column <= centre + 12 && column < static_cast<int>(right.fields);
                     ++column) {
                    const auto candidate = columnById(right, static_cast<uint32_t>(column));
                    size_t agree = 0;
                    for (const auto& [id, value] : reference) {
                        // Rows the reference leaves at zero say nothing: most
                        // of a spell's effect block is zero, so a neighbouring
                        // column of zeros agrees on tens of thousands of rows
                        // and the margin between right and wrong disappears.
                        if (value == 0) continue;
                        auto it = candidate.find(id);
                        if (it != candidate.end() && it->second == value) ++agree;
                    }
                    if (column == centre) declaredAgreement = agree;
                    else if (agree > runnerUp) runnerUp = agree;
                    if (agree > best) { best = agree; bestColumn = column; }
                }

                INFO(field << ": " << opened[a].first << " vs " << opened[b].first
                     << " - declared column " << centre << " agrees on "
                     << declaredAgreement << ", the best other column in range on "
                     << runnerUp << ", best overall is column " << bestColumn);
                CHECK(bestColumn == centre);
                // A clear win rather than a coin toss. Two columns that both
                // hold small enums can agree by accident on a lot of rows;
                // half as much again is well outside that.
                CHECK(declaredAgreement > runnerUp + runnerUp / 2);
            }
        }
    }
}
