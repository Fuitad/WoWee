#include "cli_skill_costs_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_skill_costs.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
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


void printGenSummary(const wowee::pipeline::WoweeSkillCost& c,
                     const std::string& base) {
    std::printf("Wrote %s.wscs\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
}

int handleGenProfession(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ProfessionSkillCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscs");
    auto c = wowee::pipeline::WoweeSkillCostLoader::makeProfession(name);
    if (!saveOrError<wowee::pipeline::WoweeSkillCostLoader>(c, base, "gen-scs", ".wscs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWeapon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeaponSkillCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscs");
    auto c = wowee::pipeline::WoweeSkillCostLoader::makeWeapon(name);
    if (!saveOrError<wowee::pipeline::WoweeSkillCostLoader>(c, base, "gen-scs-weapon", ".wscs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRiding(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RidingSkillCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscs");
    auto c = wowee::pipeline::WoweeSkillCostLoader::makeRiding(name);
    if (!saveOrError<wowee::pipeline::WoweeSkillCostLoader>(c, base, "gen-scs-riding", ".wscs")) return 1;
    printGenSummary(c, base);
    return 0;
}

void formatGold(uint32_t copper, char* buf, size_t bufSize) {
    uint32_t g = copper / 10000;
    uint32_t s = (copper % 10000) / 100;
    uint32_t cop = copper % 100;
    if (g > 0) {
        std::snprintf(buf, bufSize, "%ug %us %uc", g, s, cop);
    } else if (s > 0) {
        std::snprintf(buf, bufSize, "%us %uc", s, cop);
    } else {
        std::snprintf(buf, bufSize, "%uc", cop);
    }
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wscs");
    if (!wowee::pipeline::WoweeSkillCostLoader::exists(base)) {
        std::fprintf(stderr, "WSCS not found: %s.wscs\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSkillCostLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wscs"] = base + ".wscs";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"costId", e.costId},
                {"name", e.name},
                {"description", e.description},
                {"skillRankIndex", e.skillRankIndex},
                {"minSkillToLearn", e.minSkillToLearn},
                {"maxSkillUnlocked", e.maxSkillUnlocked},
                {"requiredLevel", e.requiredLevel},
                {"costKind", e.costKind},
                {"costKindName", wowee::pipeline::WoweeSkillCost::costKindName(e.costKind)},
                {"copperCost", e.copperCost},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSCS: %s.wscs\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   rank  kind          minSkill  maxSkill  lvl   cost           name\n");
    for (const auto& e : c.entries) {
        char goldBuf[32];
        formatGold(e.copperCost, goldBuf, sizeof(goldBuf));
        std::printf("  %4u   %2u   %-11s    %5u     %5u   %3u   %-13s  %s\n",
                    e.costId, e.skillRankIndex,
                    wowee::pipeline::WoweeSkillCost::costKindName(e.costKind),
                    e.minSkillToLearn, e.maxSkillUnlocked,
                    e.requiredLevel, goldBuf,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wscs");
    if (!wowee::pipeline::WoweeSkillCostLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wscs-json: WSCS not found: %s.wscs\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSkillCostLoader::load(base);
    if (outPath.empty()) outPath = base + ".wscs.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["costId"] = e.costId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["skillRankIndex"] = e.skillRankIndex;
        je["minSkillToLearn"] = e.minSkillToLearn;
        je["maxSkillUnlocked"] = e.maxSkillUnlocked;
        je["requiredLevel"] = e.requiredLevel;
        je["costKind"] = e.costKind;
        je["costKindName"] =
            wowee::pipeline::WoweeSkillCost::costKindName(e.costKind);
        je["copperCost"] = e.copperCost;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wscs-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseCostKindToken(const nlohmann::json& jv,
                           uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSkillCost::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "profession")  return wowee::pipeline::WoweeSkillCost::Profession;
        if (s == "weapon" ||
            s == "weaponskill") return wowee::pipeline::WoweeSkillCost::WeaponSkill;
        if (s == "riding" ||
            s == "ridingskill") return wowee::pipeline::WoweeSkillCost::RidingSkill;
        if (s == "class-skill" ||
            s == "classskill")  return wowee::pipeline::WoweeSkillCost::ClassSkill;
        if (s == "misc")        return wowee::pipeline::WoweeSkillCost::Misc;
    }
    return fallback;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wscs-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wscs-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSkillCost c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSkillCost::Entry e;
            if (je.contains("costId"))           e.costId = je["costId"].get<uint32_t>();
            if (je.contains("name"))             e.name = je["name"].get<std::string>();
            if (je.contains("description"))      e.description = je["description"].get<std::string>();
            if (je.contains("skillRankIndex"))   e.skillRankIndex = je["skillRankIndex"].get<uint32_t>();
            if (je.contains("minSkillToLearn"))  e.minSkillToLearn = je["minSkillToLearn"].get<uint16_t>();
            if (je.contains("maxSkillUnlocked")) e.maxSkillUnlocked = je["maxSkillUnlocked"].get<uint16_t>();
            if (je.contains("requiredLevel"))    e.requiredLevel = je["requiredLevel"].get<uint8_t>();
            uint8_t kind = wowee::pipeline::WoweeSkillCost::Profession;
            if (je.contains("costKind"))
                kind = parseCostKindToken(je["costKind"], kind);
            else if (je.contains("costKindName"))
                kind = parseCostKindToken(je["costKindName"], kind);
            e.costKind = kind;
            if (je.contains("copperCost"))    e.copperCost = je["copperCost"].get<uint32_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wscs");
    outBase = cli::withoutExt(outBase, ".wscs");
    if (!wowee::pipeline::WoweeSkillCostLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wscs-json: failed to save %s.wscs\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wscs\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wscs");
    if (!wowee::pipeline::WoweeSkillCostLoader::exists(base)) {
        return reportMissing("validate-wscs", "WSCS", base, ".wscs");
    }
    auto c = wowee::pipeline::WoweeSkillCostLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.costId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.costId == 0)
            errors.push_back(ctx + ": costId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.costKind > wowee::pipeline::WoweeSkillCost::Misc) {
            errors.push_back(ctx + ": costKind " +
                std::to_string(e.costKind) + " not in 0..4");
        }
        if (e.minSkillToLearn >= e.maxSkillUnlocked) {
            errors.push_back(ctx +
                ": minSkillToLearn " +
                std::to_string(e.minSkillToLearn) +
                " >= maxSkillUnlocked " +
                std::to_string(e.maxSkillUnlocked) +
                " — tier provides no skill range");
        }
        if (e.requiredLevel > 80) {
            warnings.push_back(ctx +
                ": requiredLevel " +
                std::to_string(e.requiredLevel) +
                " > 80 — tier unreachable at WotLK cap");
        }
        // Riding skill at lvl < 20 is unusual (Apprentice
        // requires lvl 20).
        if (e.costKind == wowee::pipeline::WoweeSkillCost::RidingSkill &&
            e.requiredLevel < 20) {
            warnings.push_back(ctx +
                ": Riding skill with requiredLevel=" +
                std::to_string(e.requiredLevel) +
                " < 20 — canonical Apprentice Riding unlocks "
                "at level 20");
        }
        // Profession with cost=0 is unusual — every standard
        // profession tier costs at least a copper.
        if (e.costKind == wowee::pipeline::WoweeSkillCost::Profession &&
            e.copperCost == 0) {
            warnings.push_back(ctx +
                ": Profession kind with copperCost=0 — "
                "unusual, profession tiers normally cost "
                "at least a copper");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.costId) {
                errors.push_back(ctx + ": duplicate costId");
                break;
            }
        }
        idsSeen.push_back(e.costId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wscs", base, errors, warnings);
    std::printf("validate-wscs: %s.wscs\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu tiers, all costIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSkillCostsCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-scs") == 0 && i + 1 < argc) {
        outRc = handleGenProfession(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-scs-weapon") == 0 && i + 1 < argc) {
        outRc = handleGenWeapon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-scs-riding") == 0 && i + 1 < argc) {
        outRc = handleGenRiding(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wscs") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wscs") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wscs-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wscs-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
