#include "cli_world_state_ui_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_world_state_ui.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeWorldStateUI& c,
                     const std::string& base) {
    std::printf("Wrote %s.wwui\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  states  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterWorldStateUI";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wwui");
    auto c = wowee::pipeline::WoweeWorldStateUILoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeWorldStateUILoader>(c, base, "gen-wsui", ".wwui")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWintergrasp(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WintergraspUI";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wwui");
    auto c = wowee::pipeline::WoweeWorldStateUILoader::makeWintergrasp(name);
    if (!saveOrError<wowee::pipeline::WoweeWorldStateUILoader>(c, base, "gen-wsui-wintergrasp", ".wwui")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDungeon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DungeonUI";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wwui");
    auto c = wowee::pipeline::WoweeWorldStateUILoader::makeDungeon(name);
    if (!saveOrError<wowee::pipeline::WoweeWorldStateUILoader>(c, base, "gen-wsui-dungeon", ".wwui")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wwui");
    if (!wowee::pipeline::WoweeWorldStateUILoader::exists(base)) {
        return reportMissing("WWUI", base, ".wwui");
    }
    auto c = wowee::pipeline::WoweeWorldStateUILoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wwui"] = base + ".wwui";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"worldStateId", e.worldStateId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"displayKind", e.displayKind},
                {"displayKindName", wowee::pipeline::WoweeWorldStateUI::displayKindName(e.displayKind)},
                {"panelPosition", e.panelPosition},
                {"panelPositionName", wowee::pipeline::WoweeWorldStateUI::panelPositionName(e.panelPosition)},
                {"alwaysVisible", e.alwaysVisible},
                {"hideWhenZero", e.hideWhenZero},
                {"mapId", e.mapId},
                {"areaId", e.areaId},
                {"variableIndex", e.variableIndex},
                {"defaultValue", e.defaultValue},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WWUI: %s.wwui\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  states  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind          pos        always  hideZ  map    area    var   default  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-13s %-9s    %u       %u    %5u  %5u   %3u   %6d   %s\n",
                    e.worldStateId,
                    wowee::pipeline::WoweeWorldStateUI::displayKindName(e.displayKind),
                    wowee::pipeline::WoweeWorldStateUI::panelPositionName(e.panelPosition),
                    e.alwaysVisible, e.hideWhenZero,
                    e.mapId, e.areaId, e.variableIndex,
                    e.defaultValue, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each world state emits all 11 scalar
    // fields plus dual int + name forms for displayKind
    // and panelPosition so hand-edits can use either.
    return cli::exportCatalogJson<wowee::pipeline::WoweeWorldStateUILoader>(
        i, argc, argv, "wwui", "WWUI", "states ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"worldStateId", e.worldStateId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"displayKind", e.displayKind},
                {"displayKindName", wowee::pipeline::WoweeWorldStateUI::displayKindName(e.displayKind)},
                {"panelPosition", e.panelPosition},
                {"panelPositionName", wowee::pipeline::WoweeWorldStateUI::panelPositionName(e.panelPosition)},
                {"alwaysVisible", e.alwaysVisible},
                {"hideWhenZero", e.hideWhenZero},
                {"mapId", e.mapId},
                {"areaId", e.areaId},
                {"variableIndex", e.variableIndex},
                {"defaultValue", e.defaultValue},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wwui");
    outBase = cli::withoutExt(outBase, ".wwui");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wwui-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wwui-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "counter")       return wowee::pipeline::WoweeWorldStateUI::Counter;
        if (s == "timer")         return wowee::pipeline::WoweeWorldStateUI::Timer;
        if (s == "flag-icon")     return wowee::pipeline::WoweeWorldStateUI::FlagIcon;
        if (s == "progress-bar")  return wowee::pipeline::WoweeWorldStateUI::ProgressBar;
        if (s == "two-sided")     return wowee::pipeline::WoweeWorldStateUI::TwoSidedScore;
        if (s == "custom")        return wowee::pipeline::WoweeWorldStateUI::Custom;
        return wowee::pipeline::WoweeWorldStateUI::Counter;
    };
    auto posFromName = [](const std::string& s) -> uint8_t {
        if (s == "top")       return wowee::pipeline::WoweeWorldStateUI::Top;
        if (s == "bottom")    return wowee::pipeline::WoweeWorldStateUI::Bottom;
        if (s == "top-left")  return wowee::pipeline::WoweeWorldStateUI::TopLeft;
        if (s == "top-right") return wowee::pipeline::WoweeWorldStateUI::TopRight;
        if (s == "center")    return wowee::pipeline::WoweeWorldStateUI::Center;
        return wowee::pipeline::WoweeWorldStateUI::Top;
    };
    wowee::pipeline::WoweeWorldStateUI c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeWorldStateUI::Entry e;
            e.worldStateId = je.value("worldStateId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("displayKind") &&
                je["displayKind"].is_number_integer()) {
                e.displayKind = static_cast<uint8_t>(
                    je["displayKind"].get<int>());
            } else if (je.contains("displayKindName") &&
                       je["displayKindName"].is_string()) {
                e.displayKind = kindFromName(
                    je["displayKindName"].get<std::string>());
            }
            if (je.contains("panelPosition") &&
                je["panelPosition"].is_number_integer()) {
                e.panelPosition = static_cast<uint8_t>(
                    je["panelPosition"].get<int>());
            } else if (je.contains("panelPositionName") &&
                       je["panelPositionName"].is_string()) {
                e.panelPosition = posFromName(
                    je["panelPositionName"].get<std::string>());
            }
            e.alwaysVisible = static_cast<uint8_t>(je.value("alwaysVisible", 0));
            e.hideWhenZero = static_cast<uint8_t>(je.value("hideWhenZero", 0));
            e.mapId = je.value("mapId", 0u);
            e.areaId = je.value("areaId", 0u);
            e.variableIndex = je.value("variableIndex", 0u);
            e.defaultValue = je.value("defaultValue", 0);
            e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeWorldStateUILoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wwui-json: failed to save %s.wwui\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wwui\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  states : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeWorldStateUILoader>(
        i, argc, argv, "wwui", "WWUI",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        std::vector<uint32_t> varsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.worldStateId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.worldStateId == 0)
                errors.push_back(ctx + ": worldStateId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.displayKind > wowee::pipeline::WoweeWorldStateUI::Custom) {
                errors.push_back(ctx + ": displayKind " +
                    std::to_string(e.displayKind) + " not in 0..5");
            }
            if (e.panelPosition > wowee::pipeline::WoweeWorldStateUI::Center) {
                errors.push_back(ctx + ": panelPosition " +
                    std::to_string(e.panelPosition) + " not in 0..4");
            }
            if (e.variableIndex == 0) {
                warnings.push_back(ctx + ": variableIndex=0 "
                    "(not bound to a server-side variable)");
            }
            // alwaysVisible + hideWhenZero is contradictory —
            // hide-when-zero implicitly negates always-visible
            // when the value happens to be 0.
            if (e.alwaysVisible && e.hideWhenZero) {
                warnings.push_back(ctx +
                    ": both alwaysVisible and hideWhenZero set "
                    "(hideWhenZero wins when value=0)");
            }
            // Two world-state entries sharing the same
            // (mapId, variableIndex) pair conflict — they'd both
            // try to read the same server slot at the same time.
            for (size_t m = 0; m < k; ++m) {
                const auto& other = c.entries[m];
                if (other.mapId == e.mapId && e.mapId != 0 &&
                    other.variableIndex == e.variableIndex &&
                    e.variableIndex != 0) {
                    warnings.push_back(ctx +
                        ": shares (mapId=" + std::to_string(e.mapId) +
                        ", variableIndex=" +
                        std::to_string(e.variableIndex) +
                        ") with entry " + std::to_string(m) +
                        " — values will collide");
                    break;
                }
            }
            if (!idsSeen.add(e.worldStateId)) errors.push_back(ctx + ": duplicate worldStateId");
            varsSeen.push_back(e.variableIndex);
        }
            return formatted("%zu world states, all worldStateIds unique", c.entries.size());
        });
}

} // namespace

bool handleWorldStateUICatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-wsui") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-wsui-wintergrasp") == 0 &&
        i + 1 < argc) {
        outRc = handleGenWintergrasp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-wsui-dungeon") == 0 &&
        i + 1 < argc) {
        outRc = handleGenDungeon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wwui") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wwui") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wwui-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wwui-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
