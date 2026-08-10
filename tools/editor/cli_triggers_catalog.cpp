#include "cli_triggers_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_triggers.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
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

bool saveOrError(const wowee::pipeline::WoweeTrigger& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeTriggerLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wtrg\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeTrigger& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtrg\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  triggers : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterTriggers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtrg");
    auto c = wowee::pipeline::WoweeTriggerLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-triggers")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDungeon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DungeonTriggers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtrg");
    auto c = wowee::pipeline::WoweeTriggerLoader::makeDungeon(name);
    if (!saveOrError(c, base, "gen-triggers-dungeon")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFlightPath(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FlightPathTriggers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtrg");
    auto c = wowee::pipeline::WoweeTriggerLoader::makeFlightPath(name);
    if (!saveOrError(c, base, "gen-triggers-flightpath")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtrg");
    if (!wowee::pipeline::WoweeTriggerLoader::exists(base)) {
        std::fprintf(stderr, "WTRG not found: %s.wtrg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTriggerLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtrg"] = base + ".wtrg";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"triggerId", e.triggerId},
                {"mapId", e.mapId},
                {"areaId", e.areaId},
                {"name", e.name},
                {"center", {e.center.x, e.center.y, e.center.z}},
                {"shape", e.shape},
                {"shapeName", wowee::pipeline::WoweeTrigger::shapeName(e.shape)},
                {"kind", e.kind},
                {"kindName", wowee::pipeline::WoweeTrigger::kindName(e.kind)},
                {"boxDims", {e.boxDims.x, e.boxDims.y, e.boxDims.z}},
                {"radius", e.radius},
                {"actionTarget", e.actionTarget},
                {"dest", {e.dest.x, e.dest.y, e.dest.z}},
                {"destOrientation", e.destOrientation},
                {"requiredQuestId", e.requiredQuestId},
                {"requiredItemId", e.requiredItemId},
                {"minLevel", e.minLevel},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTRG: %s.wtrg\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  triggers : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::printf("\n  triggerId=%u  map=%u  area=%u  kind=%s  shape=%s\n",
                    e.triggerId, e.mapId, e.areaId,
                    wowee::pipeline::WoweeTrigger::kindName(e.kind),
                    wowee::pipeline::WoweeTrigger::shapeName(e.shape));
        std::printf("    name      : %s\n", e.name.c_str());
        std::printf("    center    : (%.1f, %.1f, %.1f)\n",
                    e.center.x, e.center.y, e.center.z);
        if (e.shape == wowee::pipeline::WoweeTrigger::ShapeBox) {
            std::printf("    dims (h)  : (%.1f, %.1f, %.1f)\n",
                        e.boxDims.x, e.boxDims.y, e.boxDims.z);
        } else {
            std::printf("    radius    : %.1f\n", e.radius);
        }
        if (e.actionTarget != 0) {
            std::printf("    target    : %u\n", e.actionTarget);
        }
        if (e.kind == wowee::pipeline::WoweeTrigger::KindTeleport ||
            e.kind == wowee::pipeline::WoweeTrigger::KindInstanceEntrance) {
            std::printf("    dest      : (%.1f, %.1f, %.1f)  facing=%.2f rad\n",
                        e.dest.x, e.dest.y, e.dest.z, e.destOrientation);
        }
        if (e.requiredQuestId || e.requiredItemId || e.minLevel) {
            std::printf("    gates     :");
            if (e.requiredQuestId) std::printf(" quest=%u", e.requiredQuestId);
            if (e.requiredItemId)  std::printf(" key=%u",   e.requiredItemId);
            if (e.minLevel)        std::printf(" lvl>=%u",  e.minLevel);
            std::printf("\n");
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Vec3 fields (center / boxDims / dest)
    // become 3-element JSON arrays. Shape and kind emit dual
    // int + name forms.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wtrg");
    if (outPath.empty()) outPath = base + ".wtrg.json";
    if (!wowee::pipeline::WoweeTriggerLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtrg-json: WTRG not found: %s.wtrg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTriggerLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"triggerId", e.triggerId},
            {"mapId", e.mapId},
            {"areaId", e.areaId},
            {"name", e.name},
            {"center", {e.center.x, e.center.y, e.center.z}},
            {"shape", e.shape},
            {"shapeName", wowee::pipeline::WoweeTrigger::shapeName(e.shape)},
            {"kind", e.kind},
            {"kindName", wowee::pipeline::WoweeTrigger::kindName(e.kind)},
            {"boxDims", {e.boxDims.x, e.boxDims.y, e.boxDims.z}},
            {"radius", e.radius},
            {"actionTarget", e.actionTarget},
            {"dest", {e.dest.x, e.dest.y, e.dest.z}},
            {"destOrientation", e.destOrientation},
            {"requiredQuestId", e.requiredQuestId},
            {"requiredItemId", e.requiredItemId},
            {"minLevel", e.minLevel},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wtrg-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.wtrg\n", base.c_str());
    std::printf("  triggers : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wtrg");
    outBase = cli::withoutExt(outBase, ".wtrg");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wtrg-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wtrg-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto shapeFromName = [](const std::string& s) -> uint8_t {
        if (s == "box")    return wowee::pipeline::WoweeTrigger::ShapeBox;
        if (s == "sphere") return wowee::pipeline::WoweeTrigger::ShapeSphere;
        return wowee::pipeline::WoweeTrigger::ShapeBox;
    };
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "teleport")      return wowee::pipeline::WoweeTrigger::KindTeleport;
        if (s == "quest-explore") return wowee::pipeline::WoweeTrigger::KindQuestExploration;
        if (s == "script")        return wowee::pipeline::WoweeTrigger::KindScript;
        if (s == "instance")      return wowee::pipeline::WoweeTrigger::KindInstanceEntrance;
        if (s == "area-name")     return wowee::pipeline::WoweeTrigger::KindAreaName;
        if (s == "pvp-zone")      return wowee::pipeline::WoweeTrigger::KindCombatStartZone;
        if (s == "waypoint")      return wowee::pipeline::WoweeTrigger::KindWaypoint;
        return wowee::pipeline::WoweeTrigger::KindAreaName;
    };
    auto readVec3 = [](const nlohmann::json& jv, glm::vec3& v) {
        if (jv.is_array() && jv.size() >= 3) {
            v.x = jv[0].get<float>();
            v.y = jv[1].get<float>();
            v.z = jv[2].get<float>();
        }
    };
    wowee::pipeline::WoweeTrigger c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeTrigger::Entry e;
            e.triggerId = je.value("triggerId", 0u);
            e.mapId = je.value("mapId", 0u);
            e.areaId = je.value("areaId", 0u);
            e.name = je.value("name", std::string{});
            if (je.contains("center")) readVec3(je["center"], e.center);
            if (je.contains("shape") && je["shape"].is_number_integer()) {
                e.shape = static_cast<uint8_t>(je["shape"].get<int>());
            } else if (je.contains("shapeName") && je["shapeName"].is_string()) {
                e.shape = shapeFromName(je["shapeName"].get<std::string>());
            }
            if (je.contains("kind") && je["kind"].is_number_integer()) {
                e.kind = static_cast<uint8_t>(je["kind"].get<int>());
            } else if (je.contains("kindName") && je["kindName"].is_string()) {
                e.kind = kindFromName(je["kindName"].get<std::string>());
            }
            if (je.contains("boxDims")) readVec3(je["boxDims"], e.boxDims);
            e.radius = je.value("radius", 0.0f);
            e.actionTarget = je.value("actionTarget", 0u);
            if (je.contains("dest")) readVec3(je["dest"], e.dest);
            e.destOrientation = je.value("destOrientation", 0.0f);
            e.requiredQuestId = je.value("requiredQuestId", 0u);
            e.requiredItemId = je.value("requiredItemId", 0u);
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 0));
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeTriggerLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtrg-json: failed to save %s.wtrg\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtrg\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  triggers : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtrg");
    if (!wowee::pipeline::WoweeTriggerLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtrg: WTRG not found: %s.wtrg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTriggerLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.triggerId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.triggerId == 0) errors.push_back(ctx + ": triggerId is 0");
        if (e.shape > wowee::pipeline::WoweeTrigger::ShapeSphere) {
            errors.push_back(ctx + ": shape " +
                std::to_string(e.shape) + " not in 0..1");
        }
        if (e.kind > wowee::pipeline::WoweeTrigger::KindWaypoint) {
            errors.push_back(ctx + ": kind " +
                std::to_string(e.kind) + " not in 0..6");
        }
        if (!std::isfinite(e.center.x) ||
            !std::isfinite(e.center.y) ||
            !std::isfinite(e.center.z)) {
            errors.push_back(ctx + ": center not finite");
        }
        // Sphere needs positive radius; box needs at least one
        // positive half-extent.
        if (e.shape == wowee::pipeline::WoweeTrigger::ShapeSphere) {
            if (!std::isfinite(e.radius) || e.radius <= 0) {
                errors.push_back(ctx +
                    ": sphere shape requires positive radius");
            }
        } else {
            if (e.boxDims.x <= 0 && e.boxDims.y <= 0 && e.boxDims.z <= 0) {
                errors.push_back(ctx +
                    ": box shape has all-zero half-extents");
            }
        }
        // Teleport / InstanceEntrance must have a destination.
        if (e.kind == wowee::pipeline::WoweeTrigger::KindTeleport ||
            e.kind == wowee::pipeline::WoweeTrigger::KindInstanceEntrance) {
            if (e.dest.x == 0 && e.dest.y == 0 && e.dest.z == 0) {
                warnings.push_back(ctx +
                    ": teleport / instance trigger has dest=(0,0,0)");
            }
        }
        // Quest exploration must reference a quest id.
        if (e.kind == wowee::pipeline::WoweeTrigger::KindQuestExploration &&
            e.actionTarget == 0) {
            errors.push_back(ctx +
                ": KindQuestExploration requires actionTarget=questId");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.triggerId) {
                errors.push_back(ctx + ": duplicate triggerId");
                break;
            }
        }
        idsSeen.push_back(e.triggerId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wtrg", base, errors, warnings);
    std::printf("validate-wtrg: %s.wtrg\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu triggers, all triggerIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleTriggersCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-triggers") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-triggers-dungeon") == 0 && i + 1 < argc) {
        outRc = handleGenDungeon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-triggers-flightpath") == 0 && i + 1 < argc) {
        outRc = handleGenFlightPath(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtrg") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtrg") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtrg-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtrg-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
