#include "cli_liquids_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_liquids.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeLiquid& c,
                     const std::string& base) {
    std::printf("Wrote %s.wliq\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  liquids : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterLiquids";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wliq");
    auto c = wowee::pipeline::WoweeLiquidLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeLiquidLoader>(c, base, "gen-liquids", ".wliq")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMagical(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MagicalLiquids";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wliq");
    auto c = wowee::pipeline::WoweeLiquidLoader::makeMagical(name);
    if (!saveOrError<wowee::pipeline::WoweeLiquidLoader>(c, base, "gen-liquids-magical", ".wliq")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHazardous(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HazardousLiquids";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wliq");
    auto c = wowee::pipeline::WoweeLiquidLoader::makeHazardous(name);
    if (!saveOrError<wowee::pipeline::WoweeLiquidLoader>(c, base, "gen-liquids-hazardous", ".wliq")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wliq");
    if (!wowee::pipeline::WoweeLiquidLoader::exists(base)) {
        std::fprintf(stderr, "WLIQ not found: %s.wliq\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLiquidLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wliq"] = base + ".wliq";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"liquidId", e.liquidId},
                {"name", e.name},
                {"description", e.description},
                {"shaderPath", e.shaderPath},
                {"materialPath", e.materialPath},
                {"liquidKind", e.liquidKind},
                {"liquidKindName", wowee::pipeline::WoweeLiquid::liquidKindName(e.liquidKind)},
                {"fogColorR", e.fogColorR},
                {"fogColorG", e.fogColorG},
                {"fogColorB", e.fogColorB},
                {"fogDensity", e.fogDensity},
                {"ambientSoundId", e.ambientSoundId},
                {"splashSoundId", e.splashSoundId},
                {"damageSpellId", e.damageSpellId},
                {"damagePerSecond", e.damagePerSecond},
                {"minimapColor", e.minimapColor},
                {"flowDirection", e.flowDirection},
                {"flowSpeed", e.flowSpeed},
                {"viscosity", e.viscosity},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLIQ: %s.wliq\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  liquids : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind         fog RGB         dens   visc   dps    spell   ambient  splash  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s   %3u/%3u/%3u   %4.2f   %4.2f  %5u  %5u    %5u    %5u   %s\n",
                    e.liquidId,
                    wowee::pipeline::WoweeLiquid::liquidKindName(e.liquidKind),
                    e.fogColorR, e.fogColorG, e.fogColorB,
                    e.fogDensity, e.viscosity,
                    e.damagePerSecond, e.damageSpellId,
                    e.ambientSoundId, e.splashSoundId,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each liquid emits all 14 scalar fields
    // (including 3-byte fog color) plus a dual int + name
    // form for liquidKind so hand-edits can use either.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wliq");
    if (outPath.empty()) outPath = base + ".wliq.json";
    if (!wowee::pipeline::WoweeLiquidLoader::exists(base)) {
        return reportMissing("export-wliq-json", "WLIQ", base, ".wliq");
    }
    auto c = wowee::pipeline::WoweeLiquidLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"liquidId", e.liquidId},
            {"name", e.name},
            {"description", e.description},
            {"shaderPath", e.shaderPath},
            {"materialPath", e.materialPath},
            {"liquidKind", e.liquidKind},
            {"liquidKindName", wowee::pipeline::WoweeLiquid::liquidKindName(e.liquidKind)},
            {"fogColorR", e.fogColorR},
            {"fogColorG", e.fogColorG},
            {"fogColorB", e.fogColorB},
            {"fogDensity", e.fogDensity},
            {"ambientSoundId", e.ambientSoundId},
            {"splashSoundId", e.splashSoundId},
            {"damageSpellId", e.damageSpellId},
            {"damagePerSecond", e.damagePerSecond},
            {"minimapColor", e.minimapColor},
            {"flowDirection", e.flowDirection},
            {"flowSpeed", e.flowSpeed},
            {"viscosity", e.viscosity},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wliq-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wliq\n", base.c_str());
    std::printf("  liquids : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wliq");
    outBase = cli::withoutExt(outBase, ".wliq");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wliq-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wliq-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "water")      return wowee::pipeline::WoweeLiquid::Water;
        if (s == "magma")      return wowee::pipeline::WoweeLiquid::Magma;
        if (s == "slime")      return wowee::pipeline::WoweeLiquid::Slime;
        if (s == "ocean")      return wowee::pipeline::WoweeLiquid::OceanSalt;
        if (s == "fel-fire")   return wowee::pipeline::WoweeLiquid::FelFire;
        if (s == "holy-light") return wowee::pipeline::WoweeLiquid::HolyLight;
        if (s == "tar")        return wowee::pipeline::WoweeLiquid::TarOil;
        if (s == "acid")       return wowee::pipeline::WoweeLiquid::AcidBog;
        if (s == "frozen")     return wowee::pipeline::WoweeLiquid::FrozenWater;
        if (s == "underworld") return wowee::pipeline::WoweeLiquid::UnderworldGoo;
        return wowee::pipeline::WoweeLiquid::Water;
    };
    wowee::pipeline::WoweeLiquid c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeLiquid::Entry e;
            e.liquidId = je.value("liquidId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.shaderPath = je.value("shaderPath", std::string{});
            e.materialPath = je.value("materialPath", std::string{});
            if (je.contains("liquidKind") &&
                je["liquidKind"].is_number_integer()) {
                e.liquidKind = static_cast<uint8_t>(
                    je["liquidKind"].get<int>());
            } else if (je.contains("liquidKindName") &&
                       je["liquidKindName"].is_string()) {
                e.liquidKind = kindFromName(
                    je["liquidKindName"].get<std::string>());
            }
            e.fogColorR = static_cast<uint8_t>(je.value("fogColorR", 0));
            e.fogColorG = static_cast<uint8_t>(je.value("fogColorG", 0));
            e.fogColorB = static_cast<uint8_t>(je.value("fogColorB", 0));
            e.fogDensity = je.value("fogDensity", 0.0f);
            e.ambientSoundId = je.value("ambientSoundId", 0u);
            e.splashSoundId = je.value("splashSoundId", 0u);
            e.damageSpellId = je.value("damageSpellId", 0u);
            e.damagePerSecond = je.value("damagePerSecond", 0u);
            e.minimapColor = je.value("minimapColor", 0u);
            e.flowDirection = je.value("flowDirection", 0.0f);
            e.flowSpeed = je.value("flowSpeed", 0.0f);
            e.viscosity = je.value("viscosity", 0.0f);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeLiquidLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wliq-json: failed to save %s.wliq\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wliq\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  liquids : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeLiquidLoader>(
        i, argc, argv, "wliq", "WLIQ",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.liquidId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.liquidId == 0)
                errors.push_back(ctx + ": liquidId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.shaderPath.empty())
                errors.push_back(ctx + ": shaderPath is empty");
            if (e.materialPath.empty())
                errors.push_back(ctx + ": materialPath is empty");
            if (e.liquidKind > wowee::pipeline::WoweeLiquid::UnderworldGoo) {
                errors.push_back(ctx + ": liquidKind " +
                    std::to_string(e.liquidKind) + " not in 0..9");
            }
            if (e.fogDensity < 0.0f || e.fogDensity > 1.0f) {
                errors.push_back(ctx + ": fogDensity " +
                    std::to_string(e.fogDensity) + " not in 0..1");
            }
            if (e.viscosity < 0.0f || e.viscosity > 1.0f) {
                errors.push_back(ctx + ": viscosity " +
                    std::to_string(e.viscosity) + " not in 0..1");
            }
            // Magma / Slime / FelFire / AcidBog liquids without
            // any damage source are mechanically harmless — flag
            // as a warning so the caller can confirm intent.
            bool hazardous =
                e.liquidKind == wowee::pipeline::WoweeLiquid::Magma ||
                e.liquidKind == wowee::pipeline::WoweeLiquid::Slime ||
                e.liquidKind == wowee::pipeline::WoweeLiquid::FelFire ||
                e.liquidKind == wowee::pipeline::WoweeLiquid::AcidBog;
            if (hazardous && e.damageSpellId == 0 &&
                e.damagePerSecond == 0) {
                warnings.push_back(ctx +
                    ": hazardous liquid kind but no damageSpellId / "
                    "damagePerSecond (won't hurt anything)");
            }
            // Water and OceanSalt with non-zero damage is unusual
            // — could be intentional acid water but worth checking.
            if ((e.liquidKind == wowee::pipeline::WoweeLiquid::Water ||
                 e.liquidKind == wowee::pipeline::WoweeLiquid::OceanSalt) &&
                e.damagePerSecond > 0) {
                warnings.push_back(ctx +
                    ": Water/OceanSalt with damagePerSecond>0 "
                    "(unusual — verify intent)");
            }
            if (!idsSeen.add(e.liquidId)) errors.push_back(ctx + ": duplicate liquidId");
        }
            return formatted("%zu liquids, all liquidIds unique", c.entries.size());
        });
}

} // namespace

bool handleLiquidsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-liquids") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-liquids-magical") == 0 && i + 1 < argc) {
        outRc = handleGenMagical(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-liquids-hazardous") == 0 && i + 1 < argc) {
        outRc = handleGenHazardous(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wliq") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wliq") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wliq-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wliq-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
