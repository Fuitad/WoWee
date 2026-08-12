#include "cli_companions_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_companions.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCompanion& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcmp\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  companions : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCompanions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmp");
    auto c = wowee::pipeline::WoweeCompanionLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCompanionLoader>(c, base, "gen-cmp", ".wcmp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRare(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RareCompanions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmp");
    auto c = wowee::pipeline::WoweeCompanionLoader::makeRare(name);
    if (!saveOrError<wowee::pipeline::WoweeCompanionLoader>(c, base, "gen-cmp-rare", ".wcmp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFaction(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FactionCompanions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmp");
    auto c = wowee::pipeline::WoweeCompanionLoader::makeFaction(name);
    if (!saveOrError<wowee::pipeline::WoweeCompanionLoader>(c, base, "gen-cmp-faction", ".wcmp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcmp");
    if (!wowee::pipeline::WoweeCompanionLoader::exists(base)) {
        return reportMissing("WCMP", base, ".wcmp");
    }
    auto c = wowee::pipeline::WoweeCompanionLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcmp"] = base + ".wcmp";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"companionId", e.companionId},
                {"creatureId", e.creatureId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"companionKind", e.companionKind},
                {"companionKindName", wowee::pipeline::WoweeCompanion::companionKindName(e.companionKind)},
                {"rarity", e.rarity},
                {"rarityName", wowee::pipeline::WoweeCompanion::rarityName(e.rarity)},
                {"factionRestriction", e.factionRestriction},
                {"factionRestrictionName", wowee::pipeline::WoweeCompanion::factionRestrictionName(e.factionRestriction)},
                {"learnSpellId", e.learnSpellId},
                {"itemId", e.itemId},
                {"idleSoundId", e.idleSoundId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCMP: %s.wcmp\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  companions : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    creature   kind         rarity     faction    spell    item    sound   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %5u      %-10s   %-8s   %-8s   %5u   %5u   %5u   %s\n",
                    e.companionId, e.creatureId,
                    wowee::pipeline::WoweeCompanion::companionKindName(e.companionKind),
                    wowee::pipeline::WoweeCompanion::rarityName(e.rarity),
                    wowee::pipeline::WoweeCompanion::factionRestrictionName(e.factionRestriction),
                    e.learnSpellId, e.itemId, e.idleSoundId,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each companion emits all 9 scalar fields
    // plus dual int + name forms for companionKind / rarity
    // / factionRestriction so hand-edits can use either.
    return cli::exportCatalogJson<wowee::pipeline::WoweeCompanionLoader>(
        i, argc, argv, "wcmp", "WCMP", "companions ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"companionId", e.companionId},
                {"creatureId", e.creatureId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"companionKind", e.companionKind},
                {"companionKindName", wowee::pipeline::WoweeCompanion::companionKindName(e.companionKind)},
                {"rarity", e.rarity},
                {"rarityName", wowee::pipeline::WoweeCompanion::rarityName(e.rarity)},
                {"factionRestriction", e.factionRestriction},
                {"factionRestrictionName", wowee::pipeline::WoweeCompanion::factionRestrictionName(e.factionRestriction)},
                {"learnSpellId", e.learnSpellId},
                {"itemId", e.itemId},
                {"idleSoundId", e.idleSoundId},
            });
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wcmp");
    outBase = cli::withoutExt(outBase, ".wcmp");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wcmp-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wcmp-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "critter")    return wowee::pipeline::WoweeCompanion::Critter;
        if (s == "mechanical") return wowee::pipeline::WoweeCompanion::Mechanical;
        if (s == "dragon")     return wowee::pipeline::WoweeCompanion::DragonHatchling;
        if (s == "demonic")    return wowee::pipeline::WoweeCompanion::Demonic;
        if (s == "spectral")   return wowee::pipeline::WoweeCompanion::Spectral;
        if (s == "elemental")  return wowee::pipeline::WoweeCompanion::Elemental;
        if (s == "plush")      return wowee::pipeline::WoweeCompanion::Plush;
        if (s == "undead")     return wowee::pipeline::WoweeCompanion::UndeadCritter;
        return wowee::pipeline::WoweeCompanion::Critter;
    };
    auto rarityFromName = [](const std::string& s) -> uint8_t {
        if (s == "common")   return wowee::pipeline::WoweeCompanion::Common;
        if (s == "uncommon") return wowee::pipeline::WoweeCompanion::Uncommon;
        if (s == "rare")     return wowee::pipeline::WoweeCompanion::Rare;
        if (s == "epic")     return wowee::pipeline::WoweeCompanion::Epic;
        return wowee::pipeline::WoweeCompanion::Common;
    };
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "any")      return wowee::pipeline::WoweeCompanion::AnyFaction;
        if (s == "alliance") return wowee::pipeline::WoweeCompanion::AllianceOnly;
        if (s == "horde")    return wowee::pipeline::WoweeCompanion::HordeOnly;
        return wowee::pipeline::WoweeCompanion::AnyFaction;
    };
    wowee::pipeline::WoweeCompanion c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCompanion::Entry e;
            e.companionId = je.value("companionId", 0u);
            e.creatureId = je.value("creatureId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("companionKind") &&
                je["companionKind"].is_number_integer()) {
                e.companionKind = static_cast<uint8_t>(
                    je["companionKind"].get<int>());
            } else if (je.contains("companionKindName") &&
                       je["companionKindName"].is_string()) {
                e.companionKind = kindFromName(
                    je["companionKindName"].get<std::string>());
            }
            if (je.contains("rarity") &&
                je["rarity"].is_number_integer()) {
                e.rarity = static_cast<uint8_t>(
                    je["rarity"].get<int>());
            } else if (je.contains("rarityName") &&
                       je["rarityName"].is_string()) {
                e.rarity = rarityFromName(
                    je["rarityName"].get<std::string>());
            }
            if (je.contains("factionRestriction") &&
                je["factionRestriction"].is_number_integer()) {
                e.factionRestriction = static_cast<uint8_t>(
                    je["factionRestriction"].get<int>());
            } else if (je.contains("factionRestrictionName") &&
                       je["factionRestrictionName"].is_string()) {
                e.factionRestriction = factionFromName(
                    je["factionRestrictionName"].get<std::string>());
            }
            e.learnSpellId = je.value("learnSpellId", 0u);
            e.itemId = je.value("itemId", 0u);
            e.idleSoundId = je.value("idleSoundId", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeCompanionLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcmp-json: failed to save %s.wcmp\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcmp\n", outBase.c_str());
    std::printf("  source     : %s\n", jsonPath.c_str());
    std::printf("  companions : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeCompanionLoader>(
        i, argc, argv, "wcmp", "WCMP",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.companionId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.companionId == 0)
                errors.push_back(ctx + ": companionId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.creatureId == 0)
                errors.push_back(ctx +
                    ": creatureId is 0 (companion has no rendered model)");
            if (e.learnSpellId == 0)
                errors.push_back(ctx +
                    ": learnSpellId is 0 (no spell to summon companion)");
            if (e.companionKind > wowee::pipeline::WoweeCompanion::UndeadCritter) {
                errors.push_back(ctx + ": companionKind " +
                    std::to_string(e.companionKind) + " not in 0..7");
            }
            if (e.rarity > wowee::pipeline::WoweeCompanion::Epic) {
                errors.push_back(ctx + ": rarity " +
                    std::to_string(e.rarity) + " not in 0..3");
            }
            if (e.factionRestriction > wowee::pipeline::WoweeCompanion::HordeOnly) {
                errors.push_back(ctx + ": factionRestriction " +
                    std::to_string(e.factionRestriction) + " not in 0..2");
            }
            // Epic rarity without an itemId is unusual - promo
            // pets typically have a redemption code item or
            // collector's edition box.
            if (e.rarity == wowee::pipeline::WoweeCompanion::Epic &&
                e.itemId == 0) {
                warnings.push_back(ctx +
                    ": Epic rarity but itemId=0 (no source item - "
                    "verify intentional for code-only redemption)");
            }
            if (!idsSeen.add(e.companionId)) errors.push_back(ctx + ": duplicate companionId");
        }
            return formatted("%zu companions, all companionIds unique", c.entries.size());
        });
}

} // namespace

bool handleCompanionsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-cmp") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmp-rare") == 0 && i + 1 < argc) {
        outRc = handleGenRare(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmp-faction") == 0 && i + 1 < argc) {
        outRc = handleGenFaction(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcmp") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcmp") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcmp-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcmp-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
