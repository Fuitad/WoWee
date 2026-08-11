#include "cli_gems_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_gems.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeGem& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgem\n", base.c_str());
    std::printf("  catalog      : %s\n", c.name.c_str());
    std::printf("  gems         : %zu\n", c.gems.size());
    std::printf("  enchantments : %zu\n", c.enchantments.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterGems";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgem");
    auto c = wowee::pipeline::WoweeGemLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeGemLoader>(c, base, "gen-gems", ".wgem")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenGemSet(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FullGemSet";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgem");
    auto c = wowee::pipeline::WoweeGemLoader::makeGemSet(name);
    if (!saveOrError<wowee::pipeline::WoweeGemLoader>(c, base, "gen-gems-set", ".wgem")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenEnchants(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "EnchantSet";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgem");
    auto c = wowee::pipeline::WoweeGemLoader::makeEnchants(name);
    if (!saveOrError<wowee::pipeline::WoweeGemLoader>(c, base, "gen-gems-enchants", ".wgem")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgem");
    if (!wowee::pipeline::WoweeGemLoader::exists(base)) {
        std::fprintf(stderr, "WGEM not found: %s.wgem\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGemLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgem"] = base + ".wgem";
        j["name"] = c.name;
        j["gemCount"] = c.gems.size();
        j["enchantCount"] = c.enchantments.size();
        nlohmann::json ga = nlohmann::json::array();
        for (const auto& g : c.gems) {
            ga.push_back({
                {"gemId", g.gemId},
                {"itemIdToInsert", g.itemIdToInsert},
                {"name", g.name},
                {"color", g.color},
                {"colorName", wowee::pipeline::WoweeGem::colorName(g.color)},
                {"statType", g.statType},
                {"statValue", g.statValue},
                {"requiredItemQuality", g.requiredItemQuality},
                {"spellId", g.spellId},
            });
        }
        j["gems"] = ga;
        nlohmann::json ea = nlohmann::json::array();
        for (const auto& e : c.enchantments) {
            ea.push_back({
                {"enchantId", e.enchantId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"enchantSlot", e.enchantSlot},
                {"enchantSlotName", wowee::pipeline::WoweeGem::enchantSlotName(e.enchantSlot)},
                {"statType", e.statType},
                {"statValue", e.statValue},
                {"spellId", e.spellId},
                {"durationSeconds", e.durationSeconds},
                {"chargeCount", e.chargeCount},
            });
        }
        j["enchantments"] = ea;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGEM: %s.wgem\n", base.c_str());
    std::printf("  catalog      : %s\n", c.name.c_str());
    std::printf("  gems         : %zu\n", c.gems.size());
    std::printf("  enchantments : %zu\n", c.enchantments.size());
    if (!c.gems.empty()) {
        std::printf("\n  Gems:\n");
        std::printf("    id   color       stat/value   itemId   name\n");
        for (const auto& g : c.gems) {
            std::printf("  %4u   %-10s  %3u/%-5d   %5u    %s\n",
                        g.gemId,
                        wowee::pipeline::WoweeGem::colorName(g.color),
                        g.statType, g.statValue,
                        g.itemIdToInsert, g.name.c_str());
        }
    }
    if (!c.enchantments.empty()) {
        std::printf("\n  Enchantments:\n");
        std::printf("    id   slot         stat/value   dur(s)   chg   name\n");
        for (const auto& e : c.enchantments) {
            std::printf("  %4u   %-10s   %3u/%-5d   %5u    %3u   %s\n",
                        e.enchantId,
                        wowee::pipeline::WoweeGem::enchantSlotName(e.enchantSlot),
                        e.statType, e.statValue,
                        e.durationSeconds, e.chargeCount,
                        e.name.c_str());
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Two top-level arrays (gems / enchantments)
    // mirror the binary layout. color and enchantSlot emit
    // dual int + name forms.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wgem");
    if (outPath.empty()) outPath = base + ".wgem.json";
    if (!wowee::pipeline::WoweeGemLoader::exists(base)) {
        return reportMissing("export-wgem-json", "WGEM", base, ".wgem");
    }
    auto c = wowee::pipeline::WoweeGemLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json ga = nlohmann::json::array();
    for (const auto& g : c.gems) {
        ga.push_back({
            {"gemId", g.gemId},
            {"itemIdToInsert", g.itemIdToInsert},
            {"name", g.name},
            {"color", g.color},
            {"colorName", wowee::pipeline::WoweeGem::colorName(g.color)},
            {"statType", g.statType},
            {"statValue", g.statValue},
            {"requiredItemQuality", g.requiredItemQuality},
            {"spellId", g.spellId},
        });
    }
    j["gems"] = ga;
    nlohmann::json ea = nlohmann::json::array();
    for (const auto& e : c.enchantments) {
        ea.push_back({
            {"enchantId", e.enchantId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"enchantSlot", e.enchantSlot},
            {"enchantSlotName", wowee::pipeline::WoweeGem::enchantSlotName(e.enchantSlot)},
            {"statType", e.statType},
            {"statValue", e.statValue},
            {"spellId", e.spellId},
            {"durationSeconds", e.durationSeconds},
            {"chargeCount", e.chargeCount},
        });
    }
    j["enchantments"] = ea;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wgem-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source       : %s.wgem\n", base.c_str());
    std::printf("  gems / ench  : %zu / %zu\n",
                c.gems.size(), c.enchantments.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wgem");
    outBase = cli::withoutExt(outBase, ".wgem");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wgem-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wgem-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto colorFromName = [](const std::string& s) -> uint8_t {
        if (s == "meta")      return wowee::pipeline::WoweeGem::Meta;
        if (s == "red")       return wowee::pipeline::WoweeGem::Red;
        if (s == "yellow")    return wowee::pipeline::WoweeGem::Yellow;
        if (s == "blue")      return wowee::pipeline::WoweeGem::Blue;
        if (s == "purple")    return wowee::pipeline::WoweeGem::Purple;
        if (s == "green")     return wowee::pipeline::WoweeGem::Green;
        if (s == "orange")    return wowee::pipeline::WoweeGem::Orange;
        if (s == "prismatic") return wowee::pipeline::WoweeGem::Prismatic;
        return wowee::pipeline::WoweeGem::Red;
    };
    auto slotFromName = [](const std::string& s) -> uint8_t {
        if (s == "permanent") return wowee::pipeline::WoweeGem::Permanent;
        if (s == "temporary") return wowee::pipeline::WoweeGem::Temporary;
        if (s == "socket")    return wowee::pipeline::WoweeGem::SocketColor;
        if (s == "ring")      return wowee::pipeline::WoweeGem::Ring;
        if (s == "cloak")     return wowee::pipeline::WoweeGem::Cloak;
        return wowee::pipeline::WoweeGem::Permanent;
    };
    wowee::pipeline::WoweeGem c;
    c.name = j.value("name", std::string{});
    if (j.contains("gems") && j["gems"].is_array()) {
        for (const auto& jg : j["gems"]) {
            wowee::pipeline::WoweeGem::GemEntry g;
            g.gemId = jg.value("gemId", 0u);
            g.itemIdToInsert = jg.value("itemIdToInsert", 0u);
            g.name = jg.value("name", std::string{});
            if (jg.contains("color") && jg["color"].is_number_integer()) {
                g.color = static_cast<uint8_t>(jg["color"].get<int>());
            } else if (jg.contains("colorName") && jg["colorName"].is_string()) {
                g.color = colorFromName(jg["colorName"].get<std::string>());
            }
            g.statType = static_cast<uint8_t>(jg.value("statType", 0));
            g.statValue = static_cast<int16_t>(jg.value("statValue", 0));
            g.requiredItemQuality = static_cast<uint8_t>(
                jg.value("requiredItemQuality", 0));
            g.spellId = jg.value("spellId", 0u);
            c.gems.push_back(g);
        }
    }
    if (j.contains("enchantments") && j["enchantments"].is_array()) {
        for (const auto& je : j["enchantments"]) {
            wowee::pipeline::WoweeGem::EnchantEntry e;
            e.enchantId = je.value("enchantId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("enchantSlot") && je["enchantSlot"].is_number_integer()) {
                e.enchantSlot = static_cast<uint8_t>(je["enchantSlot"].get<int>());
            } else if (je.contains("enchantSlotName") &&
                       je["enchantSlotName"].is_string()) {
                e.enchantSlot = slotFromName(je["enchantSlotName"].get<std::string>());
            }
            e.statType = static_cast<uint8_t>(je.value("statType", 0));
            e.statValue = static_cast<int16_t>(je.value("statValue", 0));
            e.spellId = je.value("spellId", 0u);
            e.durationSeconds = je.value("durationSeconds", 0u);
            e.chargeCount = static_cast<uint16_t>(je.value("chargeCount", 0));
            c.enchantments.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeGemLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgem-json: failed to save %s.wgem\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgem\n", outBase.c_str());
    std::printf("  source       : %s\n", jsonPath.c_str());
    std::printf("  gems / ench  : %zu / %zu\n",
                c.gems.size(), c.enchantments.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgem");
    if (!wowee::pipeline::WoweeGemLoader::exists(base)) {
        return reportMissing("validate-wgem", "WGEM", base, ".wgem");
    }
    auto c = wowee::pipeline::WoweeGemLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.gems.empty() && c.enchantments.empty()) {
        warnings.push_back("catalog has zero gems and zero enchantments");
    }
    std::vector<uint32_t> gemIdsSeen;
    for (size_t k = 0; k < c.gems.size(); ++k) {
        const auto& g = c.gems[k];
        std::string ctx = "gem " + std::to_string(k) +
                          " (id=" + std::to_string(g.gemId);
        if (!g.name.empty()) ctx += " " + g.name;
        ctx += ")";
        if (g.gemId == 0) errors.push_back(ctx + ": gemId is 0");
        if (g.name.empty()) errors.push_back(ctx + ": name is empty");
        if (g.color > wowee::pipeline::WoweeGem::Prismatic) {
            errors.push_back(ctx + ": color " +
                std::to_string(g.color) + " not in 0..7");
        }
        // Stat-only gems (spellId=0) need statValue != 0.
        if (g.spellId == 0 && g.statValue == 0) {
            warnings.push_back(ctx +
                ": no spell + statValue=0 (gem provides nothing)");
        }
        for (uint32_t prev : gemIdsSeen) {
            if (prev == g.gemId) {
                errors.push_back(ctx + ": duplicate gemId");
                break;
            }
        }
        gemIdsSeen.push_back(g.gemId);
    }
    std::vector<uint32_t> enchIdsSeen;
    for (size_t k = 0; k < c.enchantments.size(); ++k) {
        const auto& e = c.enchantments[k];
        std::string ctx = "enchant " + std::to_string(k) +
                          " (id=" + std::to_string(e.enchantId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.enchantId == 0) errors.push_back(ctx + ": enchantId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.enchantSlot > wowee::pipeline::WoweeGem::Cloak) {
            errors.push_back(ctx + ": enchantSlot " +
                std::to_string(e.enchantSlot) + " not in 0..4");
        }
        if (e.spellId == 0 && e.statValue == 0) {
            warnings.push_back(ctx +
                ": no spell + statValue=0 (enchant provides nothing)");
        }
        // Charges only meaningful for Temporary enchants.
        if (e.chargeCount > 0 &&
            e.enchantSlot != wowee::pipeline::WoweeGem::Temporary) {
            warnings.push_back(ctx +
                ": chargeCount > 0 on non-Temporary slot (charges ignored)");
        }
        for (uint32_t prev : enchIdsSeen) {
            if (prev == e.enchantId) {
                errors.push_back(ctx + ": duplicate enchantId");
                break;
            }
        }
        enchIdsSeen.push_back(e.enchantId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wgem", base, errors, warnings);
    std::printf("validate-wgem: %s.wgem\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu gems, %zu enchantments, all IDs unique\n",
                    c.gems.size(), c.enchantments.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleGemsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-gems") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gems-set") == 0 && i + 1 < argc) {
        outRc = handleGenGemSet(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gems-enchants") == 0 && i + 1 < argc) {
        outRc = handleGenEnchants(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgem") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgem") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgem-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgem-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
