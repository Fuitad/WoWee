#include "cli_spell_proc_rules_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_proc_rules.hpp"
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

const char* triggerEventName(uint8_t e) {
    using P = wowee::pipeline::WoweeSpellProcRules;
    switch (e) {
        case P::OnHit:        return "onhit";
        case P::OnCrit:       return "oncrit";
        case P::OnCast:       return "oncast";
        case P::OnTakeDamage: return "ontakedamage";
        case P::OnHeal:       return "onheal";
        case P::OnDodge:      return "ondodge";
        case P::OnParry:      return "onparry";
        case P::OnBlock:      return "onblock";
        case P::OnKill:       return "onkill";
        default:              return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeSpellProcRules& c,
                     const std::string& base) {
    std::printf("Wrote %s.wprc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  procs   : %zu\n", c.entries.size());
}

int handleGenWeapon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeaponEnchantProcs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprc");
    auto c = wowee::pipeline::WoweeSpellProcRulesLoader::
        makeWeaponProcs(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellProcRulesLoader>(c, base, "gen-prc-weapon", ".wprc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRet(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RetributionPaladinProcs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprc");
    auto c = wowee::pipeline::WoweeSpellProcRulesLoader::
        makeRetPaladin(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellProcRulesLoader>(c, base, "gen-prc-ret", ".wprc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RageGenerationProcs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wprc");
    auto c = wowee::pipeline::WoweeSpellProcRulesLoader::
        makeRageGen(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellProcRulesLoader>(c, base, "gen-prc-rage", ".wprc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wprc");
    if (!wowee::pipeline::WoweeSpellProcRulesLoader::exists(base)) {
        std::fprintf(stderr, "WPRC not found: %s.wprc\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellProcRulesLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wprc"] = base + ".wprc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"procRuleId", e.procRuleId},
                {"name", e.name},
                {"sourceSpellId", e.sourceSpellId},
                {"procEffectSpellId", e.procEffectSpellId},
                {"triggerEvent", e.triggerEvent},
                {"triggerEventName",
                    triggerEventName(e.triggerEvent)},
                {"maxStacksOnTarget", e.maxStacksOnTarget},
                {"procChancePct", e.procChancePct},
                {"internalCooldownMs", e.internalCooldownMs},
                {"procFlagsMask", e.procFlagsMask},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPRC: %s.wprc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  procs   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   srcSpell  effSpell  event           chance  ICDms     stacks  flags  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %7u  %8u  %-13s   %5u   %6u   %5u  0x%04X  %s\n",
                    e.procRuleId, e.sourceSpellId,
                    e.procEffectSpellId,
                    triggerEventName(e.triggerEvent),
                    e.procChancePct, e.internalCooldownMs,
                    e.maxStacksOnTarget,
                    e.procFlagsMask, e.name.c_str());
    }
    return 0;
}

int parseTriggerEventToken(const std::string& s) {
    using P = wowee::pipeline::WoweeSpellProcRules;
    if (s == "onhit")        return P::OnHit;
    if (s == "oncrit")       return P::OnCrit;
    if (s == "oncast")       return P::OnCast;
    if (s == "ontakedamage") return P::OnTakeDamage;
    if (s == "onheal")       return P::OnHeal;
    if (s == "ondodge")      return P::OnDodge;
    if (s == "onparry")      return P::OnParry;
    if (s == "onblock")      return P::OnBlock;
    if (s == "onkill")       return P::OnKill;
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
                    "import-wprc-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wprc");
    if (!wowee::pipeline::WoweeSpellProcRulesLoader::exists(base)) {
        return reportMissing("validate-wprc", "WPRC", base, ".wprc");
    }
    auto c = wowee::pipeline::WoweeSpellProcRulesLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.procRuleId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.procRuleId == 0)
            errors.push_back(ctx + ": procRuleId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.sourceSpellId == 0)
            errors.push_back(ctx +
                ": sourceSpellId is 0 - proc has no "
                "owning aura");
        if (e.procEffectSpellId == 0)
            errors.push_back(ctx +
                ": procEffectSpellId is 0 - proc has "
                "nothing to trigger");
        if (e.triggerEvent > 8) {
            errors.push_back(ctx + ": triggerEvent " +
                std::to_string(e.triggerEvent) +
                " out of range (0..8)");
        }
        if (e.procChancePct == 0) {
            errors.push_back(ctx +
                ": procChancePct is 0 - proc never "
                "fires");
        }
        if (e.procChancePct > 10000) {
            errors.push_back(ctx +
                ": procChancePct " +
                std::to_string(e.procChancePct) +
                " exceeds 10000 (100% in basis points)");
        }
        // Self-proc on OnCast is the most dangerous
        // case - sourceSpellId == procEffectSpellId
        // with OnCast trigger could create an infinite
        // proc loop where the effect spell triggers
        // its own re-cast. Other triggers are usually
        // safe (OnHit/OnCrit don't fire on the proc
        // spell itself unless that spell hits).
        using P = wowee::pipeline::WoweeSpellProcRules;
        if (e.sourceSpellId != 0 &&
            e.sourceSpellId == e.procEffectSpellId &&
            e.triggerEvent == P::OnCast) {
            errors.push_back(ctx +
                ": sourceSpellId == procEffectSpellId="
                + std::to_string(e.sourceSpellId) +
                " on OnCast trigger - infinite proc "
                "loop (effect re-casts itself)");
        }
        // 100% proc chance with 0 ICD on OnHit/OnCrit
        // = every-melee-swing spam - almost certainly
        // unintended performance footgun. Warn unless
        // it's an OnCast bookkeeping rule (those are
        // intentional).
        if (e.procChancePct == 10000 &&
            e.internalCooldownMs == 0 &&
            (e.triggerEvent == P::OnHit ||
             e.triggerEvent == P::OnCrit ||
             e.triggerEvent == P::OnTakeDamage)) {
            warnings.push_back(ctx +
                ": 100% proc chance + 0ms ICD on "
                "high-frequency event (" +
                std::string(triggerEventName(e.triggerEvent)) +
                ") - would spam every swing; verify "
                "intentional or add an ICD");
        }
        if (!idsSeen.insert(e.procRuleId).second) {
            errors.push_back(ctx + ": duplicate procRuleId");
        }
    }
    return cli::reportValidation("wprc", base, jsonOut, errors, warnings,
                                 formatted("%zu procs, all procRuleIds "
                    "unique, sourceSpellId+procEffectSpellId "
                    "non-zero, triggerEvent 0..8, "
                    "procChancePct 1..10000, no infinite "
                    "self-proc loop on OnCast", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wprc");
    if (out.empty()) out = base + ".wprc.json";
    if (!wowee::pipeline::WoweeSpellProcRulesLoader::exists(base)) {
        return reportMissing("export-wprc-json", "WPRC", base, ".wprc");
    }
    auto c = wowee::pipeline::WoweeSpellProcRulesLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WPRC";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"procRuleId", e.procRuleId},
            {"name", e.name},
            {"sourceSpellId", e.sourceSpellId},
            {"procEffectSpellId", e.procEffectSpellId},
            {"triggerEvent", e.triggerEvent},
            {"triggerEventName",
                triggerEventName(e.triggerEvent)},
            {"maxStacksOnTarget", e.maxStacksOnTarget},
            {"procChancePct", e.procChancePct},
            {"internalCooldownMs", e.internalCooldownMs},
            {"procFlagsMask", e.procFlagsMask},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wprc-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu procs)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wprc");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wprc-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wprc-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellProcRules c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wprc-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeSpellProcRules::Entry e;
        e.procRuleId = je.value("procRuleId", 0u);
        e.name = je.value("name", std::string{});
        e.sourceSpellId = je.value("sourceSpellId", 0u);
        e.procEffectSpellId = je.value("procEffectSpellId", 0u);
        if (!readEnumField(je, "triggerEvent", "triggerEventName",
                            parseTriggerEventToken, "triggerEvent",
                            e.procRuleId, e.triggerEvent))
            return 1;
        e.maxStacksOnTarget = static_cast<uint8_t>(
            je.value("maxStacksOnTarget", 0));
        e.procChancePct = static_cast<uint16_t>(
            je.value("procChancePct", 0));
        e.internalCooldownMs =
            je.value("internalCooldownMs", 0u);
        e.procFlagsMask = static_cast<uint16_t>(
            je.value("procFlagsMask", 0));
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeSpellProcRulesLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wprc-json: failed to save %s.wprc\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wprc (%zu procs)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleSpellProcRulesCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-prc-weapon") == 0 &&
        i + 1 < argc) {
        outRc = handleGenWeapon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-prc-ret") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRet(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-prc-rage") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wprc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wprc") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wprc-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wprc-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
