#include "cli_factions_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_factions.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

void appendRepFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeFaction::VisibleOnTab) s += "visible ";
    if (flags & wowee::pipeline::WoweeFaction::AtWarDefault) s += "at-war ";
    if (flags & wowee::pipeline::WoweeFaction::Hidden)       s += "hidden ";
    if (flags & wowee::pipeline::WoweeFaction::NoReputation) s += "no-rep ";
    if (flags & wowee::pipeline::WoweeFaction::IsHeader)     s += "header ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}


void printGenSummary(const wowee::pipeline::WoweeFaction& c,
                     const std::string& base) {
    std::printf("Wrote %s.wfac\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  factions : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterFactions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wfac");
    auto c = wowee::pipeline::WoweeFactionLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeFactionLoader>(c, base, "gen-factions", ".wfac")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAlliance(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllianceFactions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wfac");
    auto c = wowee::pipeline::WoweeFactionLoader::makeAlliance(name);
    if (!saveOrError<wowee::pipeline::WoweeFactionLoader>(c, base, "gen-factions-alliance", ".wfac")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWildlife(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WildlifeFactions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wfac");
    auto c = wowee::pipeline::WoweeFactionLoader::makeWildlife(name);
    if (!saveOrError<wowee::pipeline::WoweeFactionLoader>(c, base, "gen-factions-wildlife", ".wfac")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wfac");
    if (!wowee::pipeline::WoweeFactionLoader::exists(base)) {
        return reportMissing("WFAC", base, ".wfac");
    }
    auto c = wowee::pipeline::WoweeFactionLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wfac"] = base + ".wfac";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendRepFlagsStr(fs, e.reputationFlags);
            nlohmann::json je;
            je["factionId"] = e.factionId;
            je["parentFactionId"] = e.parentFactionId;
            je["name"] = e.name;
            je["description"] = e.description;
            je["reputationFlags"] = e.reputationFlags;
            je["reputationFlagsStr"] = fs;
            je["baseReputation"] = e.baseReputation;
            je["thresholdHostile"] = e.thresholdHostile;
            je["thresholdUnfriendly"] = e.thresholdUnfriendly;
            je["thresholdNeutral"] = e.thresholdNeutral;
            je["thresholdFriendly"] = e.thresholdFriendly;
            je["thresholdHonored"] = e.thresholdHonored;
            je["thresholdRevered"] = e.thresholdRevered;
            je["thresholdExalted"] = e.thresholdExalted;
            je["enemies"] = e.enemies;
            je["friends"] = e.friends;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WFAC: %s.wfac\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  factions : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id     parent  flags        enemies  friends   name\n");
    for (const auto& e : c.entries) {
        std::string fs;
        appendRepFlagsStr(fs, e.reputationFlags);
        std::printf("  %4u    %4u    %-12s   %2zu     %2zu       %s\n",
                    e.factionId, e.parentFactionId, fs.c_str(),
                    e.enemies.size(), e.friends.size(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each faction emits all 13 scalar fields
    // plus the variable-length enemies + friends arrays and
    // a string-array form for the reputation flag bitset.
    return cli::exportCatalogJson<wowee::pipeline::WoweeFactionLoader>(
        i, argc, argv, "wfac", "WFAC", "factions ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json je;
            je["factionId"] = e.factionId;
            je["parentFactionId"] = e.parentFactionId;
            je["name"] = e.name;
            je["description"] = e.description;
            je["reputationFlags"] = e.reputationFlags;
            nlohmann::json fa = nlohmann::json::array();
            if (e.reputationFlags & wowee::pipeline::WoweeFaction::VisibleOnTab) fa.push_back("visible");
            if (e.reputationFlags & wowee::pipeline::WoweeFaction::AtWarDefault) fa.push_back("at-war");
            if (e.reputationFlags & wowee::pipeline::WoweeFaction::Hidden)       fa.push_back("hidden");
            if (e.reputationFlags & wowee::pipeline::WoweeFaction::NoReputation) fa.push_back("no-rep");
            if (e.reputationFlags & wowee::pipeline::WoweeFaction::IsHeader)     fa.push_back("header");
            je["reputationFlagsList"] = fa;
            je["baseReputation"] = e.baseReputation;
            je["thresholdHostile"] = e.thresholdHostile;
            je["thresholdUnfriendly"] = e.thresholdUnfriendly;
            je["thresholdNeutral"] = e.thresholdNeutral;
            je["thresholdFriendly"] = e.thresholdFriendly;
            je["thresholdHonored"] = e.thresholdHonored;
            je["thresholdRevered"] = e.thresholdRevered;
            je["thresholdExalted"] = e.thresholdExalted;
            je["enemies"] = e.enemies;
            je["friends"] = e.friends;
            arr.push_back(je);
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wfac");
    outBase = cli::withoutExt(outBase, ".wfac");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wfac-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wfac-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto repFlagFromName = [](const std::string& s) -> uint32_t {
        if (s == "visible") return wowee::pipeline::WoweeFaction::VisibleOnTab;
        if (s == "at-war")  return wowee::pipeline::WoweeFaction::AtWarDefault;
        if (s == "hidden")  return wowee::pipeline::WoweeFaction::Hidden;
        if (s == "no-rep")  return wowee::pipeline::WoweeFaction::NoReputation;
        if (s == "header")  return wowee::pipeline::WoweeFaction::IsHeader;
        return 0;
    };
    auto readU32Vec = [](const nlohmann::json& jv, std::vector<uint32_t>& v) {
        if (jv.is_array()) {
            for (const auto& e : jv) {
                if (e.is_number_integer()) v.push_back(e.get<uint32_t>());
            }
        }
    };
    wowee::pipeline::WoweeFaction c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeFaction::Entry e;
            e.factionId = je.value("factionId", 0u);
            e.parentFactionId = je.value("parentFactionId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("reputationFlags") &&
                je["reputationFlags"].is_number_integer()) {
                e.reputationFlags = je["reputationFlags"].get<uint32_t>();
            } else if (je.contains("reputationFlagsList") &&
                       je["reputationFlagsList"].is_array()) {
                e.reputationFlags = 0;
                for (const auto& f : je["reputationFlagsList"]) {
                    if (f.is_string())
                        e.reputationFlags |= repFlagFromName(f.get<std::string>());
                }
            }
            e.baseReputation = je.value("baseReputation", 0);
            e.thresholdHostile = je.value("thresholdHostile",
                static_cast<int32_t>(wowee::pipeline::WoweeFaction::Hostile));
            e.thresholdUnfriendly = je.value("thresholdUnfriendly",
                static_cast<int32_t>(wowee::pipeline::WoweeFaction::Unfriendly));
            e.thresholdNeutral = je.value("thresholdNeutral", 0);
            e.thresholdFriendly = je.value("thresholdFriendly",
                static_cast<int32_t>(wowee::pipeline::WoweeFaction::Friendly));
            e.thresholdHonored = je.value("thresholdHonored",
                static_cast<int32_t>(wowee::pipeline::WoweeFaction::Honored));
            e.thresholdRevered = je.value("thresholdRevered",
                static_cast<int32_t>(wowee::pipeline::WoweeFaction::Revered));
            e.thresholdExalted = je.value("thresholdExalted",
                static_cast<int32_t>(wowee::pipeline::WoweeFaction::Exalted));
            if (je.contains("enemies")) readU32Vec(je["enemies"], e.enemies);
            if (je.contains("friends")) readU32Vec(je["friends"], e.friends);
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeFactionLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wfac-json: failed to save %s.wfac\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wfac\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  factions : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeFactionLoader>(
        i, argc, argv, "wfac", "WFAC",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        idsSeen.reserve(c.entries.size());
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.factionId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.factionId == 0) {
                errors.push_back(ctx + ": factionId is 0");
            }
            if (e.name.empty()) {
                errors.push_back(ctx + ": name is empty");
            }
            // Threshold ordering: hostile < unfriendly < neutral <
            // friendly < honored < revered < exalted.
            if (e.thresholdHostile >= e.thresholdUnfriendly ||
                e.thresholdUnfriendly >= e.thresholdNeutral ||
                e.thresholdNeutral >= e.thresholdFriendly ||
                e.thresholdFriendly >= e.thresholdHonored ||
                e.thresholdHonored >= e.thresholdRevered ||
                e.thresholdRevered >= e.thresholdExalted) {
                errors.push_back(ctx +
                    ": reputation thresholds not strictly ascending "
                    "(hostile<unfriendly<neutral<friendly<honored<revered<exalted)");
            }
            // Self-relationship: a faction can't be its own enemy.
            for (uint32_t en : e.enemies) {
                if (en == e.factionId) {
                    errors.push_back(ctx + ": faction lists itself as enemy");
                    break;
                }
            }
            for (uint32_t fr : e.friends) {
                if (fr == e.factionId) {
                    errors.push_back(ctx + ": faction lists itself as friend");
                    break;
                }
            }
            // A faction in both enemies AND friends is incoherent.
            for (uint32_t en : e.enemies) {
                for (uint32_t fr : e.friends) {
                    if (en == fr) {
                        errors.push_back(ctx +
                            ": faction " + std::to_string(en) +
                            " appears in both enemies and friends");
                        break;
                    }
                }
            }
            if (!idsSeen.add(e.factionId)) errors.push_back(ctx + ": duplicate factionId");
        }
            return formatted("%zu factions, all factionIds unique", c.entries.size());
        });
}

} // namespace

bool handleFactionsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-factions") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-factions-alliance") == 0 && i + 1 < argc) {
        outRc = handleGenAlliance(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-factions-wildlife") == 0 && i + 1 < argc) {
        outRc = handleGenWildlife(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wfac") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wfac") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wfac-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wfac-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
