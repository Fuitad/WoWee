#include "cli_spell_pack_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_pack.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

std::string stripWspkExt(std::string base) {
    stripExt(base, ".wspk");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeSpellPack& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpellPackLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wspk\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpellPack& c,
                     const std::string& base) {
    std::printf("Wrote %s.wspk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  packs   : %zu\n", c.entries.size());
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorSpellPack";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWspkExt(base);
    auto c = wowee::pipeline::WoweeSpellPackLoader::
        makeWarriorPack(name);
    if (!saveOrError(c, base, "gen-spk-warrior")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageSpellPack";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWspkExt(base);
    auto c = wowee::pipeline::WoweeSpellPackLoader::
        makeMagePack(name);
    if (!saveOrError(c, base, "gen-spk-mage")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRogue(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RogueSpellPack";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWspkExt(base);
    auto c = wowee::pipeline::WoweeSpellPackLoader::
        makeRoguePack(name);
    if (!saveOrError(c, base, "gen-spk-rogue")) return 1;
    printGenSummary(c, base);
    return 0;
}

const char* classIdName(uint8_t c) {
    // Vanilla 1.12 PlayerClass DBC ids — used for the
    // info-table display only.
    switch (c) {
        case 1:  return "Warrior";
        case 2:  return "Paladin";
        case 3:  return "Hunter";
        case 4:  return "Rogue";
        case 5:  return "Priest";
        case 7:  return "Shaman";
        case 8:  return "Mage";
        case 9:  return "Warlock";
        case 11: return "Druid";
        default: return "?";
    }
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWspkExt(base);
    if (!wowee::pipeline::WoweeSpellPackLoader::exists(base)) {
        std::fprintf(stderr, "WSPK not found: %s.wspk\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellPackLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wspk"] = base + ".wspk";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"packId", e.packId},
                {"classId", e.classId},
                {"className", classIdName(e.classId)},
                {"tabIndex", e.tabIndex},
                {"iconIndex", e.iconIndex},
                {"tabName", e.tabName},
                {"spellIds", e.spellIds},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPK: %s.wspk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  packs   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  class           tab  icon  spells  tabName\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %2u %-9s    %3u  %4u   %5zu  %s\n",
                    e.packId, e.classId,
                    classIdName(e.classId),
                    e.tabIndex, e.iconIndex,
                    e.spellIds.size(), e.tabName.c_str());
    }
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWspkExt(base);
    if (!wowee::pipeline::WoweeSpellPackLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wspk: WSPK not found: %s.wspk\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellPackLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> packIdsSeen;
    std::set<std::pair<uint8_t, uint8_t>> classTabPairs;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (packId=" + std::to_string(e.packId);
        if (!e.tabName.empty()) ctx += " " + e.tabName;
        ctx += ")";
        if (e.packId == 0)
            errors.push_back(ctx + ": packId is 0");
        if (e.tabName.empty())
            errors.push_back(ctx + ": tabName is empty");
        // Vanilla classes: 1..11 with id 6 + 10 unused.
        if (e.classId == 0 || e.classId > 11) {
            errors.push_back(ctx + ": classId " +
                std::to_string(e.classId) +
                " out of vanilla range (1..11)");
        }
        if (e.classId == 6 || e.classId == 10) {
            warnings.push_back(ctx + ": classId " +
                std::to_string(e.classId) +
                " is unused in vanilla (gap in PlayerClass DBC)");
        }
        // Tab 0 = General; 1..3 = the three spec trees.
        if (e.tabIndex > 3) {
            errors.push_back(ctx + ": tabIndex " +
                std::to_string(e.tabIndex) +
                " out of range (0..3 — General + 3 specs)");
        }
        // (classId, tabIndex) MUST be unique — the
        // spellbook UI dispatches by this pair, two
        // entries with the same pair would tie.
        auto pair = std::make_pair(e.classId, e.tabIndex);
        if (!classTabPairs.insert(pair).second) {
            errors.push_back(ctx +
                ": duplicate (classId=" +
                std::to_string(e.classId) +
                ", tabIndex=" +
                std::to_string(e.tabIndex) +
                ") — spellbook UI tab dispatch tie");
        }
        if (!packIdsSeen.insert(e.packId).second) {
            errors.push_back(ctx + ": duplicate packId");
        }
        // Per-tab spell uniqueness — the same spellId
        // appearing twice in one tab is a copy-paste bug
        // (the UI would render it twice).
        std::set<uint32_t> spellsInTab;
        for (uint32_t sid : e.spellIds) {
            if (sid == 0) {
                errors.push_back(ctx +
                    ": tab contains spellId 0 (placeholder "
                    "or copy-paste error)");
            }
            if (sid != 0 && !spellsInTab.insert(sid).second) {
                errors.push_back(ctx +
                    ": duplicate spellId " +
                    std::to_string(sid) +
                    " within tab — would render twice in "
                    "spellbook");
            }
        }
        // Empty tab: warn — General tab with zero spells
        // means the player starts with no abilities at
        // all on that tree.
        if (e.spellIds.empty()) {
            warnings.push_back(ctx +
                ": tab has zero spells — player would see "
                "an empty spellbook tab");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wspk", base, errors, warnings);
    std::printf("validate-wspk: %s.wspk\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu packs, all packIds unique, "
                    "(classId,tabIndex) unique, classId in "
                    "1..11, tabIndex in 0..3, no duplicate "
                    "spellIds within any tab\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = stripWspkExt(base);
    if (out.empty()) out = base + ".wspk.json";
    if (!wowee::pipeline::WoweeSpellPackLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wspk-json: WSPK not found: %s.wspk\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellPackLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WSPK";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"packId", e.packId},
            {"classId", e.classId},
            {"className", classIdName(e.classId)},
            {"tabIndex", e.tabIndex},
            {"iconIndex", e.iconIndex},
            {"tabName", e.tabName},
            {"spellIds", e.spellIds},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wspk-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu packs)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = in;
        if (outBase.size() >= 10 &&
            outBase.substr(outBase.size() - 10) == ".wspk.json") {
            outBase.resize(outBase.size() - 10);
        } else {
            stripExt(outBase, ".json");
            stripExt(outBase, ".wspk");
        }
    }
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wspk-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wspk-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellPack c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wspk-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeSpellPack::Entry e;
        e.packId = je.value("packId", 0u);
        e.classId = static_cast<uint8_t>(je.value("classId", 0));
        e.tabIndex = static_cast<uint8_t>(je.value("tabIndex", 0));
        e.iconIndex = static_cast<uint8_t>(je.value("iconIndex", 0));
        e.tabName = je.value("tabName", std::string{});
        if (je.contains("spellIds") &&
            je["spellIds"].is_array()) {
            for (const auto& s : je["spellIds"]) {
                if (s.is_number_unsigned() ||
                    s.is_number_integer()) {
                    e.spellIds.push_back(s.get<uint32_t>());
                }
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeSpellPackLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wspk-json: failed to save %s.wspk\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wspk (%zu packs)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleSpellPackCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-spk-warrior") == 0 &&
        i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spk-mage") == 0 &&
        i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spk-rogue") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRogue(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wspk") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wspk") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wspk-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wspk-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
