#include "cli_skills_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_skills.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeSkill& c,
                     const std::string& base) {
    std::printf("Wrote %s.wskl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  skills  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterSkills";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wskl");
    auto c = wowee::pipeline::WoweeSkillLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeSkillLoader>(c, base, "gen-skills", ".wskl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenProfessions(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ProfessionSkills";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wskl");
    auto c = wowee::pipeline::WoweeSkillLoader::makeProfessions(name);
    if (!saveOrError<wowee::pipeline::WoweeSkillLoader>(c, base, "gen-skills-professions", ".wskl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWeapons(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeaponSkills";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wskl");
    auto c = wowee::pipeline::WoweeSkillLoader::makeWeapons(name);
    if (!saveOrError<wowee::pipeline::WoweeSkillLoader>(c, base, "gen-skills-weapons", ".wskl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wskl");
    if (!wowee::pipeline::WoweeSkillLoader::exists(base)) {
        std::fprintf(stderr, "WSKL not found: %s.wskl\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSkillLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wskl"] = base + ".wskl";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"skillId", e.skillId},
                {"name", e.name},
                {"description", e.description},
                {"categoryId", e.categoryId},
                {"categoryName", wowee::pipeline::WoweeSkill::categoryName(e.categoryId)},
                {"canTrain", e.canTrain},
                {"maxRank", e.maxRank},
                {"rankPerLevel", e.rankPerLevel},
                {"iconPath", e.iconPath},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSKL: %s.wskl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  skills  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   category     max  /lvl  train   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-11s  %3u   %2u    %s     %s\n",
                    e.skillId,
                    wowee::pipeline::WoweeSkill::categoryName(e.categoryId),
                    e.maxRank, e.rankPerLevel,
                    e.canTrain ? "yes" : "no ",
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each skill emits all 8 scalar fields plus
    // dual int + name forms for categoryId.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wskl");
    if (outPath.empty()) outPath = base + ".wskl.json";
    if (!wowee::pipeline::WoweeSkillLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wskl-json: WSKL not found: %s.wskl\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSkillLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"skillId", e.skillId},
            {"name", e.name},
            {"description", e.description},
            {"categoryId", e.categoryId},
            {"categoryName", wowee::pipeline::WoweeSkill::categoryName(e.categoryId)},
            {"canTrain", e.canTrain},
            {"maxRank", e.maxRank},
            {"rankPerLevel", e.rankPerLevel},
            {"iconPath", e.iconPath},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wskl-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wskl\n", base.c_str());
    std::printf("  skills : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wskl");
    outBase = cli::withoutExt(outBase, ".wskl");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wskl-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wskl-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto categoryFromName = [](const std::string& s) -> uint8_t {
        if (s == "weapon")      return wowee::pipeline::WoweeSkill::Weapon;
        if (s == "class")       return wowee::pipeline::WoweeSkill::Class;
        if (s == "profession")  return wowee::pipeline::WoweeSkill::Profession;
        if (s == "secondary")   return wowee::pipeline::WoweeSkill::SecondaryProfession;
        if (s == "language")    return wowee::pipeline::WoweeSkill::Language;
        if (s == "armor")       return wowee::pipeline::WoweeSkill::ArmorProficiency;
        if (s == "riding")      return wowee::pipeline::WoweeSkill::Riding;
        if (s == "weapon-spec") return wowee::pipeline::WoweeSkill::WeaponSpec;
        return wowee::pipeline::WoweeSkill::Profession;
    };
    wowee::pipeline::WoweeSkill c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSkill::Entry e;
            e.skillId = je.value("skillId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("categoryId") && je["categoryId"].is_number_integer()) {
                e.categoryId = static_cast<uint8_t>(je["categoryId"].get<int>());
            } else if (je.contains("categoryName") && je["categoryName"].is_string()) {
                e.categoryId = categoryFromName(je["categoryName"].get<std::string>());
            }
            e.canTrain = static_cast<uint8_t>(je.value("canTrain", 1));
            e.maxRank = static_cast<uint16_t>(je.value("maxRank", 300));
            e.rankPerLevel = static_cast<uint16_t>(je.value("rankPerLevel", 0));
            e.iconPath = je.value("iconPath", std::string{});
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeSkillLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wskl-json: failed to save %s.wskl\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wskl\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  skills : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wskl");
    if (!wowee::pipeline::WoweeSkillLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wskl: WSKL not found: %s.wskl\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSkillLoader::load(base);
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
                          " (id=" + std::to_string(e.skillId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.skillId == 0) {
            errors.push_back(ctx + ": skillId is 0");
        }
        if (e.name.empty()) {
            errors.push_back(ctx + ": name is empty");
        }
        if (e.maxRank == 0) {
            errors.push_back(ctx + ": maxRank is 0");
        }
        if (e.categoryId > wowee::pipeline::WoweeSkill::WeaponSpec) {
            errors.push_back(ctx + ": categoryId " +
                std::to_string(e.categoryId) + " not in 0..7");
        }
        // Languages have maxRank=1 (you either know it or you don't);
        // anything else with maxRank=1 is suspicious.
        if (e.maxRank == 1 &&
            e.categoryId != wowee::pipeline::WoweeSkill::Language) {
            warnings.push_back(ctx +
                ": maxRank=1 on non-Language skill (only languages cap at 1)");
        }
        // Weapon skills should auto-grow (rankPerLevel > 0).
        if (e.categoryId == wowee::pipeline::WoweeSkill::Weapon &&
            e.rankPerLevel == 0) {
            warnings.push_back(ctx +
                ": weapon skill with rankPerLevel=0 (won't auto-grow on use)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.skillId) {
                errors.push_back(ctx + ": duplicate skillId");
                break;
            }
        }
        idsSeen.push_back(e.skillId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wskl", base, errors, warnings);
    std::printf("validate-wskl: %s.wskl\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu skills, all skillIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSkillsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-skills") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-skills-professions") == 0 && i + 1 < argc) {
        outRc = handleGenProfessions(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-skills-weapons") == 0 && i + 1 < argc) {
        outRc = handleGenWeapons(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wskl") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wskl") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wskl-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wskl-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
