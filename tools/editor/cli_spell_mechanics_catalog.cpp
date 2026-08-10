#include "cli_spell_mechanics_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_mechanics.hpp"
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

std::string stripWsmcExt(std::string base) {
    stripExt(base, ".wsmc");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeSpellMechanic& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpellMechanicLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wsmc\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpellMechanic& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsmc\n", base.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  mechanics : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterMechanics";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWsmcExt(base);
    auto c = wowee::pipeline::WoweeSpellMechanicLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-smc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHardCC(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HardCCMechanics";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWsmcExt(base);
    auto c = wowee::pipeline::WoweeSpellMechanicLoader::makeHardCC(name);
    if (!saveOrError(c, base, "gen-smc-hard")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRoots(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RootMechanics";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWsmcExt(base);
    auto c = wowee::pipeline::WoweeSpellMechanicLoader::makeRoots(name);
    if (!saveOrError(c, base, "gen-smc-roots")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWsmcExt(base);
    if (!wowee::pipeline::WoweeSpellMechanicLoader::exists(base)) {
        std::fprintf(stderr, "WSMC not found: %s.wsmc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellMechanicLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsmc"] = base + ".wsmc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"mechanicId", e.mechanicId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"breaksOnDamage", e.breaksOnDamage},
                {"canBeDispelled", e.canBeDispelled},
                {"drCategory", e.drCategory},
                {"drCategoryName", wowee::pipeline::WoweeSpellMechanic::drCategoryName(e.drCategory)},
                {"dispelType", e.dispelType},
                {"dispelTypeName", wowee::pipeline::WoweeSpellMechanic::dispelTypeName(e.dispelType)},
                {"defaultDurationMs", e.defaultDurationMs},
                {"maxStacks", e.maxStacks},
                {"conflictsMask", e.conflictsMask},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSMC: %s.wsmc\n", base.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  mechanics : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    DR-cat       dispel    breaks  disp  dur(ms)  stack  conflicts   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s   %-8s    %u       %u    %5u     %3u   0x%08x  %s\n",
                    e.mechanicId,
                    wowee::pipeline::WoweeSpellMechanic::drCategoryName(e.drCategory),
                    wowee::pipeline::WoweeSpellMechanic::dispelTypeName(e.dispelType),
                    e.breaksOnDamage, e.canBeDispelled,
                    e.defaultDurationMs, e.maxStacks,
                    e.conflictsMask, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each mechanic emits all 9 scalar fields
    // plus dual int + name forms for drCategory and dispelType.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = stripWsmcExt(base);
    if (outPath.empty()) outPath = base + ".wsmc.json";
    if (!wowee::pipeline::WoweeSpellMechanicLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsmc-json: WSMC not found: %s.wsmc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellMechanicLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"mechanicId", e.mechanicId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"breaksOnDamage", e.breaksOnDamage},
            {"canBeDispelled", e.canBeDispelled},
            {"drCategory", e.drCategory},
            {"drCategoryName", wowee::pipeline::WoweeSpellMechanic::drCategoryName(e.drCategory)},
            {"dispelType", e.dispelType},
            {"dispelTypeName", wowee::pipeline::WoweeSpellMechanic::dispelTypeName(e.dispelType)},
            {"defaultDurationMs", e.defaultDurationMs},
            {"maxStacks", e.maxStacks},
            {"conflictsMask", e.conflictsMask},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wsmc-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source    : %s.wsmc\n", base.c_str());
    std::printf("  mechanics : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wsmc.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = stripWsmcExt(outBase);
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wsmc-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wsmc-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto drFromName = [](const std::string& s) -> uint8_t {
        if (s == "none")       return wowee::pipeline::WoweeSpellMechanic::DRNone;
        if (s == "stun")       return wowee::pipeline::WoweeSpellMechanic::DRStun;
        if (s == "disorient")  return wowee::pipeline::WoweeSpellMechanic::DRDisorient;
        if (s == "silence")    return wowee::pipeline::WoweeSpellMechanic::DRSilence;
        if (s == "root")       return wowee::pipeline::WoweeSpellMechanic::DRRoot;
        if (s == "polymorph")  return wowee::pipeline::WoweeSpellMechanic::DRPolymorph;
        if (s == "controlled") return wowee::pipeline::WoweeSpellMechanic::DRControlled;
        if (s == "misc")       return wowee::pipeline::WoweeSpellMechanic::DRMisc;
        return wowee::pipeline::WoweeSpellMechanic::DRNone;
    };
    auto dispelFromName = [](const std::string& s) -> uint8_t {
        if (s == "none")    return wowee::pipeline::WoweeSpellMechanic::DispelNone;
        if (s == "magic")   return wowee::pipeline::WoweeSpellMechanic::DispelMagic;
        if (s == "curse")   return wowee::pipeline::WoweeSpellMechanic::DispelCurse;
        if (s == "disease") return wowee::pipeline::WoweeSpellMechanic::DispelDisease;
        if (s == "poison")  return wowee::pipeline::WoweeSpellMechanic::DispelPoison;
        if (s == "enrage")  return wowee::pipeline::WoweeSpellMechanic::DispelEnrage;
        if (s == "stealth") return wowee::pipeline::WoweeSpellMechanic::DispelStealth;
        return wowee::pipeline::WoweeSpellMechanic::DispelNone;
    };
    wowee::pipeline::WoweeSpellMechanic c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellMechanic::Entry e;
            e.mechanicId = je.value("mechanicId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            e.breaksOnDamage = static_cast<uint8_t>(
                je.value("breaksOnDamage", 0));
            e.canBeDispelled = static_cast<uint8_t>(
                je.value("canBeDispelled", 0));
            if (je.contains("drCategory") &&
                je["drCategory"].is_number_integer()) {
                e.drCategory = static_cast<uint8_t>(
                    je["drCategory"].get<int>());
            } else if (je.contains("drCategoryName") &&
                       je["drCategoryName"].is_string()) {
                e.drCategory = drFromName(
                    je["drCategoryName"].get<std::string>());
            }
            if (je.contains("dispelType") &&
                je["dispelType"].is_number_integer()) {
                e.dispelType = static_cast<uint8_t>(
                    je["dispelType"].get<int>());
            } else if (je.contains("dispelTypeName") &&
                       je["dispelTypeName"].is_string()) {
                e.dispelType = dispelFromName(
                    je["dispelTypeName"].get<std::string>());
            }
            e.defaultDurationMs = je.value("defaultDurationMs", 0u);
            e.maxStacks = static_cast<uint8_t>(
                je.value("maxStacks", 1));
            e.conflictsMask = je.value("conflictsMask", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeSpellMechanicLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsmc-json: failed to save %s.wsmc\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsmc\n", outBase.c_str());
    std::printf("  source    : %s\n", jsonPath.c_str());
    std::printf("  mechanics : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWsmcExt(base);
    if (!wowee::pipeline::WoweeSpellMechanicLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsmc: WSMC not found: %s.wsmc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellMechanicLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.mechanicId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.mechanicId == 0)
            errors.push_back(ctx + ": mechanicId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.drCategory > wowee::pipeline::WoweeSpellMechanic::DRMisc) {
            errors.push_back(ctx + ": drCategory " +
                std::to_string(e.drCategory) + " not in 0..7");
        }
        if (e.dispelType > wowee::pipeline::WoweeSpellMechanic::DispelStealth) {
            errors.push_back(ctx + ": dispelType " +
                std::to_string(e.dispelType) + " not in 0..6");
        }
        if (e.maxStacks == 0) {
            errors.push_back(ctx +
                ": maxStacks=0 (mechanic could never apply)");
        }
        // canBeDispelled=1 with dispelType=None is contradictory
        // — without a dispel category, no spell can target this
        // mechanic for removal.
        if (e.canBeDispelled &&
            e.dispelType == wowee::pipeline::WoweeSpellMechanic::DispelNone) {
            errors.push_back(ctx +
                ": canBeDispelled=1 but dispelType=none "
                "(no dispel spell can target this)");
        }
        // A mechanic that conflicts with itself is wrong —
        // `conflictsMask & (1 << mechanicId)` would mean the
        // mechanic blocks itself.
        if (e.mechanicId < 32 &&
            (e.conflictsMask & (1u << e.mechanicId))) {
            errors.push_back(ctx +
                ": conflictsMask includes own mechanicId bit "
                "(mechanic conflicts with itself)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.mechanicId) {
                errors.push_back(ctx + ": duplicate mechanicId");
                break;
            }
        }
        idsSeen.push_back(e.mechanicId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsmc", base, errors, warnings);
    std::printf("validate-wsmc: %s.wsmc\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu mechanics, all mechanicIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellMechanicsCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-smc") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-smc-hard") == 0 && i + 1 < argc) {
        outRc = handleGenHardCC(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-smc-roots") == 0 && i + 1 < argc) {
        outRc = handleGenRoots(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsmc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsmc") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsmc-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsmc-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
