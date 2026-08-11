#include "cli_combat_formulas_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_combat_formulas.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* outputStatKindName(uint8_t k) {
    using F = wowee::pipeline::WoweeCombatFormulas;
    switch (k) {
        case F::AttackPower:  return "ap";
        case F::SpellPower:   return "sp";
        case F::CritPct:      return "crit";
        case F::DodgePct:     return "dodge";
        case F::ParryPct:     return "parry";
        case F::HitPct:       return "hit";
        case F::SpellCritPct: return "spellcrit";
        case F::HastePct:     return "haste";
        default:              return "?";
    }
}

const char* inputStatKindName(uint8_t k) {
    using F = wowee::pipeline::WoweeCombatFormulas;
    switch (k) {
        case F::Strength:  return "str";
        case F::Agility:   return "agi";
        case F::Stamina:   return "sta";
        case F::Intellect: return "int";
        case F::Spirit:    return "spi";
        default:           return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeCombatFormulas& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcfr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  formulas: %zu\n", c.entries.size());
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorCombatFormulas";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcfr");
    auto c = wowee::pipeline::WoweeCombatFormulasLoader::
        makeWarriorFormulas(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatFormulasLoader>(c, base, "gen-cfr-warrior", ".wcfr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageCombatFormulas";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcfr");
    auto c = wowee::pipeline::WoweeCombatFormulasLoader::
        makeMageFormulas(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatFormulasLoader>(c, base, "gen-cfr-mage", ".wcfr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRogue(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RogueCombatFormulas";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcfr");
    auto c = wowee::pipeline::WoweeCombatFormulasLoader::
        makeRogueFormulas(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatFormulasLoader>(c, base, "gen-cfr-rogue", ".wcfr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcfr");
    if (!wowee::pipeline::WoweeCombatFormulasLoader::exists(base)) {
        std::fprintf(stderr, "WCFR not found: %s.wcfr\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCombatFormulasLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcfr"] = base + ".wcfr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"formulaId", e.formulaId},
                {"name", e.name},
                {"outputStatKind", e.outputStatKind},
                {"outputStatKindName",
                    outputStatKindName(e.outputStatKind)},
                {"inputStatKind", e.inputStatKind},
                {"inputStatKindName",
                    inputStatKindName(e.inputStatKind)},
                {"levelMin", e.levelMin},
                {"levelMax", e.levelMax},
                {"classRestriction", e.classRestriction},
                {"conversionRatioFp_x100",
                    e.conversionRatioFp_x100},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCFR: %s.wcfr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  formulas: %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  output     input  classBits  lvlRange  ratio (x100)  name\n");
    for (const auto& e : c.entries) {
        char lvlBuf[16];
        if (e.levelMax == 0) {
            std::snprintf(lvlBuf, sizeof(lvlBuf),
                          "%3u..*", e.levelMin);
        } else {
            std::snprintf(lvlBuf, sizeof(lvlBuf),
                          "%3u..%-2u", e.levelMin,
                          e.levelMax);
        }
        std::printf("  %4u  %-9s  %-3s    0x%04X     %-8s  %12u  %s\n",
                    e.formulaId,
                    outputStatKindName(e.outputStatKind),
                    inputStatKindName(e.inputStatKind),
                    e.classRestriction,
                    lvlBuf,
                    e.conversionRatioFp_x100,
                    e.name.c_str());
    }
    return 0;
}

int parseOutputStatKindToken(const std::string& s) {
    using F = wowee::pipeline::WoweeCombatFormulas;
    if (s == "ap")        return F::AttackPower;
    if (s == "sp")        return F::SpellPower;
    if (s == "crit")      return F::CritPct;
    if (s == "dodge")     return F::DodgePct;
    if (s == "parry")     return F::ParryPct;
    if (s == "hit")       return F::HitPct;
    if (s == "spellcrit") return F::SpellCritPct;
    if (s == "haste")     return F::HastePct;
    return -1;
}

int parseInputStatKindToken(const std::string& s) {
    using F = wowee::pipeline::WoweeCombatFormulas;
    if (s == "str") return F::Strength;
    if (s == "agi") return F::Agility;
    if (s == "sta") return F::Stamina;
    if (s == "int") return F::Intellect;
    if (s == "spi") return F::Spirit;
    return -1;
}

template <typename ParseFn>
bool readEnumField(const nlohmann::json& je,
                    const char* intKey,
                    const char* nameKey,
                    ParseFn parseFn,
                    const char* label,
                    uint32_t entryId,
                    uint8_t& outValue) {
    if (je.contains(intKey)) {
        const auto& v = je[intKey];
        if (v.is_string()) {
            int parsed = parseFn(v.get<std::string>());
            if (parsed < 0) {
                std::fprintf(stderr,
                    "import-wcfr-json: unknown %s token "
                    "'%s' on entry id=%u\n",
                    label, v.get<std::string>().c_str(),
                    entryId);
                return false;
            }
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
        if (v.is_number_integer()) {
            outValue = static_cast<uint8_t>(v.get<int>());
            return true;
        }
    }
    if (je.contains(nameKey) && je[nameKey].is_string()) {
        int parsed = parseFn(je[nameKey].get<std::string>());
        if (parsed >= 0) {
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
    }
    return true;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcfr");
    if (!wowee::pipeline::WoweeCombatFormulasLoader::exists(base)) {
        return reportMissing("validate-wcfr", "WCFR", base, ".wcfr");
    }
    auto c = wowee::pipeline::WoweeCombatFormulasLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    using Quad = std::tuple<uint8_t, uint8_t, uint16_t, uint8_t>;
    std::set<Quad> applicabilityKeys;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.formulaId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.formulaId == 0)
            errors.push_back(ctx + ": formulaId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.outputStatKind > 7) {
            errors.push_back(ctx + ": outputStatKind " +
                std::to_string(e.outputStatKind) +
                " out of range (0..7)");
        }
        if (e.inputStatKind > 4) {
            errors.push_back(ctx + ": inputStatKind " +
                std::to_string(e.inputStatKind) +
                " out of range (0..4)");
        }
        // Conversion ratio = 0 means input stat
        // never produces any output — a no-op
        // formula. Almost certainly a typo.
        if (e.conversionRatioFp_x100 == 0) {
            errors.push_back(ctx +
                ": conversionRatioFp_x100 is 0 — "
                "input stat never produces output "
                "(no-op formula)");
        }
        if (e.levelMax > 0 && e.levelMax < e.levelMin) {
            errors.push_back(ctx + ": levelMax=" +
                std::to_string(e.levelMax) +
                " < levelMin=" +
                std::to_string(e.levelMin));
        }
        // Ratio > 100x (= 10000 fp_x100) is almost
        // certainly a units-mismatch typo (e.g.
        // forgot to divide by 100 when porting from
        // a percentage table).
        if (e.conversionRatioFp_x100 > 10000) {
            warnings.push_back(ctx +
                ": conversionRatioFp_x100=" +
                std::to_string(e.conversionRatioFp_x100) +
                " (= " +
                std::to_string(e.conversionRatioFp_x100 /
                                100.0) +
                "x) — exceeds 100x ratio; likely a "
                "units-mismatch typo (forgot to divide "
                "by 100?)");
        }
        // Per-(output, input, class, level-band)
        // applicability: walk class bitmask to detect
        // overlapping coverage. A simpler (and more
        // useful) check: same (output, input,
        // classRestriction, levelMin) quad seen twice
        // = duplicate at same scope. When two
        // formulas overlap at the same scope, the
        // runtime stat-compute would apply BOTH (or
        // pick one ambiguously).
        Quad key{e.outputStatKind, e.inputStatKind,
                  e.classRestriction, e.levelMin};
        if (!applicabilityKeys.insert(key).second) {
            errors.push_back(ctx +
                ": duplicate (output=" +
                std::string(outputStatKindName(e.outputStatKind))
                + ", input=" +
                std::string(inputStatKindName(e.inputStatKind))
                + ", classMask=0x" +
                std::to_string(e.classRestriction) +
                ", levelMin=" +
                std::to_string(e.levelMin) +
                ") — runtime stat-compute would apply "
                "both formulas, doubling the contribution");
        }
        if (!idsSeen.insert(e.formulaId).second) {
            errors.push_back(ctx + ": duplicate formulaId");
        }
    }
    return cli::reportValidation("wcfr", base, jsonOut, errors, warnings,
                                 formatted("%zu formulas, all formulaIds + "
                    "(output,input,classMask,levelMin) quads "
                    "unique, outputStatKind 0..7, "
                    "inputStatKind 0..4, conversionRatio > 0, "
                    "valid level range", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcfr");
    if (out.empty()) out = base + ".wcfr.json";
    if (!wowee::pipeline::WoweeCombatFormulasLoader::exists(base)) {
        return reportMissing("export-wcfr-json", "WCFR", base, ".wcfr");
    }
    auto c = wowee::pipeline::WoweeCombatFormulasLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCFR";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"formulaId", e.formulaId},
            {"name", e.name},
            {"outputStatKind", e.outputStatKind},
            {"outputStatKindName",
                outputStatKindName(e.outputStatKind)},
            {"inputStatKind", e.inputStatKind},
            {"inputStatKindName",
                inputStatKindName(e.inputStatKind)},
            {"levelMin", e.levelMin},
            {"levelMax", e.levelMax},
            {"classRestriction", e.classRestriction},
            {"conversionRatioFp_x100",
                e.conversionRatioFp_x100},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcfr-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu formulas)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcfr");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcfr-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcfr-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCombatFormulas c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcfr-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeCombatFormulas::Entry e;
        e.formulaId = je.value("formulaId", 0u);
        e.name = je.value("name", std::string{});
        if (!readEnumField(je, "outputStatKind",
                            "outputStatKindName",
                            parseOutputStatKindToken,
                            "outputStatKind", e.formulaId,
                            e.outputStatKind)) return 1;
        if (!readEnumField(je, "inputStatKind",
                            "inputStatKindName",
                            parseInputStatKindToken,
                            "inputStatKind", e.formulaId,
                            e.inputStatKind)) return 1;
        e.levelMin = static_cast<uint8_t>(
            je.value("levelMin", 0));
        e.levelMax = static_cast<uint8_t>(
            je.value("levelMax", 0));
        e.classRestriction = static_cast<uint16_t>(
            je.value("classRestriction", 0));
        e.conversionRatioFp_x100 =
            je.value("conversionRatioFp_x100", 0u);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeCombatFormulasLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcfr-json: failed to save %s.wcfr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcfr (%zu formulas)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleCombatFormulasCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-cfr-warrior") == 0 &&
        i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cfr-mage") == 0 &&
        i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cfr-rogue") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRogue(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcfr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcfr") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcfr-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcfr-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
