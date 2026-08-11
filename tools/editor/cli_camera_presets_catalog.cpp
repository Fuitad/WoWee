#include "cli_camera_presets_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_camera_presets.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
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

const char* purposeKindName(uint8_t k) {
    using C = wowee::pipeline::WoweeCameraPresets;
    switch (k) {
        case C::Cinematic: return "cinematic";
        case C::Combat:    return "combat";
        case C::Mounted:   return "mounted";
        case C::Vehicle:   return "vehicle";
        case C::Cutscene:  return "cutscene";
        case C::PhotoMode: return "photomode";
        default:           return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeCameraPresets& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcam\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  presets : %zu\n", c.entries.size());
}

int handleGenCombat(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CombatCameraPresets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcam");
    auto c = wowee::pipeline::WoweeCameraPresetsLoader::
        makeCombatPresets(name);
    if (!saveOrError<wowee::pipeline::WoweeCameraPresetsLoader>(c, base, "gen-cam-combat", ".wcam")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMounted(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MountedCameraPresets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcam");
    auto c = wowee::pipeline::WoweeCameraPresetsLoader::
        makeMountedPresets(name);
    if (!saveOrError<wowee::pipeline::WoweeCameraPresetsLoader>(c, base, "gen-cam-mounted", ".wcam")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCinematic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CinematicCameraPresets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcam");
    auto c = wowee::pipeline::WoweeCameraPresetsLoader::
        makeCinematicPresets(name);
    if (!saveOrError<wowee::pipeline::WoweeCameraPresetsLoader>(c, base, "gen-cam-cinematic", ".wcam")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcam");
    if (!wowee::pipeline::WoweeCameraPresetsLoader::exists(base)) {
        std::fprintf(stderr, "WCAM not found: %s.wcam\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCameraPresetsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcam"] = base + ".wcam";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"presetId", e.presetId},
                {"name", e.name},
                {"purposeKind", e.purposeKind},
                {"purposeKindName",
                    purposeKindName(e.purposeKind)},
                {"motionDamping", e.motionDamping},
                {"fovDegrees", e.fovDegrees},
                {"distanceFromTarget",
                    e.distanceFromTarget},
                {"pitchDegrees", e.pitchDegrees},
                {"yawOffsetDegrees", e.yawOffsetDegrees},
                {"shoulderOffsetMeters",
                    e.shoulderOffsetMeters},
                {"focusBoneId", e.focusBoneId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCAM: %s.wcam\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  presets : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  purpose      damp   FOV    dist    pitch    yaw  shoulder  bone   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-10s   %3u  %5.1f  %6.2f  %+6.1f  %+5.1f  %+8.2f  %4u   %s\n",
                    e.presetId,
                    purposeKindName(e.purposeKind),
                    e.motionDamping,
                    e.fovDegrees, e.distanceFromTarget,
                    e.pitchDegrees, e.yawOffsetDegrees,
                    e.shoulderOffsetMeters,
                    e.focusBoneId, e.name.c_str());
    }
    return 0;
}

int parsePurposeKindToken(const std::string& s) {
    using C = wowee::pipeline::WoweeCameraPresets;
    if (s == "cinematic") return C::Cinematic;
    if (s == "combat")    return C::Combat;
    if (s == "mounted")   return C::Mounted;
    if (s == "vehicle")   return C::Vehicle;
    if (s == "cutscene")  return C::Cutscene;
    if (s == "photomode") return C::PhotoMode;
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
                    "import-wcam-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wcam");
    if (!wowee::pipeline::WoweeCameraPresetsLoader::exists(base)) {
        return reportMissing("validate-wcam", "WCAM", base, ".wcam");
    }
    auto c = wowee::pipeline::WoweeCameraPresetsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.presetId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.presetId == 0)
            errors.push_back(ctx + ": presetId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.purposeKind > 5) {
            errors.push_back(ctx + ": purposeKind " +
                std::to_string(e.purposeKind) +
                " out of range (0..5)");
        }
        // FOV must be in (0..180). Negative or zero
        // FOV makes no sense; >= 180 inverts the view
        // frustum.
        if (e.fovDegrees <= 0.f || e.fovDegrees >= 180.f) {
            errors.push_back(ctx + ": fovDegrees=" +
                std::to_string(e.fovDegrees) +
                " out of range (must be in (0, 180))");
        }
        // FOV outside human-comfort range warns. <30
        // = extreme telephoto; >120 = fish-eye that
        // disorients players.
        if (e.fovDegrees > 0.f &&
            (e.fovDegrees < 30.f || e.fovDegrees > 120.f)) {
            warnings.push_back(ctx + ": fovDegrees=" +
                std::to_string(e.fovDegrees) +
                " outside 30..120 player-comfort range "
                "— may cause motion sickness or "
                "extreme telephoto compression");
        }
        // Negative distance places camera in front of
        // target — almost certainly a typo.
        if (e.distanceFromTarget < 0.f) {
            errors.push_back(ctx +
                ": distanceFromTarget=" +
                std::to_string(e.distanceFromTarget) +
                " is negative — camera would render in "
                "front of target");
        }
        // Very small distance (< 0.5m) clips into the
        // model — warn, since some shots want this
        // (extreme close-up portraits).
        if (e.distanceFromTarget > 0.f &&
            e.distanceFromTarget < 0.5f) {
            warnings.push_back(ctx +
                ": distanceFromTarget=" +
                std::to_string(e.distanceFromTarget) +
                " under 0.5m — likely clips into the "
                "model; verify intentional");
        }
        // Pitch outside ±89° gimbal-locks the camera.
        if (e.pitchDegrees < -89.f ||
            e.pitchDegrees > 89.f) {
            errors.push_back(ctx + ": pitchDegrees=" +
                std::to_string(e.pitchDegrees) +
                " gimbal-locks the camera (must be "
                "within (-89, +89))");
        }
        // Yaw beyond ±180° is degenerate (wraps).
        if (std::fabs(e.yawOffsetDegrees) > 180.f) {
            warnings.push_back(ctx +
                ": yawOffsetDegrees=" +
                std::to_string(e.yawOffsetDegrees) +
                " beyond ±180° — wraps to a smaller "
                "equivalent angle, simplify");
        }
        if (!idsSeen.insert(e.presetId).second) {
            errors.push_back(ctx + ": duplicate presetId");
        }
    }
    return cli::reportValidation("wcam", base, jsonOut, errors, warnings,
                                 formatted("%zu presets, all presetIds "
                    "unique, purposeKind 0..5, FOV in "
                    "(0,180), distanceFromTarget >= 0, "
                    "pitch within (-89, +89)", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcam");
    if (out.empty()) out = base + ".wcam.json";
    if (!wowee::pipeline::WoweeCameraPresetsLoader::exists(base)) {
        return reportMissing("export-wcam-json", "WCAM", base, ".wcam");
    }
    auto c = wowee::pipeline::WoweeCameraPresetsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCAM";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"presetId", e.presetId},
            {"name", e.name},
            {"purposeKind", e.purposeKind},
            {"purposeKindName",
                purposeKindName(e.purposeKind)},
            {"motionDamping", e.motionDamping},
            {"fovDegrees", e.fovDegrees},
            {"distanceFromTarget", e.distanceFromTarget},
            {"pitchDegrees", e.pitchDegrees},
            {"yawOffsetDegrees", e.yawOffsetDegrees},
            {"shoulderOffsetMeters",
                e.shoulderOffsetMeters},
            {"focusBoneId", e.focusBoneId},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcam-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu presets)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcam");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcam-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcam-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCameraPresets c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcam-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeCameraPresets::Entry e;
        e.presetId = je.value("presetId", 0u);
        e.name = je.value("name", std::string{});
        if (!readEnumField(je, "purposeKind",
                            "purposeKindName",
                            parsePurposeKindToken,
                            "purposeKind", e.presetId,
                            e.purposeKind)) return 1;
        e.motionDamping = static_cast<uint8_t>(
            je.value("motionDamping", 0));
        e.fovDegrees = je.value("fovDegrees", 0.f);
        e.distanceFromTarget =
            je.value("distanceFromTarget", 0.f);
        e.pitchDegrees = je.value("pitchDegrees", 0.f);
        e.yawOffsetDegrees =
            je.value("yawOffsetDegrees", 0.f);
        e.shoulderOffsetMeters =
            je.value("shoulderOffsetMeters", 0.f);
        e.focusBoneId = je.value("focusBoneId", 0u);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeCameraPresetsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcam-json: failed to save %s.wcam\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcam (%zu presets)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleCameraPresetsCatalog(int& i, int argc, char** argv,
                                  int& outRc) {
    if (std::strcmp(argv[i], "--gen-cam-combat") == 0 &&
        i + 1 < argc) {
        outRc = handleGenCombat(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cam-mounted") == 0 &&
        i + 1 < argc) {
        outRc = handleGenMounted(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cam-cinematic") == 0 &&
        i + 1 < argc) {
        outRc = handleGenCinematic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcam") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcam") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcam-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcam-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
