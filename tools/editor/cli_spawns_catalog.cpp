#include "cli_spawns_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spawns.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeSpawns& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpawnsLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wspn\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpawns& c,
                     const std::string& base) {
    std::printf("Wrote %s.wspn\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu (creature=%u object=%u doodad=%u)\n",
                c.entries.size(),
                c.countByKind(wowee::pipeline::WoweeSpawns::Creature),
                c.countByKind(wowee::pipeline::WoweeSpawns::GameObject),
                c.countByKind(wowee::pipeline::WoweeSpawns::Doodad));
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterSpawns";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspn");
    auto c = wowee::pipeline::WoweeSpawnsLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-spawns")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCamp(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BanditCamp";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspn");
    auto c = wowee::pipeline::WoweeSpawnsLoader::makeCamp(name);
    if (!saveOrError(c, base, "gen-spawns-camp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenVillage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "VillageSpawns";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspn");
    auto c = wowee::pipeline::WoweeSpawnsLoader::makeVillage(name);
    if (!saveOrError(c, base, "gen-spawns-village")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspn");
    if (!wowee::pipeline::WoweeSpawnsLoader::exists(base)) {
        std::fprintf(stderr, "WSPN not found: %s.wspn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpawnsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wspn"] = base + ".wspn";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        j["countCreature"] =
            c.countByKind(wowee::pipeline::WoweeSpawns::Creature);
        j["countObject"] =
            c.countByKind(wowee::pipeline::WoweeSpawns::GameObject);
        j["countDoodad"] =
            c.countByKind(wowee::pipeline::WoweeSpawns::Doodad);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"kind", e.kind},
                {"kindName", wowee::pipeline::WoweeSpawns::kindName(e.kind)},
                {"entryId", e.entryId},
                {"position", {e.position.x, e.position.y, e.position.z}},
                {"rotation", {e.rotation.x, e.rotation.y, e.rotation.z}},
                {"scale", e.scale},
                {"flags", e.flags},
                {"respawnSec", e.respawnSec},
                {"factionId", e.factionId},
                {"questIdRequired", e.questIdRequired},
                {"wanderRadius", e.wanderRadius},
                {"label", e.label},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPN: %s.wspn\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu (creature=%u object=%u doodad=%u)\n",
                c.entries.size(),
                c.countByKind(wowee::pipeline::WoweeSpawns::Creature),
                c.countByKind(wowee::pipeline::WoweeSpawns::GameObject),
                c.countByKind(wowee::pipeline::WoweeSpawns::Doodad));
    if (c.entries.empty()) return 0;
    std::printf("    kind     entry  pos (x, y, z)        respawn  rad  label\n");
    for (const auto& e : c.entries) {
        std::printf("  %-9s %5u  (%6.1f,%6.1f,%6.1f)  %5us  %4.1f  %s\n",
                    wowee::pipeline::WoweeSpawns::kindName(e.kind),
                    e.entryId,
                    e.position.x, e.position.y, e.position.z,
                    e.respawnSec, e.wanderRadius,
                    e.label.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Export a .wspn to a human-editable JSON sidecar.
    // Mirrors the WOL/WOW/WOMX/WSND JSON pairs — gives a
    // quick-author surface for hand-editing spawn entries
    // without writing a binary patcher. Vector fields are
    // emitted as 3-element arrays; kind and flags both have
    // dual int + string-array forms so the importer accepts
    // either.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wspn");
    if (outPath.empty()) outPath = base + ".wspn.json";
    if (!wowee::pipeline::WoweeSpawnsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wspn-json: WSPN not found: %s.wspn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpawnsLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["kind"] = e.kind;
        je["kindName"] = wowee::pipeline::WoweeSpawns::kindName(e.kind);
        je["entryId"] = e.entryId;
        je["position"] = {e.position.x, e.position.y, e.position.z};
        je["rotation"] = {e.rotation.x, e.rotation.y, e.rotation.z};
        je["scale"] = e.scale;
        je["flags"] = e.flags;
        nlohmann::json fa = nlohmann::json::array();
        if (e.flags & wowee::pipeline::WoweeSpawns::Disabled)
            fa.push_back("disabled");
        if (e.flags & wowee::pipeline::WoweeSpawns::EventOnly)
            fa.push_back("event-only");
        if (e.flags & wowee::pipeline::WoweeSpawns::QuestPhased)
            fa.push_back("quest-phased");
        je["flagsList"] = fa;
        je["respawnSec"] = e.respawnSec;
        je["factionId"] = e.factionId;
        je["questIdRequired"] = e.questIdRequired;
        je["wanderRadius"] = e.wanderRadius;
        je["label"] = e.label;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wspn-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wspn\n", base.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wspn.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wspn");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wspn-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wspn-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "creature") return wowee::pipeline::WoweeSpawns::Creature;
        if (s == "object")   return wowee::pipeline::WoweeSpawns::GameObject;
        if (s == "doodad")   return wowee::pipeline::WoweeSpawns::Doodad;
        return wowee::pipeline::WoweeSpawns::Creature;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "disabled")     return wowee::pipeline::WoweeSpawns::Disabled;
        if (s == "event-only")   return wowee::pipeline::WoweeSpawns::EventOnly;
        if (s == "quest-phased") return wowee::pipeline::WoweeSpawns::QuestPhased;
        return 0;
    };
    auto readVec3 = [](const nlohmann::json& jv, glm::vec3& v) {
        if (jv.is_array() && jv.size() >= 3) {
            v.x = jv[0].get<float>();
            v.y = jv[1].get<float>();
            v.z = jv[2].get<float>();
        }
    };
    wowee::pipeline::WoweeSpawns c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpawns::Entry e;
            if (je.contains("kind") && je["kind"].is_number_integer()) {
                e.kind = static_cast<uint8_t>(je["kind"].get<int>());
            } else if (je.contains("kindName") && je["kindName"].is_string()) {
                e.kind = kindFromName(je["kindName"].get<std::string>());
            }
            e.entryId = je.value("entryId", 0u);
            if (je.contains("position")) readVec3(je["position"], e.position);
            if (je.contains("rotation")) readVec3(je["rotation"], e.rotation);
            e.scale = je.value("scale", 1.0f);
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= flagFromName(f.get<std::string>());
                }
            }
            e.respawnSec = je.value("respawnSec", 0u);
            e.factionId = je.value("factionId", 0u);
            e.questIdRequired = je.value("questIdRequired", 0u);
            e.wanderRadius = je.value("wanderRadius", 0.0f);
            e.label = je.value("label", std::string{});
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeSpawnsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wspn-json: failed to save %s.wspn\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wspn\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspn");
    if (!wowee::pipeline::WoweeSpawnsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wspn: WSPN not found: %s.wspn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpawnsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k);
        if (!e.label.empty()) ctx += " (" + e.label + ")";
        if (e.kind > wowee::pipeline::WoweeSpawns::Doodad) {
            errors.push_back(ctx + ": kind " + std::to_string(e.kind) +
                             " not in known range 0..2");
        }
        if (!std::isfinite(e.position.x) ||
            !std::isfinite(e.position.y) ||
            !std::isfinite(e.position.z) ||
            !std::isfinite(e.rotation.x) ||
            !std::isfinite(e.rotation.y) ||
            !std::isfinite(e.rotation.z)) {
            errors.push_back(ctx + ": position/rotation not finite");
        }
        if (!std::isfinite(e.scale) || e.scale <= 0) {
            errors.push_back(ctx + ": scale not finite or <= 0");
        }
        if (!std::isfinite(e.wanderRadius) || e.wanderRadius < 0) {
            errors.push_back(ctx + ": wanderRadius not finite or < 0");
        }
        // Doodads should not have a respawn timer (they are
        // permanent visual props). Catch the common misuse.
        if (e.kind == wowee::pipeline::WoweeSpawns::Doodad &&
            e.respawnSec != 0) {
            warnings.push_back(ctx +
                ": doodad has non-zero respawnSec — doodads are static");
        }
        // Creatures with respawn 0 will spawn once and never
        // come back; flag as a warning since it's almost
        // always a mistake.
        if (e.kind == wowee::pipeline::WoweeSpawns::Creature &&
            e.respawnSec == 0 &&
            !(e.flags & wowee::pipeline::WoweeSpawns::EventOnly)) {
            warnings.push_back(ctx +
                ": creature with respawnSec=0 will not respawn after kill");
        }
        if (e.entryId == 0) {
            warnings.push_back(ctx +
                ": entryId is 0 (no template referenced)");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wspn", base, errors, warnings);
    std::printf("validate-wspn: %s.wspn\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu entries (creature=%u object=%u doodad=%u)\n",
                    c.entries.size(),
                    c.countByKind(wowee::pipeline::WoweeSpawns::Creature),
                    c.countByKind(wowee::pipeline::WoweeSpawns::GameObject),
                    c.countByKind(wowee::pipeline::WoweeSpawns::Doodad));
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpawnsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-spawns") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spawns-camp") == 0 && i + 1 < argc) {
        outRc = handleGenCamp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spawns-village") == 0 && i + 1 < argc) {
        outRc = handleGenVillage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wspn") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wspn") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wspn-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wspn-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
