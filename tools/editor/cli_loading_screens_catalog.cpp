#include "cli_loading_screens_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_loading_screens.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeLoadingScreen& c,
                     const std::string& base) {
    std::printf("Wrote %s.wlds\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  screens : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterLoadingScreens";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlds");
    auto c = wowee::pipeline::WoweeLoadingScreenLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeLoadingScreenLoader>(c, base, "gen-lds", ".wlds")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenInstances(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "InstanceLoadingScreens";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlds");
    auto c = wowee::pipeline::WoweeLoadingScreenLoader::makeInstances(name);
    if (!saveOrError<wowee::pipeline::WoweeLoadingScreenLoader>(c, base, "gen-lds-instances", ".wlds")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaidIntros(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidIntroLoadingScreens";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlds");
    auto c = wowee::pipeline::WoweeLoadingScreenLoader::makeRaidIntros(name);
    if (!saveOrError<wowee::pipeline::WoweeLoadingScreenLoader>(c, base, "gen-lds-raid", ".wlds")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlds");
    if (!wowee::pipeline::WoweeLoadingScreenLoader::exists(base)) {
        std::fprintf(stderr, "WLDS not found: %s.wlds\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLoadingScreenLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wlds"] = base + ".wlds";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"screenId", e.screenId},
                {"mapId", e.mapId},
                {"name", e.name},
                {"description", e.description},
                {"texturePath", e.texturePath},
                {"iconPath", e.iconPath},
                {"attribution", e.attribution},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"displayWeight", e.displayWeight},
                {"expansionRequired", e.expansionRequired},
                {"expansionRequiredName", wowee::pipeline::WoweeLoadingScreen::expansionGateName(e.expansionRequired)},
                {"isAnimated", e.isAnimated},
                {"isWideAspect", e.isWideAspect},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLDS: %s.wlds\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  screens : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    map    levels   wt   exp        anim  wide  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %4u   %3u-%3u  %3u   %-7s    %u     %u    %s\n",
                    e.screenId, e.mapId,
                    e.minLevel, e.maxLevel,
                    e.displayWeight,
                    wowee::pipeline::WoweeLoadingScreen::expansionGateName(e.expansionRequired),
                    e.isAnimated, e.isWideAspect, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each screen emits all 11 scalar fields
    // plus a dual int + name form for expansionRequired.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wlds");
    if (outPath.empty()) outPath = base + ".wlds.json";
    if (!wowee::pipeline::WoweeLoadingScreenLoader::exists(base)) {
        return reportMissing("export-wlds-json", "WLDS", base, ".wlds");
    }
    auto c = wowee::pipeline::WoweeLoadingScreenLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"screenId", e.screenId},
            {"mapId", e.mapId},
            {"name", e.name},
            {"description", e.description},
            {"texturePath", e.texturePath},
            {"iconPath", e.iconPath},
            {"attribution", e.attribution},
            {"minLevel", e.minLevel},
            {"maxLevel", e.maxLevel},
            {"displayWeight", e.displayWeight},
            {"expansionRequired", e.expansionRequired},
            {"expansionRequiredName", wowee::pipeline::WoweeLoadingScreen::expansionGateName(e.expansionRequired)},
            {"isAnimated", e.isAnimated},
            {"isWideAspect", e.isWideAspect},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wlds-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wlds\n", base.c_str());
    std::printf("  screens : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wlds");
    outBase = cli::withoutExt(outBase, ".wlds");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wlds-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wlds-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto expansionFromName = [](const std::string& s) -> uint8_t {
        if (s == "classic") return wowee::pipeline::WoweeLoadingScreen::Classic;
        if (s == "tbc")     return wowee::pipeline::WoweeLoadingScreen::TBC;
        if (s == "wotlk")   return wowee::pipeline::WoweeLoadingScreen::WotLK;
        if (s == "turtle")  return wowee::pipeline::WoweeLoadingScreen::TurtleWoW;
        return wowee::pipeline::WoweeLoadingScreen::Classic;
    };
    wowee::pipeline::WoweeLoadingScreen c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeLoadingScreen::Entry e;
            e.screenId = je.value("screenId", 0u);
            e.mapId = je.value("mapId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.texturePath = je.value("texturePath", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            e.attribution = je.value("attribution", std::string{});
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            e.maxLevel = static_cast<uint16_t>(je.value("maxLevel", 80));
            e.displayWeight = static_cast<uint16_t>(
                je.value("displayWeight", 1));
            if (je.contains("expansionRequired") &&
                je["expansionRequired"].is_number_integer()) {
                e.expansionRequired = static_cast<uint8_t>(
                    je["expansionRequired"].get<int>());
            } else if (je.contains("expansionRequiredName") &&
                       je["expansionRequiredName"].is_string()) {
                e.expansionRequired = expansionFromName(
                    je["expansionRequiredName"].get<std::string>());
            }
            e.isAnimated = static_cast<uint8_t>(
                je.value("isAnimated", 0));
            e.isWideAspect = static_cast<uint8_t>(
                je.value("isWideAspect", 0));
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeLoadingScreenLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wlds-json: failed to save %s.wlds\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wlds\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  screens : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlds");
    if (!wowee::pipeline::WoweeLoadingScreenLoader::exists(base)) {
        return reportMissing("validate-wlds", "WLDS", base, ".wlds");
    }
    auto c = wowee::pipeline::WoweeLoadingScreenLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.screenId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.screenId == 0)
            errors.push_back(ctx + ": screenId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.texturePath.empty())
            errors.push_back(ctx + ": texturePath is empty "
                "(screen has no image to display)");
        if (e.expansionRequired > wowee::pipeline::WoweeLoadingScreen::TurtleWoW) {
            errors.push_back(ctx + ": expansionRequired " +
                std::to_string(e.expansionRequired) + " not in 0..3");
        }
        if (e.minLevel > e.maxLevel) {
            errors.push_back(ctx + ": minLevel " +
                std::to_string(e.minLevel) + " > maxLevel " +
                std::to_string(e.maxLevel));
        }
        if (e.displayWeight == 0) {
            warnings.push_back(ctx +
                ": displayWeight=0 (screen is in pool but never picked)");
        }
        // mapId=0 means catch-all — flag if there are
        // multiple catch-all screens in the same level
        // bracket, since the random pick becomes ambiguous.
        if (e.mapId == 0 && c.entries.size() > 1) {
            uint32_t conflicts = 0;
            for (size_t m = 0; m < c.entries.size(); ++m) {
                if (m == k) continue;
                const auto& other = c.entries[m];
                if (other.mapId != 0) continue;
                // Overlap level brackets count as conflicts.
                if (other.minLevel <= e.maxLevel &&
                    other.maxLevel >= e.minLevel) {
                    ++conflicts;
                }
            }
            if (conflicts > 0) {
                warnings.push_back(ctx +
                    ": catch-all screen (mapId=0) overlaps " +
                    std::to_string(conflicts) +
                    " other catch-all in same level bracket "
                    "— random pick is non-deterministic");
            }
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.screenId) {
                errors.push_back(ctx + ": duplicate screenId");
                break;
            }
        }
        idsSeen.push_back(e.screenId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wlds", base, errors, warnings);
    std::printf("validate-wlds: %s.wlds\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu screens, all screenIds unique, no overlap conflicts\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleLoadingScreensCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-lds") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lds-instances") == 0 && i + 1 < argc) {
        outRc = handleGenInstances(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lds-raid") == 0 && i + 1 < argc) {
        outRc = handleGenRaidIntros(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wlds") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wlds") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wlds-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wlds-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
