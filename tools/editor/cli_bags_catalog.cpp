#include "cli_bags_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_bags.hpp"
#include <nlohmann/json.hpp>

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


void printGenSummary(const wowee::pipeline::WoweeBagSlot& c,
                     const std::string& base) {
    std::printf("Wrote %s.wbnk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterBags";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbnk");
    auto c = wowee::pipeline::WoweeBagSlotLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeBagSlotLoader>(c, base, "gen-bnk", ".wbnk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBank(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BankBags";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbnk");
    auto c = wowee::pipeline::WoweeBagSlotLoader::makeBank(name);
    if (!saveOrError<wowee::pipeline::WoweeBagSlotLoader>(c, base, "gen-bnk-bank", ".wbnk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenSpecial(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SpecialBags";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbnk");
    auto c = wowee::pipeline::WoweeBagSlotLoader::makeSpecial(name);
    if (!saveOrError<wowee::pipeline::WoweeBagSlotLoader>(c, base, "gen-bnk-special", ".wbnk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbnk");
    if (!wowee::pipeline::WoweeBagSlotLoader::exists(base)) {
        return reportMissing("WBNK", base, ".wbnk");
    }
    auto c = wowee::pipeline::WoweeBagSlotLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wbnk"] = base + ".wbnk";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"bagSlotId", e.bagSlotId},
                {"name", e.name},
                {"description", e.description},
                {"bagKind", e.bagKind},
                {"bagKindName", wowee::pipeline::WoweeBagSlot::bagKindName(e.bagKind)},
                {"containerSize", e.containerSize},
                {"displayOrder", e.displayOrder},
                {"isUnlocked", e.isUnlocked},
                {"fixedBagItemId", e.fixedBagItemId},
                {"unlockCostCopper", e.unlockCostCopper},
                {"acceptsBagSubclassMask", e.acceptsBagSubclassMask},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WBNK: %s.wbnk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind         size  order  unlock  cost(c)    accepts-mask  fixedBag  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s   %3u    %3u    %u      %7u   0x%08x   %5u  %s\n",
                    e.bagSlotId,
                    wowee::pipeline::WoweeBagSlot::bagKindName(e.bagKind),
                    e.containerSize, e.displayOrder, e.isUnlocked,
                    e.unlockCostCopper, e.acceptsBagSubclassMask,
                    e.fixedBagItemId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each slot emits all 8 scalar fields plus
    // a dual int + name form for bagKind so hand-edits can
    // use either representation. acceptsBagSubclassMask is
    // dumped as a raw uint32 — users hand-edit using the
    // kAccepts* bit constants documented in the header.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wbnk");
    if (outPath.empty()) outPath = base + ".wbnk.json";
    if (!wowee::pipeline::WoweeBagSlotLoader::exists(base)) {
        return reportMissing("export-wbnk-json", "WBNK", base, ".wbnk");
    }
    auto c = wowee::pipeline::WoweeBagSlotLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"bagSlotId", e.bagSlotId},
            {"name", e.name},
            {"description", e.description},
            {"bagKind", e.bagKind},
            {"bagKindName", wowee::pipeline::WoweeBagSlot::bagKindName(e.bagKind)},
            {"containerSize", e.containerSize},
            {"displayOrder", e.displayOrder},
            {"isUnlocked", e.isUnlocked},
            {"fixedBagItemId", e.fixedBagItemId},
            {"unlockCostCopper", e.unlockCostCopper},
            {"acceptsBagSubclassMask", e.acceptsBagSubclassMask},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wbnk-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wbnk\n", base.c_str());
    std::printf("  slots  : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wbnk");
    outBase = cli::withoutExt(outBase, ".wbnk");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wbnk-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wbnk-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "inventory")  return wowee::pipeline::WoweeBagSlot::Inventory;
        if (s == "bank")       return wowee::pipeline::WoweeBagSlot::Bank;
        if (s == "keyring")    return wowee::pipeline::WoweeBagSlot::Keyring;
        if (s == "quiver")     return wowee::pipeline::WoweeBagSlot::Quiver;
        if (s == "soul-shard") return wowee::pipeline::WoweeBagSlot::SoulShard;
        if (s == "stable")     return wowee::pipeline::WoweeBagSlot::Stable;
        if (s == "reagent")    return wowee::pipeline::WoweeBagSlot::Reagent;
        if (s == "wallet")     return wowee::pipeline::WoweeBagSlot::Wallet;
        return wowee::pipeline::WoweeBagSlot::Inventory;
    };
    wowee::pipeline::WoweeBagSlot c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeBagSlot::Entry e;
            e.bagSlotId = je.value("bagSlotId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("bagKind") &&
                je["bagKind"].is_number_integer()) {
                e.bagKind = static_cast<uint8_t>(
                    je["bagKind"].get<int>());
            } else if (je.contains("bagKindName") &&
                       je["bagKindName"].is_string()) {
                e.bagKind = kindFromName(
                    je["bagKindName"].get<std::string>());
            }
            e.containerSize = static_cast<uint8_t>(
                je.value("containerSize", 0));
            e.displayOrder = static_cast<uint8_t>(
                je.value("displayOrder", 0));
            // isUnlocked defaults to 1 when omitted — most
            // slots ship unlocked at character creation; only
            // bank-bag slots typically need explicit gold.
            e.isUnlocked = static_cast<uint8_t>(
                je.value("isUnlocked", 1));
            e.fixedBagItemId = je.value("fixedBagItemId", 0u);
            e.unlockCostCopper = je.value("unlockCostCopper", 0u);
            // acceptsBagSubclassMask defaults to kAcceptsAny
            // Container so a sidecar that omits the mask still
            // produces a working generic-bag slot.
            e.acceptsBagSubclassMask =
                je.value("acceptsBagSubclassMask",
                    wowee::pipeline::WoweeBagSlot::kAcceptsAnyContainer);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeBagSlotLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wbnk-json: failed to save %s.wbnk\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wbnk\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  slots  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeBagSlotLoader>(
        i, argc, argv, "wbnk", "WBNK",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        // displayOrder values within the same bagKind should be
        // unique — duplicates would cause UI shuffle ambiguity.
        std::set<std::string> orderSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.bagSlotId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.bagSlotId == 0)
                errors.push_back(ctx + ": bagSlotId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.bagKind > wowee::pipeline::WoweeBagSlot::Wallet) {
                errors.push_back(ctx + ": bagKind " +
                    std::to_string(e.bagKind) + " not in 0..7");
            }
            // A slot that's not unlocked but has unlockCostCopper=0
            // can never be unlocked through normal gameplay — flag.
            if (e.isUnlocked == 0 && e.unlockCostCopper == 0) {
                warnings.push_back(ctx +
                    ": isUnlocked=0 with unlockCostCopper=0 "
                    "(slot can never be unlocked in-game)");
            }
            // A fixed-bag slot (containerSize > 0, fixedBagItemId
            // = 0) with a non-zero acceptsBagSubclassMask is
            // contradictory — fixed slots don't accept equippable
            // bags. The starter MainBackpack illustrates this:
            // size=16, mask=0.
            if (e.containerSize > 0 && e.fixedBagItemId == 0 &&
                e.acceptsBagSubclassMask != 0) {
                warnings.push_back(ctx +
                    ": fixed-size slot (containerSize=" +
                    std::to_string(e.containerSize) +
                    ") with non-zero acceptsBagSubclassMask "
                    "(equippable bag would be ignored)");
            }
            // A variable slot (containerSize=0) with mask=0 can
            // never accept any bag.
            if (e.containerSize == 0 && e.acceptsBagSubclassMask == 0 &&
                e.bagKind != wowee::pipeline::WoweeBagSlot::Stable) {
                errors.push_back(ctx +
                    ": variable slot (containerSize=0) with "
                    "acceptsBagSubclassMask=0 — no bag can fit here");
            }
            // (bagKind, displayOrder) tuple uniqueness — within
            // the same kind the UI sorts by displayOrder, so
            // duplicates would cause ambiguous ordering.
            std::string tuple =
                std::to_string(e.bagKind) + "/" +
                std::to_string(e.displayOrder);
            if (orderSeen.count(tuple)) {
                warnings.push_back(ctx +
                    ": duplicate (bagKind=" +
                    wowee::pipeline::WoweeBagSlot::bagKindName(e.bagKind) +
                    ", displayOrder=" +
                    std::to_string(e.displayOrder) +
                    ") — UI sort order is ambiguous");
            }
            orderSeen.insert(tuple);
            if (!idsSeen.add(e.bagSlotId)) errors.push_back(ctx + ": duplicate bagSlotId");
        }
            return formatted("%zu slots, all bagSlotIds unique, no order ambiguity", c.entries.size());
        });
}

} // namespace

bool handleBagsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-bnk") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bnk-bank") == 0 && i + 1 < argc) {
        outRc = handleGenBank(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bnk-special") == 0 && i + 1 < argc) {
        outRc = handleGenSpecial(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wbnk") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wbnk") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wbnk-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wbnk-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
