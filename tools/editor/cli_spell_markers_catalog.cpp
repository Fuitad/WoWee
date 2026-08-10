#include "cli_spell_markers_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_markers.hpp"
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

const char* edgeFadeModeName(uint8_t m) {
    using S = wowee::pipeline::WoweeSpellMarkers;
    switch (m) {
        case S::Hard:     return "hard";
        case S::SoftEdge: return "softedge";
        case S::Pulse:    return "pulse";
        default:          return "unknown";
    }
}

bool saveOrError(const wowee::pipeline::WoweeSpellMarkers& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpellMarkersLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wspm\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpellMarkers& c,
                     const std::string& base) {
    std::printf("Wrote %s.wspm\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  markers : %zu\n", c.entries.size());
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageAoEMarkers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspm");
    auto c = wowee::pipeline::WoweeSpellMarkersLoader::makeMageAoE(name);
    if (!saveOrError(c, base, "gen-spm")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidHazardMarkers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspm");
    auto c = wowee::pipeline::WoweeSpellMarkersLoader::makeRaidHazards(name);
    if (!saveOrError(c, base, "gen-spm-raid")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenEnvironment(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "EnvironmentMarkers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspm");
    auto c = wowee::pipeline::WoweeSpellMarkersLoader::makeEnvironment(name);
    if (!saveOrError(c, base, "gen-spm-env")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspm");
    if (!wowee::pipeline::WoweeSpellMarkersLoader::exists(base)) {
        std::fprintf(stderr, "WSPM not found: %s.wspm\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellMarkersLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wspm"] = base + ".wspm";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"markerId", e.markerId},
                {"name", e.name},
                {"description", e.description},
                {"spellId", e.spellId},
                {"groundTexturePath", e.groundTexturePath},
                {"radius", e.radius},
                {"duration", e.duration},
                {"tickIntervalMs", e.tickIntervalMs},
                {"decalColor", e.decalColor},
                {"edgeFadeMode", e.edgeFadeMode},
                {"edgeFadeModeName",
                    edgeFadeModeName(e.edgeFadeMode)},
                {"stackable", e.stackable != 0},
                {"destroyOnCancel", e.destroyOnCancel != 0},
                {"tickSoundId", e.tickSoundId},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPM: %s.wspm\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  markers : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   spell    radius  dur(s)  tick(ms)  fade        stack  dest   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %5u    %5.1f   %5.1f   %5u     %-9s    %s    %s    %s\n",
                    e.markerId, e.spellId, e.radius,
                    e.duration, e.tickIntervalMs,
                    edgeFadeModeName(e.edgeFadeMode),
                    e.stackable ? "yes" : "no ",
                    e.destroyOnCancel ? "yes" : "no ",
                    e.name.c_str());
    }
    return 0;
}

int parseEdgeFadeModeToken(const std::string& s) {
    using S = wowee::pipeline::WoweeSpellMarkers;
    if (s == "hard")     return S::Hard;
    if (s == "softedge") return S::SoftEdge;
    if (s == "pulse")    return S::Pulse;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wspm");
    if (out.empty()) out = base + ".wspm.json";
    if (!wowee::pipeline::WoweeSpellMarkersLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wspm-json: WSPM not found: %s.wspm\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellMarkersLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WSPM";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"markerId", e.markerId},
            {"name", e.name},
            {"description", e.description},
            {"spellId", e.spellId},
            {"groundTexturePath", e.groundTexturePath},
            {"radius", e.radius},
            {"duration", e.duration},
            {"tickIntervalMs", e.tickIntervalMs},
            {"decalColor", e.decalColor},
            {"edgeFadeMode", e.edgeFadeMode},
            {"edgeFadeModeName",
                edgeFadeModeName(e.edgeFadeMode)},
            {"stackable", e.stackable != 0},
            {"destroyOnCancel", e.destroyOnCancel != 0},
            {"tickSoundId", e.tickSoundId},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wspm-json: failed to open %s for write\n",
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
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wspm");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wspm-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wspm-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellMarkers c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wspm-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeSpellMarkers::Entry e;
        e.markerId = je.value("markerId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.spellId = je.value("spellId", 0u);
        e.groundTexturePath = je.value("groundTexturePath",
                                         std::string{});
        e.radius = je.value("radius", 0.0f);
        e.duration = je.value("duration", 0.0f);
        e.tickIntervalMs = je.value("tickIntervalMs", 0u);
        e.decalColor = je.value("decalColor", 0xFFFFFFFFu);
        if (je.contains("edgeFadeMode")) {
            const auto& v = je["edgeFadeMode"];
            if (v.is_string()) {
                int parsed = parseEdgeFadeModeToken(
                    v.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wspm-json: unknown "
                        "edgeFadeMode token '%s' on entry "
                        "id=%u\n",
                        v.get<std::string>().c_str(),
                        e.markerId);
                    return 1;
                }
                e.edgeFadeMode = static_cast<uint8_t>(parsed);
            } else if (v.is_number_integer()) {
                e.edgeFadeMode = static_cast<uint8_t>(
                    v.get<int>());
            }
        } else if (je.contains("edgeFadeModeName") &&
                   je["edgeFadeModeName"].is_string()) {
            int parsed = parseEdgeFadeModeToken(
                je["edgeFadeModeName"].get<std::string>());
            if (parsed >= 0)
                e.edgeFadeMode = static_cast<uint8_t>(parsed);
        }
        if (je.contains("stackable")) {
            const auto& v = je["stackable"];
            if (v.is_boolean())
                e.stackable = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.stackable = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        if (je.contains("destroyOnCancel")) {
            const auto& v = je["destroyOnCancel"];
            if (v.is_boolean())
                e.destroyOnCancel = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.destroyOnCancel = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        e.tickSoundId = je.value("tickSoundId", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeSpellMarkersLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wspm-json: failed to save %s.wspm\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wspm (%zu markers)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspm");
    if (!wowee::pipeline::WoweeSpellMarkersLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wspm: WSPM not found: %s.wspm\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellMarkersLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<uint32_t> spellIdsSeen;
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
        if (e.spellId == 0) {
            errors.push_back(ctx +
                ": spellId is 0 — marker is not bound to "
                "any spell");
        }
        if (e.groundTexturePath.empty()) {
            errors.push_back(ctx +
                ": groundTexturePath is empty — decal "
                "would render as untextured solid color");
        }
        if (e.radius <= 0.0f) {
            errors.push_back(ctx + ": radius " +
                std::to_string(e.radius) +
                " <= 0 — decal would have zero area");
        }
        if (e.radius > 100.0f) {
            warnings.push_back(ctx + ": radius " +
                std::to_string(e.radius) +
                " > 100 yards — covers more than the "
                "average raid arena, verify if intentional");
        }
        if (e.edgeFadeMode > 2) {
            errors.push_back(ctx + ": edgeFadeMode " +
                std::to_string(e.edgeFadeMode) +
                " out of range (must be 0..2)");
        }
        // tickIntervalMs sanity: zero is legal (one-shot
        // burst), but very short intervals would tank
        // performance with many simultaneous markers.
        if (e.tickIntervalMs > 0 && e.tickIntervalMs < 100) {
            warnings.push_back(ctx + ": tickIntervalMs " +
                std::to_string(e.tickIntervalMs) +
                " < 100ms — fires more than 10× per second; "
                "verify performance impact for stackable "
                "markers");
        }
        // Decal alpha=0 = invisible — likely an error.
        uint8_t alpha = (e.decalColor >> 24) & 0xFF;
        if (alpha == 0) {
            warnings.push_back(ctx +
                ": decalColor has alpha=0 — marker would "
                "render fully transparent / invisible");
        }
        // Multiple markers binding the same spellId is
        // ambiguous (which decal does the spell spawn?).
        if (e.spellId != 0 &&
            !spellIdsSeen.insert(e.spellId).second) {
            errors.push_back(ctx +
                ": spellId " + std::to_string(e.spellId) +
                " is already bound by another marker — "
                "spell-cast lookup would be ambiguous");
        }
        if (!idsSeen.insert(e.markerId).second) {
            errors.push_back(ctx + ": duplicate markerId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wspm", base, errors, warnings);
    std::printf("validate-wspm: %s.wspm\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu markers, all markerIds + "
                    "spellIds unique\n", c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellMarkersCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-spm") == 0 && i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spm-raid") == 0 && i + 1 < argc) {
        outRc = handleGenRaid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spm-env") == 0 && i + 1 < argc) {
        outRc = handleGenEnvironment(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wspm") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wspm") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wspm-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wspm-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
