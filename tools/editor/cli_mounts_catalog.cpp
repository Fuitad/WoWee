#include "cli_mounts_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_mounts.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeMount& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeMountLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wmou\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeMount& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmou\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  mounts  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterMounts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmou");
    auto c = wowee::pipeline::WoweeMountLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-mounts")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRacial(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RacialMounts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmou");
    auto c = wowee::pipeline::WoweeMountLoader::makeRacial(name);
    if (!saveOrError(c, base, "gen-mounts-racial")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFlying(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FlyingMounts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmou");
    auto c = wowee::pipeline::WoweeMountLoader::makeFlying(name);
    if (!saveOrError(c, base, "gen-mounts-flying")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmou");
    if (!wowee::pipeline::WoweeMountLoader::exists(base)) {
        std::fprintf(stderr, "WMOU not found: %s.wmou\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMountLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmou"] = base + ".wmou";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"mountId", e.mountId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"displayId", e.displayId},
                {"summonSpellId", e.summonSpellId},
                {"itemIdToLearn", e.itemIdToLearn},
                {"requiredSkillId", e.requiredSkillId},
                {"requiredSkillRank", e.requiredSkillRank},
                {"speedPercent", e.speedPercent},
                {"mountKind", e.mountKind},
                {"mountKindName", wowee::pipeline::WoweeMount::kindName(e.mountKind)},
                {"factionId", e.factionId},
                {"factionName", wowee::pipeline::WoweeMount::factionName(e.factionId)},
                {"categoryId", e.categoryId},
                {"categoryName", wowee::pipeline::WoweeMount::categoryName(e.categoryId)},
                {"raceMask", e.raceMask},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMOU: %s.wmou\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  mounts  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind     speed  rank   faction    category      name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-7s  %4u%%  %4u   %-9s  %-12s  %s\n",
                    e.mountId,
                    wowee::pipeline::WoweeMount::kindName(e.mountKind),
                    e.speedPercent, e.requiredSkillRank,
                    wowee::pipeline::WoweeMount::factionName(e.factionId),
                    wowee::pipeline::WoweeMount::categoryName(e.categoryId),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each mount emits all 14 scalar fields
    // plus dual int + name forms for kind / faction /
    // category.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wmou");
    if (outPath.empty()) outPath = base + ".wmou.json";
    if (!wowee::pipeline::WoweeMountLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wmou-json: WMOU not found: %s.wmou\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMountLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"mountId", e.mountId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"displayId", e.displayId},
            {"summonSpellId", e.summonSpellId},
            {"itemIdToLearn", e.itemIdToLearn},
            {"requiredSkillId", e.requiredSkillId},
            {"requiredSkillRank", e.requiredSkillRank},
            {"speedPercent", e.speedPercent},
            {"mountKind", e.mountKind},
            {"mountKindName", wowee::pipeline::WoweeMount::kindName(e.mountKind)},
            {"factionId", e.factionId},
            {"factionName", wowee::pipeline::WoweeMount::factionName(e.factionId)},
            {"categoryId", e.categoryId},
            {"categoryName", wowee::pipeline::WoweeMount::categoryName(e.categoryId)},
            {"raceMask", e.raceMask},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wmou-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wmou\n", base.c_str());
    std::printf("  mounts : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wmou.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wmou");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wmou-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wmou-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "ground")   return wowee::pipeline::WoweeMount::Ground;
        if (s == "flying")   return wowee::pipeline::WoweeMount::Flying;
        if (s == "swimming") return wowee::pipeline::WoweeMount::Swimming;
        if (s == "hybrid")   return wowee::pipeline::WoweeMount::Hybrid;
        if (s == "aquatic")  return wowee::pipeline::WoweeMount::Aquatic;
        return wowee::pipeline::WoweeMount::Ground;
    };
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "both")     return wowee::pipeline::WoweeMount::Both;
        if (s == "alliance") return wowee::pipeline::WoweeMount::Alliance;
        if (s == "horde")    return wowee::pipeline::WoweeMount::Horde;
        return wowee::pipeline::WoweeMount::Both;
    };
    auto categoryFromName = [](const std::string& s) -> uint8_t {
        if (s == "common")      return wowee::pipeline::WoweeMount::Common;
        if (s == "epic")        return wowee::pipeline::WoweeMount::Epic;
        if (s == "racial")      return wowee::pipeline::WoweeMount::Racial;
        if (s == "event")       return wowee::pipeline::WoweeMount::Event;
        if (s == "achievement") return wowee::pipeline::WoweeMount::Achievement;
        if (s == "pvp")         return wowee::pipeline::WoweeMount::Pvp;
        if (s == "quest")       return wowee::pipeline::WoweeMount::Quest;
        if (s == "class")       return wowee::pipeline::WoweeMount::ClassMount;
        return wowee::pipeline::WoweeMount::Common;
    };
    wowee::pipeline::WoweeMount c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeMount::Entry e;
            e.mountId = je.value("mountId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            e.displayId = je.value("displayId", 0u);
            e.summonSpellId = je.value("summonSpellId", 0u);
            e.itemIdToLearn = je.value("itemIdToLearn", 0u);
            e.requiredSkillId = je.value("requiredSkillId", 0u);
            e.requiredSkillRank = static_cast<uint16_t>(
                je.value("requiredSkillRank", 0));
            e.speedPercent = static_cast<uint16_t>(
                je.value("speedPercent", 60));
            if (je.contains("mountKind") && je["mountKind"].is_number_integer()) {
                e.mountKind = static_cast<uint8_t>(je["mountKind"].get<int>());
            } else if (je.contains("mountKindName") &&
                       je["mountKindName"].is_string()) {
                e.mountKind = kindFromName(je["mountKindName"].get<std::string>());
            }
            if (je.contains("factionId") && je["factionId"].is_number_integer()) {
                e.factionId = static_cast<uint8_t>(je["factionId"].get<int>());
            } else if (je.contains("factionName") &&
                       je["factionName"].is_string()) {
                e.factionId = factionFromName(je["factionName"].get<std::string>());
            }
            if (je.contains("categoryId") && je["categoryId"].is_number_integer()) {
                e.categoryId = static_cast<uint8_t>(je["categoryId"].get<int>());
            } else if (je.contains("categoryName") &&
                       je["categoryName"].is_string()) {
                e.categoryId = categoryFromName(je["categoryName"].get<std::string>());
            }
            e.raceMask = je.value("raceMask", 0u);
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeMountLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmou-json: failed to save %s.wmou\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmou\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  mounts : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmou");
    if (!wowee::pipeline::WoweeMountLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wmou: WMOU not found: %s.wmou\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMountLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.mountId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.mountId == 0) errors.push_back(ctx + ": mountId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.summonSpellId == 0) {
            errors.push_back(ctx + ": summonSpellId is 0 (mount cannot be cast)");
        }
        if (e.mountKind > wowee::pipeline::WoweeMount::Aquatic) {
            errors.push_back(ctx + ": mountKind " +
                std::to_string(e.mountKind) + " not in 0..4");
        }
        if (e.factionId > wowee::pipeline::WoweeMount::Horde) {
            errors.push_back(ctx + ": factionId " +
                std::to_string(e.factionId) + " not in 0..2");
        }
        if (e.categoryId > wowee::pipeline::WoweeMount::ClassMount) {
            errors.push_back(ctx + ": categoryId " +
                std::to_string(e.categoryId) + " not in 0..7");
        }
        if (e.speedPercent == 0) {
            warnings.push_back(ctx +
                ": speedPercent=0 (mount provides no speed bonus)");
        }
        // Flying / Hybrid mounts need >= journeyman riding
        // (rank 150 in canonical Classic+TBC scaling).
        if ((e.mountKind == wowee::pipeline::WoweeMount::Flying ||
             e.mountKind == wowee::pipeline::WoweeMount::Hybrid) &&
            e.requiredSkillRank < 150) {
            warnings.push_back(ctx +
                ": flying mount with riding rank < 150 (player can't fly)");
        }
        // Racial category needs raceMask; non-racial shouldn't have one.
        if (e.categoryId == wowee::pipeline::WoweeMount::Racial &&
            e.raceMask == 0) {
            warnings.push_back(ctx +
                ": Racial category but raceMask=0 (any race can use)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.mountId) {
                errors.push_back(ctx + ": duplicate mountId");
                break;
            }
        }
        idsSeen.push_back(e.mountId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wmou", base, errors, warnings);
    std::printf("validate-wmou: %s.wmou\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu mounts, all mountIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleMountsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-mounts") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mounts-racial") == 0 && i + 1 < argc) {
        outRc = handleGenRacial(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mounts-flying") == 0 && i + 1 < argc) {
        outRc = handleGenFlying(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmou") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmou") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmou-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmou-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
