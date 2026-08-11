#include "cli_spells_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spells.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
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

void appendSpellFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeSpell::Passive)        s += "passive ";
    if (flags & wowee::pipeline::WoweeSpell::Hidden)         s += "hidden ";
    if (flags & wowee::pipeline::WoweeSpell::Channeled)      s += "channeled ";
    if (flags & wowee::pipeline::WoweeSpell::Ranged)         s += "ranged ";
    if (flags & wowee::pipeline::WoweeSpell::AreaOfEffect)   s += "aoe ";
    if (flags & wowee::pipeline::WoweeSpell::Triggered)      s += "triggered ";
    if (flags & wowee::pipeline::WoweeSpell::UnitTargetOnly) s += "unit-only ";
    if (flags & wowee::pipeline::WoweeSpell::FriendlyOnly)   s += "friendly ";
    if (flags & wowee::pipeline::WoweeSpell::HostileOnly)    s += "hostile ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}


void printGenSummary(const wowee::pipeline::WoweeSpell& c,
                     const std::string& base) {
    std::printf("Wrote %s.wspl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  spells  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterSpells";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspl");
    auto c = wowee::pipeline::WoweeSpellLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellLoader>(c, base, "gen-spells", ".wspl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageSpells";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspl");
    auto c = wowee::pipeline::WoweeSpellLoader::makeMage(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellLoader>(c, base, "gen-spells-mage", ".wspl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorSpells";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspl");
    auto c = wowee::pipeline::WoweeSpellLoader::makeWarrior(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellLoader>(c, base, "gen-spells-warrior", ".wspl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspl");
    if (!wowee::pipeline::WoweeSpellLoader::exists(base)) {
        std::fprintf(stderr, "WSPL not found: %s.wspl\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wspl"] = base + ".wspl";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendSpellFlagsStr(fs, e.flags);
            arr.push_back({
                {"spellId", e.spellId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"school", e.school},
                {"schoolName", wowee::pipeline::WoweeSpell::schoolName(e.school)},
                {"targetType", e.targetType},
                {"targetTypeName", wowee::pipeline::WoweeSpell::targetTypeName(e.targetType)},
                {"effectKind", e.effectKind},
                {"effectKindName", wowee::pipeline::WoweeSpell::effectKindName(e.effectKind)},
                {"castTimeMs", e.castTimeMs},
                {"cooldownMs", e.cooldownMs},
                {"gcdMs", e.gcdMs},
                {"manaCost", e.manaCost},
                {"rangeMin", e.rangeMin},
                {"rangeMax", e.rangeMax},
                {"minLevel", e.minLevel},
                {"maxStacks", e.maxStacks},
                {"durationMs", e.durationMs},
                {"effectValueMin", e.effectValueMin},
                {"effectValueMax", e.effectValueMax},
                {"effectMisc", e.effectMisc},
                {"flags", e.flags},
                {"flagsStr", fs},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPL: %s.wspl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  spells  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id      school    effect     cast  cd   mana  range   value     name\n");
    for (const auto& e : c.entries) {
        std::printf("  %5u   %-8s  %-9s  %4ums  %4us  %3u  %4.0f-%-4.0f  %4d-%-4d  %s\n",
                    e.spellId,
                    wowee::pipeline::WoweeSpell::schoolName(e.school),
                    wowee::pipeline::WoweeSpell::effectKindName(e.effectKind),
                    e.castTimeMs, e.cooldownMs / 1000, e.manaCost,
                    e.rangeMin, e.rangeMax,
                    e.effectValueMin, e.effectValueMax,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each spell emits all 18 scalar fields
    // plus dual int + name forms for school, targetType,
    // effectKind, and the flags bitset.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wspl");
    if (outPath.empty()) outPath = base + ".wspl.json";
    if (!wowee::pipeline::WoweeSpellLoader::exists(base)) {
        return reportMissing("export-wspl-json", "WSPL", base, ".wspl");
    }
    auto c = wowee::pipeline::WoweeSpellLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["spellId"] = e.spellId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["iconPath"] = e.iconPath;
        je["school"] = e.school;
        je["schoolName"] = wowee::pipeline::WoweeSpell::schoolName(e.school);
        je["targetType"] = e.targetType;
        je["targetTypeName"] = wowee::pipeline::WoweeSpell::targetTypeName(e.targetType);
        je["effectKind"] = e.effectKind;
        je["effectKindName"] = wowee::pipeline::WoweeSpell::effectKindName(e.effectKind);
        je["castTimeMs"] = e.castTimeMs;
        je["cooldownMs"] = e.cooldownMs;
        je["gcdMs"] = e.gcdMs;
        je["manaCost"] = e.manaCost;
        je["rangeMin"] = e.rangeMin;
        je["rangeMax"] = e.rangeMax;
        je["minLevel"] = e.minLevel;
        je["maxStacks"] = e.maxStacks;
        je["durationMs"] = e.durationMs;
        je["effectValueMin"] = e.effectValueMin;
        je["effectValueMax"] = e.effectValueMax;
        je["effectMisc"] = e.effectMisc;
        je["flags"] = e.flags;
        nlohmann::json fa = nlohmann::json::array();
        if (e.flags & wowee::pipeline::WoweeSpell::Passive)        fa.push_back("passive");
        if (e.flags & wowee::pipeline::WoweeSpell::Hidden)         fa.push_back("hidden");
        if (e.flags & wowee::pipeline::WoweeSpell::Channeled)      fa.push_back("channeled");
        if (e.flags & wowee::pipeline::WoweeSpell::Ranged)         fa.push_back("ranged");
        if (e.flags & wowee::pipeline::WoweeSpell::AreaOfEffect)   fa.push_back("aoe");
        if (e.flags & wowee::pipeline::WoweeSpell::Triggered)      fa.push_back("triggered");
        if (e.flags & wowee::pipeline::WoweeSpell::UnitTargetOnly) fa.push_back("unit-only");
        if (e.flags & wowee::pipeline::WoweeSpell::FriendlyOnly)   fa.push_back("friendly");
        if (e.flags & wowee::pipeline::WoweeSpell::HostileOnly)    fa.push_back("hostile");
        je["flagsList"] = fa;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wspl-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wspl\n", base.c_str());
    std::printf("  spells : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wspl");
    outBase = cli::withoutExt(outBase, ".wspl");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wspl-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wspl-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto schoolFromName = [](const std::string& s) -> uint8_t {
        if (s == "physical") return wowee::pipeline::WoweeSpell::SchoolPhysical;
        if (s == "holy")     return wowee::pipeline::WoweeSpell::SchoolHoly;
        if (s == "fire")     return wowee::pipeline::WoweeSpell::SchoolFire;
        if (s == "nature")   return wowee::pipeline::WoweeSpell::SchoolNature;
        if (s == "frost")    return wowee::pipeline::WoweeSpell::SchoolFrost;
        if (s == "shadow")   return wowee::pipeline::WoweeSpell::SchoolShadow;
        if (s == "arcane")   return wowee::pipeline::WoweeSpell::SchoolArcane;
        return wowee::pipeline::WoweeSpell::SchoolPhysical;
    };
    auto targetFromName = [](const std::string& s) -> uint8_t {
        if (s == "self")        return wowee::pipeline::WoweeSpell::TargetSelf;
        if (s == "single")      return wowee::pipeline::WoweeSpell::TargetSingle;
        if (s == "cone")        return wowee::pipeline::WoweeSpell::TargetCone;
        if (s == "aoe-self")    return wowee::pipeline::WoweeSpell::TargetAoeFromSelf;
        if (s == "line")        return wowee::pipeline::WoweeSpell::TargetLine;
        if (s == "ground")      return wowee::pipeline::WoweeSpell::TargetGround;
        return wowee::pipeline::WoweeSpell::TargetSelf;
    };
    auto effectFromName = [](const std::string& s) -> uint8_t {
        if (s == "damage")   return wowee::pipeline::WoweeSpell::EffectDamage;
        if (s == "heal")     return wowee::pipeline::WoweeSpell::EffectHeal;
        if (s == "buff")     return wowee::pipeline::WoweeSpell::EffectBuff;
        if (s == "debuff")   return wowee::pipeline::WoweeSpell::EffectDebuff;
        if (s == "teleport") return wowee::pipeline::WoweeSpell::EffectTeleport;
        if (s == "summon")   return wowee::pipeline::WoweeSpell::EffectSummon;
        if (s == "dispel")   return wowee::pipeline::WoweeSpell::EffectDispel;
        return wowee::pipeline::WoweeSpell::EffectDamage;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "passive")    return wowee::pipeline::WoweeSpell::Passive;
        if (s == "hidden")     return wowee::pipeline::WoweeSpell::Hidden;
        if (s == "channeled")  return wowee::pipeline::WoweeSpell::Channeled;
        if (s == "ranged")     return wowee::pipeline::WoweeSpell::Ranged;
        if (s == "aoe")        return wowee::pipeline::WoweeSpell::AreaOfEffect;
        if (s == "triggered")  return wowee::pipeline::WoweeSpell::Triggered;
        if (s == "unit-only")  return wowee::pipeline::WoweeSpell::UnitTargetOnly;
        if (s == "friendly")   return wowee::pipeline::WoweeSpell::FriendlyOnly;
        if (s == "hostile")    return wowee::pipeline::WoweeSpell::HostileOnly;
        return 0;
    };
    wowee::pipeline::WoweeSpell c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpell::Entry e;
            e.spellId = je.value("spellId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("school") && je["school"].is_number_integer()) {
                e.school = static_cast<uint8_t>(je["school"].get<int>());
            } else if (je.contains("schoolName") && je["schoolName"].is_string()) {
                e.school = schoolFromName(je["schoolName"].get<std::string>());
            }
            if (je.contains("targetType") && je["targetType"].is_number_integer()) {
                e.targetType = static_cast<uint8_t>(je["targetType"].get<int>());
            } else if (je.contains("targetTypeName") && je["targetTypeName"].is_string()) {
                e.targetType = targetFromName(je["targetTypeName"].get<std::string>());
            }
            if (je.contains("effectKind") && je["effectKind"].is_number_integer()) {
                e.effectKind = static_cast<uint8_t>(je["effectKind"].get<int>());
            } else if (je.contains("effectKindName") && je["effectKindName"].is_string()) {
                e.effectKind = effectFromName(je["effectKindName"].get<std::string>());
            }
            e.castTimeMs = je.value("castTimeMs", 0u);
            e.cooldownMs = je.value("cooldownMs", 0u);
            e.gcdMs = je.value("gcdMs", 1500u);
            e.manaCost = je.value("manaCost", 0u);
            e.rangeMin = je.value("rangeMin", 0.0f);
            e.rangeMax = je.value("rangeMax", 5.0f);
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            e.maxStacks = static_cast<uint16_t>(je.value("maxStacks", 1));
            e.durationMs = je.value("durationMs", 0);
            e.effectValueMin = je.value("effectValueMin", 0);
            e.effectValueMax = je.value("effectValueMax", 0);
            e.effectMisc = je.value("effectMisc", 0);
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= flagFromName(f.get<std::string>());
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeSpellLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wspl-json: failed to save %s.wspl\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wspl\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  spells : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspl");
    if (!wowee::pipeline::WoweeSpellLoader::exists(base)) {
        return reportMissing("validate-wspl", "WSPL", base, ".wspl");
    }
    auto c = wowee::pipeline::WoweeSpellLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    idsSeen.reserve(c.entries.size());
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.spellId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.spellId == 0) {
            errors.push_back(ctx + ": spellId is 0");
        }
        if (e.name.empty()) {
            errors.push_back(ctx + ": name is empty");
        }
        if (e.school > wowee::pipeline::WoweeSpell::SchoolArcane) {
            errors.push_back(ctx + ": school " +
                std::to_string(e.school) + " not in 0..6");
        }
        if (e.effectKind > wowee::pipeline::WoweeSpell::EffectDispel) {
            errors.push_back(ctx + ": effectKind " +
                std::to_string(e.effectKind) + " not in 0..6");
        }
        if (!std::isfinite(e.rangeMin) || !std::isfinite(e.rangeMax)) {
            errors.push_back(ctx + ": rangeMin/Max not finite");
        }
        if (e.rangeMin > e.rangeMax) {
            errors.push_back(ctx + ": rangeMin > rangeMax");
        }
        if (e.effectValueMin > e.effectValueMax) {
            errors.push_back(ctx + ": effectValueMin > effectValueMax");
        }
        // Friendly + Hostile target restrictions are mutually exclusive.
        if ((e.flags & wowee::pipeline::WoweeSpell::FriendlyOnly) &&
            (e.flags & wowee::pipeline::WoweeSpell::HostileOnly)) {
            errors.push_back(ctx +
                ": FriendlyOnly and HostileOnly both set (incoherent)");
        }
        // Damage / debuff effects on a friendly-only spell don't make sense.
        if ((e.flags & wowee::pipeline::WoweeSpell::FriendlyOnly) &&
            (e.effectKind == wowee::pipeline::WoweeSpell::EffectDamage ||
             e.effectKind == wowee::pipeline::WoweeSpell::EffectDebuff)) {
            warnings.push_back(ctx +
                ": friendly-only spell with damage/debuff effect");
        }
        // Heal / buff on a hostile-only spell is incoherent.
        if ((e.flags & wowee::pipeline::WoweeSpell::HostileOnly) &&
            (e.effectKind == wowee::pipeline::WoweeSpell::EffectHeal ||
             e.effectKind == wowee::pipeline::WoweeSpell::EffectBuff)) {
            warnings.push_back(ctx +
                ": hostile-only spell with heal/buff effect");
        }
        // Buff / debuff effects need a non-zero duration to mean anything.
        if ((e.effectKind == wowee::pipeline::WoweeSpell::EffectBuff ||
             e.effectKind == wowee::pipeline::WoweeSpell::EffectDebuff) &&
            e.durationMs == 0) {
            warnings.push_back(ctx +
                ": buff/debuff effect with durationMs=0 (instant fade)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.spellId) {
                errors.push_back(ctx + ": duplicate spellId");
                break;
            }
        }
        idsSeen.push_back(e.spellId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wspl", base, errors, warnings);
    std::printf("validate-wspl: %s.wspl\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu spells, all spellIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-spells") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spells-mage") == 0 && i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spells-warrior") == 0 && i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wspl") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wspl") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wspl-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wspl-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
