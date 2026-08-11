#include "cli_boss_encounters_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_boss_encounters.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeBossEncounter& c,
                     const std::string& base) {
    std::printf("Wrote %s.wbos\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  encounters : %zu\n", c.entries.size());
}

int handleGenFiveMan(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FiveManBosses";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbos");
    auto c = wowee::pipeline::WoweeBossEncounterLoader::makeFiveMan(name);
    if (!saveOrError<wowee::pipeline::WoweeBossEncounterLoader>(c, base, "gen-bos", ".wbos")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaid10(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ICC10NormalBosses";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbos");
    auto c = wowee::pipeline::WoweeBossEncounterLoader::makeRaid10(name);
    if (!saveOrError<wowee::pipeline::WoweeBossEncounterLoader>(c, base, "gen-bos-raid10", ".wbos")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWorldBoss(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WorldBosses";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbos");
    auto c = wowee::pipeline::WoweeBossEncounterLoader::makeWorldBoss(name);
    if (!saveOrError<wowee::pipeline::WoweeBossEncounterLoader>(c, base, "gen-bos-world", ".wbos")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbos");
    if (!wowee::pipeline::WoweeBossEncounterLoader::exists(base)) {
        std::fprintf(stderr, "WBOS not found: %s.wbos\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeBossEncounterLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wbos"] = base + ".wbos";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"encounterId", e.encounterId},
                {"name", e.name},
                {"description", e.description},
                {"bossCreatureId", e.bossCreatureId},
                {"mapId", e.mapId},
                {"difficultyId", e.difficultyId},
                {"berserkSpellId", e.berserkSpellId},
                {"enrageTimerMs", e.enrageTimerMs},
                {"phaseCount", e.phaseCount},
                {"requiredPartySize", e.requiredPartySize},
                {"recommendedItemLevel", e.recommendedItemLevel},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WBOS: %s.wbos\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  encounters : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    boss     map   diff  phases  size  ilvl   enrage(min)  berserk  name\n");
    for (const auto& e : c.entries) {
        char enrageBuf[16];
        if (e.enrageTimerMs == 0) {
            std::snprintf(enrageBuf, sizeof(enrageBuf), "-");
        } else {
            std::snprintf(enrageBuf, sizeof(enrageBuf), "%.1f",
                          e.enrageTimerMs / 60000.0);
        }
        std::printf("  %4u   %5u   %5u  %4u   %3u    %3u   %4u   %-9s    %5u    %s\n",
                    e.encounterId, e.bossCreatureId,
                    e.mapId, e.difficultyId,
                    e.phaseCount, e.requiredPartySize,
                    e.recommendedItemLevel, enrageBuf,
                    e.berserkSpellId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wbos");
    if (!wowee::pipeline::WoweeBossEncounterLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wbos-json: WBOS not found: %s.wbos\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeBossEncounterLoader::load(base);
    if (outPath.empty()) outPath = base + ".wbos.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["encounterId"] = e.encounterId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["bossCreatureId"] = e.bossCreatureId;
        je["mapId"] = e.mapId;
        je["difficultyId"] = e.difficultyId;
        je["berserkSpellId"] = e.berserkSpellId;
        je["enrageTimerMs"] = e.enrageTimerMs;
        je["phaseCount"] = e.phaseCount;
        je["requiredPartySize"] = e.requiredPartySize;
        je["recommendedItemLevel"] = e.recommendedItemLevel;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wbos-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  encounters : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wbos-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wbos-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeBossEncounter c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeBossEncounter::Entry e;
            if (je.contains("encounterId"))    e.encounterId = je["encounterId"].get<uint32_t>();
            if (je.contains("name"))           e.name = je["name"].get<std::string>();
            if (je.contains("description"))    e.description = je["description"].get<std::string>();
            if (je.contains("bossCreatureId")) e.bossCreatureId = je["bossCreatureId"].get<uint32_t>();
            if (je.contains("mapId"))          e.mapId = je["mapId"].get<uint32_t>();
            if (je.contains("difficultyId"))   e.difficultyId = je["difficultyId"].get<uint32_t>();
            if (je.contains("berserkSpellId")) e.berserkSpellId = je["berserkSpellId"].get<uint32_t>();
            if (je.contains("enrageTimerMs"))  e.enrageTimerMs = je["enrageTimerMs"].get<uint32_t>();
            if (je.contains("phaseCount"))     e.phaseCount = je["phaseCount"].get<uint8_t>();
            if (je.contains("requiredPartySize")) e.requiredPartySize = je["requiredPartySize"].get<uint8_t>();
            if (je.contains("recommendedItemLevel")) e.recommendedItemLevel = je["recommendedItemLevel"].get<uint16_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wbos");
    outBase = cli::withoutExt(outBase, ".wbos");
    if (!wowee::pipeline::WoweeBossEncounterLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wbos-json: failed to save %s.wbos\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wbos\n", outBase.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  encounters : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbos");
    if (!wowee::pipeline::WoweeBossEncounterLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wbos: WBOS not found: %s.wbos\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeBossEncounterLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.encounterId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.encounterId == 0)
            errors.push_back(ctx + ": encounterId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.bossCreatureId == 0)
            errors.push_back(ctx +
                ": bossCreatureId is 0 — encounter has no boss");
        if (e.mapId == 0)
            errors.push_back(ctx +
                ": mapId is 0 — encounter is unbound to a map");
        if (e.phaseCount == 0)
            errors.push_back(ctx +
                ": phaseCount is 0 — encounter has no phases");
        if (e.requiredPartySize == 0)
            errors.push_back(ctx +
                ": requiredPartySize is 0 — invalid group size");
        if (e.requiredPartySize > 40)
            warnings.push_back(ctx +
                ": requiredPartySize " +
                std::to_string(e.requiredPartySize) +
                " > 40 (max raid size)");
        // Standard sizes are 5/10/25/40 — anything else is a
        // server-custom raid size, worth flagging.
        if (e.requiredPartySize != 5 && e.requiredPartySize != 10 &&
            e.requiredPartySize != 25 && e.requiredPartySize != 40) {
            warnings.push_back(ctx +
                ": non-standard requiredPartySize " +
                std::to_string(e.requiredPartySize) +
                " (canonical sizes are 5/10/25/40)");
        }
        // berserkSpellId without enrageTimerMs is contradictory
        // (the spell never fires).
        if (e.berserkSpellId != 0 && e.enrageTimerMs == 0) {
            warnings.push_back(ctx +
                ": berserkSpellId=" +
                std::to_string(e.berserkSpellId) +
                " set but enrageTimerMs=0 — spell will never "
                "fire (no enrage countdown)");
        }
        // Enrage > 30 minutes is suspicious — typical raid
        // encounters cap at ~15-20 minutes.
        if (e.enrageTimerMs > 1800000) {
            warnings.push_back(ctx +
                ": enrageTimerMs " +
                std::to_string(e.enrageTimerMs) +
                " > 30 min (1800000ms) — exceptionally long "
                "soft enrage, double-check");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.encounterId) {
                errors.push_back(ctx + ": duplicate encounterId");
                break;
            }
        }
        idsSeen.push_back(e.encounterId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wbos", base, errors, warnings);
    std::printf("validate-wbos: %s.wbos\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu encounters, all encounterIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleBossEncountersCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-bos") == 0 && i + 1 < argc) {
        outRc = handleGenFiveMan(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bos-raid10") == 0 && i + 1 < argc) {
        outRc = handleGenRaid10(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bos-world") == 0 && i + 1 < argc) {
        outRc = handleGenWorldBoss(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wbos") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wbos") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wbos-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wbos-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
