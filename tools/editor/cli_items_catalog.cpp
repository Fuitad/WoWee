#include "cli_items_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_items.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeItem& c,
                     const std::string& base) {
    std::printf("Wrote %s.wit\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterItems";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wit");
    auto c = wowee::pipeline::WoweeItemLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeItemLoader>(c, base, "gen-items", ".wit")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWeapons(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeaponCatalog";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wit");
    auto c = wowee::pipeline::WoweeItemLoader::makeWeapons(name);
    if (!saveOrError<wowee::pipeline::WoweeItemLoader>(c, base, "gen-items-weapons", ".wit")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenArmor(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ArmorCatalog";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wit");
    auto c = wowee::pipeline::WoweeItemLoader::makeArmor(name);
    if (!saveOrError<wowee::pipeline::WoweeItemLoader>(c, base, "gen-items-armor", ".wit")) return 1;
    printGenSummary(c, base);
    return 0;
}

void printPriceCopper(uint32_t copper) {
    uint32_t gold = copper / 10000;
    uint32_t silver = (copper / 100) % 100;
    uint32_t cop = copper % 100;
    std::printf("%ug %us %uc", gold, silver, cop);
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wit");
    if (!wowee::pipeline::WoweeItemLoader::exists(base)) {
        std::fprintf(stderr, "WIT not found: %s.wit\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wit"] = base + ".wit";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json je;
            je["itemId"] = e.itemId;
            je["displayId"] = e.displayId;
            je["quality"] = e.quality;
            je["qualityName"] = wowee::pipeline::WoweeItem::qualityName(e.quality);
            je["itemClass"] = e.itemClass;
            je["itemClassName"] = wowee::pipeline::WoweeItem::classNameOf(e.itemClass);
            je["itemSubClass"] = e.itemSubClass;
            je["inventoryType"] = e.inventoryType;
            je["slotName"] = wowee::pipeline::WoweeItem::slotName(e.inventoryType);
            je["flags"] = e.flags;
            je["requiredLevel"] = e.requiredLevel;
            je["itemLevel"] = e.itemLevel;
            je["sellPriceCopper"] = e.sellPriceCopper;
            je["buyPriceCopper"] = e.buyPriceCopper;
            je["maxStack"] = e.maxStack;
            je["durability"] = e.durability;
            je["damageMin"] = e.damageMin;
            je["damageMax"] = e.damageMax;
            je["attackSpeedMs"] = e.attackSpeedMs;
            nlohmann::json sa = nlohmann::json::array();
            for (const auto& s : e.stats) {
                sa.push_back({
                    {"type", s.type},
                    {"typeName", wowee::pipeline::WoweeItem::statName(s.type)},
                    {"value", s.value}
                });
            }
            je["stats"] = sa;
            je["name"] = e.name;
            je["description"] = e.description;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WIT: %s.wit\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   ilvl  quality   class       slot       buy           name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %4u  %-9s %-11s %-10s ",
                    e.itemId, e.itemLevel,
                    wowee::pipeline::WoweeItem::qualityName(e.quality),
                    wowee::pipeline::WoweeItem::classNameOf(e.itemClass),
                    wowee::pipeline::WoweeItem::slotName(e.inventoryType));
        printPriceCopper(e.buyPriceCopper);
        std::printf("    %s\n", e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Export a .wit to a human-editable JSON sidecar.
    // Mirrors WOL/WOW/WOMX/WSND/WSPN JSON pairs. Each entry
    // round-trips all 18 scalar fields plus the stats array.
    // Both quality / itemClass / inventoryType emit dual int +
    // name forms so a hand-author can use either.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wit");
    if (outPath.empty()) outPath = base + ".wit.json";
    if (!wowee::pipeline::WoweeItemLoader::exists(base)) {
        return reportMissing("export-wit-json", "WIT", base, ".wit");
    }
    auto c = wowee::pipeline::WoweeItemLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["itemId"] = e.itemId;
        je["displayId"] = e.displayId;
        je["quality"] = e.quality;
        je["qualityName"] = wowee::pipeline::WoweeItem::qualityName(e.quality);
        je["itemClass"] = e.itemClass;
        je["itemClassName"] = wowee::pipeline::WoweeItem::classNameOf(e.itemClass);
        je["itemSubClass"] = e.itemSubClass;
        je["inventoryType"] = e.inventoryType;
        je["slotName"] = wowee::pipeline::WoweeItem::slotName(e.inventoryType);
        je["flags"] = e.flags;
        je["requiredLevel"] = e.requiredLevel;
        je["itemLevel"] = e.itemLevel;
        je["sellPriceCopper"] = e.sellPriceCopper;
        je["buyPriceCopper"] = e.buyPriceCopper;
        je["maxStack"] = e.maxStack;
        je["durability"] = e.durability;
        je["damageMin"] = e.damageMin;
        je["damageMax"] = e.damageMax;
        je["attackSpeedMs"] = e.attackSpeedMs;
        nlohmann::json sa = nlohmann::json::array();
        for (const auto& s : e.stats) {
            sa.push_back({
                {"type", s.type},
                {"typeName", wowee::pipeline::WoweeItem::statName(s.type)},
                {"value", s.value},
            });
        }
        je["stats"] = sa;
        je["name"] = e.name;
        je["description"] = e.description;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wit-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wit\n", base.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wit");
    outBase = cli::withoutExt(outBase, ".wit");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wit-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wit-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto qualityFromName = [](const std::string& s) -> uint8_t {
        if (s == "poor")      return wowee::pipeline::WoweeItem::Poor;
        if (s == "common")    return wowee::pipeline::WoweeItem::Common;
        if (s == "uncommon")  return wowee::pipeline::WoweeItem::Uncommon;
        if (s == "rare")      return wowee::pipeline::WoweeItem::Rare;
        if (s == "epic")      return wowee::pipeline::WoweeItem::Epic;
        if (s == "legendary") return wowee::pipeline::WoweeItem::Legendary;
        if (s == "artifact")  return wowee::pipeline::WoweeItem::Artifact;
        if (s == "heirloom")  return wowee::pipeline::WoweeItem::Heirloom;
        return wowee::pipeline::WoweeItem::Common;
    };
    auto classFromName = [](const std::string& s) -> uint8_t {
        if (s == "consumable")   return wowee::pipeline::WoweeItem::Consumable;
        if (s == "container")    return wowee::pipeline::WoweeItem::Container;
        if (s == "weapon")       return wowee::pipeline::WoweeItem::Weapon;
        if (s == "gem")          return wowee::pipeline::WoweeItem::Gem;
        if (s == "armor")        return wowee::pipeline::WoweeItem::Armor;
        if (s == "reagent")      return wowee::pipeline::WoweeItem::Reagent;
        if (s == "projectile")   return wowee::pipeline::WoweeItem::Projectile;
        if (s == "trade-goods")  return wowee::pipeline::WoweeItem::TradeGoods;
        if (s == "recipe")       return wowee::pipeline::WoweeItem::Recipe;
        if (s == "quiver")       return wowee::pipeline::WoweeItem::Quiver;
        if (s == "quest")        return wowee::pipeline::WoweeItem::Quest;
        if (s == "key")          return wowee::pipeline::WoweeItem::Key;
        if (s == "misc")         return wowee::pipeline::WoweeItem::Misc;
        return wowee::pipeline::WoweeItem::Misc;
    };
    auto slotFromName = [](const std::string& s) -> uint8_t {
        if (s == "-" || s.empty()) return wowee::pipeline::WoweeItem::NonEquip;
        if (s == "head")      return wowee::pipeline::WoweeItem::Head;
        if (s == "neck")      return wowee::pipeline::WoweeItem::Neck;
        if (s == "shoulders") return wowee::pipeline::WoweeItem::Shoulders;
        if (s == "shirt")     return wowee::pipeline::WoweeItem::Body;
        if (s == "chest")     return wowee::pipeline::WoweeItem::Chest;
        if (s == "waist")     return wowee::pipeline::WoweeItem::Waist;
        if (s == "legs")      return wowee::pipeline::WoweeItem::Legs;
        if (s == "feet")      return wowee::pipeline::WoweeItem::Feet;
        if (s == "wrists")    return wowee::pipeline::WoweeItem::Wrists;
        if (s == "hands")     return wowee::pipeline::WoweeItem::Hands;
        if (s == "finger")    return wowee::pipeline::WoweeItem::Finger;
        if (s == "trinket")   return wowee::pipeline::WoweeItem::Trinket;
        if (s == "weapon-1h") return wowee::pipeline::WoweeItem::Weapon1H;
        if (s == "shield")    return wowee::pipeline::WoweeItem::Shield;
        if (s == "ranged")    return wowee::pipeline::WoweeItem::Ranged;
        if (s == "cloak")     return wowee::pipeline::WoweeItem::Cloak;
        if (s == "weapon-2h") return wowee::pipeline::WoweeItem::Weapon2H;
        return wowee::pipeline::WoweeItem::NonEquip;
    };
    wowee::pipeline::WoweeItem c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeItem::Entry e;
            e.itemId = je.value("itemId", 0u);
            e.displayId = je.value("displayId", 0u);
            if (je.contains("quality") && je["quality"].is_number_integer()) {
                e.quality = static_cast<uint8_t>(je["quality"].get<int>());
            } else if (je.contains("qualityName") && je["qualityName"].is_string()) {
                e.quality = qualityFromName(je["qualityName"].get<std::string>());
            }
            if (je.contains("itemClass") && je["itemClass"].is_number_integer()) {
                e.itemClass = static_cast<uint8_t>(je["itemClass"].get<int>());
            } else if (je.contains("itemClassName") && je["itemClassName"].is_string()) {
                e.itemClass = classFromName(je["itemClassName"].get<std::string>());
            }
            e.itemSubClass = static_cast<uint8_t>(je.value("itemSubClass", 0));
            if (je.contains("inventoryType") && je["inventoryType"].is_number_integer()) {
                e.inventoryType = static_cast<uint8_t>(je["inventoryType"].get<int>());
            } else if (je.contains("slotName") && je["slotName"].is_string()) {
                e.inventoryType = slotFromName(je["slotName"].get<std::string>());
            }
            e.flags = je.value("flags", 0u);
            e.requiredLevel = static_cast<uint16_t>(je.value("requiredLevel", 0));
            e.itemLevel = static_cast<uint16_t>(je.value("itemLevel", 1));
            e.sellPriceCopper = je.value("sellPriceCopper", 0u);
            e.buyPriceCopper = je.value("buyPriceCopper", 0u);
            e.maxStack = static_cast<uint16_t>(je.value("maxStack", 1));
            e.durability = static_cast<uint16_t>(je.value("durability", 0));
            e.damageMin = je.value("damageMin", 0u);
            e.damageMax = je.value("damageMax", 0u);
            e.attackSpeedMs = je.value("attackSpeedMs", 0u);
            if (je.contains("stats") && je["stats"].is_array()) {
                for (const auto& js : je["stats"]) {
                    wowee::pipeline::WoweeItem::Stat s;
                    s.type = static_cast<uint8_t>(js.value("type", 0));
                    s.value = static_cast<int16_t>(js.value("value", 0));
                    e.stats.push_back(s);
                }
            }
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeItemLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wit-json: failed to save %s.wit\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wit\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wit");
    if (!wowee::pipeline::WoweeItemLoader::exists(base)) {
        return reportMissing("validate-wit", "WIT", base, ".wit");
    }
    auto c = wowee::pipeline::WoweeItemLoader::load(base);
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
                          " (id=" + std::to_string(e.itemId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.itemId == 0) {
            errors.push_back(ctx + ": itemId is 0");
        }
        if (e.quality > wowee::pipeline::WoweeItem::Heirloom) {
            errors.push_back(ctx + ": quality " +
                std::to_string(e.quality) + " not in 0..7");
        }
        // Weapon class implies damage fields > 0 and a 1H/2H slot.
        if (e.itemClass == wowee::pipeline::WoweeItem::Weapon) {
            if (e.damageMin == 0 || e.damageMax == 0) {
                errors.push_back(ctx + ": weapon has zero damage");
            }
            if (e.damageMin > e.damageMax) {
                errors.push_back(ctx + ": damageMin > damageMax");
            }
            if (e.attackSpeedMs == 0) {
                errors.push_back(ctx + ": weapon has zero attackSpeedMs");
            }
            if (e.inventoryType != wowee::pipeline::WoweeItem::Weapon1H &&
                e.inventoryType != wowee::pipeline::WoweeItem::Weapon2H &&
                e.inventoryType != wowee::pipeline::WoweeItem::Ranged) {
                warnings.push_back(ctx + ": weapon has non-weapon inventoryType");
            }
        }
        // Equippable items should have non-zero durability (catches
        // common armor authoring oversight).
        if (e.inventoryType != wowee::pipeline::WoweeItem::NonEquip &&
            e.durability == 0) {
            warnings.push_back(ctx +
                ": equippable item with durability=0");
        }
        // Stack-of-one items shouldn't have maxStack > 1
        // (unique-equip case is already guarded by the Unique flag).
        if (e.inventoryType != wowee::pipeline::WoweeItem::NonEquip &&
            e.maxStack > 1) {
            warnings.push_back(ctx +
                ": equippable item with maxStack > 1");
        }
        // Buy price should be greater than sell price (vendor margin).
        if (e.buyPriceCopper > 0 && e.sellPriceCopper > 0 &&
            e.sellPriceCopper >= e.buyPriceCopper) {
            warnings.push_back(ctx +
                ": sellPrice >= buyPrice (vendor would lose money)");
        }
        if (!idsSeen.add(e.itemId)) errors.push_back(ctx + ": duplicate itemId");
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wit", base, errors, warnings);
    std::printf("validate-wit: %s.wit\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu items, all itemIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleItemsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-items") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-items-weapons") == 0 && i + 1 < argc) {
        outRc = handleGenWeapons(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-items-armor") == 0 && i + 1 < argc) {
        outRc = handleGenArmor(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wit") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wit") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wit-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wit-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
