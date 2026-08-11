#include "cli_raid_markers_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_raid_markers.hpp"
#include <nlohmann/json.hpp>

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

const char* markerKindName(uint8_t k) {
    using M = wowee::pipeline::WoweeRaidMarkers;
    switch (k) {
        case M::RaidTarget: return "raidtarget";
        case M::WorldMap:   return "worldmap";
        case M::Party:      return "party";
        case M::Custom:     return "custom";
        default:            return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeRaidMarkers& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmar\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  markers : %zu\n", c.entries.size());
}

int handleGenRaid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidTargetMarkers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmar");
    auto c = wowee::pipeline::WoweeRaidMarkersLoader::makeRaidTargets(name);
    if (!saveOrError<wowee::pipeline::WoweeRaidMarkersLoader>(c, base, "gen-mar", ".wmar")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWorld(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WorldMapPinMarkers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmar");
    auto c = wowee::pipeline::WoweeRaidMarkersLoader::makeWorldMapPins(name);
    if (!saveOrError<wowee::pipeline::WoweeRaidMarkersLoader>(c, base, "gen-mar-world", ".wmar")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenParty(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PartyRoleMarkers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmar");
    auto c = wowee::pipeline::WoweeRaidMarkersLoader::makeParty(name);
    if (!saveOrError<wowee::pipeline::WoweeRaidMarkersLoader>(c, base, "gen-mar-party", ".wmar")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmar");
    if (!wowee::pipeline::WoweeRaidMarkersLoader::exists(base)) {
        std::fprintf(stderr, "WMAR not found: %s.wmar\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRaidMarkersLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmar"] = base + ".wmar";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"markerId", e.markerId},
                {"name", e.name},
                {"description", e.description},
                {"markerKind", e.markerKind},
                {"markerKindName", markerKindName(e.markerKind)},
                {"priority", e.priority},
                {"iconPath", e.iconPath},
                {"displayChar", e.displayChar},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMAR: %s.wmar\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  markers : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind         prio  glyph   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s    %3u   '%s'    %s\n",
                    e.markerId,
                    markerKindName(e.markerKind),
                    e.priority,
                    e.displayChar.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int parseMarkerKindToken(const std::string& s) {
    using M = wowee::pipeline::WoweeRaidMarkers;
    if (s == "raidtarget") return M::RaidTarget;
    if (s == "worldmap")   return M::WorldMap;
    if (s == "party")      return M::Party;
    if (s == "custom")     return M::Custom;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wmar");
    if (out.empty()) out = base + ".wmar.json";
    if (!wowee::pipeline::WoweeRaidMarkersLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wmar-json: WMAR not found: %s.wmar\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRaidMarkersLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WMAR";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"markerId", e.markerId},
            {"name", e.name},
            {"description", e.description},
            {"markerKind", e.markerKind},
            {"markerKindName", markerKindName(e.markerKind)},
            {"priority", e.priority},
            {"iconPath", e.iconPath},
            {"displayChar", e.displayChar},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wmar-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu markers)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wmar");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wmar-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wmar-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeRaidMarkers c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wmar-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeRaidMarkers::Entry e;
        e.markerId = je.value("markerId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (je.contains("markerKind")) {
            const auto& v = je["markerKind"];
            if (v.is_string()) {
                int parsed = parseMarkerKindToken(
                    v.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wmar-json: unknown "
                        "markerKind token '%s' on entry "
                        "id=%u\n",
                        v.get<std::string>().c_str(),
                        e.markerId);
                    return 1;
                }
                e.markerKind = static_cast<uint8_t>(parsed);
            } else if (v.is_number_integer()) {
                e.markerKind = static_cast<uint8_t>(v.get<int>());
            }
        } else if (je.contains("markerKindName") &&
                   je["markerKindName"].is_string()) {
            int parsed = parseMarkerKindToken(
                je["markerKindName"].get<std::string>());
            if (parsed >= 0)
                e.markerKind = static_cast<uint8_t>(parsed);
        }
        e.priority = static_cast<uint8_t>(je.value("priority", 0u));
        e.iconPath = je.value("iconPath", std::string{});
        e.displayChar = je.value("displayChar", std::string{});
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeRaidMarkersLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmar-json: failed to save %s.wmar\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmar (%zu markers)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmar");
    if (!wowee::pipeline::WoweeRaidMarkersLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wmar: WMAR not found: %s.wmar\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRaidMarkersLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-(markerKind, priority) tuple uniqueness — two
    // markers at same kind+priority would render in
    // unstable order in the picker UI.
    std::set<uint32_t> kindPrioSeen;
    auto kindPrioKey = [](uint8_t kind, uint8_t prio) {
        return (static_cast<uint32_t>(kind) << 8) | prio;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.markerId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.markerId == 0)
            errors.push_back(ctx + ": markerId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.markerKind > 3) {
            errors.push_back(ctx + ": markerKind " +
                std::to_string(e.markerKind) +
                " out of range (must be 0..3)");
        }
        if (e.iconPath.empty()) {
            warnings.push_back(ctx +
                ": iconPath is empty — marker would "
                "render as untextured fallback glyph");
        }
        // displayChar should be 1-3 visible characters
        // (single ASCII like "*" or short Unicode code-
        // point sequences like "<>" for diamond). Empty
        // would break chat-overlay text.
        if (e.displayChar.empty()) {
            warnings.push_back(ctx +
                ": displayChar is empty — chat overlay "
                "(e.g. \"{star}\" link) would render "
                "blank");
        }
        if (e.displayChar.size() > 4) {
            warnings.push_back(ctx +
                ": displayChar is " +
                std::to_string(e.displayChar.size()) +
                " bytes (>4) — chat overlay glyphs "
                "should be terse (1-3 chars typical)");
        }
        // RaidTarget kind has a canonical 8-marker max
        // (priorities 0-7). More would conflict with
        // the Blizzard-canonical /raidicon dispatch.
        using M = wowee::pipeline::WoweeRaidMarkers;
        if (e.markerKind == M::RaidTarget && e.priority > 7) {
            warnings.push_back(ctx +
                ": RaidTarget priority " +
                std::to_string(e.priority) +
                " > 7 — exceeds the canonical 8-slot "
                "/raidicon dispatch range; client "
                "keybind macros may not reach this slot");
        }
        // Tuple uniqueness: two markers at same
        // (kind, priority) would render unstably.
        uint32_t key = kindPrioKey(e.markerKind, e.priority);
        if (!kindPrioSeen.insert(key).second) {
            errors.push_back(ctx +
                ": (markerKind=" +
                std::string(markerKindName(e.markerKind)) +
                ", priority=" + std::to_string(e.priority) +
                ") slot already occupied by another "
                "marker — picker UI sort would be non-"
                "deterministic");
        }
        if (!idsSeen.insert(e.markerId).second) {
            errors.push_back(ctx + ": duplicate markerId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wmar", base, errors, warnings);
    std::printf("validate-wmar: %s.wmar\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu markers, all markerIds + "
                    "(kind,priority) tuples unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleRaidMarkersCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-mar") == 0 && i + 1 < argc) {
        outRc = handleGenRaid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mar-world") == 0 && i + 1 < argc) {
        outRc = handleGenWorld(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mar-party") == 0 && i + 1 < argc) {
        outRc = handleGenParty(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmar") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmar") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmar-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmar-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
