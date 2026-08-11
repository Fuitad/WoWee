#include "cli_quest_sorts_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_quest_sorts.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeQuestSort& c,
                     const std::string& base) {
    std::printf("Wrote %s.wqso\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sorts   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterQuestSorts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wqso");
    auto c = wowee::pipeline::WoweeQuestSortLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeQuestSortLoader>(c, base, "gen-qso", ".wqso")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenClass(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ClassQuestSorts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wqso");
    auto c = wowee::pipeline::WoweeQuestSortLoader::makeClass(name);
    if (!saveOrError<wowee::pipeline::WoweeQuestSortLoader>(c, base, "gen-qso-class", ".wqso")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenProfession(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ProfessionQuestSorts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wqso");
    auto c = wowee::pipeline::WoweeQuestSortLoader::makeProfession(name);
    if (!saveOrError<wowee::pipeline::WoweeQuestSortLoader>(c, base, "gen-qso-profession", ".wqso")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wqso");
    if (!wowee::pipeline::WoweeQuestSortLoader::exists(base)) {
        std::fprintf(stderr, "WQSO not found: %s.wqso\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeQuestSortLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wqso"] = base + ".wqso";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"sortId", e.sortId},
                {"name", e.name},
                {"displayName", e.displayName},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"sortKind", e.sortKind},
                {"sortKindName", wowee::pipeline::WoweeQuestSort::sortKindName(e.sortKind)},
                {"displayPriority", e.displayPriority},
                {"targetProfessionId", e.targetProfessionId},
                {"targetClassMask", e.targetClassMask},
                {"targetFactionId", e.targetFactionId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WQSO: %s.wqso\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sorts   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind         prio  classMask    profId  factionId  displayName\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-11s  %3u   0x%08x   %5u    %5u     %s\n",
                    e.sortId,
                    wowee::pipeline::WoweeQuestSort::sortKindName(e.sortKind),
                    e.displayPriority,
                    e.targetClassMask, e.targetProfessionId,
                    e.targetFactionId,
                    e.displayName.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wqso");
    if (outPath.empty()) outPath = base + ".wqso.json";
    if (!wowee::pipeline::WoweeQuestSortLoader::exists(base)) {
        return reportMissing("export-wqso-json", "WQSO", base, ".wqso");
    }
    auto c = wowee::pipeline::WoweeQuestSortLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"sortId", e.sortId},
            {"name", e.name},
            {"displayName", e.displayName},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"sortKind", e.sortKind},
            {"sortKindName", wowee::pipeline::WoweeQuestSort::sortKindName(e.sortKind)},
            {"displayPriority", e.displayPriority},
            {"targetProfessionId", e.targetProfessionId},
            {"targetClassMask", e.targetClassMask},
            {"targetFactionId", e.targetFactionId},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wqso-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wqso\n", base.c_str());
    std::printf("  sorts  : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wqso");
    outBase = cli::withoutExt(outBase, ".wqso");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wqso-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wqso-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "general")    return wowee::pipeline::WoweeQuestSort::General;
        if (s == "class")      return wowee::pipeline::WoweeQuestSort::ClassQuest;
        if (s == "profession") return wowee::pipeline::WoweeQuestSort::Profession;
        if (s == "daily")      return wowee::pipeline::WoweeQuestSort::Daily;
        if (s == "holiday")    return wowee::pipeline::WoweeQuestSort::Holiday;
        if (s == "reputation") return wowee::pipeline::WoweeQuestSort::Reputation;
        if (s == "dungeon")    return wowee::pipeline::WoweeQuestSort::Dungeon;
        if (s == "raid")       return wowee::pipeline::WoweeQuestSort::Raid;
        if (s == "heroic")     return wowee::pipeline::WoweeQuestSort::Heroic;
        if (s == "repeatable") return wowee::pipeline::WoweeQuestSort::Repeatable;
        if (s == "pvp")        return wowee::pipeline::WoweeQuestSort::PvP;
        if (s == "tournament") return wowee::pipeline::WoweeQuestSort::Tournament;
        return wowee::pipeline::WoweeQuestSort::General;
    };
    wowee::pipeline::WoweeQuestSort c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeQuestSort::Entry e;
            e.sortId = je.value("sortId", 0u);
            e.name = je.value("name", std::string{});
            e.displayName = je.value("displayName", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("sortKind") &&
                je["sortKind"].is_number_integer()) {
                e.sortKind = static_cast<uint8_t>(
                    je["sortKind"].get<int>());
            } else if (je.contains("sortKindName") &&
                       je["sortKindName"].is_string()) {
                e.sortKind = kindFromName(
                    je["sortKindName"].get<std::string>());
            }
            e.displayPriority = static_cast<uint8_t>(
                je.value("displayPriority", 0));
            e.targetProfessionId = static_cast<uint8_t>(
                je.value("targetProfessionId", 0));
            e.targetClassMask = je.value("targetClassMask", 0u);
            e.targetFactionId = je.value("targetFactionId", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeQuestSortLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wqso-json: failed to save %s.wqso\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wqso\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  sorts  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeQuestSortLoader>(
        i, argc, argv, "wqso", "WQSO",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.sortId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.sortId == 0)
                errors.push_back(ctx + ": sortId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.displayName.empty())
                errors.push_back(ctx +
                    ": displayName is empty (UI would show no header)");
            if (e.sortKind > wowee::pipeline::WoweeQuestSort::Tournament) {
                errors.push_back(ctx + ": sortKind " +
                    std::to_string(e.sortKind) + " not in 0..11");
            }
            // ClassQuest sortKind requires a non-zero classMask
            // — otherwise it's not actually class-restricted.
            if (e.sortKind == wowee::pipeline::WoweeQuestSort::ClassQuest &&
                e.targetClassMask == 0) {
                errors.push_back(ctx +
                    ": ClassQuest kind with targetClassMask=0 "
                    "(should pick at least one class bit)");
            }
            // Profession sortKind requires a profession ID hint —
            // 0 means Blacksmithing in the WTSK enum but having
            // it left as zero with non-Blacksmithing kind might
            // be a typo. Warn rather than error since 0 IS a
            // valid profession value.
            if (e.sortKind == wowee::pipeline::WoweeQuestSort::Profession &&
                e.targetProfessionId == 0 &&
                e.name.find("Blacksmith") == std::string::npos) {
                warnings.push_back(ctx +
                    ": Profession kind with targetProfessionId=0 "
                    "(0=Blacksmithing in WTSK; verify intent)");
            }
            // Reputation sortKind needs a factionId.
            if (e.sortKind == wowee::pipeline::WoweeQuestSort::Reputation &&
                e.targetFactionId == 0) {
                errors.push_back(ctx +
                    ": Reputation kind with targetFactionId=0 "
                    "(no faction to grind reputation with)");
            }
            if (!idsSeen.add(e.sortId)) errors.push_back(ctx + ": duplicate sortId");
        }
            return formatted("%zu sorts, all sortIds unique, all kind-target pairings consistent", c.entries.size());
        });
}

} // namespace

bool handleQuestSortsCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-qso") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-qso-class") == 0 && i + 1 < argc) {
        outRc = handleGenClass(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-qso-profession") == 0 &&
        i + 1 < argc) {
        outRc = handleGenProfession(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wqso") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wqso") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wqso-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wqso-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
