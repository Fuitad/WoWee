#include "cli_spell_effect_types_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_effect_types.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeSpellEffectType& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpellEffectTypeLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wsef\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpellEffectType& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsef\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  effects : %zu\n", c.entries.size());
}

int handleGenDamage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DamageEffects";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsef");
    auto c = wowee::pipeline::WoweeSpellEffectTypeLoader::makeDamage(name);
    if (!saveOrError(c, base, "gen-sef")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHealing(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HealingEffects";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsef");
    auto c = wowee::pipeline::WoweeSpellEffectTypeLoader::makeHealing(name);
    if (!saveOrError(c, base, "gen-sef-healing")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAura(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AuraEffects";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsef");
    auto c = wowee::pipeline::WoweeSpellEffectTypeLoader::makeAura(name);
    if (!saveOrError(c, base, "gen-sef-aura")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendBehaviorFlagNames(uint8_t flags, std::string& out) {
    using F = wowee::pipeline::WoweeSpellEffectType;
    auto add = [&](const char* n) {
        if (!out.empty()) out += "|";
        out += n;
    };
    if (flags & F::RequiresTarget)      add("RequiresTarget");
    if (flags & F::RequiresLineOfSight) add("RequiresLineOfSight");
    if (flags & F::IsHostileEffect)     add("IsHostileEffect");
    if (flags & F::IsBeneficialEffect)  add("IsBeneficialEffect");
    if (flags & F::IgnoresImmunities)   add("IgnoresImmunities");
    if (flags & F::TriggersGCD)         add("TriggersGCD");
    if (out.empty()) out = "-";
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsef");
    if (!wowee::pipeline::WoweeSpellEffectTypeLoader::exists(base)) {
        std::fprintf(stderr, "WSEF not found: %s.wsef\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellEffectTypeLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsef"] = base + ".wsef";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string flagNames;
            appendBehaviorFlagNames(e.behaviorFlags, flagNames);
            arr.push_back({
                {"effectId", e.effectId},
                {"name", e.name},
                {"description", e.description},
                {"effectKind", e.effectKind},
                {"effectKindName", wowee::pipeline::WoweeSpellEffectType::effectKindName(e.effectKind)},
                {"behaviorFlags", e.behaviorFlags},
                {"behaviorFlagsLabels", flagNames},
                {"baseAmount", e.baseAmount},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSEF: %s.wsef\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  effects : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind       baseAmt   flags                                                name\n");
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendBehaviorFlagNames(e.behaviorFlags, flagNames);
        std::printf("  %4u   %-9s  %7d   %-50s   %s\n",
                    e.effectId,
                    wowee::pipeline::WoweeSpellEffectType::effectKindName(e.effectKind),
                    e.baseAmount,
                    flagNames.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wsef");
    if (!wowee::pipeline::WoweeSpellEffectTypeLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsef-json: WSEF not found: %s.wsef\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellEffectTypeLoader::load(base);
    if (outPath.empty()) outPath = base + ".wsef.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendBehaviorFlagNames(e.behaviorFlags, flagNames);
        nlohmann::json je;
        je["effectId"] = e.effectId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["effectKind"] = e.effectKind;
        je["effectKindName"] =
            wowee::pipeline::WoweeSpellEffectType::effectKindName(e.effectKind);
        je["behaviorFlags"] = e.behaviorFlags;
        je["behaviorFlagsLabels"] = flagNames;
        je["baseAmount"] = e.baseAmount;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wsef-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  effects : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseEffectKindToken(const nlohmann::json& jv,
                             uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellEffectType::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "damage")   return wowee::pipeline::WoweeSpellEffectType::Damage;
        if (s == "heal")     return wowee::pipeline::WoweeSpellEffectType::Heal;
        if (s == "aura")     return wowee::pipeline::WoweeSpellEffectType::Aura;
        if (s == "energize") return wowee::pipeline::WoweeSpellEffectType::Energize;
        if (s == "trigger")  return wowee::pipeline::WoweeSpellEffectType::Trigger;
        if (s == "movement") return wowee::pipeline::WoweeSpellEffectType::Movement;
        if (s == "summon")   return wowee::pipeline::WoweeSpellEffectType::Summon;
        if (s == "dispel")   return wowee::pipeline::WoweeSpellEffectType::Dispel;
        if (s == "dummy")    return wowee::pipeline::WoweeSpellEffectType::Dummy;
        if (s == "misc")     return wowee::pipeline::WoweeSpellEffectType::Misc;
    }
    return fallback;
}

uint8_t parseBehaviorFlagsField(const nlohmann::json& jv) {
    using F = wowee::pipeline::WoweeSpellEffectType;
    if (jv.is_number_integer() || jv.is_number_unsigned())
        return jv.get<uint8_t>();
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        uint8_t out = 0;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find('|', pos);
            if (end == std::string::npos) end = s.size();
            std::string tok = s.substr(pos, end - pos);
            for (auto& ch : tok) ch = static_cast<char>(std::tolower(ch));
            if (tok == "requirestarget")        out |= F::RequiresTarget;
            else if (tok == "requireslineofsight") out |= F::RequiresLineOfSight;
            else if (tok == "ishostileeffect")  out |= F::IsHostileEffect;
            else if (tok == "isbeneficialeffect") out |= F::IsBeneficialEffect;
            else if (tok == "ignoresimmunities") out |= F::IgnoresImmunities;
            else if (tok == "triggersgcd")      out |= F::TriggersGCD;
            pos = end + 1;
        }
        return out;
    }
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wsef-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wsef-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellEffectType c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellEffectType::Entry e;
            if (je.contains("effectId"))    e.effectId = je["effectId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeSpellEffectType::Damage;
            if (je.contains("effectKind"))
                kind = parseEffectKindToken(je["effectKind"], kind);
            else if (je.contains("effectKindName"))
                kind = parseEffectKindToken(je["effectKindName"], kind);
            e.effectKind = kind;
            if (je.contains("behaviorFlags"))
                e.behaviorFlags = parseBehaviorFlagsField(je["behaviorFlags"]);
            else if (je.contains("behaviorFlagsLabels"))
                e.behaviorFlags = parseBehaviorFlagsField(je["behaviorFlagsLabels"]);
            if (je.contains("baseAmount"))    e.baseAmount = je["baseAmount"].get<int32_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wsef");
    outBase = cli::withoutExt(outBase, ".wsef");
    if (!wowee::pipeline::WoweeSpellEffectTypeLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsef-json: failed to save %s.wsef\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsef\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  effects : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsef");
    if (!wowee::pipeline::WoweeSpellEffectTypeLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsef: WSEF not found: %s.wsef\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellEffectTypeLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    constexpr uint8_t kKnownFlagMask =
        wowee::pipeline::WoweeSpellEffectType::RequiresTarget |
        wowee::pipeline::WoweeSpellEffectType::RequiresLineOfSight |
        wowee::pipeline::WoweeSpellEffectType::IsHostileEffect |
        wowee::pipeline::WoweeSpellEffectType::IsBeneficialEffect |
        wowee::pipeline::WoweeSpellEffectType::IgnoresImmunities |
        wowee::pipeline::WoweeSpellEffectType::TriggersGCD;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.effectId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.effectKind > wowee::pipeline::WoweeSpellEffectType::Misc) {
            errors.push_back(ctx + ": effectKind " +
                std::to_string(e.effectKind) + " not in 0..9");
        }
        if (e.behaviorFlags & ~kKnownFlagMask) {
            warnings.push_back(ctx +
                ": behaviorFlags has bits outside known mask " +
                "(0x" + std::to_string(e.behaviorFlags & ~kKnownFlagMask) +
                ") — engine will ignore unknown flags");
        }
        // Both Hostile and Beneficial set is contradictory.
        if ((e.behaviorFlags & wowee::pipeline::WoweeSpellEffectType::IsHostileEffect) &&
            (e.behaviorFlags & wowee::pipeline::WoweeSpellEffectType::IsBeneficialEffect)) {
            warnings.push_back(ctx +
                ": both IsHostileEffect and IsBeneficialEffect "
                "flags set — engine treats this as Hostile (flag "
                "wins) but the contradiction suggests a config bug");
        }
        // Damage kind without TriggersGCD is unusual.
        if (e.effectKind == wowee::pipeline::WoweeSpellEffectType::Damage &&
            !(e.behaviorFlags & wowee::pipeline::WoweeSpellEffectType::TriggersGCD) &&
            e.effectId != 13) {  // EnvironmentalDamage doesn't trigger GCD
            warnings.push_back(ctx +
                ": Damage kind without TriggersGCD — most damage "
                "effects should be on the GCD; double-check this "
                "is intentional");
        }
        // Heal kind without IsBeneficialEffect is suspicious.
        if (e.effectKind == wowee::pipeline::WoweeSpellEffectType::Heal &&
            !(e.behaviorFlags & wowee::pipeline::WoweeSpellEffectType::IsBeneficialEffect)) {
            warnings.push_back(ctx +
                ": Heal kind without IsBeneficialEffect — "
                "engine treats heals as ungated, may damage enemies");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.effectId) {
                errors.push_back(ctx + ": duplicate effectId");
                break;
            }
        }
        idsSeen.push_back(e.effectId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsef", base, errors, warnings);
    std::printf("validate-wsef: %s.wsef\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu effects, all effectIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellEffectTypesCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-sef") == 0 && i + 1 < argc) {
        outRc = handleGenDamage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sef-healing") == 0 && i + 1 < argc) {
        outRc = handleGenHealing(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sef-aura") == 0 && i + 1 < argc) {
        outRc = handleGenAura(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsef") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsef") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsef-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsef-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
