#include "cli_random_property_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_random_property.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeRandomProperty& c,
                     const std::string& base) {
    std::printf("Wrote %s.wirc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  pools   : %zu\n", c.entries.size());
}

int handleGenBear(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "OfTheBearPool";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wirc");
    auto c = wowee::pipeline::WoweeRandomPropertyLoader::
        makeOfTheBear(name);
    if (!saveOrError<wowee::pipeline::WoweeRandomPropertyLoader>(c, base, "gen-irc-bear", ".wirc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenEagle(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "OfTheEaglePool";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wirc");
    auto c = wowee::pipeline::WoweeRandomPropertyLoader::
        makeOfTheEagle(name);
    if (!saveOrError<wowee::pipeline::WoweeRandomPropertyLoader>(c, base, "gen-irc-eagle", ".wirc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTiger(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "OfTheTigerPool";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wirc");
    auto c = wowee::pipeline::WoweeRandomPropertyLoader::
        makeOfTheTiger(name);
    if (!saveOrError<wowee::pipeline::WoweeRandomPropertyLoader>(c, base, "gen-irc-tiger", ".wirc")) return 1;
    printGenSummary(c, base);
    return 0;
}

std::string slotsMaskString(uint8_t mask) {
    using R = wowee::pipeline::WoweeRandomProperty;
    if (mask == 0) return "(none)";
    std::string s;
    auto add = [&](uint8_t bit, const char* label) {
        if (mask & bit) {
            if (!s.empty()) s += "|";
            s += label;
        }
    };
    add(R::Helm, "Helm"); add(R::Shoulder, "Shoulder");
    add(R::Chest, "Chest"); add(R::Leg, "Leg");
    add(R::Boot, "Boot"); add(R::Glove, "Glove");
    add(R::Bracer, "Bracer"); add(R::Belt, "Belt");
    return s;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wirc");
    if (!wowee::pipeline::WoweeRandomPropertyLoader::exists(base)) {
        std::fprintf(stderr, "WIRC not found: %s.wirc\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRandomPropertyLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wirc"] = base + ".wirc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json enchants = nlohmann::json::array();
            for (const auto& en : e.enchants) {
                enchants.push_back({
                    {"enchantId", en.enchantId},
                    {"weight", en.weight},
                });
            }
            arr.push_back({
                {"poolId", e.poolId},
                {"name", e.name},
                {"scaleLevel", e.scaleLevel},
                {"allowedSlotsMask", e.allowedSlotsMask},
                {"allowedSlotsString",
                    slotsMaskString(e.allowedSlotsMask)},
                {"allowedClassesMask", e.allowedClassesMask},
                {"totalWeight", e.totalWeight},
                {"enchants", enchants},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WIRC: %s.wirc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  pools   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  scale  slots                          classes  total  enchants  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %5u  %-30s  0x%04X  %5u  %8zu  %s\n",
                    e.poolId, e.scaleLevel,
                    slotsMaskString(e.allowedSlotsMask).c_str(),
                    e.allowedClassesMask,
                    e.totalWeight,
                    e.enchants.size(),
                    e.name.c_str());
    }
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wirc");
    if (!wowee::pipeline::WoweeRandomPropertyLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wirc: WIRC not found: %s.wirc\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRandomPropertyLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.poolId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.poolId == 0)
            errors.push_back(ctx + ": poolId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        // allowedSlotsMask=0 means no slot can roll
        // this pool — pool is unreachable.
        if (e.allowedSlotsMask == 0) {
            errors.push_back(ctx +
                ": allowedSlotsMask is 0 — no slot "
                "would ever roll this pool (unreachable)");
        }
        // Empty enchant array means the loot generator
        // would have nothing to pick from.
        if (e.enchants.empty()) {
            errors.push_back(ctx +
                ": no enchants — loot generator would "
                "have nothing to pick");
        }
        // Per-enchant checks.
        std::set<uint32_t> enchantsSeen;
        uint64_t weightSum = 0;
        for (size_t k2 = 0; k2 < e.enchants.size(); ++k2) {
            const auto& en = e.enchants[k2];
            if (en.enchantId == 0) {
                errors.push_back(ctx +
                    ": enchant[" + std::to_string(k2) +
                    "].enchantId is 0");
            }
            // Weight 0 means the enchant is in the
            // pool but never picked — wastes catalog
            // space. Warn.
            if (en.weight == 0 && en.enchantId != 0) {
                warnings.push_back(ctx +
                    ": enchant[" + std::to_string(k2) +
                    "] enchantId=" +
                    std::to_string(en.enchantId) +
                    " has weight=0 — never picked, "
                    "remove or assign weight");
            }
            // Same enchant listed twice — should be
            // merged into single entry with summed
            // weight.
            if (en.enchantId != 0 &&
                !enchantsSeen.insert(en.enchantId).second) {
                errors.push_back(ctx +
                    ": enchant id " +
                    std::to_string(en.enchantId) +
                    " appears twice in same pool — "
                    "should be merged into single entry "
                    "with summed weight");
            }
            weightSum += en.weight;
        }
        // totalWeight should match sum of enchant
        // weights — if not, the loot generator's
        // denormalized rolling won't pick the right
        // distribution.
        if (e.totalWeight !=
            static_cast<uint32_t>(weightSum)) {
            errors.push_back(ctx +
                ": totalWeight=" +
                std::to_string(e.totalWeight) +
                " does not match sum of enchant weights="
                + std::to_string(weightSum) +
                " — loot generator would mis-pick");
        }
        if (!idsSeen.insert(e.poolId).second) {
            errors.push_back(ctx + ": duplicate poolId");
        }
    }
    return cli::reportValidation("wirc", base, jsonOut, errors, warnings,
                                 formatted("%zu pools, all poolIds unique, "
                    "non-zero allowedSlotsMask, non-empty "
                    "enchant array, no zero-id enchants, no "
                    "duplicate enchants in same pool, "
                    "totalWeight matches enchant-weight sum", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wirc");
    if (out.empty()) out = base + ".wirc.json";
    if (!wowee::pipeline::WoweeRandomPropertyLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wirc-json: WIRC not found: %s.wirc\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRandomPropertyLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WIRC";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json enchants = nlohmann::json::array();
        for (const auto& en : e.enchants) {
            enchants.push_back({
                {"enchantId", en.enchantId},
                {"weight", en.weight},
            });
        }
        arr.push_back({
            {"poolId", e.poolId},
            {"name", e.name},
            {"scaleLevel", e.scaleLevel},
            {"allowedSlotsMask", e.allowedSlotsMask},
            {"allowedSlotsString",
                slotsMaskString(e.allowedSlotsMask)},
            {"allowedClassesMask", e.allowedClassesMask},
            {"totalWeight", e.totalWeight},
            {"enchants", enchants},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wirc-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu pools)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wirc");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wirc-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wirc-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeRandomProperty c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wirc-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeRandomProperty::Entry e;
        e.poolId = je.value("poolId", 0u);
        e.name = je.value("name", std::string{});
        e.scaleLevel = static_cast<uint8_t>(
            je.value("scaleLevel", 0));
        e.allowedSlotsMask = static_cast<uint8_t>(
            je.value("allowedSlotsMask", 0));
        e.allowedClassesMask = static_cast<uint16_t>(
            je.value("allowedClassesMask", 0));
        e.totalWeight = je.value("totalWeight", 0u);
        if (je.contains("enchants") &&
            je["enchants"].is_array()) {
            for (const auto& enj : je["enchants"]) {
                wowee::pipeline::WoweeRandomProperty::
                    EnchantEntry en;
                en.enchantId = enj.value("enchantId", 0u);
                en.weight = enj.value("weight", 0u);
                e.enchants.push_back(en);
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeRandomPropertyLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wirc-json: failed to save %s.wirc\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wirc (%zu pools)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleRandomPropertyCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-irc-bear") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBear(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-irc-eagle") == 0 &&
        i + 1 < argc) {
        outRc = handleGenEagle(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-irc-tiger") == 0 &&
        i + 1 < argc) {
        outRc = handleGenTiger(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wirc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wirc") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wirc-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wirc-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
