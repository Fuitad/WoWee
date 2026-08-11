#include "cli_channels_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_channels.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeChannel& c,
                     const std::string& base) {
    std::printf("Wrote %s.wchn\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  channels : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterChannels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchn");
    auto c = wowee::pipeline::WoweeChannelLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeChannelLoader>(c, base, "gen-channels", ".wchn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CityChannels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchn");
    auto c = wowee::pipeline::WoweeChannelLoader::makeCity(name);
    if (!saveOrError<wowee::pipeline::WoweeChannelLoader>(c, base, "gen-channels-city", ".wchn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenModerated(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ModeratedChannels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchn");
    auto c = wowee::pipeline::WoweeChannelLoader::makeModerated(name);
    if (!saveOrError<wowee::pipeline::WoweeChannelLoader>(c, base, "gen-channels-moderated", ".wchn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wchn");
    if (!wowee::pipeline::WoweeChannelLoader::exists(base)) {
        std::fprintf(stderr, "WCHN not found: %s.wchn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeChannelLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wchn"] = base + ".wchn";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"channelId", e.channelId},
                {"name", e.name},
                {"description", e.description},
                {"channelType", e.channelType},
                {"channelTypeName", wowee::pipeline::WoweeChannel::channelTypeName(e.channelType)},
                {"factionAccess", e.factionAccess},
                {"factionAccessName", wowee::pipeline::WoweeChannel::factionAccessName(e.factionAccess)},
                {"autoJoin", e.autoJoin},
                {"announce", e.announce},
                {"moderated", e.moderated},
                {"minLevel", e.minLevel},
                {"areaIdGate", e.areaIdGate},
                {"mapIdGate", e.mapIdGate},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCHN: %s.wchn\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  channels : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    type           faction    auto  ann  mod  lvl   map  area  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-13s  %-8s   %u     %u    %u    %3u   %3u  %4u  %s\n",
                    e.channelId,
                    wowee::pipeline::WoweeChannel::channelTypeName(e.channelType),
                    wowee::pipeline::WoweeChannel::factionAccessName(e.factionAccess),
                    e.autoJoin, e.announce, e.moderated,
                    e.minLevel, e.mapIdGate, e.areaIdGate,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each channel emits all 10 scalar fields
    // plus dual int + name forms for channelType and
    // factionAccess (so hand-edits can use either form).
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wchn");
    if (outPath.empty()) outPath = base + ".wchn.json";
    if (!wowee::pipeline::WoweeChannelLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wchn-json: WCHN not found: %s.wchn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeChannelLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"channelId", e.channelId},
            {"name", e.name},
            {"description", e.description},
            {"channelType", e.channelType},
            {"channelTypeName", wowee::pipeline::WoweeChannel::channelTypeName(e.channelType)},
            {"factionAccess", e.factionAccess},
            {"factionAccessName", wowee::pipeline::WoweeChannel::factionAccessName(e.factionAccess)},
            {"autoJoin", e.autoJoin},
            {"announce", e.announce},
            {"moderated", e.moderated},
            {"minLevel", e.minLevel},
            {"areaIdGate", e.areaIdGate},
            {"mapIdGate", e.mapIdGate},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wchn-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.wchn\n", base.c_str());
    std::printf("  channels : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wchn");
    outBase = cli::withoutExt(outBase, ".wchn");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wchn-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wchn-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto channelTypeFromName = [](const std::string& s) -> uint8_t {
        if (s == "area-local")    return wowee::pipeline::WoweeChannel::AreaLocal;
        if (s == "zone")          return wowee::pipeline::WoweeChannel::Zone;
        if (s == "continent")     return wowee::pipeline::WoweeChannel::Continent;
        if (s == "world")         return wowee::pipeline::WoweeChannel::World;
        if (s == "trade")         return wowee::pipeline::WoweeChannel::Trade;
        if (s == "lfg")           return wowee::pipeline::WoweeChannel::LookingForGroup;
        if (s == "guild-recruit") return wowee::pipeline::WoweeChannel::GuildRecruit;
        if (s == "local-defense") return wowee::pipeline::WoweeChannel::LocalDefense;
        if (s == "custom")        return wowee::pipeline::WoweeChannel::Custom;
        if (s == "pvp")           return wowee::pipeline::WoweeChannel::Pvp;
        return wowee::pipeline::WoweeChannel::AreaLocal;
    };
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "alliance") return wowee::pipeline::WoweeChannel::Alliance;
        if (s == "horde")    return wowee::pipeline::WoweeChannel::Horde;
        if (s == "both")     return wowee::pipeline::WoweeChannel::Both;
        return wowee::pipeline::WoweeChannel::Both;
    };
    wowee::pipeline::WoweeChannel c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeChannel::Entry e;
            e.channelId = je.value("channelId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("channelType") &&
                je["channelType"].is_number_integer()) {
                e.channelType = static_cast<uint8_t>(
                    je["channelType"].get<int>());
            } else if (je.contains("channelTypeName") &&
                       je["channelTypeName"].is_string()) {
                e.channelType = channelTypeFromName(
                    je["channelTypeName"].get<std::string>());
            }
            if (je.contains("factionAccess") &&
                je["factionAccess"].is_number_integer()) {
                e.factionAccess = static_cast<uint8_t>(
                    je["factionAccess"].get<int>());
            } else if (je.contains("factionAccessName") &&
                       je["factionAccessName"].is_string()) {
                e.factionAccess = factionFromName(
                    je["factionAccessName"].get<std::string>());
            }
            e.autoJoin = static_cast<uint8_t>(je.value("autoJoin", 0));
            e.announce = static_cast<uint8_t>(je.value("announce", 1));
            e.moderated = static_cast<uint8_t>(je.value("moderated", 0));
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            e.areaIdGate = je.value("areaIdGate", 0u);
            e.mapIdGate = je.value("mapIdGate", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeChannelLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wchn-json: failed to save %s.wchn\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wchn\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  channels : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wchn");
    if (!wowee::pipeline::WoweeChannelLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wchn: WCHN not found: %s.wchn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeChannelLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.channelId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.channelId == 0) errors.push_back(ctx + ": channelId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.channelType > wowee::pipeline::WoweeChannel::Pvp) {
            errors.push_back(ctx + ": channelType " +
                std::to_string(e.channelType) + " not in 0..9");
        }
        if (e.factionAccess > wowee::pipeline::WoweeChannel::Both) {
            errors.push_back(ctx + ": factionAccess " +
                std::to_string(e.factionAccess) + " not in 0..2");
        }
        // AreaLocal / Zone channels with no area gate aren't broken,
        // but world / continent channels with an area gate is
        // contradictory.
        if ((e.channelType == wowee::pipeline::WoweeChannel::World ||
             e.channelType == wowee::pipeline::WoweeChannel::Continent) &&
            (e.areaIdGate != 0 || e.mapIdGate != 0)) {
            warnings.push_back(ctx +
                ": world/continent channel with area or map gate "
                "(gate is silently ignored at runtime)");
        }
        if (e.minLevel == 0) {
            warnings.push_back(ctx + ": minLevel=0 (no level gate at all)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.channelId) {
                errors.push_back(ctx + ": duplicate channelId");
                break;
            }
        }
        idsSeen.push_back(e.channelId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wchn", base, errors, warnings);
    std::printf("validate-wchn: %s.wchn\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu channels, all channelIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleChannelsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-channels") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-channels-city") == 0 && i + 1 < argc) {
        outRc = handleGenCity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-channels-moderated") == 0 && i + 1 < argc) {
        outRc = handleGenModerated(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wchn") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wchn") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wchn-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wchn-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
