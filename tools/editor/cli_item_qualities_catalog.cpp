#include "cli_item_qualities_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_item_qualities.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeItemQuality& c,
                     const std::string& base) {
    std::printf("Wrote %s.wiqr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardQualities";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wiqr");
    auto c = wowee::pipeline::WoweeItemQualityLoader::makeStandard(name);
    if (!saveOrError<wowee::pipeline::WoweeItemQualityLoader>(c, base, "gen-iqr", ".wiqr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenServerCustom(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ServerCustomQualities";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wiqr");
    auto c = wowee::pipeline::WoweeItemQualityLoader::makeServerCustom(name);
    if (!saveOrError<wowee::pipeline::WoweeItemQualityLoader>(c, base, "gen-iqr-server", ".wiqr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaidTiers(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidProgressionQualities";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wiqr");
    auto c = wowee::pipeline::WoweeItemQualityLoader::makeRaidTiers(name);
    if (!saveOrError<wowee::pipeline::WoweeItemQualityLoader>(c, base, "gen-iqr-raid", ".wiqr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wiqr");
    if (!wowee::pipeline::WoweeItemQualityLoader::exists(base)) {
        std::fprintf(stderr, "WIQR not found: %s.wiqr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemQualityLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wiqr"] = base + ".wiqr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"qualityId", e.qualityId},
                {"name", e.name},
                {"description", e.description},
                {"nameColorRGBA", e.nameColorRGBA},
                {"borderColorRGBA", e.borderColorRGBA},
                {"vendorPriceMultiplier", e.vendorPriceMultiplier},
                {"minLevelToDrop", e.minLevelToDrop},
                {"maxLevelToDrop", e.maxLevelToDrop},
                {"canBeDisenchanted", e.canBeDisenchanted != 0},
                {"inventoryBorderTexture", e.inventoryBorderTexture},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WIQR: %s.wiqr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id     name        nameColor    vendorMul   minLvl   maxLvl   DE   border\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s   0x%08x   %6.2fx     %4u     %4u   %s   %s\n",
                    e.qualityId, e.name.c_str(),
                    e.nameColorRGBA, e.vendorPriceMultiplier,
                    e.minLevelToDrop, e.maxLevelToDrop,
                    e.canBeDisenchanted ? "yes" : "no ",
                    e.inventoryBorderTexture.empty() ? "(none)" :
                        e.inventoryBorderTexture.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wiqr");
    if (!wowee::pipeline::WoweeItemQualityLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wiqr-json: WIQR not found: %s.wiqr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemQualityLoader::load(base);
    if (outPath.empty()) outPath = base + ".wiqr.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["qualityId"] = e.qualityId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["nameColorRGBA"] = e.nameColorRGBA;
        je["borderColorRGBA"] = e.borderColorRGBA;
        je["vendorPriceMultiplier"] = e.vendorPriceMultiplier;
        je["minLevelToDrop"] = e.minLevelToDrop;
        je["maxLevelToDrop"] = e.maxLevelToDrop;
        je["canBeDisenchanted"] = e.canBeDisenchanted != 0;
        je["inventoryBorderTexture"] = e.inventoryBorderTexture;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wiqr-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wiqr-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wiqr-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeItemQuality c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeItemQuality::Entry e;
            if (je.contains("qualityId"))    e.qualityId = je["qualityId"].get<uint32_t>();
            if (je.contains("name"))         e.name = je["name"].get<std::string>();
            if (je.contains("description"))  e.description = je["description"].get<std::string>();
            if (je.contains("nameColorRGBA"))   e.nameColorRGBA = je["nameColorRGBA"].get<uint32_t>();
            if (je.contains("borderColorRGBA")) e.borderColorRGBA = je["borderColorRGBA"].get<uint32_t>();
            if (je.contains("vendorPriceMultiplier")) e.vendorPriceMultiplier = je["vendorPriceMultiplier"].get<float>();
            if (je.contains("minLevelToDrop")) e.minLevelToDrop = je["minLevelToDrop"].get<uint8_t>();
            if (je.contains("maxLevelToDrop")) e.maxLevelToDrop = je["maxLevelToDrop"].get<uint8_t>();
            if (je.contains("canBeDisenchanted")) {
                if (je["canBeDisenchanted"].is_boolean())
                    e.canBeDisenchanted = je["canBeDisenchanted"].get<bool>() ? 1 : 0;
                else
                    e.canBeDisenchanted = je["canBeDisenchanted"].get<uint8_t>() ? 1 : 0;
            }
            if (je.contains("inventoryBorderTexture"))
                e.inventoryBorderTexture = je["inventoryBorderTexture"].get<std::string>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wiqr");
    outBase = cli::withoutExt(outBase, ".wiqr");
    if (!wowee::pipeline::WoweeItemQualityLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wiqr-json: failed to save %s.wiqr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wiqr\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wiqr");
    if (!wowee::pipeline::WoweeItemQualityLoader::exists(base)) {
        return reportMissing("validate-wiqr", "WIQR", base, ".wiqr");
    }
    auto c = wowee::pipeline::WoweeItemQualityLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.qualityId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.vendorPriceMultiplier < 0.0f) {
            errors.push_back(ctx +
                ": vendorPriceMultiplier < 0 — vendor would "
                "pay the player to take items");
        }
        if (e.maxLevelToDrop != 0 &&
            e.minLevelToDrop > e.maxLevelToDrop) {
            errors.push_back(ctx + ": minLevelToDrop " +
                std::to_string(e.minLevelToDrop) +
                " > maxLevelToDrop " +
                std::to_string(e.maxLevelToDrop) +
                " — quality will never drop");
        }
        if (e.minLevelToDrop > 80) {
            warnings.push_back(ctx +
                ": minLevelToDrop " +
                std::to_string(e.minLevelToDrop) +
                " > 80 — quality unreachable at WotLK cap");
        }
        if (e.vendorPriceMultiplier > 100.0f) {
            warnings.push_back(ctx +
                ": vendorPriceMultiplier " +
                std::to_string(e.vendorPriceMultiplier) +
                "x is very high — sanity check the economy");
        }
        // Pure transparent color is suspicious (alpha=0).
        if ((e.nameColorRGBA & 0xFF000000u) == 0) {
            warnings.push_back(ctx +
                ": nameColorRGBA has alpha=0 — text will be "
                "invisible in tooltips");
        }
        if (!idsSeen.add(e.qualityId)) errors.push_back(ctx + ": duplicate qualityId");
    }
    return cli::reportValidation("wiqr", base, jsonOut, errors, warnings,
                                 formatted("%zu tiers, all qualityIds unique", c.entries.size()));
}

} // namespace

bool handleItemQualitiesCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-iqr") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-iqr-server") == 0 && i + 1 < argc) {
        outRc = handleGenServerCustom(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-iqr-raid") == 0 && i + 1 < argc) {
        outRc = handleGenRaidTiers(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wiqr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wiqr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wiqr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wiqr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
