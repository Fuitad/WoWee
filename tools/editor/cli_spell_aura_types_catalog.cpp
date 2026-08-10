#include "cli_spell_aura_types_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_aura_types.hpp"
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

std::string stripWaurExt(std::string base) {
    stripExt(base, ".waur");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeSpellAuraType& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpellAuraTypeLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.waur\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpellAuraType& c,
                     const std::string& base) {
    std::printf("Wrote %s.waur\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  auras   : %zu\n", c.entries.size());
}

int handleGenPeriodic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PeriodicAuras";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWaurExt(base);
    auto c = wowee::pipeline::WoweeSpellAuraTypeLoader::makePeriodic(name);
    if (!saveOrError(c, base, "gen-aur")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenStatMod(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StatModAuras";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWaurExt(base);
    auto c = wowee::pipeline::WoweeSpellAuraTypeLoader::makeStatMod(name);
    if (!saveOrError(c, base, "gen-aur-stats")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMovement(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MovementCCAuras";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWaurExt(base);
    auto c = wowee::pipeline::WoweeSpellAuraTypeLoader::makeMovement(name);
    if (!saveOrError(c, base, "gen-aur-movement")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWaurExt(base);
    if (!wowee::pipeline::WoweeSpellAuraTypeLoader::exists(base)) {
        std::fprintf(stderr, "WAUR not found: %s.waur\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellAuraTypeLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["waur"] = base + ".waur";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"auraTypeId", e.auraTypeId},
                {"name", e.name},
                {"description", e.description},
                {"auraKind", e.auraKind},
                {"auraKindName", wowee::pipeline::WoweeSpellAuraType::auraKindName(e.auraKind)},
                {"targetingHint", e.targetingHint},
                {"targetingHintName", wowee::pipeline::WoweeSpellAuraType::targetingHintName(e.targetingHint)},
                {"isStackable", e.isStackable != 0},
                {"maxStackCount", e.maxStackCount},
                {"updateFrequencyMs", e.updateFrequencyMs},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WAUR: %s.waur\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  auras   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind         target       stack   max    tickMs   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s   %-10s    %s     %3u   %6u    %s\n",
                    e.auraTypeId,
                    wowee::pipeline::WoweeSpellAuraType::auraKindName(e.auraKind),
                    wowee::pipeline::WoweeSpellAuraType::targetingHintName(e.targetingHint),
                    e.isStackable ? "yes" : "no ",
                    e.maxStackCount, e.updateFrequencyMs,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = stripWaurExt(base);
    if (!wowee::pipeline::WoweeSpellAuraTypeLoader::exists(base)) {
        std::fprintf(stderr,
            "export-waur-json: WAUR not found: %s.waur\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellAuraTypeLoader::load(base);
    if (outPath.empty()) outPath = base + ".waur.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["auraTypeId"] = e.auraTypeId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["auraKind"] = e.auraKind;
        je["auraKindName"] =
            wowee::pipeline::WoweeSpellAuraType::auraKindName(e.auraKind);
        je["targetingHint"] = e.targetingHint;
        je["targetingHintName"] =
            wowee::pipeline::WoweeSpellAuraType::targetingHintName(e.targetingHint);
        je["isStackable"] = e.isStackable != 0;
        je["maxStackCount"] = e.maxStackCount;
        je["updateFrequencyMs"] = e.updateFrequencyMs;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-waur-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  auras   : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseAuraKindToken(const nlohmann::json& jv,
                           uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellAuraType::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "periodic")    return wowee::pipeline::WoweeSpellAuraType::Periodic;
        if (s == "stat-mod" ||
            s == "statmod")     return wowee::pipeline::WoweeSpellAuraType::StatMod;
        if (s == "damage-mod" ||
            s == "damagemod")   return wowee::pipeline::WoweeSpellAuraType::DamageMod;
        if (s == "movement")    return wowee::pipeline::WoweeSpellAuraType::Movement;
        if (s == "visual")      return wowee::pipeline::WoweeSpellAuraType::Visual;
        if (s == "trigger")     return wowee::pipeline::WoweeSpellAuraType::Trigger;
        if (s == "resource")    return wowee::pipeline::WoweeSpellAuraType::Resource;
        if (s == "control")     return wowee::pipeline::WoweeSpellAuraType::Control;
        if (s == "misc")        return wowee::pipeline::WoweeSpellAuraType::Misc;
    }
    return fallback;
}

uint8_t parseTargetingHintToken(const nlohmann::json& jv,
                                uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellAuraType::BeneficialOnly)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "any" ||
            s == "anyunit")        return wowee::pipeline::WoweeSpellAuraType::AnyUnit;
        if (s == "self" ||
            s == "selfonly")       return wowee::pipeline::WoweeSpellAuraType::SelfOnly;
        if (s == "hostile" ||
            s == "hostileonly")    return wowee::pipeline::WoweeSpellAuraType::HostileOnly;
        if (s == "beneficial" ||
            s == "beneficialonly") return wowee::pipeline::WoweeSpellAuraType::BeneficialOnly;
    }
    return fallback;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-waur-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-waur-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellAuraType c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellAuraType::Entry e;
            if (je.contains("auraTypeId"))   e.auraTypeId = je["auraTypeId"].get<uint32_t>();
            if (je.contains("name"))         e.name = je["name"].get<std::string>();
            if (je.contains("description"))  e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeSpellAuraType::Periodic;
            if (je.contains("auraKind"))
                kind = parseAuraKindToken(je["auraKind"], kind);
            else if (je.contains("auraKindName"))
                kind = parseAuraKindToken(je["auraKindName"], kind);
            e.auraKind = kind;
            uint8_t hint = wowee::pipeline::WoweeSpellAuraType::AnyUnit;
            if (je.contains("targetingHint"))
                hint = parseTargetingHintToken(je["targetingHint"], hint);
            else if (je.contains("targetingHintName"))
                hint = parseTargetingHintToken(je["targetingHintName"], hint);
            e.targetingHint = hint;
            if (je.contains("isStackable")) {
                if (je["isStackable"].is_boolean())
                    e.isStackable = je["isStackable"].get<bool>() ? 1 : 0;
                else
                    e.isStackable = je["isStackable"].get<uint8_t>() ? 1 : 0;
            }
            if (je.contains("maxStackCount"))     e.maxStackCount = je["maxStackCount"].get<uint8_t>();
            if (je.contains("updateFrequencyMs")) e.updateFrequencyMs = je["updateFrequencyMs"].get<uint32_t>();
            if (je.contains("iconColorRGBA"))     e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) {
        outBase = jsonPath;
        const std::string suffix1 = ".waur.json";
        const std::string suffix2 = ".json";
        if (outBase.size() >= suffix1.size() &&
            outBase.compare(outBase.size() - suffix1.size(),
                            suffix1.size(), suffix1) == 0) {
            outBase.resize(outBase.size() - suffix1.size());
        } else if (outBase.size() >= suffix2.size() &&
                   outBase.compare(outBase.size() - suffix2.size(),
                                   suffix2.size(), suffix2) == 0) {
            outBase.resize(outBase.size() - suffix2.size());
        }
    }
    outBase = stripWaurExt(outBase);
    if (!wowee::pipeline::WoweeSpellAuraTypeLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-waur-json: failed to save %s.waur\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.waur\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  auras   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWaurExt(base);
    if (!wowee::pipeline::WoweeSpellAuraTypeLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-waur: WAUR not found: %s.waur\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellAuraTypeLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.auraTypeId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.auraKind > wowee::pipeline::WoweeSpellAuraType::Misc) {
            errors.push_back(ctx + ": auraKind " +
                std::to_string(e.auraKind) + " not in 0..8");
        }
        if (e.targetingHint > wowee::pipeline::WoweeSpellAuraType::BeneficialOnly) {
            errors.push_back(ctx + ": targetingHint " +
                std::to_string(e.targetingHint) + " not in 0..3");
        }
        // Periodic kind requires updateFrequencyMs > 0.
        if (e.auraKind == wowee::pipeline::WoweeSpellAuraType::Periodic &&
            e.updateFrequencyMs == 0) {
            errors.push_back(ctx +
                ": Periodic kind requires updateFrequencyMs > 0 "
                "— aura would never tick");
        }
        // Non-periodic with updateFrequencyMs > 0 is suspicious.
        if (e.auraKind != wowee::pipeline::WoweeSpellAuraType::Periodic &&
            e.auraKind != wowee::pipeline::WoweeSpellAuraType::Trigger &&
            e.updateFrequencyMs != 0) {
            warnings.push_back(ctx +
                ": " +
                wowee::pipeline::WoweeSpellAuraType::auraKindName(e.auraKind) +
                " kind with updateFrequencyMs=" +
                std::to_string(e.updateFrequencyMs) +
                " — non-periodic auras don't tick, engine ignores");
        }
        // maxStackCount > 0 with isStackable=0 is contradictory.
        if (e.isStackable == 0 && e.maxStackCount > 0) {
            warnings.push_back(ctx +
                ": maxStackCount=" + std::to_string(e.maxStackCount) +
                " set but isStackable=false — stack cap is "
                "unreachable");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.auraTypeId) {
                errors.push_back(ctx + ": duplicate auraTypeId");
                break;
            }
        }
        idsSeen.push_back(e.auraTypeId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("waur", base, errors, warnings);
    std::printf("validate-waur: %s.waur\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu auras, all auraTypeIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellAuraTypesCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-aur") == 0 && i + 1 < argc) {
        outRc = handleGenPeriodic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-aur-stats") == 0 && i + 1 < argc) {
        outRc = handleGenStatMod(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-aur-movement") == 0 && i + 1 < argc) {
        outRc = handleGenMovement(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-waur") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-waur") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-waur-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-waur-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
