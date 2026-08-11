#include "cli_pvp_ranks_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_pvp_ranks.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* factionFilterName(uint8_t f) {
    using P = wowee::pipeline::WoweePvPRanks;
    switch (f) {
        case P::AllianceOnly: return "alliance";
        case P::HordeOnly:    return "horde";
        default:              return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweePvPRanks& c,
                     const std::string& base) {
    std::printf("Wrote %s.wprg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranks   : %zu\n", c.entries.size());
}

int handleGenAlliance(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllianceLowerRanks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprg");
    auto c = wowee::pipeline::WoweePvPRanksLoader::makeAllianceRanks(name);
    if (!saveOrError<wowee::pipeline::WoweePvPRanksLoader>(c, base, "gen-prg", ".wprg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHorde(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HordeLowerRanks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprg");
    auto c = wowee::pipeline::WoweePvPRanksLoader::makeHordeRanks(name);
    if (!saveOrError<wowee::pipeline::WoweePvPRanksLoader>(c, base, "gen-prg-horde", ".wprg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHigh(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HighRanks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprg");
    auto c = wowee::pipeline::WoweePvPRanksLoader::makeHighRanks(name);
    if (!saveOrError<wowee::pipeline::WoweePvPRanksLoader>(c, base, "gen-prg-high", ".wprg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wprg");
    if (!wowee::pipeline::WoweePvPRanksLoader::exists(base)) {
        std::fprintf(stderr, "WPRG not found: %s.wprg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePvPRanksLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wprg"] = base + ".wprg";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"rankId", e.rankId},
                {"name", e.name},
                {"description", e.description},
                {"factionFilter", e.factionFilter},
                {"factionFilterName",
                    factionFilterName(e.factionFilter)},
                {"tier", e.tier},
                {"honorRequiredWeekly", e.honorRequiredWeekly},
                {"honorRequiredAchieve", e.honorRequiredAchieve},
                {"titlePrefix", e.titlePrefix},
                {"gearItemId", e.gearItemId},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPRG: %s.wprg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranks   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   faction    tier  weekly RP   total RP   gear   title           name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-8s    %3u  %9u  %9u   %5u  %-15s   %s\n",
                    e.rankId, factionFilterName(e.factionFilter),
                    e.tier, e.honorRequiredWeekly,
                    e.honorRequiredAchieve, e.gearItemId,
                    e.titlePrefix.c_str(), e.name.c_str());
    }
    return 0;
}

int parseFactionFilterToken(const std::string& s) {
    using P = wowee::pipeline::WoweePvPRanks;
    if (s == "alliance") return P::AllianceOnly;
    if (s == "horde")    return P::HordeOnly;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wprg");
    if (out.empty()) out = base + ".wprg.json";
    if (!wowee::pipeline::WoweePvPRanksLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wprg-json: WPRG not found: %s.wprg\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePvPRanksLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WPRG";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"rankId", e.rankId},
            {"name", e.name},
            {"description", e.description},
            {"factionFilter", e.factionFilter},
            {"factionFilterName",
                factionFilterName(e.factionFilter)},
            {"tier", e.tier},
            {"honorRequiredWeekly", e.honorRequiredWeekly},
            {"honorRequiredAchieve", e.honorRequiredAchieve},
            {"titlePrefix", e.titlePrefix},
            {"gearItemId", e.gearItemId},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wprg-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu ranks)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wprg");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wprg-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wprg-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweePvPRanks c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wprg-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweePvPRanks::Entry e;
        e.rankId = je.value("rankId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (je.contains("factionFilter")) {
            const auto& v = je["factionFilter"];
            if (v.is_string()) {
                int parsed = parseFactionFilterToken(
                    v.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wprg-json: unknown "
                        "factionFilter token '%s' on "
                        "entry id=%u\n",
                        v.get<std::string>().c_str(),
                        e.rankId);
                    return 1;
                }
                e.factionFilter = static_cast<uint8_t>(parsed);
            } else if (v.is_number_integer()) {
                e.factionFilter = static_cast<uint8_t>(
                    v.get<int>());
            }
        } else if (je.contains("factionFilterName") &&
                   je["factionFilterName"].is_string()) {
            int parsed = parseFactionFilterToken(
                je["factionFilterName"].get<std::string>());
            if (parsed >= 0)
                e.factionFilter = static_cast<uint8_t>(parsed);
        }
        e.tier = static_cast<uint8_t>(je.value("tier", 1u));
        e.honorRequiredWeekly = je.value("honorRequiredWeekly", 0u);
        e.honorRequiredAchieve = je.value("honorRequiredAchieve",
                                            0u);
        e.titlePrefix = je.value("titlePrefix", std::string{});
        e.gearItemId = je.value("gearItemId", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweePvPRanksLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wprg-json: failed to save %s.wprg\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wprg (%zu ranks)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wprg");
    if (!wowee::pipeline::WoweePvPRanksLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wprg: WPRG not found: %s.wprg\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePvPRanksLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-(faction, tier) tuple uniqueness — two ranks
    // at the same tier for the same faction would tie
    // at runtime when the rank-progression UI looks up
    // "what's tier 5 for Alliance?"
    std::set<uint16_t> factionTierSeen;
    auto factionTierKey = [](uint8_t faction, uint8_t tier) {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(faction) << 8) | tier);
    };
    // Per-faction monotonicity: honorRequiredAchieve
    // should be non-decreasing as tier increases.
    std::map<uint8_t, std::vector<
        const wowee::pipeline::WoweePvPRanks::Entry*>>
        byFaction;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.rankId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.rankId == 0)
            errors.push_back(ctx + ": rankId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.factionFilter != 1 && e.factionFilter != 2) {
            errors.push_back(ctx + ": factionFilter " +
                std::to_string(e.factionFilter) +
                " out of range (must be 1=Alliance or "
                "2=Horde)");
        }
        if (e.tier < 1 || e.tier > 14) {
            errors.push_back(ctx + ": tier " +
                std::to_string(e.tier) +
                " out of range (must be 1..14 — vanilla "
                "ladder)");
        }
        if (e.titlePrefix.empty()) {
            warnings.push_back(ctx +
                ": titlePrefix is empty — UI rank-name "
                "display would render blank");
        }
        if (e.tier <= 14 && (e.factionFilter == 1 ||
                              e.factionFilter == 2)) {
            uint16_t key = factionTierKey(e.factionFilter,
                                            e.tier);
            if (!factionTierSeen.insert(key).second) {
                errors.push_back(ctx +
                    ": (faction=" +
                    std::string(factionFilterName(e.factionFilter)) +
                    ", tier=" + std::to_string(e.tier) +
                    ") slot already occupied by another "
                    "rank — runtime lookup would tie");
            }
        }
        if (!idsSeen.insert(e.rankId).second) {
            errors.push_back(ctx + ": duplicate rankId");
        }
        byFaction[e.factionFilter].push_back(&e);
    }
    // Per-faction monotonicity check.
    for (auto& [faction, ranks] : byFaction) {
        if (ranks.size() < 2) continue;
        std::sort(ranks.begin(), ranks.end(),
            [](auto* a, auto* b) {
                return a->tier < b->tier;
            });
        for (size_t k = 1; k < ranks.size(); ++k) {
            if (ranks[k]->honorRequiredAchieve <
                ranks[k-1]->honorRequiredAchieve) {
                warnings.push_back("faction " +
                    std::string(factionFilterName(faction)) +
                    " has decreasing honor threshold: tier " +
                    std::to_string(ranks[k-1]->tier) +
                    " (" + ranks[k-1]->name + ") requires " +
                    std::to_string(ranks[k-1]->honorRequiredAchieve) +
                    " > tier " + std::to_string(ranks[k]->tier) +
                    " (" + ranks[k]->name + ") requiring " +
                    std::to_string(ranks[k]->honorRequiredAchieve) +
                    " — higher tier should require more "
                    "honor, not less");
            }
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wprg", base, errors, warnings);
    std::printf("validate-wprg: %s.wprg\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu ranks, all rankIds + "
                    "(faction,tier) tuples unique, honor "
                    "thresholds monotonic per faction\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handlePvPRanksCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-prg") == 0 && i + 1 < argc) {
        outRc = handleGenAlliance(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-prg-horde") == 0 &&
        i + 1 < argc) {
        outRc = handleGenHorde(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-prg-high") == 0 &&
        i + 1 < argc) {
        outRc = handleGenHigh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wprg") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wprg") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wprg-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wprg-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
