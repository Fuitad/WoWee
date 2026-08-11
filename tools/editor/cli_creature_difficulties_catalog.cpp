#include "cli_creature_difficulties_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_creature_difficulties.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeCreatureDifficulty& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcdf\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  routes  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterDifficulties";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcdf");
    auto c = wowee::pipeline::WoweeCreatureDifficultyLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureDifficultyLoader>(c, base, "gen-cdf", ".wcdf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWotlkRaid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WotlkICCBosses";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcdf");
    auto c = wowee::pipeline::WoweeCreatureDifficultyLoader::makeWotlkRaid(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureDifficultyLoader>(c, base, "gen-cdf-wotlk-raid", ".wcdf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFiveMan(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FiveManDungeons";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcdf");
    auto c = wowee::pipeline::WoweeCreatureDifficultyLoader::makeFiveMan(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureDifficultyLoader>(c, base, "gen-cdf-fiveman", ".wcdf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcdf");
    if (!wowee::pipeline::WoweeCreatureDifficultyLoader::exists(base)) {
        std::fprintf(stderr, "WCDF not found: %s.wcdf\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureDifficultyLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcdf"] = base + ".wcdf";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"difficultyId", e.difficultyId},
                {"name", e.name},
                {"description", e.description},
                {"baseCreatureId", e.baseCreatureId},
                {"normal10Id", e.normal10Id},
                {"normal25Id", e.normal25Id},
                {"heroic10Id", e.heroic10Id},
                {"heroic25Id", e.heroic25Id},
                {"spawnGroupKind", e.spawnGroupKind},
                {"spawnGroupKindName", wowee::pipeline::WoweeCreatureDifficulty::spawnGroupKindName(e.spawnGroupKind)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCDF: %s.wcdf\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  routes  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind         base    n10    n25    h10    h25    name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %-10s %5u  %5u  %5u  %5u  %5u    %s\n",
                    e.difficultyId,
                    wowee::pipeline::WoweeCreatureDifficulty::spawnGroupKindName(e.spawnGroupKind),
                    e.baseCreatureId,
                    e.normal10Id, e.normal25Id,
                    e.heroic10Id, e.heroic25Id,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wcdf");
    if (!wowee::pipeline::WoweeCreatureDifficultyLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wcdf-json: WCDF not found: %s.wcdf\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureDifficultyLoader::load(base);
    if (outPath.empty()) outPath = base + ".wcdf.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["difficultyId"] = e.difficultyId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["baseCreatureId"] = e.baseCreatureId;
        je["normal10Id"] = e.normal10Id;
        je["normal25Id"] = e.normal25Id;
        je["heroic10Id"] = e.heroic10Id;
        je["heroic25Id"] = e.heroic25Id;
        je["spawnGroupKind"] = e.spawnGroupKind;
        je["spawnGroupKindName"] =
            wowee::pipeline::WoweeCreatureDifficulty::spawnGroupKindName(e.spawnGroupKind);
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wcdf-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  routes  : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseSpawnGroupKindToken(const nlohmann::json& jv,
                                 uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeCreatureDifficulty::WorldBoss)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "boss")        return wowee::pipeline::WoweeCreatureDifficulty::Boss;
        if (s == "mini-boss" ||
            s == "miniboss")    return wowee::pipeline::WoweeCreatureDifficulty::MiniBoss;
        if (s == "rare-elite" ||
            s == "rareelite")   return wowee::pipeline::WoweeCreatureDifficulty::RareElite;
        if (s == "trash")       return wowee::pipeline::WoweeCreatureDifficulty::Trash;
        if (s == "add")         return wowee::pipeline::WoweeCreatureDifficulty::Add;
        if (s == "world-boss" ||
            s == "worldboss")   return wowee::pipeline::WoweeCreatureDifficulty::WorldBoss;
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
            "import-wcdf-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcdf-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCreatureDifficulty c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCreatureDifficulty::Entry e;
            if (je.contains("difficultyId"))   e.difficultyId = je["difficultyId"].get<uint32_t>();
            if (je.contains("name"))           e.name = je["name"].get<std::string>();
            if (je.contains("description"))    e.description = je["description"].get<std::string>();
            if (je.contains("baseCreatureId")) e.baseCreatureId = je["baseCreatureId"].get<uint32_t>();
            if (je.contains("normal10Id"))     e.normal10Id = je["normal10Id"].get<uint32_t>();
            if (je.contains("normal25Id"))     e.normal25Id = je["normal25Id"].get<uint32_t>();
            if (je.contains("heroic10Id"))     e.heroic10Id = je["heroic10Id"].get<uint32_t>();
            if (je.contains("heroic25Id"))     e.heroic25Id = je["heroic25Id"].get<uint32_t>();
            uint8_t kind = wowee::pipeline::WoweeCreatureDifficulty::Boss;
            if (je.contains("spawnGroupKind"))
                kind = parseSpawnGroupKindToken(je["spawnGroupKind"], kind);
            else if (je.contains("spawnGroupKindName"))
                kind = parseSpawnGroupKindToken(je["spawnGroupKindName"], kind);
            e.spawnGroupKind = kind;
            if (je.contains("iconColorRGBA"))
                e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wcdf");
    outBase = cli::withoutExt(outBase, ".wcdf");
    if (!wowee::pipeline::WoweeCreatureDifficultyLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcdf-json: failed to save %s.wcdf\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcdf\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  routes  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcdf");
    if (!wowee::pipeline::WoweeCreatureDifficultyLoader::exists(base)) {
        return reportMissing("validate-wcdf", "WCDF", base, ".wcdf");
    }
    auto c = wowee::pipeline::WoweeCreatureDifficultyLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    std::vector<uint32_t> baseSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.difficultyId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.difficultyId == 0)
            errors.push_back(ctx + ": difficultyId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.spawnGroupKind > wowee::pipeline::WoweeCreatureDifficulty::WorldBoss) {
            errors.push_back(ctx + ": spawnGroupKind " +
                std::to_string(e.spawnGroupKind) + " not in 0..5");
        }
        if (e.baseCreatureId == 0)
            errors.push_back(ctx +
                ": baseCreatureId is 0 — missing WCRT cross-ref");
        // World bosses don't scale, so all 4 variant fields
        // should be 0 (engine falls through to base).
        if (e.spawnGroupKind == wowee::pipeline::WoweeCreatureDifficulty::WorldBoss &&
            (e.normal10Id || e.normal25Id || e.heroic10Id || e.heroic25Id)) {
            warnings.push_back(ctx +
                ": WorldBoss kind with non-zero variant ids — "
                "world bosses don't scale, set variant fields to 0");
        }
        // The asymmetric case n25 set without n10 is
        // suspicious — typically a typo, since raid
        // sequencing always introduces n10 alongside n25.
        // (5-man bosses legitimately have only n10/h10, so
        // we don't warn on missing n25 alone.)
        if (e.spawnGroupKind == wowee::pipeline::WoweeCreatureDifficulty::Boss &&
            e.normal25Id && !e.normal10Id) {
            warnings.push_back(ctx +
                ": Boss has normal25Id but not normal10Id — "
                "raid sequencing introduces n10 alongside n25; "
                "this is probably a typo");
        }
        if (!idsSeen.add(e.difficultyId)) errors.push_back(ctx + ": duplicate difficultyId");
        // Two routes for the same base creature collide —
        // engine would only honor the first.
        if (e.baseCreatureId != 0) {
            for (uint32_t prevBase : baseSeen) {
                if (prevBase == e.baseCreatureId) {
                    warnings.push_back(ctx +
                        ": duplicate baseCreatureId " +
                        std::to_string(e.baseCreatureId) +
                        " — only the first route entry will be honored");
                    break;
                }
            }
            baseSeen.push_back(e.baseCreatureId);
        }
        // Check for self-reference loops (base == any
        // variant) which are valid for world bosses but
        // nonsensical otherwise.
        if (e.spawnGroupKind != wowee::pipeline::WoweeCreatureDifficulty::WorldBoss) {
            if ((e.normal10Id == e.baseCreatureId &&
                 e.normal25Id == e.baseCreatureId &&
                 e.heroic10Id == e.baseCreatureId &&
                 e.heroic25Id == e.baseCreatureId) &&
                e.normal10Id != 0) {
                warnings.push_back(ctx +
                    ": all four variants point at baseCreatureId — "
                    "creature doesn't scale; consider WorldBoss kind");
            }
        }
    }
    return cli::reportValidation("wcdf", base, jsonOut, errors, warnings,
                                 formatted("%zu routes, all difficultyIds unique, all base ids set", c.entries.size()));
}

} // namespace

bool handleCreatureDifficultiesCatalog(int& i, int argc,
                                       char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-cdf") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cdf-wotlk-raid") == 0 && i + 1 < argc) {
        outRc = handleGenWotlkRaid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cdf-fiveman") == 0 && i + 1 < argc) {
        outRc = handleGenFiveMan(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcdf") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcdf") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcdf-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcdf-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
