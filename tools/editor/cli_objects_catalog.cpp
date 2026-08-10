#include "cli_objects_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_objects.hpp"
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

void appendObjFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeGameObject::Disabled)        s += "disabled ";
    if (flags & wowee::pipeline::WoweeGameObject::ScriptOnly)      s += "script-only ";
    if (flags & wowee::pipeline::WoweeGameObject::UsableFromMount) s += "from-mount ";
    if (flags & wowee::pipeline::WoweeGameObject::Despawn)         s += "despawn ";
    if (flags & wowee::pipeline::WoweeGameObject::Frozen)          s += "frozen ";
    if (flags & wowee::pipeline::WoweeGameObject::QuestGated)      s += "quest-gated ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

bool saveOrError(const wowee::pipeline::WoweeGameObject& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeGameObjectLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wgot\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeGameObject& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgot\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  objects : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterObjects";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgot");
    auto c = wowee::pipeline::WoweeGameObjectLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-objects")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDungeon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DungeonObjects";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgot");
    auto c = wowee::pipeline::WoweeGameObjectLoader::makeDungeon(name);
    if (!saveOrError(c, base, "gen-objects-dungeon")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenGather(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "GatheringNodes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgot");
    auto c = wowee::pipeline::WoweeGameObjectLoader::makeGather(name);
    if (!saveOrError(c, base, "gen-objects-gather")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgot");
    if (!wowee::pipeline::WoweeGameObjectLoader::exists(base)) {
        std::fprintf(stderr, "WGOT not found: %s.wgot\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGameObjectLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgot"] = base + ".wgot";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendObjFlagsStr(fs, e.flags);
            arr.push_back({
                {"objectId", e.objectId},
                {"displayId", e.displayId},
                {"name", e.name},
                {"typeId", e.typeId},
                {"typeName", wowee::pipeline::WoweeGameObject::typeName(e.typeId)},
                {"size", e.size},
                {"castBarCaption", e.castBarCaption},
                {"requiredSkill", e.requiredSkill},
                {"requiredSkillValue", e.requiredSkillValue},
                {"lockId", e.lockId},
                {"lootTableId", e.lootTableId},
                {"minOpenTimeMs", e.minOpenTimeMs},
                {"maxOpenTimeMs", e.maxOpenTimeMs},
                {"flags", e.flags},
                {"flagsStr", fs},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGOT: %s.wgot\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  objects : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   type           lock  loot   skill            name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-13s  %4u  %4u   %4u/%-3u    %s\n",
                    e.objectId,
                    wowee::pipeline::WoweeGameObject::typeName(e.typeId),
                    e.lockId, e.lootTableId,
                    e.requiredSkill, e.requiredSkillValue,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each object emits all 13 scalar fields
    // plus dual int + name forms for typeId and flags.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wgot");
    if (outPath.empty()) outPath = base + ".wgot.json";
    if (!wowee::pipeline::WoweeGameObjectLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wgot-json: WGOT not found: %s.wgot\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGameObjectLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["objectId"] = e.objectId;
        je["displayId"] = e.displayId;
        je["name"] = e.name;
        je["typeId"] = e.typeId;
        je["typeName"] = wowee::pipeline::WoweeGameObject::typeName(e.typeId);
        je["size"] = e.size;
        je["castBarCaption"] = e.castBarCaption;
        je["requiredSkill"] = e.requiredSkill;
        je["requiredSkillValue"] = e.requiredSkillValue;
        je["lockId"] = e.lockId;
        je["lootTableId"] = e.lootTableId;
        je["minOpenTimeMs"] = e.minOpenTimeMs;
        je["maxOpenTimeMs"] = e.maxOpenTimeMs;
        je["flags"] = e.flags;
        nlohmann::json fa = nlohmann::json::array();
        if (e.flags & wowee::pipeline::WoweeGameObject::Disabled)        fa.push_back("disabled");
        if (e.flags & wowee::pipeline::WoweeGameObject::ScriptOnly)      fa.push_back("script-only");
        if (e.flags & wowee::pipeline::WoweeGameObject::UsableFromMount) fa.push_back("from-mount");
        if (e.flags & wowee::pipeline::WoweeGameObject::Despawn)         fa.push_back("despawn");
        if (e.flags & wowee::pipeline::WoweeGameObject::Frozen)          fa.push_back("frozen");
        if (e.flags & wowee::pipeline::WoweeGameObject::QuestGated)      fa.push_back("quest-gated");
        je["flagsList"] = fa;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wgot-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wgot\n", base.c_str());
    std::printf("  objects : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wgot");
    outBase = cli::withoutExt(outBase, ".wgot");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wgot-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wgot-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto typeFromName = [](const std::string& s) -> uint8_t {
        if (s == "door")          return wowee::pipeline::WoweeGameObject::Door;
        if (s == "button")        return wowee::pipeline::WoweeGameObject::Button;
        if (s == "chest")         return wowee::pipeline::WoweeGameObject::Chest;
        if (s == "container")     return wowee::pipeline::WoweeGameObject::Container;
        if (s == "quest-giver")   return wowee::pipeline::WoweeGameObject::QuestGiver;
        if (s == "text")          return wowee::pipeline::WoweeGameObject::Text;
        if (s == "trap")          return wowee::pipeline::WoweeGameObject::Trap;
        if (s == "goober")        return wowee::pipeline::WoweeGameObject::Goober;
        if (s == "transport")     return wowee::pipeline::WoweeGameObject::Transport;
        if (s == "mailbox")       return wowee::pipeline::WoweeGameObject::Mailbox;
        if (s == "ore-node")      return wowee::pipeline::WoweeGameObject::MineralNode;
        if (s == "herb-node")     return wowee::pipeline::WoweeGameObject::HerbNode;
        if (s == "fishing-node")  return wowee::pipeline::WoweeGameObject::FishingNode;
        if (s == "mount")         return wowee::pipeline::WoweeGameObject::Mount;
        if (s == "sign")          return wowee::pipeline::WoweeGameObject::Sign;
        if (s == "bonfire")       return wowee::pipeline::WoweeGameObject::Bonfire;
        return wowee::pipeline::WoweeGameObject::Goober;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "disabled")    return wowee::pipeline::WoweeGameObject::Disabled;
        if (s == "script-only") return wowee::pipeline::WoweeGameObject::ScriptOnly;
        if (s == "from-mount")  return wowee::pipeline::WoweeGameObject::UsableFromMount;
        if (s == "despawn")     return wowee::pipeline::WoweeGameObject::Despawn;
        if (s == "frozen")      return wowee::pipeline::WoweeGameObject::Frozen;
        if (s == "quest-gated") return wowee::pipeline::WoweeGameObject::QuestGated;
        return 0;
    };
    wowee::pipeline::WoweeGameObject c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeGameObject::Entry e;
            e.objectId = je.value("objectId", 0u);
            e.displayId = je.value("displayId", 0u);
            e.name = je.value("name", std::string{});
            if (je.contains("typeId") && je["typeId"].is_number_integer()) {
                e.typeId = static_cast<uint8_t>(je["typeId"].get<int>());
            } else if (je.contains("typeName") && je["typeName"].is_string()) {
                e.typeId = typeFromName(je["typeName"].get<std::string>());
            }
            e.size = je.value("size", 1.0f);
            e.castBarCaption = je.value("castBarCaption", std::string{});
            e.requiredSkill = je.value("requiredSkill", 0u);
            e.requiredSkillValue = je.value("requiredSkillValue", 0u);
            e.lockId = je.value("lockId", 0u);
            e.lootTableId = je.value("lootTableId", 0u);
            e.minOpenTimeMs = je.value("minOpenTimeMs", 0u);
            e.maxOpenTimeMs = je.value("maxOpenTimeMs", 0u);
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= flagFromName(f.get<std::string>());
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeGameObjectLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgot-json: failed to save %s.wgot\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgot\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  objects : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgot");
    if (!wowee::pipeline::WoweeGameObjectLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wgot: WGOT not found: %s.wgot\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGameObjectLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    idsSeen.reserve(c.entries.size());
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.objectId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.objectId == 0) {
            errors.push_back(ctx + ": objectId is 0");
        }
        if (e.size <= 0.0f) {
            errors.push_back(ctx + ": size <= 0");
        }
        if (e.minOpenTimeMs > e.maxOpenTimeMs) {
            errors.push_back(ctx + ": minOpenTimeMs > maxOpenTimeMs");
        }
        // Gathering nodes need a skill requirement to be useful.
        if ((e.typeId == wowee::pipeline::WoweeGameObject::HerbNode ||
             e.typeId == wowee::pipeline::WoweeGameObject::MineralNode ||
             e.typeId == wowee::pipeline::WoweeGameObject::FishingNode) &&
            e.requiredSkill == 0) {
            warnings.push_back(ctx +
                ": gathering node has no required skill (anyone can harvest)");
        }
        // Chest with no loot table is rare but possible (event-spawn
        // chests fill via script).
        if (e.typeId == wowee::pipeline::WoweeGameObject::Chest &&
            e.lootTableId == 0) {
            warnings.push_back(ctx +
                ": chest has no lootTableId (script must populate)");
        }
        // requiredSkillValue without requiredSkill is incoherent.
        if (e.requiredSkill == 0 && e.requiredSkillValue > 0) {
            errors.push_back(ctx +
                ": requiredSkillValue > 0 but requiredSkill is 0");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.objectId) {
                errors.push_back(ctx + ": duplicate objectId");
                break;
            }
        }
        idsSeen.push_back(e.objectId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wgot", base, errors, warnings);
    std::printf("validate-wgot: %s.wgot\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu objects, all objectIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleObjectsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-objects") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-objects-dungeon") == 0 && i + 1 < argc) {
        outRc = handleGenDungeon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-objects-gather") == 0 && i + 1 < argc) {
        outRc = handleGenGather(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgot") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgot") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgot-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgot-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
