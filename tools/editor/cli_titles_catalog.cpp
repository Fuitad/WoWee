#include "cli_titles_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_titles.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeTitle& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtit\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  titles  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterTitles";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtit");
    auto c = wowee::pipeline::WoweeTitleLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeTitleLoader>(c, base, "gen-titles", ".wtit")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPvp(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvpTitles";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtit");
    auto c = wowee::pipeline::WoweeTitleLoader::makePvp(name);
    if (!saveOrError<wowee::pipeline::WoweeTitleLoader>(c, base, "gen-titles-pvp", ".wtit")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAchievement(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AchievementTitles";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtit");
    auto c = wowee::pipeline::WoweeTitleLoader::makeAchievement(name);
    if (!saveOrError<wowee::pipeline::WoweeTitleLoader>(c, base, "gen-titles-achievement", ".wtit")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtit");
    if (!wowee::pipeline::WoweeTitleLoader::exists(base)) {
        std::fprintf(stderr, "WTIT not found: %s.wtit\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTitleLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtit"] = base + ".wtit";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"titleId", e.titleId},
                {"name", e.name},
                {"nameMale", e.nameMale},
                {"nameFemale", e.nameFemale},
                {"iconPath", e.iconPath},
                {"prefix", e.prefix},
                {"prefixName", e.prefix ? "prefix" : "suffix"},
                {"category", e.category},
                {"categoryName", wowee::pipeline::WoweeTitle::categoryName(e.category)},
                {"sortOrder", e.sortOrder},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTIT: %s.wtit\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  titles  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   sort   pos     category      name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %4u   %-6s  %-12s  %s\n",
                    e.titleId, e.sortOrder,
                    e.prefix ? "prefix" : "suffix",
                    wowee::pipeline::WoweeTitle::categoryName(e.category),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each title emits all 8 scalar fields
    // plus dual int + name forms for category and prefix.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wtit");
    if (outPath.empty()) outPath = base + ".wtit.json";
    if (!wowee::pipeline::WoweeTitleLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtit-json: WTIT not found: %s.wtit\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTitleLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"titleId", e.titleId},
            {"name", e.name},
            {"nameMale", e.nameMale},
            {"nameFemale", e.nameFemale},
            {"iconPath", e.iconPath},
            {"prefix", e.prefix},
            {"prefixName", e.prefix ? "prefix" : "suffix"},
            {"category", e.category},
            {"categoryName", wowee::pipeline::WoweeTitle::categoryName(e.category)},
            {"sortOrder", e.sortOrder},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wtit-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wtit\n", base.c_str());
    std::printf("  titles : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wtit");
    outBase = cli::withoutExt(outBase, ".wtit");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wtit-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wtit-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto categoryFromName = [](const std::string& s) -> uint8_t {
        if (s == "achievement") return wowee::pipeline::WoweeTitle::Achievement;
        if (s == "pvp")         return wowee::pipeline::WoweeTitle::Pvp;
        if (s == "raid")        return wowee::pipeline::WoweeTitle::Raid;
        if (s == "class")       return wowee::pipeline::WoweeTitle::ClassTitle;
        if (s == "event")       return wowee::pipeline::WoweeTitle::Event;
        if (s == "profession")  return wowee::pipeline::WoweeTitle::Profession;
        if (s == "lore")        return wowee::pipeline::WoweeTitle::Lore;
        if (s == "custom")      return wowee::pipeline::WoweeTitle::Custom;
        return wowee::pipeline::WoweeTitle::Achievement;
    };
    wowee::pipeline::WoweeTitle c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeTitle::Entry e;
            e.titleId = je.value("titleId", 0u);
            e.name = je.value("name", std::string{});
            e.nameMale = je.value("nameMale", std::string{});
            e.nameFemale = je.value("nameFemale", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("prefix") && je["prefix"].is_number_integer()) {
                e.prefix = static_cast<uint8_t>(je["prefix"].get<int>());
            } else if (je.contains("prefixName") && je["prefixName"].is_string()) {
                e.prefix = je["prefixName"].get<std::string>() == "prefix" ? 1 : 0;
            }
            if (je.contains("category") && je["category"].is_number_integer()) {
                e.category = static_cast<uint8_t>(je["category"].get<int>());
            } else if (je.contains("categoryName") && je["categoryName"].is_string()) {
                e.category = categoryFromName(je["categoryName"].get<std::string>());
            }
            e.sortOrder = static_cast<uint16_t>(je.value("sortOrder", 0));
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeTitleLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtit-json: failed to save %s.wtit\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtit\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  titles : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtit");
    if (!wowee::pipeline::WoweeTitleLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtit: WTIT not found: %s.wtit\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTitleLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.titleId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.titleId == 0) errors.push_back(ctx + ": titleId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.category > wowee::pipeline::WoweeTitle::Custom) {
            errors.push_back(ctx + ": category " +
                std::to_string(e.category) + " not in 0..7");
        }
        // If gender variants are set, both should be set (otherwise
        // the runtime fall-back to canonical leaves one gender
        // displaying the wrong form).
        if (!e.nameMale.empty() && e.nameFemale.empty()) {
            warnings.push_back(ctx +
                ": nameMale set but nameFemale empty (mixed-gender display)");
        }
        if (!e.nameFemale.empty() && e.nameMale.empty()) {
            warnings.push_back(ctx +
                ": nameFemale set but nameMale empty (mixed-gender display)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.titleId) {
                errors.push_back(ctx + ": duplicate titleId");
                break;
            }
        }
        idsSeen.push_back(e.titleId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wtit", base, errors, warnings);
    std::printf("validate-wtit: %s.wtit\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu titles, all titleIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleTitlesCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-titles") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-titles-pvp") == 0 && i + 1 < argc) {
        outRc = handleGenPvp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-titles-achievement") == 0 && i + 1 < argc) {
        outRc = handleGenAchievement(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtit") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtit") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtit-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtit-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
