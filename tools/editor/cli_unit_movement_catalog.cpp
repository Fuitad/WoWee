#include "cli_unit_movement_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_unit_movement.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeUnitMovement& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeUnitMovementLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wumv\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeUnitMovement& c,
                     const std::string& base) {
    std::printf("Wrote %s.wumv\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  types   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterMovementTypes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wumv");
    auto c = wowee::pipeline::WoweeUnitMovementLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-umv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFlight(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FlightMovementTypes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wumv");
    auto c = wowee::pipeline::WoweeUnitMovementLoader::makeFlight(name);
    if (!saveOrError(c, base, "gen-umv-flight")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBuffs(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MovementBuffs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wumv");
    auto c = wowee::pipeline::WoweeUnitMovementLoader::makeBuffs(name);
    if (!saveOrError(c, base, "gen-umv-buffs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wumv");
    if (!wowee::pipeline::WoweeUnitMovementLoader::exists(base)) {
        std::fprintf(stderr, "WUMV not found: %s.wumv\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeUnitMovementLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wumv"] = base + ".wumv";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"moveTypeId", e.moveTypeId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"movementCategory", e.movementCategory},
                {"movementCategoryName", wowee::pipeline::WoweeUnitMovement::movementCategoryName(e.movementCategory)},
                {"requiresFlight", e.requiresFlight},
                {"canStackBuffs", e.canStackBuffs},
                {"baseSpeed", e.baseSpeed},
                {"baseMultiplier", e.baseMultiplier},
                {"maxMultiplier", e.maxMultiplier},
                {"defaultDurationMs", e.defaultDurationMs},
                {"stackingPriority", e.stackingPriority},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WUMV: %s.wumv\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  types   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    category     baseSpeed  baseMul  maxMul  dur(ms)  prio  flight  stack  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-11s   %7.2f    %5.2f    %5.2f   %5u   %3u    %u       %u    %s\n",
                    e.moveTypeId,
                    wowee::pipeline::WoweeUnitMovement::movementCategoryName(e.movementCategory),
                    e.baseSpeed, e.baseMultiplier, e.maxMultiplier,
                    e.defaultDurationMs, e.stackingPriority,
                    e.requiresFlight, e.canStackBuffs,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wumv");
    if (outPath.empty()) outPath = base + ".wumv.json";
    if (!wowee::pipeline::WoweeUnitMovementLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wumv-json: WUMV not found: %s.wumv\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeUnitMovementLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"moveTypeId", e.moveTypeId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"movementCategory", e.movementCategory},
            {"movementCategoryName", wowee::pipeline::WoweeUnitMovement::movementCategoryName(e.movementCategory)},
            {"requiresFlight", e.requiresFlight},
            {"canStackBuffs", e.canStackBuffs},
            {"baseSpeed", e.baseSpeed},
            {"baseMultiplier", e.baseMultiplier},
            {"maxMultiplier", e.maxMultiplier},
            {"defaultDurationMs", e.defaultDurationMs},
            {"stackingPriority", e.stackingPriority},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wumv-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wumv\n", base.c_str());
    std::printf("  types  : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wumv.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wumv");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wumv-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wumv-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto categoryFromName = [](const std::string& s) -> uint8_t {
        if (s == "walk")        return wowee::pipeline::WoweeUnitMovement::Walk;
        if (s == "run")         return wowee::pipeline::WoweeUnitMovement::Run;
        if (s == "backward")    return wowee::pipeline::WoweeUnitMovement::Backward;
        if (s == "swim")        return wowee::pipeline::WoweeUnitMovement::Swim;
        if (s == "swim-back")   return wowee::pipeline::WoweeUnitMovement::SwimBack;
        if (s == "turn")        return wowee::pipeline::WoweeUnitMovement::Turn;
        if (s == "flight")      return wowee::pipeline::WoweeUnitMovement::Flight;
        if (s == "flight-back") return wowee::pipeline::WoweeUnitMovement::FlightBack;
        if (s == "pitch")       return wowee::pipeline::WoweeUnitMovement::Pitch;
        if (s == "fly")         return wowee::pipeline::WoweeUnitMovement::Fly;
        if (s == "fly-back")    return wowee::pipeline::WoweeUnitMovement::FlyBack;
        if (s == "temp-buff")   return wowee::pipeline::WoweeUnitMovement::TempBuff;
        return wowee::pipeline::WoweeUnitMovement::Run;
    };
    wowee::pipeline::WoweeUnitMovement c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeUnitMovement::Entry e;
            e.moveTypeId = je.value("moveTypeId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("movementCategory") &&
                je["movementCategory"].is_number_integer()) {
                e.movementCategory = static_cast<uint8_t>(
                    je["movementCategory"].get<int>());
            } else if (je.contains("movementCategoryName") &&
                       je["movementCategoryName"].is_string()) {
                e.movementCategory = categoryFromName(
                    je["movementCategoryName"].get<std::string>());
            }
            e.requiresFlight = static_cast<uint8_t>(
                je.value("requiresFlight", 0));
            e.canStackBuffs = static_cast<uint8_t>(
                je.value("canStackBuffs", 1));
            e.baseSpeed = je.value("baseSpeed", 7.0f);
            e.baseMultiplier = je.value("baseMultiplier", 1.0f);
            e.maxMultiplier = je.value("maxMultiplier", 1.4f);
            e.defaultDurationMs = je.value("defaultDurationMs", 0u);
            e.stackingPriority = static_cast<uint8_t>(
                je.value("stackingPriority", 0));
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeUnitMovementLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wumv-json: failed to save %s.wumv\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wumv\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  types  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wumv");
    if (!wowee::pipeline::WoweeUnitMovementLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wumv: WUMV not found: %s.wumv\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeUnitMovementLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.moveTypeId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.moveTypeId == 0)
            errors.push_back(ctx + ": moveTypeId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.movementCategory > wowee::pipeline::WoweeUnitMovement::TempBuff) {
            errors.push_back(ctx + ": movementCategory " +
                std::to_string(e.movementCategory) + " not in 0..11");
        }
        if (e.baseMultiplier <= 0.0f) {
            errors.push_back(ctx +
                ": baseMultiplier " +
                std::to_string(e.baseMultiplier) +
                " <= 0 (would freeze unit in place)");
        }
        if (e.maxMultiplier < e.baseMultiplier) {
            errors.push_back(ctx + ": maxMultiplier " +
                std::to_string(e.maxMultiplier) +
                " < baseMultiplier " +
                std::to_string(e.baseMultiplier) +
                " (cap below floor — base would be clamped down)");
        }
        // Baseline movement (Walk/Run/Swim/Turn) should have
        // a positive baseSpeed — TempBuff entries are
        // multiplier-only and may have baseSpeed=0.
        bool isBaseline =
            e.movementCategory >= wowee::pipeline::WoweeUnitMovement::Walk &&
            e.movementCategory <= wowee::pipeline::WoweeUnitMovement::FlyBack;
        if (isBaseline && e.baseSpeed <= 0.0f) {
            errors.push_back(ctx +
                ": baseline movement category with baseSpeed " +
                std::to_string(e.baseSpeed) + " <= 0 — unit "
                "won't move on this category");
        }
        // Run speed below walk speed is suspicious — flag.
        // Run is ~7.0y/s, walk is ~2.5y/s in canonical WoW.
        if (e.movementCategory == wowee::pipeline::WoweeUnitMovement::Run &&
            e.baseSpeed < 3.0f) {
            warnings.push_back(ctx +
                ": Run baseSpeed " +
                std::to_string(e.baseSpeed) +
                " unusually slow (canonical 7.0y/s) — verify intent");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.moveTypeId) {
                errors.push_back(ctx + ": duplicate moveTypeId");
                break;
            }
        }
        idsSeen.push_back(e.moveTypeId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wumv", base, errors, warnings);
    std::printf("validate-wumv: %s.wumv\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu movement types, all moveTypeIds unique, all multipliers consistent\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleUnitMovementCatalog(int& i, int argc, char** argv,
                               int& outRc) {
    if (std::strcmp(argv[i], "--gen-umv") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-umv-flight") == 0 && i + 1 < argc) {
        outRc = handleGenFlight(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-umv-buffs") == 0 && i + 1 < argc) {
        outRc = handleGenBuffs(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wumv") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wumv") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wumv-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wumv-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
