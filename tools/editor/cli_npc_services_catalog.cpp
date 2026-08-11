#include "cli_npc_services_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_npc_services.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeNPCService& c,
                     const std::string& base) {
    std::printf("Wrote %s.wbkd\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  services : %zu\n", c.entries.size());
}

int handleGenCity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CityServices";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbkd");
    auto c = wowee::pipeline::WoweeNPCServiceLoader::makeCity(name);
    if (!saveOrError<wowee::pipeline::WoweeNPCServiceLoader>(c, base, "gen-bkd", ".wbkd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBattle(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BattleServices";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbkd");
    auto c = wowee::pipeline::WoweeNPCServiceLoader::makeBattle(name);
    if (!saveOrError<wowee::pipeline::WoweeNPCServiceLoader>(c, base, "gen-bkd-battle", ".wbkd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenProfession(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ProfessionServices";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbkd");
    auto c = wowee::pipeline::WoweeNPCServiceLoader::makeProfession(name);
    if (!saveOrError<wowee::pipeline::WoweeNPCServiceLoader>(c, base, "gen-bkd-profession", ".wbkd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbkd");
    if (!wowee::pipeline::WoweeNPCServiceLoader::exists(base)) {
        std::fprintf(stderr, "WBKD not found: %s.wbkd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeNPCServiceLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wbkd"] = base + ".wbkd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"serviceId", e.serviceId},
                {"name", e.name},
                {"description", e.description},
                {"serviceKind", e.serviceKind},
                {"serviceKindName", wowee::pipeline::WoweeNPCService::serviceKindName(e.serviceKind)},
                {"requiresGold", e.requiresGold},
                {"factionRequiredId", e.factionRequiredId},
                {"gossipTextId", e.gossipTextId},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WBKD: %s.wbkd\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  services : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind             gold      faction   gossip   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %-13s   %6u      %5u    %5u    %s\n",
                    e.serviceId,
                    wowee::pipeline::WoweeNPCService::serviceKindName(e.serviceKind),
                    e.requiresGold, e.factionRequiredId,
                    e.gossipTextId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wbkd");
    if (!wowee::pipeline::WoweeNPCServiceLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wbkd-json: WBKD not found: %s.wbkd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeNPCServiceLoader::load(base);
    if (outPath.empty()) outPath = base + ".wbkd.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["serviceId"] = e.serviceId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["serviceKind"] = e.serviceKind;
        je["serviceKindName"] =
            wowee::pipeline::WoweeNPCService::serviceKindName(e.serviceKind);
        je["requiresGold"] = e.requiresGold;
        je["factionRequiredId"] = e.factionRequiredId;
        je["gossipTextId"] = e.gossipTextId;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wbkd-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  services : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseServiceKindToken(const nlohmann::json& jv,
                              uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeNPCService::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "banker")        return wowee::pipeline::WoweeNPCService::Banker;
        if (s == "mailbox")       return wowee::pipeline::WoweeNPCService::Mailbox;
        if (s == "auctioneer")    return wowee::pipeline::WoweeNPCService::Auctioneer;
        if (s == "stable-master" ||
            s == "stablemaster")  return wowee::pipeline::WoweeNPCService::StableMaster;
        if (s == "flight-master" ||
            s == "flightmaster")  return wowee::pipeline::WoweeNPCService::FlightMaster;
        if (s == "trainer")       return wowee::pipeline::WoweeNPCService::Trainer;
        if (s == "innkeeper")     return wowee::pipeline::WoweeNPCService::Innkeeper;
        if (s == "battlemaster")  return wowee::pipeline::WoweeNPCService::Battlemaster;
        if (s == "guild-banker" ||
            s == "guildbanker")   return wowee::pipeline::WoweeNPCService::GuildBanker;
        if (s == "reagent-vendor" ||
            s == "reagentvendor") return wowee::pipeline::WoweeNPCService::ReagentVendor;
        if (s == "tabard-vendor" ||
            s == "tabardvendor")  return wowee::pipeline::WoweeNPCService::TabardVendor;
        if (s == "misc")          return wowee::pipeline::WoweeNPCService::Misc;
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
            "import-wbkd-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wbkd-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeNPCService c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeNPCService::Entry e;
            if (je.contains("serviceId"))    e.serviceId = je["serviceId"].get<uint32_t>();
            if (je.contains("name"))         e.name = je["name"].get<std::string>();
            if (je.contains("description"))  e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeNPCService::Banker;
            if (je.contains("serviceKind"))
                kind = parseServiceKindToken(je["serviceKind"], kind);
            else if (je.contains("serviceKindName"))
                kind = parseServiceKindToken(je["serviceKindName"], kind);
            e.serviceKind = kind;
            if (je.contains("requiresGold"))      e.requiresGold = je["requiresGold"].get<uint32_t>();
            if (je.contains("factionRequiredId")) e.factionRequiredId = je["factionRequiredId"].get<uint32_t>();
            if (je.contains("gossipTextId"))      e.gossipTextId = je["gossipTextId"].get<uint32_t>();
            if (je.contains("iconColorRGBA"))     e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wbkd");
    outBase = cli::withoutExt(outBase, ".wbkd");
    if (!wowee::pipeline::WoweeNPCServiceLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wbkd-json: failed to save %s.wbkd\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wbkd\n", outBase.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  services : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbkd");
    if (!wowee::pipeline::WoweeNPCServiceLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wbkd: WBKD not found: %s.wbkd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeNPCServiceLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.serviceId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.serviceId == 0)
            errors.push_back(ctx + ": serviceId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.serviceKind > wowee::pipeline::WoweeNPCService::Misc) {
            errors.push_back(ctx + ": serviceKind " +
                std::to_string(e.serviceKind) + " not in 0..11");
        }
        // Mailbox is a gameobject service, not an NPC —
        // shouldn't have a gossipTextId. Warn so authors
        // double-check.
        if (e.serviceKind == wowee::pipeline::WoweeNPCService::Mailbox &&
            e.gossipTextId != 0) {
            warnings.push_back(ctx +
                ": Mailbox kind with gossipTextId=" +
                std::to_string(e.gossipTextId) +
                " — mailboxes are gameobject services with "
                "no NPC dialogue; gossip will not display");
        }
        // Innkeeper without a gossip text reads as a silent
        // bind interaction — usually a missing link.
        if (e.serviceKind == wowee::pipeline::WoweeNPCService::Innkeeper &&
            e.gossipTextId == 0) {
            warnings.push_back(ctx +
                ": Innkeeper kind with gossipTextId=0 — "
                "no welcome/bind dialogue, will silently bind");
        }
        // Battlemaster gold cost > 0 is unusual — battle
        // queues are typically free.
        if (e.serviceKind == wowee::pipeline::WoweeNPCService::Battlemaster &&
            e.requiresGold > 0) {
            warnings.push_back(ctx +
                ": Battlemaster kind with requiresGold=" +
                std::to_string(e.requiresGold) +
                " — battle queue services are typically free");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.serviceId) {
                errors.push_back(ctx + ": duplicate serviceId");
                break;
            }
        }
        idsSeen.push_back(e.serviceId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wbkd", base, errors, warnings);
    std::printf("validate-wbkd: %s.wbkd\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu services, all serviceIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleNPCServicesCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-bkd") == 0 && i + 1 < argc) {
        outRc = handleGenCity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bkd-battle") == 0 && i + 1 < argc) {
        outRc = handleGenBattle(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bkd-profession") == 0 && i + 1 < argc) {
        outRc = handleGenProfession(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wbkd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wbkd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wbkd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wbkd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
