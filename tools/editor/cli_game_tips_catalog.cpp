#include "cli_game_tips_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_game_tips.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeGameTip& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgtp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tips    : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterTips";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgtp");
    auto c = wowee::pipeline::WoweeGameTipLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeGameTipLoader>(c, base, "gen-tips", ".wgtp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenNewPlayer(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "NewPlayerTips";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgtp");
    auto c = wowee::pipeline::WoweeGameTipLoader::makeNewPlayer(name);
    if (!saveOrError<wowee::pipeline::WoweeGameTipLoader>(c, base, "gen-tips-new-player", ".wgtp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAdvanced(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AdvancedTips";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgtp");
    auto c = wowee::pipeline::WoweeGameTipLoader::makeAdvanced(name);
    if (!saveOrError<wowee::pipeline::WoweeGameTipLoader>(c, base, "gen-tips-advanced", ".wgtp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgtp");
    if (!wowee::pipeline::WoweeGameTipLoader::exists(base)) {
        std::fprintf(stderr, "WGTP not found: %s.wgtp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGameTipLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgtp"] = base + ".wgtp";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"tipId", e.tipId},
                {"name", e.name},
                {"text", e.text},
                {"iconPath", e.iconPath},
                {"displayKind", e.displayKind},
                {"displayKindName", wowee::pipeline::WoweeGameTip::displayKindName(e.displayKind)},
                {"audienceFilter", e.audienceFilter},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"displayWeight", e.displayWeight},
                {"conditionId", e.conditionId},
                {"requiredClassMask", e.requiredClassMask},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGTP: %s.wgtp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tips    : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind            audience    levels    wt   cond   classMask  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-13s   0x%08x  %3u-%3u   %3u  %5u   0x%08x  %s\n",
                    e.tipId,
                    wowee::pipeline::WoweeGameTip::displayKindName(e.displayKind),
                    e.audienceFilter,
                    e.minLevel, e.maxLevel,
                    e.displayWeight, e.conditionId,
                    e.requiredClassMask,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each tip emits all 11 scalar fields plus
    // a dual int + name form for displayKind so hand-edits
    // can use either representation.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wgtp");
    if (outPath.empty()) outPath = base + ".wgtp.json";
    if (!wowee::pipeline::WoweeGameTipLoader::exists(base)) {
        return reportMissing("export-wgtp-json", "WGTP", base, ".wgtp");
    }
    auto c = wowee::pipeline::WoweeGameTipLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"tipId", e.tipId},
            {"name", e.name},
            {"text", e.text},
            {"iconPath", e.iconPath},
            {"displayKind", e.displayKind},
            {"displayKindName", wowee::pipeline::WoweeGameTip::displayKindName(e.displayKind)},
            {"audienceFilter", e.audienceFilter},
            {"minLevel", e.minLevel},
            {"maxLevel", e.maxLevel},
            {"displayWeight", e.displayWeight},
            {"conditionId", e.conditionId},
            {"requiredClassMask", e.requiredClassMask},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wgtp-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wgtp\n", base.c_str());
    std::printf("  tips   : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wgtp");
    outBase = cli::withoutExt(outBase, ".wgtp");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wgtp-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wgtp-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "loading-screen") return wowee::pipeline::WoweeGameTip::LoadingScreen;
        if (s == "tutorial")       return wowee::pipeline::WoweeGameTip::Tutorial;
        if (s == "tooltip-help")   return wowee::pipeline::WoweeGameTip::TooltipHelp;
        if (s == "hint")           return wowee::pipeline::WoweeGameTip::Hint;
        return wowee::pipeline::WoweeGameTip::LoadingScreen;
    };
    wowee::pipeline::WoweeGameTip c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeGameTip::Entry e;
            e.tipId = je.value("tipId", 0u);
            e.name = je.value("name", std::string{});
            e.text = je.value("text", std::string{});
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
            // audienceFilter defaults to kAudienceAll so an
            // omitted field doesn't accidentally silence the
            // tip — explicit 0 is still respected.
            e.audienceFilter = je.value("audienceFilter",
                wowee::pipeline::WoweeGameTip::kAudienceAll);
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            e.maxLevel = static_cast<uint16_t>(je.value("maxLevel", 80));
            e.displayWeight = static_cast<uint16_t>(
                je.value("displayWeight", 1));
            e.conditionId = je.value("conditionId", 0u);
            e.requiredClassMask = je.value("requiredClassMask", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeGameTipLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgtp-json: failed to save %s.wgtp\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgtp\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  tips   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeGameTipLoader>(
        i, argc, argv, "wgtp", "WGTP",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.tipId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.tipId == 0)
                errors.push_back(ctx + ": tipId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.text.empty())
                errors.push_back(ctx + ": text is empty");
            if (e.displayKind > wowee::pipeline::WoweeGameTip::Hint) {
                errors.push_back(ctx + ": displayKind " +
                    std::to_string(e.displayKind) + " not in 0..3");
            }
            if (e.audienceFilter == 0) {
                errors.push_back(ctx +
                    ": audienceFilter=0 (tip would never be shown)");
            }
            if (e.minLevel > e.maxLevel) {
                errors.push_back(ctx + ": minLevel " +
                    std::to_string(e.minLevel) + " > maxLevel " +
                    std::to_string(e.maxLevel));
            }
            if (e.displayWeight == 0) {
                warnings.push_back(ctx +
                    ": displayWeight=0 (tip is in pool but never picked)");
            }
            // Tutorial / Hint kinds typically need to be brief —
            // > 280 characters won't fit cleanly on screen.
            bool brief = e.displayKind == wowee::pipeline::WoweeGameTip::Tutorial ||
                          e.displayKind == wowee::pipeline::WoweeGameTip::Hint;
            if (brief && e.text.size() > 280) {
                warnings.push_back(ctx +
                    ": text length " + std::to_string(e.text.size()) +
                    " exceeds 280 chars (tutorial/hint should be brief)");
            }
            if (!idsSeen.add(e.tipId)) errors.push_back(ctx + ": duplicate tipId");
        }
            return formatted("%zu tips, all tipIds unique, all level ranges valid", c.entries.size());
        });
}

} // namespace

bool handleGameTipsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-tips") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tips-new-player") == 0 &&
        i + 1 < argc) {
        outRc = handleGenNewPlayer(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tips-advanced") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAdvanced(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgtp") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgtp") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgtp-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgtp-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
