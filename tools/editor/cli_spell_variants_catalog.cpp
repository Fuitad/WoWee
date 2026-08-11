#include "cli_spell_variants_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_variants.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* conditionKindName(uint8_t k) {
    using V = wowee::pipeline::WoweeSpellVariants;
    switch (k) {
        case V::Stance:         return "stance";
        case V::Form:           return "form";
        case V::Talent:         return "talent";
        case V::Race:           return "race";
        case V::EquippedWeapon: return "equippedweapon";
        case V::AuraActive:     return "auraactive";
        default:                return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeSpellVariants& c,
                     const std::string& base) {
    std::printf("Wrote %s.wspv\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  variants : %zu\n", c.entries.size());
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorStanceVariants";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspv");
    auto c = wowee::pipeline::WoweeSpellVariantsLoader::makeWarriorStance(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellVariantsLoader>(c, base, "gen-spv", ".wspv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTalent(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TalentModifiedVariants";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspv");
    auto c = wowee::pipeline::WoweeSpellVariantsLoader::makeTalentMod(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellVariantsLoader>(c, base, "gen-spv-talent", ".wspv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRacial(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RacialVariants";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspv");
    auto c = wowee::pipeline::WoweeSpellVariantsLoader::makeRacial(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellVariantsLoader>(c, base, "gen-spv-racial", ".wspv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspv");
    if (!wowee::pipeline::WoweeSpellVariantsLoader::exists(base)) {
        return reportMissing("WSPV", base, ".wspv");
    }
    auto c = wowee::pipeline::WoweeSpellVariantsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wspv"] = base + ".wspv";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"variantId", e.variantId},
                {"name", e.name},
                {"description", e.description},
                {"baseSpellId", e.baseSpellId},
                {"variantSpellId", e.variantSpellId},
                {"conditionKind", e.conditionKind},
                {"conditionKindName",
                    conditionKindName(e.conditionKind)},
                {"priority", e.priority},
                {"conditionValue", e.conditionValue},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPV: %s.wspv\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  variants : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   baseSp  varSp   condition       condVal  prio  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %5u   %5u   %-13s   %5u   %3u   %s\n",
                    e.variantId, e.baseSpellId,
                    e.variantSpellId,
                    conditionKindName(e.conditionKind),
                    e.conditionValue, e.priority,
                    e.name.c_str());
    }
    return 0;
}

int parseConditionKindToken(const std::string& s) {
    using V = wowee::pipeline::WoweeSpellVariants;
    if (s == "stance")         return V::Stance;
    if (s == "form")           return V::Form;
    if (s == "talent")         return V::Talent;
    if (s == "race")           return V::Race;
    if (s == "equippedweapon") return V::EquippedWeapon;
    if (s == "auraactive")     return V::AuraActive;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wspv");
    if (out.empty()) out = base + ".wspv.json";
    if (!wowee::pipeline::WoweeSpellVariantsLoader::exists(base)) {
        return reportMissing("export-wspv-json", "WSPV", base, ".wspv");
    }
    auto c = wowee::pipeline::WoweeSpellVariantsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WSPV";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"variantId", e.variantId},
            {"name", e.name},
            {"description", e.description},
            {"baseSpellId", e.baseSpellId},
            {"variantSpellId", e.variantSpellId},
            {"conditionKind", e.conditionKind},
            {"conditionKindName", conditionKindName(e.conditionKind)},
            {"priority", e.priority},
            {"conditionValue", e.conditionValue},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wspv-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu variants)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wspv");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wspv-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wspv-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellVariants c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wspv-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeSpellVariants::Entry e;
        e.variantId = je.value("variantId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.baseSpellId = je.value("baseSpellId", 0u);
        e.variantSpellId = je.value("variantSpellId", 0u);
        if (je.contains("conditionKind")) {
            const auto& v = je["conditionKind"];
            if (v.is_string()) {
                int parsed = parseConditionKindToken(
                    v.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wspv-json: unknown "
                        "conditionKind token '%s' on "
                        "entry id=%u\n",
                        v.get<std::string>().c_str(),
                        e.variantId);
                    return 1;
                }
                e.conditionKind = static_cast<uint8_t>(parsed);
            } else if (v.is_number_integer()) {
                e.conditionKind = static_cast<uint8_t>(
                    v.get<int>());
            }
        } else if (je.contains("conditionKindName") &&
                   je["conditionKindName"].is_string()) {
            int parsed = parseConditionKindToken(
                je["conditionKindName"].get<std::string>());
            if (parsed >= 0)
                e.conditionKind = static_cast<uint8_t>(parsed);
        }
        e.priority = static_cast<uint8_t>(
            je.value("priority", 1u));
        e.conditionValue = je.value("conditionValue", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeSpellVariantsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wspv-json: failed to save %s.wspv\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wspv (%zu variants)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspv");
    if (!wowee::pipeline::WoweeSpellVariantsLoader::exists(base)) {
        return reportMissing("validate-wspv", "WSPV", base, ".wspv");
    }
    auto c = wowee::pipeline::WoweeSpellVariantsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-(baseSpell, conditionKind, conditionValue,
    // priority) tuple uniqueness — two variants with all
    // four matching would tie at runtime and resolve
    // non-deterministically.
    std::set<uint64_t> tupleSeen;
    auto tupleKey = [](uint32_t base, uint8_t kind,
                       uint32_t value, uint8_t prio) {
        // Pack into 64 bits: base (32) | value (16
        // truncated) | kind (8) | prio (8). Tight packing
        // so we don't need a multi-key set.
        uint64_t k = static_cast<uint64_t>(base) << 32;
        k |= (static_cast<uint64_t>(value & 0xFFFF) << 16);
        k |= (static_cast<uint64_t>(kind) << 8);
        k |= prio;
        return k;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.variantId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.variantId == 0)
            errors.push_back(ctx + ": variantId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.baseSpellId == 0) {
            errors.push_back(ctx +
                ": baseSpellId is 0 — variant has no "
                "base spell to substitute for");
        }
        if (e.variantSpellId == 0) {
            errors.push_back(ctx +
                ": variantSpellId is 0 — variant has no "
                "spell to substitute INTO");
        }
        if (e.conditionKind > 5) {
            errors.push_back(ctx + ": conditionKind " +
                std::to_string(e.conditionKind) +
                " out of range (must be 0..5)");
        }
        if (e.conditionValue == 0) {
            warnings.push_back(ctx +
                ": conditionValue is 0 — condition would "
                "match the always-zero default; verify "
                "if intentional (the gate becomes a "
                "no-op)");
        }
        // Tuple uniqueness check.
        uint64_t key = tupleKey(e.baseSpellId,
                                  e.conditionKind,
                                  e.conditionValue,
                                  e.priority);
        if (!tupleSeen.insert(key).second) {
            errors.push_back(ctx +
                ": (baseSpell=" +
                std::to_string(e.baseSpellId) +
                ", conditionKind=" +
                std::string(conditionKindName(e.conditionKind)) +
                ", conditionValue=" +
                std::to_string(e.conditionValue) +
                ", priority=" +
                std::to_string(e.priority) +
                ") tuple already bound by another variant "
                "— spell-cast pipeline lookup would be "
                "non-deterministic");
        }
        if (!idsSeen.insert(e.variantId).second) {
            errors.push_back(ctx + ": duplicate variantId");
        }
    }
    return cli::reportValidation("wspv", base, jsonOut, errors, warnings,
                                 formatted("%zu variants, all variantIds + "
                    "(base,kind,val,prio) tuples unique", c.entries.size()));
}

} // namespace

bool handleSpellVariantsCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-spv") == 0 && i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spv-talent") == 0 &&
        i + 1 < argc) {
        outRc = handleGenTalent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spv-racial") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRacial(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wspv") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wspv") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wspv-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wspv-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
