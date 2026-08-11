#include "cli_creatures_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_creatures.hpp"
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

void appendNpcFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeCreature::Vendor)       s += "vendor ";
    if (flags & wowee::pipeline::WoweeCreature::QuestGiver)   s += "quest ";
    if (flags & wowee::pipeline::WoweeCreature::Trainer)      s += "trainer ";
    if (flags & wowee::pipeline::WoweeCreature::Banker)       s += "banker ";
    if (flags & wowee::pipeline::WoweeCreature::Innkeeper)    s += "innkeeper ";
    if (flags & wowee::pipeline::WoweeCreature::FlightMaster) s += "flight ";
    if (flags & wowee::pipeline::WoweeCreature::Auctioneer)   s += "auction ";
    if (flags & wowee::pipeline::WoweeCreature::Repair)       s += "repair ";
    if (flags & wowee::pipeline::WoweeCreature::Stable)       s += "stable ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

void appendAiFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeCreature::AiPassive)    s += "passive ";
    if (flags & wowee::pipeline::WoweeCreature::AiAggressive) s += "aggressive ";
    if (flags & wowee::pipeline::WoweeCreature::AiFleeLowHp)  s += "flee ";
    if (flags & wowee::pipeline::WoweeCreature::AiCallHelp)   s += "call-help ";
    if (flags & wowee::pipeline::WoweeCreature::AiNoLeash)    s += "no-leash ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}


void printGenSummary(const wowee::pipeline::WoweeCreature& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcrt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCreatures";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcrt");
    auto c = wowee::pipeline::WoweeCreatureLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureLoader>(c, base, "gen-creatures", ".wcrt")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBandit(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BanditCreatures";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcrt");
    auto c = wowee::pipeline::WoweeCreatureLoader::makeBandit(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureLoader>(c, base, "gen-creatures-bandit", ".wcrt")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMerchants(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "VillageMerchants";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcrt");
    auto c = wowee::pipeline::WoweeCreatureLoader::makeMerchants(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureLoader>(c, base, "gen-creatures-merchants", ".wcrt")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcrt");
    if (!wowee::pipeline::WoweeCreatureLoader::exists(base)) {
        std::fprintf(stderr, "WCRT not found: %s.wcrt\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcrt"] = base + ".wcrt";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string ns, ais;
            appendNpcFlagsStr(ns, e.npcFlags);
            appendAiFlagsStr(ais, e.aiFlags);
            arr.push_back({
                {"creatureId", e.creatureId},
                {"displayId", e.displayId},
                {"name", e.name},
                {"subname", e.subname},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"baseHealth", e.baseHealth},
                {"healthPerLevel", e.healthPerLevel},
                {"baseMana", e.baseMana},
                {"manaPerLevel", e.manaPerLevel},
                {"factionId", e.factionId},
                {"npcFlags", e.npcFlags},
                {"npcFlagsStr", ns},
                {"typeId", e.typeId},
                {"typeName", wowee::pipeline::WoweeCreature::typeName(e.typeId)},
                {"familyId", e.familyId},
                {"familyName", wowee::pipeline::WoweeCreature::familyName(e.familyId)},
                {"damageMin", e.damageMin},
                {"damageMax", e.damageMax},
                {"attackSpeedMs", e.attackSpeedMs},
                {"baseArmor", e.baseArmor},
                {"walkSpeed", e.walkSpeed},
                {"runSpeed", e.runSpeed},
                {"gossipId", e.gossipId},
                {"equippedMain", e.equippedMain},
                {"equippedOffhand", e.equippedOffhand},
                {"equippedRanged", e.equippedRanged},
                {"aiFlags", e.aiFlags},
                {"aiFlagsStr", ais},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCRT: %s.wcrt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   level    hp    type      faction  npc-flags        name\n");
    for (const auto& e : c.entries) {
        std::string ns;
        appendNpcFlagsStr(ns, e.npcFlags);
        std::printf("  %4u   %2u-%2u  %5u  %-9s  %5u    %-15s  %s%s%s\n",
                    e.creatureId, e.minLevel, e.maxLevel,
                    e.baseHealth,
                    wowee::pipeline::WoweeCreature::typeName(e.typeId),
                    e.factionId, ns.c_str(),
                    e.name.c_str(),
                    e.subname.empty() ? "" : " <",
                    e.subname.empty() ? "" : (e.subname + ">").c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each creature emits all 22 scalar fields
    // plus dual int + name forms for typeId, familyId, and
    // both flag bitsets so the importer accepts either form.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wcrt");
    if (outPath.empty()) outPath = base + ".wcrt.json";
    if (!wowee::pipeline::WoweeCreatureLoader::exists(base)) {
        return reportMissing("export-wcrt-json", "WCRT", base, ".wcrt");
    }
    auto c = wowee::pipeline::WoweeCreatureLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["creatureId"] = e.creatureId;
        je["displayId"] = e.displayId;
        je["name"] = e.name;
        je["subname"] = e.subname;
        je["minLevel"] = e.minLevel;
        je["maxLevel"] = e.maxLevel;
        je["baseHealth"] = e.baseHealth;
        je["healthPerLevel"] = e.healthPerLevel;
        je["baseMana"] = e.baseMana;
        je["manaPerLevel"] = e.manaPerLevel;
        je["factionId"] = e.factionId;
        je["npcFlags"] = e.npcFlags;
        nlohmann::json npcArr = nlohmann::json::array();
        if (e.npcFlags & wowee::pipeline::WoweeCreature::Vendor)       npcArr.push_back("vendor");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::QuestGiver)   npcArr.push_back("quest");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::Trainer)      npcArr.push_back("trainer");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::Banker)       npcArr.push_back("banker");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::Innkeeper)    npcArr.push_back("innkeeper");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::FlightMaster) npcArr.push_back("flight");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::Auctioneer)   npcArr.push_back("auction");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::Repair)       npcArr.push_back("repair");
        if (e.npcFlags & wowee::pipeline::WoweeCreature::Stable)       npcArr.push_back("stable");
        je["npcFlagsList"] = npcArr;
        je["typeId"] = e.typeId;
        je["typeName"] = wowee::pipeline::WoweeCreature::typeName(e.typeId);
        je["familyId"] = e.familyId;
        je["familyName"] = wowee::pipeline::WoweeCreature::familyName(e.familyId);
        je["damageMin"] = e.damageMin;
        je["damageMax"] = e.damageMax;
        je["attackSpeedMs"] = e.attackSpeedMs;
        je["baseArmor"] = e.baseArmor;
        je["walkSpeed"] = e.walkSpeed;
        je["runSpeed"] = e.runSpeed;
        je["gossipId"] = e.gossipId;
        je["equippedMain"] = e.equippedMain;
        je["equippedOffhand"] = e.equippedOffhand;
        je["equippedRanged"] = e.equippedRanged;
        je["aiFlags"] = e.aiFlags;
        nlohmann::json aiArr = nlohmann::json::array();
        if (e.aiFlags & wowee::pipeline::WoweeCreature::AiPassive)    aiArr.push_back("passive");
        if (e.aiFlags & wowee::pipeline::WoweeCreature::AiAggressive) aiArr.push_back("aggressive");
        if (e.aiFlags & wowee::pipeline::WoweeCreature::AiFleeLowHp)  aiArr.push_back("flee");
        if (e.aiFlags & wowee::pipeline::WoweeCreature::AiCallHelp)   aiArr.push_back("call-help");
        if (e.aiFlags & wowee::pipeline::WoweeCreature::AiNoLeash)    aiArr.push_back("no-leash");
        je["aiFlagsList"] = aiArr;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wcrt-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wcrt\n", base.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wcrt");
    outBase = cli::withoutExt(outBase, ".wcrt");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wcrt-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wcrt-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto typeFromName = [](const std::string& s) -> uint8_t {
        if (s == "beast")      return wowee::pipeline::WoweeCreature::Beast;
        if (s == "dragon")     return wowee::pipeline::WoweeCreature::Dragon;
        if (s == "demon")      return wowee::pipeline::WoweeCreature::Demon;
        if (s == "elemental")  return wowee::pipeline::WoweeCreature::Elemental;
        if (s == "giant")      return wowee::pipeline::WoweeCreature::Giant;
        if (s == "undead")     return wowee::pipeline::WoweeCreature::Undead;
        if (s == "humanoid")   return wowee::pipeline::WoweeCreature::Humanoid;
        if (s == "critter")    return wowee::pipeline::WoweeCreature::Critter;
        if (s == "mechanical") return wowee::pipeline::WoweeCreature::Mechanical;
        return wowee::pipeline::WoweeCreature::Humanoid;
    };
    auto familyFromName = [](const std::string& s) -> uint8_t {
        if (s == "wolf")    return wowee::pipeline::WoweeCreature::FamWolf;
        if (s == "cat")     return wowee::pipeline::WoweeCreature::FamCat;
        if (s == "bear")    return wowee::pipeline::WoweeCreature::FamBear;
        if (s == "boar")    return wowee::pipeline::WoweeCreature::FamBoar;
        if (s == "raptor")  return wowee::pipeline::WoweeCreature::FamRaptor;
        if (s == "hyena")   return wowee::pipeline::WoweeCreature::FamHyena;
        if (s == "spider")  return wowee::pipeline::WoweeCreature::FamSpider;
        if (s == "gorilla") return wowee::pipeline::WoweeCreature::FamGorilla;
        if (s == "crab")    return wowee::pipeline::WoweeCreature::FamCrab;
        return wowee::pipeline::WoweeCreature::FamNone;
    };
    auto npcFlagFromName = [](const std::string& s) -> uint32_t {
        if (s == "vendor")     return wowee::pipeline::WoweeCreature::Vendor;
        if (s == "quest")      return wowee::pipeline::WoweeCreature::QuestGiver;
        if (s == "trainer")    return wowee::pipeline::WoweeCreature::Trainer;
        if (s == "banker")     return wowee::pipeline::WoweeCreature::Banker;
        if (s == "innkeeper")  return wowee::pipeline::WoweeCreature::Innkeeper;
        if (s == "flight")     return wowee::pipeline::WoweeCreature::FlightMaster;
        if (s == "auction")    return wowee::pipeline::WoweeCreature::Auctioneer;
        if (s == "repair")     return wowee::pipeline::WoweeCreature::Repair;
        if (s == "stable")     return wowee::pipeline::WoweeCreature::Stable;
        return 0;
    };
    auto aiFlagFromName = [](const std::string& s) -> uint32_t {
        if (s == "passive")    return wowee::pipeline::WoweeCreature::AiPassive;
        if (s == "aggressive") return wowee::pipeline::WoweeCreature::AiAggressive;
        if (s == "flee")       return wowee::pipeline::WoweeCreature::AiFleeLowHp;
        if (s == "call-help")  return wowee::pipeline::WoweeCreature::AiCallHelp;
        if (s == "no-leash")   return wowee::pipeline::WoweeCreature::AiNoLeash;
        return 0;
    };
    wowee::pipeline::WoweeCreature c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCreature::Entry e;
            e.creatureId = je.value("creatureId", 0u);
            e.displayId = je.value("displayId", 0u);
            e.name = je.value("name", std::string{});
            e.subname = je.value("subname", std::string{});
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            e.maxLevel = static_cast<uint16_t>(je.value("maxLevel", 1));
            e.baseHealth = je.value("baseHealth", 50u);
            e.healthPerLevel = static_cast<uint16_t>(je.value("healthPerLevel", 0));
            e.baseMana = je.value("baseMana", 0u);
            e.manaPerLevel = static_cast<uint16_t>(je.value("manaPerLevel", 0));
            e.factionId = je.value("factionId", 35u);
            if (je.contains("npcFlags") && je["npcFlags"].is_number_integer()) {
                e.npcFlags = je["npcFlags"].get<uint32_t>();
            } else if (je.contains("npcFlagsList") && je["npcFlagsList"].is_array()) {
                for (const auto& f : je["npcFlagsList"]) {
                    if (f.is_string()) e.npcFlags |= npcFlagFromName(f.get<std::string>());
                }
            }
            if (je.contains("typeId") && je["typeId"].is_number_integer()) {
                e.typeId = static_cast<uint8_t>(je["typeId"].get<int>());
            } else if (je.contains("typeName") && je["typeName"].is_string()) {
                e.typeId = typeFromName(je["typeName"].get<std::string>());
            }
            if (je.contains("familyId") && je["familyId"].is_number_integer()) {
                e.familyId = static_cast<uint8_t>(je["familyId"].get<int>());
            } else if (je.contains("familyName") && je["familyName"].is_string()) {
                e.familyId = familyFromName(je["familyName"].get<std::string>());
            }
            e.damageMin = je.value("damageMin", 1u);
            e.damageMax = je.value("damageMax", 3u);
            e.attackSpeedMs = je.value("attackSpeedMs", 2000u);
            e.baseArmor = je.value("baseArmor", 0u);
            e.walkSpeed = je.value("walkSpeed", 1.0f);
            e.runSpeed = je.value("runSpeed", 1.14f);
            e.gossipId = je.value("gossipId", 0u);
            e.equippedMain = je.value("equippedMain", 0u);
            e.equippedOffhand = je.value("equippedOffhand", 0u);
            e.equippedRanged = je.value("equippedRanged", 0u);
            if (je.contains("aiFlags") && je["aiFlags"].is_number_integer()) {
                e.aiFlags = je["aiFlags"].get<uint32_t>();
            } else if (je.contains("aiFlagsList") && je["aiFlagsList"].is_array()) {
                e.aiFlags = 0;
                for (const auto& f : je["aiFlagsList"]) {
                    if (f.is_string()) e.aiFlags |= aiFlagFromName(f.get<std::string>());
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeCreatureLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcrt-json: failed to save %s.wcrt\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcrt\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcrt");
    if (!wowee::pipeline::WoweeCreatureLoader::exists(base)) {
        return reportMissing("validate-wcrt", "WCRT", base, ".wcrt");
    }
    auto c = wowee::pipeline::WoweeCreatureLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    idsSeen.reserve(c.entries.size());
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.creatureId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.creatureId == 0) {
            errors.push_back(ctx + ": creatureId is 0");
        }
        if (e.minLevel == 0) {
            errors.push_back(ctx + ": minLevel is 0");
        }
        if (e.minLevel > e.maxLevel) {
            errors.push_back(ctx + ": minLevel > maxLevel");
        }
        if (e.baseHealth == 0) {
            errors.push_back(ctx + ": baseHealth is 0 (creature dies on spawn)");
        }
        if (e.damageMin > e.damageMax) {
            errors.push_back(ctx + ": damageMin > damageMax");
        }
        if (e.attackSpeedMs == 0) {
            errors.push_back(ctx + ": attackSpeedMs is 0 (would divide by zero)");
        }
        if (e.runSpeed <= 0 || e.walkSpeed <= 0) {
            errors.push_back(ctx + ": walk/runSpeed must be positive");
        }
        // Conflicting AI flags: passive AND aggressive is incoherent.
        if ((e.aiFlags & wowee::pipeline::WoweeCreature::AiPassive) &&
            (e.aiFlags & wowee::pipeline::WoweeCreature::AiAggressive)) {
            warnings.push_back(ctx + ": both AiPassive and AiAggressive set");
        }
        // Vendor + hostile is rare but possible (gnomish merchants
        // surrounded by hostile NPCs); flag as warning to catch typos.
        if ((e.npcFlags & wowee::pipeline::WoweeCreature::Vendor) &&
            (e.aiFlags & wowee::pipeline::WoweeCreature::AiAggressive)) {
            warnings.push_back(ctx +
                ": vendor with aggressive AI (player can't trade)");
        }
        if (!idsSeen.add(e.creatureId)) errors.push_back(ctx + ": duplicate creatureId");
    }
    return cli::reportValidation("wcrt", base, jsonOut, errors, warnings,
                                 formatted("%zu creatures, all creatureIds unique", c.entries.size()));
}

} // namespace

bool handleCreaturesCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-creatures") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-creatures-bandit") == 0 && i + 1 < argc) {
        outRc = handleGenBandit(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-creatures-merchants") == 0 && i + 1 < argc) {
        outRc = handleGenMerchants(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcrt") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcrt") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcrt-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcrt-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
