#include "cli_battlegrounds_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_battlegrounds.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeBattleground& c,
                     const std::string& base) {
    std::printf("Wrote %s.wbgd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  bgs     : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterBgs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbgd");
    auto c = wowee::pipeline::WoweeBattlegroundLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeBattlegroundLoader>(c, base, "gen-bg", ".wbgd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenClassic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ClassicBgs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbgd");
    auto c = wowee::pipeline::WoweeBattlegroundLoader::makeClassic(name);
    if (!saveOrError<wowee::pipeline::WoweeBattlegroundLoader>(c, base, "gen-bg-classic", ".wbgd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenArena(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ArenaSet";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbgd");
    auto c = wowee::pipeline::WoweeBattlegroundLoader::makeArena(name);
    if (!saveOrError<wowee::pipeline::WoweeBattlegroundLoader>(c, base, "gen-bg-arena", ".wbgd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbgd");
    if (!wowee::pipeline::WoweeBattlegroundLoader::exists(base)) {
        std::fprintf(stderr, "WBGD not found: %s.wbgd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeBattlegroundLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wbgd"] = base + ".wbgd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"battlegroundId", e.battlegroundId},
                {"mapId", e.mapId},
                {"name", e.name},
                {"description", e.description},
                {"objectiveKind", e.objectiveKind},
                {"objectiveKindName", wowee::pipeline::WoweeBattleground::objectiveKindName(e.objectiveKind)},
                {"minPlayersPerSide", e.minPlayersPerSide},
                {"maxPlayersPerSide", e.maxPlayersPerSide},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"scoreToWin", e.scoreToWin},
                {"timeLimitSeconds", e.timeLimitSeconds},
                {"bracketSize", e.bracketSize},
                {"allianceStart", {e.allianceStart.x, e.allianceStart.y, e.allianceStart.z}},
                {"allianceFacing", e.allianceFacing},
                {"hordeStart", {e.hordeStart.x, e.hordeStart.y, e.hordeStart.z}},
                {"hordeFacing", e.hordeFacing},
                {"respawnTimeSeconds", e.respawnTimeSeconds},
                {"markTokenId", e.markTokenId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WBGD: %s.wbgd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  bgs     : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::printf("\n  bgId=%u  map=%u  objective=%s  vs%uv%u  level=%u-%u\n",
                    e.battlegroundId, e.mapId,
                    wowee::pipeline::WoweeBattleground::objectiveKindName(e.objectiveKind),
                    e.minPlayersPerSide, e.maxPlayersPerSide,
                    e.minLevel, e.maxLevel);
        std::printf("    name      : %s\n", e.name.c_str());
        std::printf("    score     : %u to win  /  %us time limit\n",
                    e.scoreToWin, e.timeLimitSeconds);
        std::printf("    respawn   : %us  bracket: %u levels  markToken: %u\n",
                    e.respawnTimeSeconds, e.bracketSize, e.markTokenId);
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Vec3 fields (allianceStart / hordeStart)
    // become 3-element JSON arrays. objectiveKind emits dual
    // int + name forms.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wbgd");
    if (outPath.empty()) outPath = base + ".wbgd.json";
    if (!wowee::pipeline::WoweeBattlegroundLoader::exists(base)) {
        return reportMissing("export-wbgd-json", "WBGD", base, ".wbgd");
    }
    auto c = wowee::pipeline::WoweeBattlegroundLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"battlegroundId", e.battlegroundId},
            {"mapId", e.mapId},
            {"name", e.name},
            {"description", e.description},
            {"objectiveKind", e.objectiveKind},
            {"objectiveKindName", wowee::pipeline::WoweeBattleground::objectiveKindName(e.objectiveKind)},
            {"minPlayersPerSide", e.minPlayersPerSide},
            {"maxPlayersPerSide", e.maxPlayersPerSide},
            {"minLevel", e.minLevel},
            {"maxLevel", e.maxLevel},
            {"scoreToWin", e.scoreToWin},
            {"timeLimitSeconds", e.timeLimitSeconds},
            {"bracketSize", e.bracketSize},
            {"allianceStart", {e.allianceStart.x, e.allianceStart.y, e.allianceStart.z}},
            {"allianceFacing", e.allianceFacing},
            {"hordeStart", {e.hordeStart.x, e.hordeStart.y, e.hordeStart.z}},
            {"hordeFacing", e.hordeFacing},
            {"respawnTimeSeconds", e.respawnTimeSeconds},
            {"markTokenId", e.markTokenId},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wbgd-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wbgd\n", base.c_str());
    std::printf("  bgs    : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wbgd");
    outBase = cli::withoutExt(outBase, ".wbgd");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wbgd-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wbgd-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "annihilation")  return wowee::pipeline::WoweeBattleground::Annihilation;
        if (s == "ctf")           return wowee::pipeline::WoweeBattleground::CaptureFlag;
        if (s == "nodes")         return wowee::pipeline::WoweeBattleground::ControlNodes;
        if (s == "koh")           return wowee::pipeline::WoweeBattleground::KingOfHill;
        if (s == "resource-race") return wowee::pipeline::WoweeBattleground::ResourceRace;
        if (s == "carry-object")  return wowee::pipeline::WoweeBattleground::CarryObject;
        return wowee::pipeline::WoweeBattleground::Annihilation;
    };
    auto readVec3 = [](const nlohmann::json& jv, glm::vec3& v) {
        if (jv.is_array() && jv.size() >= 3) {
            v.x = jv[0].get<float>();
            v.y = jv[1].get<float>();
            v.z = jv[2].get<float>();
        }
    };
    wowee::pipeline::WoweeBattleground c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeBattleground::Entry e;
            e.battlegroundId = je.value("battlegroundId", 0u);
            e.mapId = je.value("mapId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("objectiveKind") && je["objectiveKind"].is_number_integer()) {
                e.objectiveKind = static_cast<uint8_t>(je["objectiveKind"].get<int>());
            } else if (je.contains("objectiveKindName") &&
                       je["objectiveKindName"].is_string()) {
                e.objectiveKind = kindFromName(je["objectiveKindName"].get<std::string>());
            }
            e.minPlayersPerSide = static_cast<uint8_t>(
                je.value("minPlayersPerSide", 5));
            e.maxPlayersPerSide = static_cast<uint8_t>(
                je.value("maxPlayersPerSide", 10));
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 10));
            e.maxLevel = static_cast<uint16_t>(je.value("maxLevel", 80));
            e.scoreToWin = static_cast<uint16_t>(je.value("scoreToWin", 3));
            e.timeLimitSeconds = static_cast<uint16_t>(
                je.value("timeLimitSeconds", 1800));
            e.bracketSize = static_cast<uint8_t>(je.value("bracketSize", 10));
            if (je.contains("allianceStart")) readVec3(je["allianceStart"], e.allianceStart);
            e.allianceFacing = je.value("allianceFacing", 0.0f);
            if (je.contains("hordeStart")) readVec3(je["hordeStart"], e.hordeStart);
            e.hordeFacing = je.value("hordeFacing", 0.0f);
            e.respawnTimeSeconds = static_cast<uint16_t>(
                je.value("respawnTimeSeconds", 30));
            e.markTokenId = je.value("markTokenId", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeBattlegroundLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wbgd-json: failed to save %s.wbgd\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wbgd\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  bgs    : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbgd");
    if (!wowee::pipeline::WoweeBattlegroundLoader::exists(base)) {
        return reportMissing("validate-wbgd", "WBGD", base, ".wbgd");
    }
    auto c = wowee::pipeline::WoweeBattlegroundLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (bgId=" + std::to_string(e.battlegroundId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.battlegroundId == 0) {
            errors.push_back(ctx + ": battlegroundId is 0");
        }
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.objectiveKind > wowee::pipeline::WoweeBattleground::CarryObject) {
            errors.push_back(ctx + ": objectiveKind " +
                std::to_string(e.objectiveKind) + " not in 0..5");
        }
        if (e.minPlayersPerSide == 0 || e.maxPlayersPerSide == 0) {
            errors.push_back(ctx + ": player count is 0");
        }
        if (e.minPlayersPerSide > e.maxPlayersPerSide) {
            errors.push_back(ctx +
                ": minPlayersPerSide > maxPlayersPerSide");
        }
        if (e.minLevel > e.maxLevel) {
            errors.push_back(ctx + ": minLevel > maxLevel");
        }
        if (e.scoreToWin == 0) {
            errors.push_back(ctx +
                ": scoreToWin is 0 (no win condition)");
        }
        // Annihilation BGs typically have respawnTimeSeconds=0
        // (no respawn until match ends). Other kinds need
        // respawn > 0 or the losing side can't recover.
        if (e.objectiveKind != wowee::pipeline::WoweeBattleground::Annihilation &&
            e.respawnTimeSeconds == 0) {
            warnings.push_back(ctx +
                ": non-annihilation BG with respawnTimeSeconds=0 "
                "(losing side cannot recover)");
        }
        if (!idsSeen.add(e.battlegroundId)) errors.push_back(ctx + ": duplicate battlegroundId");
    }
    return cli::reportValidation("wbgd", base, jsonOut, errors, warnings,
                                 formatted("%zu battlegrounds, all bgIds unique", c.entries.size()));
}

} // namespace

bool handleBattlegroundsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-bg") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bg-classic") == 0 && i + 1 < argc) {
        outRc = handleGenClassic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bg-arena") == 0 && i + 1 < argc) {
        outRc = handleGenArena(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wbgd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wbgd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wbgd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wbgd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
