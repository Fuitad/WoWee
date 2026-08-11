#include "cli_creature_equipment_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_creature_equipment.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCreatureEquipment& c,
                     const std::string& base) {
    std::printf("Wrote %s.wceq\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  loadouts   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCreatureEq";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wceq");
    auto c = wowee::pipeline::WoweeCreatureEquipmentLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureEquipmentLoader>(c, base, "gen-ceq", ".wceq")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBosses(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BossLoadouts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wceq");
    auto c = wowee::pipeline::WoweeCreatureEquipmentLoader::makeBosses(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureEquipmentLoader>(c, base, "gen-ceq-bosses", ".wceq")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRanged(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RangedLoadouts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wceq");
    auto c = wowee::pipeline::WoweeCreatureEquipmentLoader::makeRanged(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureEquipmentLoader>(c, base, "gen-ceq-ranged", ".wceq")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wceq");
    if (!wowee::pipeline::WoweeCreatureEquipmentLoader::exists(base)) {
        std::fprintf(stderr, "WCEQ not found: %s.wceq\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureEquipmentLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wceq"] = base + ".wceq";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"equipmentId", e.equipmentId},
                {"creatureId", e.creatureId},
                {"name", e.name},
                {"description", e.description},
                {"mainHandItemId", e.mainHandItemId},
                {"offHandItemId", e.offHandItemId},
                {"rangedItemId", e.rangedItemId},
                {"mainHandSlot", e.mainHandSlot},
                {"offHandSlot", e.offHandSlot},
                {"rangedSlot", e.rangedSlot},
                {"equipFlags", e.equipFlags},
                {"mainHandVisualId", e.mainHandVisualId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCEQ: %s.wceq\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  loadouts   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    creature   mainHand   offHand   ranged    flags  visKit  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %5u      %5u      %5u    %5u   0x%02x   %5u  %s\n",
                    e.equipmentId, e.creatureId,
                    e.mainHandItemId, e.offHandItemId,
                    e.rangedItemId, e.equipFlags,
                    e.mainHandVisualId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each loadout emits all 11 scalar fields —
    // no enum widening needed (all fields are raw numerics
    // or item ID cross-refs to other catalogs).
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wceq");
    if (outPath.empty()) outPath = base + ".wceq.json";
    if (!wowee::pipeline::WoweeCreatureEquipmentLoader::exists(base)) {
        return reportMissing("export-wceq-json", "WCEQ", base, ".wceq");
    }
    auto c = wowee::pipeline::WoweeCreatureEquipmentLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"equipmentId", e.equipmentId},
            {"creatureId", e.creatureId},
            {"name", e.name},
            {"description", e.description},
            {"mainHandItemId", e.mainHandItemId},
            {"offHandItemId", e.offHandItemId},
            {"rangedItemId", e.rangedItemId},
            {"mainHandSlot", e.mainHandSlot},
            {"offHandSlot", e.offHandSlot},
            {"rangedSlot", e.rangedSlot},
            {"equipFlags", e.equipFlags},
            {"mainHandVisualId", e.mainHandVisualId},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wceq-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.wceq\n", base.c_str());
    std::printf("  loadouts : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wceq");
    outBase = cli::withoutExt(outBase, ".wceq");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wceq-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wceq-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    wowee::pipeline::WoweeCreatureEquipment c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCreatureEquipment::Entry e;
            e.equipmentId = je.value("equipmentId", 0u);
            e.creatureId = je.value("creatureId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.mainHandItemId = je.value("mainHandItemId", 0u);
            e.offHandItemId = je.value("offHandItemId", 0u);
            e.rangedItemId = je.value("rangedItemId", 0u);
            // Slot defaults match the canonical attachment table
            // (mainhand=16, offhand=17, ranged=18) so a sidecar
            // that omits these fields still produces sensible
            // binaries.
            e.mainHandSlot = static_cast<uint8_t>(je.value("mainHandSlot",
                wowee::pipeline::WoweeCreatureEquipment::kSlotMainHand));
            e.offHandSlot = static_cast<uint8_t>(je.value("offHandSlot",
                wowee::pipeline::WoweeCreatureEquipment::kSlotOffHand));
            e.rangedSlot = static_cast<uint8_t>(je.value("rangedSlot",
                wowee::pipeline::WoweeCreatureEquipment::kSlotRanged));
            e.equipFlags = static_cast<uint8_t>(je.value("equipFlags", 0));
            e.mainHandVisualId = je.value("mainHandVisualId", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeCreatureEquipmentLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wceq-json: failed to save %s.wceq\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wceq\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  loadouts : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wceq");
    if (!wowee::pipeline::WoweeCreatureEquipmentLoader::exists(base)) {
        return reportMissing("validate-wceq", "WCEQ", base, ".wceq");
    }
    auto c = wowee::pipeline::WoweeCreatureEquipmentLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.equipmentId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.equipmentId == 0)
            errors.push_back(ctx + ": equipmentId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.creatureId == 0)
            errors.push_back(ctx +
                ": creatureId is 0 (loadout not bound to a WCRT entry)");
        // All three slots empty = nothing to render. Most
        // useful for "unarmed but visible" creatures, so
        // warn rather than error.
        if (e.mainHandItemId == 0 && e.offHandItemId == 0 &&
            e.rangedItemId == 0) {
            warnings.push_back(ctx +
                ": all three slots empty (creature is unarmed)");
        }
        // dual-wield flag set but no offhand item, OR no
        // mainhand — both are inconsistent.
        if ((e.equipFlags &
             wowee::pipeline::WoweeCreatureEquipment::kFlagDualWield) &&
            (e.mainHandItemId == 0 || e.offHandItemId == 0)) {
            errors.push_back(ctx +
                ": kFlagDualWield set but missing mainhand or offhand "
                "item (dual-wield needs both slots filled)");
        }
        // shield flag set but no offhand item.
        if ((e.equipFlags &
             wowee::pipeline::WoweeCreatureEquipment::kFlagShieldOffhand) &&
            e.offHandItemId == 0) {
            errors.push_back(ctx +
                ": kFlagShieldOffhand set but offHandItemId=0");
        }
        // dual-wield + shield are mutually exclusive — can't
        // hold a shield AND a second weapon in the offhand.
        if ((e.equipFlags &
             wowee::pipeline::WoweeCreatureEquipment::kFlagDualWield) &&
            (e.equipFlags &
             wowee::pipeline::WoweeCreatureEquipment::kFlagShieldOffhand)) {
            errors.push_back(ctx +
                ": both kFlagDualWield and kFlagShieldOffhand set "
                "(mutually exclusive)");
        }
        // 2H polearm flag set with an offhand item — polearms
        // occupy both hands.
        if ((e.equipFlags &
             wowee::pipeline::WoweeCreatureEquipment::kFlagPolearmTwoHand) &&
            e.offHandItemId != 0) {
            errors.push_back(ctx +
                ": kFlagPolearmTwoHand set but offHandItemId=" +
                std::to_string(e.offHandItemId) +
                " (2H polearm occupies both hands)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.equipmentId) {
                errors.push_back(ctx + ": duplicate equipmentId");
                break;
            }
        }
        idsSeen.push_back(e.equipmentId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wceq", base, errors, warnings);
    std::printf("validate-wceq: %s.wceq\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu loadouts, all equipmentIds unique, all flag combos consistent\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleCreatureEquipmentCatalog(int& i, int argc, char** argv,
                                    int& outRc) {
    if (std::strcmp(argv[i], "--gen-ceq") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ceq-bosses") == 0 && i + 1 < argc) {
        outRc = handleGenBosses(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ceq-ranged") == 0 && i + 1 < argc) {
        outRc = handleGenRanged(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wceq") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wceq") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wceq-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wceq-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
