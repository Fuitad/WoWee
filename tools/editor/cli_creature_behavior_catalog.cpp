#include "cli_creature_behavior_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_creature_behavior.hpp"
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

const char* creatureKindName(uint8_t k) {
    using B = wowee::pipeline::WoweeCreatureBehavior;
    switch (k) {
        case B::Melee:  return "melee";
        case B::Caster: return "caster";
        case B::Tank:   return "tank";
        case B::Healer: return "healer";
        case B::Pet:    return "pet";
        case B::Beast:  return "beast";
        default:        return "?";
    }
}

const char* evadeBehaviorName(uint8_t e) {
    using B = wowee::pipeline::WoweeCreatureBehavior;
    switch (e) {
        case B::ResetToSpawn: return "resettospawn";
        case B::HealAtPath:   return "healatpath";
        case B::FleeToSpawn:  return "fleetospawn";
        case B::NoEvade:      return "noevade";
        default:              return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeCreatureBehavior& c,
                     const std::string& base) {
    std::printf("Wrote %s.wbhv\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  behaviors: %zu\n", c.entries.size());
}

int handleGenMelee(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MeleeBehaviors";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbhv");
    auto c = wowee::pipeline::WoweeCreatureBehaviorLoader::
        makeMeleeBehaviors(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureBehaviorLoader>(c, base, "gen-bhv-melee", ".wbhv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCaster(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CasterBehaviors";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbhv");
    auto c = wowee::pipeline::WoweeCreatureBehaviorLoader::
        makeCasterBehaviors(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureBehaviorLoader>(c, base, "gen-bhv-caster", ".wbhv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBoss(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BossBehaviors";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbhv");
    auto c = wowee::pipeline::WoweeCreatureBehaviorLoader::
        makeBossBehaviors(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureBehaviorLoader>(c, base, "gen-bhv-boss", ".wbhv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbhv");
    if (!wowee::pipeline::WoweeCreatureBehaviorLoader::exists(base)) {
        std::fprintf(stderr, "WBHV not found: %s.wbhv\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureBehaviorLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wbhv"] = base + ".wbhv";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json specs = nlohmann::json::array();
            for (const auto& s : e.specialAbilities) {
                specs.push_back({
                    {"spellId", s.spellId},
                    {"cooldownMs", s.cooldownMs},
                    {"useChancePct", s.useChancePct},
                });
            }
            arr.push_back({
                {"behaviorId", e.behaviorId},
                {"name", e.name},
                {"creatureKind", e.creatureKind},
                {"creatureKindName",
                    creatureKindName(e.creatureKind)},
                {"evadeBehavior", e.evadeBehavior},
                {"evadeBehaviorName",
                    evadeBehaviorName(e.evadeBehavior)},
                {"aggroRadius", e.aggroRadius},
                {"leashRadius", e.leashRadius},
                {"corpseDurationSec", e.corpseDurationSec},
                {"mainAttackSpellId", e.mainAttackSpellId},
                {"specialAbilities", specs},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WBHV: %s.wbhv\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  behaviors: %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  kind     evade            aggro  leash  corpse  main-spell  specs  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-7s  %-13s   %5.1f  %5.1f  %5us   %8u  %5zu  %s\n",
                    e.behaviorId,
                    creatureKindName(e.creatureKind),
                    evadeBehaviorName(e.evadeBehavior),
                    e.aggroRadius, e.leashRadius,
                    e.corpseDurationSec,
                    e.mainAttackSpellId,
                    e.specialAbilities.size(),
                    e.name.c_str());
    }
    return 0;
}

int parseCreatureKindToken(const std::string& s) {
    using B = wowee::pipeline::WoweeCreatureBehavior;
    if (s == "melee")  return B::Melee;
    if (s == "caster") return B::Caster;
    if (s == "tank")   return B::Tank;
    if (s == "healer") return B::Healer;
    if (s == "pet")    return B::Pet;
    if (s == "beast")  return B::Beast;
    return -1;
}

int parseEvadeBehaviorToken(const std::string& s) {
    using B = wowee::pipeline::WoweeCreatureBehavior;
    if (s == "resettospawn") return B::ResetToSpawn;
    if (s == "healatpath")   return B::HealAtPath;
    if (s == "fleetospawn")  return B::FleeToSpawn;
    if (s == "noevade")      return B::NoEvade;
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
                    "import-wbhv-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wbhv");
    if (!wowee::pipeline::WoweeCreatureBehaviorLoader::exists(base)) {
        return reportMissing("validate-wbhv", "WBHV", base, ".wbhv");
    }
    auto c = wowee::pipeline::WoweeCreatureBehaviorLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.behaviorId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.behaviorId == 0)
            errors.push_back(ctx + ": behaviorId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.creatureKind > 5) {
            errors.push_back(ctx + ": creatureKind " +
                std::to_string(e.creatureKind) +
                " out of range (0..5)");
        }
        if (e.evadeBehavior > 3) {
            errors.push_back(ctx + ": evadeBehavior " +
                std::to_string(e.evadeBehavior) +
                " out of range (0..3)");
        }
        if (e.aggroRadius <= 0.f) {
            errors.push_back(ctx +
                ": aggroRadius must be > 0 (creature would "
                "never engage)");
        }
        // CRITICAL invariant: leashRadius MUST be >=
        // aggroRadius, else the creature would evade
        // back to spawn before reaching its target —
        // permanently un-killable from outside the
        // leash radius.
        if (e.leashRadius > 0.f &&
            e.leashRadius < e.aggroRadius) {
            errors.push_back(ctx +
                ": leashRadius=" +
                std::to_string(e.leashRadius) +
                " < aggroRadius=" +
                std::to_string(e.aggroRadius) +
                " — creature would evade before "
                "engaging (un-killable from outside "
                "the leash)");
        }
        // Corpse < 60s makes looting impossible for
        // anyone but the killer (even the killer in
        // a busy zone).
        if (e.corpseDurationSec > 0 &&
            e.corpseDurationSec < 60) {
            warnings.push_back(ctx +
                ": corpseDurationSec=" +
                std::to_string(e.corpseDurationSec) +
                " is below 60s — looting may fail in "
                "busy zones");
        }
        // Per-special checks.
        std::set<uint32_t> specSpellsSeen;
        for (size_t s = 0; s < e.specialAbilities.size(); ++s) {
            const auto& sp = e.specialAbilities[s];
            if (sp.spellId == 0) {
                errors.push_back(ctx +
                    ": specialAbility[" +
                    std::to_string(s) +
                    "].spellId is 0");
            }
            // useChancePct == 0 means the ability is
            // never auto-fired — only valid for owner-
            // triggered (e.g., warlock Sacrifice).
            // Warn so the editor flags it.
            if (sp.useChancePct == 0 && sp.spellId != 0) {
                warnings.push_back(ctx +
                    ": specialAbility[" +
                    std::to_string(s) +
                    "].useChancePct=0 — ability never "
                    "auto-fires; verify intentional "
                    "(e.g. owner-triggered like Sacrifice)");
            }
            // Same spellId twice in same behavior is
            // a copy-paste bug — both entries would
            // share an internal-cooldown bucket but
            // count as separate slots.
            if (sp.spellId != 0 &&
                !specSpellsSeen.insert(sp.spellId).second) {
                errors.push_back(ctx +
                    ": specialAbility spellId " +
                    std::to_string(sp.spellId) +
                    " appears twice in same behavior — "
                    "duplicate slot is wasted (merge or "
                    "rename)");
            }
        }
        if (!idsSeen.insert(e.behaviorId).second) {
            errors.push_back(ctx + ": duplicate behaviorId");
        }
    }
    return cli::reportValidation("wbhv", base, jsonOut, errors, warnings,
                                 formatted("%zu behaviors, all behaviorIds "
                    "unique, creatureKind 0..5, evadeBehavior "
                    "0..3, aggroRadius > 0, leashRadius >= "
                    "aggroRadius (creature can engage), no "
                    "zero-spellId specials, no duplicate "
                    "specials within same behavior", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wbhv");
    if (out.empty()) out = base + ".wbhv.json";
    if (!wowee::pipeline::WoweeCreatureBehaviorLoader::exists(base)) {
        return reportMissing("export-wbhv-json", "WBHV", base, ".wbhv");
    }
    auto c = wowee::pipeline::WoweeCreatureBehaviorLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WBHV";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json specs = nlohmann::json::array();
        for (const auto& s : e.specialAbilities) {
            specs.push_back({
                {"spellId", s.spellId},
                {"cooldownMs", s.cooldownMs},
                {"useChancePct", s.useChancePct},
            });
        }
        arr.push_back({
            {"behaviorId", e.behaviorId},
            {"name", e.name},
            {"creatureKind", e.creatureKind},
            {"creatureKindName",
                creatureKindName(e.creatureKind)},
            {"evadeBehavior", e.evadeBehavior},
            {"evadeBehaviorName",
                evadeBehaviorName(e.evadeBehavior)},
            {"aggroRadius", e.aggroRadius},
            {"leashRadius", e.leashRadius},
            {"corpseDurationSec", e.corpseDurationSec},
            {"mainAttackSpellId", e.mainAttackSpellId},
            {"specialAbilities", specs},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wbhv-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu behaviors)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wbhv");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wbhv-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wbhv-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCreatureBehavior c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wbhv-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeCreatureBehavior::Entry e;
        e.behaviorId = je.value("behaviorId", 0u);
        e.name = je.value("name", std::string{});
        if (!readEnumField(je, "creatureKind",
                            "creatureKindName",
                            parseCreatureKindToken,
                            "creatureKind", e.behaviorId,
                            e.creatureKind)) return 1;
        if (!readEnumField(je, "evadeBehavior",
                            "evadeBehaviorName",
                            parseEvadeBehaviorToken,
                            "evadeBehavior", e.behaviorId,
                            e.evadeBehavior)) return 1;
        e.aggroRadius = je.value("aggroRadius", 0.f);
        e.leashRadius = je.value("leashRadius", 0.f);
        e.corpseDurationSec =
            je.value("corpseDurationSec", 0u);
        e.mainAttackSpellId =
            je.value("mainAttackSpellId", 0u);
        if (je.contains("specialAbilities") &&
            je["specialAbilities"].is_array()) {
            for (const auto& sj : je["specialAbilities"]) {
                wowee::pipeline::WoweeCreatureBehavior::
                    SpecialAbility s;
                s.spellId = sj.value("spellId", 0u);
                s.cooldownMs = sj.value("cooldownMs", 0u);
                s.useChancePct = static_cast<uint16_t>(
                    sj.value("useChancePct", 0));
                e.specialAbilities.push_back(s);
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeCreatureBehaviorLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wbhv-json: failed to save %s.wbhv\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wbhv (%zu behaviors)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleCreatureBehaviorCatalog(int& i, int argc, char** argv,
                                     int& outRc) {
    if (std::strcmp(argv[i], "--gen-bhv-melee") == 0 &&
        i + 1 < argc) {
        outRc = handleGenMelee(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bhv-caster") == 0 &&
        i + 1 < argc) {
        outRc = handleGenCaster(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bhv-boss") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBoss(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wbhv") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wbhv") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wbhv-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wbhv-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
