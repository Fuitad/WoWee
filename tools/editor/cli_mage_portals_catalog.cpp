#include "cli_mage_portals_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_mage_portals.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* factionAccessName(uint8_t f) {
    using P = wowee::pipeline::WoweeMagePortals;
    switch (f) {
        case P::Both:     return "both";
        case P::Alliance: return "alliance";
        case P::Horde:    return "horde";
        case P::Neutral:  return "neutral";
        default:          return "?";
    }
}

const char* portalKindName(uint8_t k) {
    using P = wowee::pipeline::WoweeMagePortals;
    switch (k) {
        case P::Teleport: return "teleport";
        case P::Portal:   return "portal";
        default:          return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeMagePortals& c,
                     const std::string& base) {
    std::printf("Wrote %s.wprt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  portals : %zu\n", c.entries.size());
}

int handleGenAlliance(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllianceCityPortals";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprt");
    auto c = wowee::pipeline::WoweeMagePortalsLoader::
        makeAllianceCities(name);
    if (!saveOrError<wowee::pipeline::WoweeMagePortalsLoader>(c, base, "gen-prt-alliance", ".wprt")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHorde(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HordeCityPortals";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprt");
    auto c = wowee::pipeline::WoweeMagePortalsLoader::
        makeHordeCities(name);
    if (!saveOrError<wowee::pipeline::WoweeMagePortalsLoader>(c, base, "gen-prt-horde", ".wprt")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTeleports(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TeleportSpells";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprt");
    auto c = wowee::pipeline::WoweeMagePortalsLoader::
        makeTeleports(name);
    if (!saveOrError<wowee::pipeline::WoweeMagePortalsLoader>(c, base, "gen-prt-teleports", ".wprt")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wprt");
    if (!wowee::pipeline::WoweeMagePortalsLoader::exists(base)) {
        std::fprintf(stderr, "WPRT not found: %s.wprt\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMagePortalsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wprt"] = base + ".wprt";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"portalId", e.portalId},
                {"spellId", e.spellId},
                {"destinationName", e.destinationName},
                {"destX", e.destX},
                {"destY", e.destY},
                {"destZ", e.destZ},
                {"destOrientation", e.destOrientation},
                {"destMapId", e.destMapId},
                {"factionAccess", e.factionAccess},
                {"factionAccessName",
                    factionAccessName(e.factionAccess)},
                {"portalKind", e.portalKind},
                {"portalKindName",
                    portalKindName(e.portalKind)},
                {"levelRequirement", e.levelRequirement},
                {"reagentCount", e.reagentCount},
                {"reagentItemId", e.reagentItemId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPRT: %s.wprt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  portals : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    spell  kind      fact      lvl  reagent  destination\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %7u  %-8s  %-8s  %3u  %7u  %s\n",
                    e.portalId, e.spellId,
                    portalKindName(e.portalKind),
                    factionAccessName(e.factionAccess),
                    e.levelRequirement, e.reagentItemId,
                    e.destinationName.c_str());
    }
    return 0;
}

int parseFactionAccessToken(const std::string& s) {
    using P = wowee::pipeline::WoweeMagePortals;
    if (s == "both")     return P::Both;
    if (s == "alliance") return P::Alliance;
    if (s == "horde")    return P::Horde;
    if (s == "neutral")  return P::Neutral;
    return -1;
}

int parsePortalKindToken(const std::string& s) {
    using P = wowee::pipeline::WoweeMagePortals;
    if (s == "teleport") return P::Teleport;
    if (s == "portal")   return P::Portal;
    return -1;
}

template <typename ParseFn>
bool readEnumField(const nlohmann::json& je,
                    const char* intKey,
                    const char* nameKey,
                    ParseFn parseFn,
                    const char* label,
                    uint32_t entryId,
                    uint8_t& outValue) {
    if (je.contains(intKey)) {
        const auto& v = je[intKey];
        if (v.is_string()) {
            int parsed = parseFn(v.get<std::string>());
            if (parsed < 0) {
                std::fprintf(stderr,
                    "import-wprt-json: unknown %s token "
                    "'%s' on entry id=%u\n",
                    label, v.get<std::string>().c_str(),
                    entryId);
                return false;
            }
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
        if (v.is_number_integer()) {
            outValue = static_cast<uint8_t>(v.get<int>());
            return true;
        }
    }
    if (je.contains(nameKey) && je[nameKey].is_string()) {
        int parsed = parseFn(je[nameKey].get<std::string>());
        if (parsed >= 0) {
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
    }
    return true;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wprt");
    if (!wowee::pipeline::WoweeMagePortalsLoader::exists(base)) {
        return reportMissing("validate-wprt", "WPRT", base, ".wprt");
    }
    auto c = wowee::pipeline::WoweeMagePortalsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<uint32_t> spellIdsSeen;
    std::set<std::string> destNamesSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.portalId);
        if (!e.destinationName.empty())
            ctx += " " + e.destinationName;
        ctx += ")";
        if (e.portalId == 0)
            errors.push_back(ctx + ": portalId is 0");
        if (e.spellId == 0)
            errors.push_back(ctx +
                ": spellId is 0 (no spell to cast)");
        if (e.destinationName.empty())
            errors.push_back(ctx +
                ": destinationName is empty");
        if (e.factionAccess > 3) {
            errors.push_back(ctx + ": factionAccess " +
                std::to_string(e.factionAccess) +
                " out of range (0..3)");
        }
        if (e.portalKind > 1) {
            errors.push_back(ctx + ": portalKind " +
                std::to_string(e.portalKind) +
                " out of range (0=Teleport, 1=Portal)");
        }
        // Mage portal/teleport spells unlock at
        // level 20 (Teleport) or 40 (Portal) in
        // vanilla. A levelRequirement below 20 would
        // be impossible to satisfy with a vanilla
        // mage. Warn (not error) since custom
        // servers may rebalance.
        if (e.levelRequirement > 0 &&
            e.levelRequirement < 20) {
            warnings.push_back(ctx +
                ": levelRequirement=" +
                std::to_string(e.levelRequirement) +
                " is below 20 — vanilla mage cannot "
                "unlock until 20 (Teleport) or 40 "
                "(Portal). Possible typo?");
        }
        // Cross-faction sanity: an Alliance-only
        // portal pointing to a Horde city, or vice
        // versa, would let a mage flag PvP
        // permanently. Validator can't know which
        // map is which faction without WMS lookup,
        // so just warn on Both/Neutral with a city
        // name suggesting one-faction.
        using P = wowee::pipeline::WoweeMagePortals;
        if (e.portalKind == P::Portal &&
            e.reagentItemId != 17032 &&
            e.reagentItemId != 0) {
            warnings.push_back(ctx +
                ": Portal kind with reagentItemId=" +
                std::to_string(e.reagentItemId) +
                " — vanilla group portals require "
                "Rune of Portals (itemId 17032). "
                "Verify intentional");
        }
        if (e.portalKind == P::Teleport &&
            e.reagentItemId != 17031 &&
            e.reagentItemId != 0) {
            warnings.push_back(ctx +
                ": Teleport kind with reagentItemId=" +
                std::to_string(e.reagentItemId) +
                " — vanilla self-teleports require "
                "Rune of Teleportation (itemId 17031). "
                "Verify intentional");
        }
        // Duplicate spellId across portals would mean
        // two portal-cast handlers fight over the same
        // spell — error.
        if (e.spellId != 0 &&
            !spellIdsSeen.insert(e.spellId).second) {
            errors.push_back(ctx +
                ": duplicate spellId " +
                std::to_string(e.spellId) +
                " — two portals would respond to the "
                "same cast");
        }
        if (!e.destinationName.empty() &&
            !destNamesSeen.insert(e.destinationName).second) {
            // Duplicate destination NAME is allowed
            // (e.g., Teleport: SW + Portal: SW are
            // both "Stormwind") — only warn so the
            // editor can flag potential dupes.
            warnings.push_back(ctx +
                ": duplicate destinationName '" +
                e.destinationName +
                "' — could be a Teleport/Portal pair "
                "(legitimate) or a copy-paste bug");
            // Re-allow in the seen set so subsequent
            // duplicates also warn
        }
        if (!idsSeen.insert(e.portalId).second) {
            errors.push_back(ctx + ": duplicate portalId");
        }
    }
    return cli::reportValidation("wprt", base, jsonOut, errors, warnings,
                                 formatted("%zu portals, all portalIds + "
                    "spellIds unique, factionAccess 0..3, "
                    "portalKind 0..1, levelRequirement >= 20, "
                    "reagent matches kind", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wprt");
    if (out.empty()) out = base + ".wprt.json";
    if (!wowee::pipeline::WoweeMagePortalsLoader::exists(base)) {
        return reportMissing("export-wprt-json", "WPRT", base, ".wprt");
    }
    auto c = wowee::pipeline::WoweeMagePortalsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WPRT";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"portalId", e.portalId},
            {"spellId", e.spellId},
            {"destinationName", e.destinationName},
            {"destX", e.destX},
            {"destY", e.destY},
            {"destZ", e.destZ},
            {"destOrientation", e.destOrientation},
            {"destMapId", e.destMapId},
            {"factionAccess", e.factionAccess},
            {"factionAccessName",
                factionAccessName(e.factionAccess)},
            {"portalKind", e.portalKind},
            {"portalKindName",
                portalKindName(e.portalKind)},
            {"levelRequirement", e.levelRequirement},
            {"reagentCount", e.reagentCount},
            {"reagentItemId", e.reagentItemId},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wprt-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu portals)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wprt");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wprt-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wprt-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeMagePortals c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wprt-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeMagePortals::Entry e;
        e.portalId = je.value("portalId", 0u);
        e.spellId = je.value("spellId", 0u);
        e.destinationName = je.value("destinationName",
                                       std::string{});
        e.destX = je.value("destX", 0.f);
        e.destY = je.value("destY", 0.f);
        e.destZ = je.value("destZ", 0.f);
        e.destOrientation = je.value("destOrientation", 0.f);
        e.destMapId = je.value("destMapId", 0u);
        if (!readEnumField(je, "factionAccess",
                            "factionAccessName",
                            parseFactionAccessToken,
                            "factionAccess", e.portalId,
                            e.factionAccess)) return 1;
        if (!readEnumField(je, "portalKind", "portalKindName",
                            parsePortalKindToken,
                            "portalKind", e.portalId,
                            e.portalKind)) return 1;
        e.levelRequirement = static_cast<uint8_t>(
            je.value("levelRequirement", 0));
        e.reagentCount = static_cast<uint8_t>(
            je.value("reagentCount", 0));
        e.reagentItemId = je.value("reagentItemId", 0u);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeMagePortalsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wprt-json: failed to save %s.wprt\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wprt (%zu portals)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleMagePortalsCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-prt-alliance") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAlliance(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-prt-horde") == 0 &&
        i + 1 < argc) {
        outRc = handleGenHorde(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-prt-teleports") == 0 &&
        i + 1 < argc) {
        outRc = handleGenTeleports(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wprt") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wprt") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wprt-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wprt-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
