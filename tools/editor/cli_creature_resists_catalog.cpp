#include "cli_creature_resists_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_creature_resists.hpp"
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

std::string ccImmunityString(uint16_t mask) {
    using R = wowee::pipeline::WoweeCreatureResists;
    if (mask == 0) return "none";
    if (mask == 0xFFFF) return "all";
    std::string out;
    auto add = [&](const char* tag) {
        if (!out.empty()) out += "+";
        out += tag;
    };
    if (mask & R::ImmuneRoot)      add("root");
    if (mask & R::ImmuneSnare)     add("snare");
    if (mask & R::ImmuneStun)      add("stun");
    if (mask & R::ImmuneFear)      add("fear");
    if (mask & R::ImmuneSleep)     add("sleep");
    if (mask & R::ImmuneSilence)   add("silence");
    if (mask & R::ImmuneCharm)     add("charm");
    if (mask & R::ImmuneDisarm)    add("disarm");
    if (mask & R::ImmunePolymorph) add("polymorph");
    if (mask & R::ImmuneBanish)    add("banish");
    if (mask & R::ImmuneKnockback) add("knockback");
    if (mask & R::ImmuneInterrupt) add("interrupt");
    if (mask & R::ImmuneTaunt)     add("taunt");
    if (mask & R::ImmuneBleed)     add("bleed");
    return out.empty() ? "none" : out;
}


void printGenSummary(const wowee::pipeline::WoweeCreatureResists& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcre\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  resists : %zu\n", c.entries.size());
}

int handleGenBosses(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidBossResists";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcre");
    auto c = wowee::pipeline::WoweeCreatureResistsLoader::makeRaidBosses(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureResistsLoader>(c, base, "gen-cre", ".wcre")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenElites(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "EliteResists";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcre");
    auto c = wowee::pipeline::WoweeCreatureResistsLoader::makeElites(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureResistsLoader>(c, base, "gen-cre-elites", ".wcre")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenImmunities(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CCImmunityProfiles";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcre");
    auto c = wowee::pipeline::WoweeCreatureResistsLoader::makeImmunities(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureResistsLoader>(c, base, "gen-cre-immune", ".wcre")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcre");
    if (!wowee::pipeline::WoweeCreatureResistsLoader::exists(base)) {
        std::fprintf(stderr, "WCRE not found: %s.wcre\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureResistsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcre"] = base + ".wcre";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"resistId", e.resistId},
                {"name", e.name},
                {"description", e.description},
                {"creatureEntry", e.creatureEntry},
                {"holyResist", e.holyResist},
                {"fireResist", e.fireResist},
                {"natureResist", e.natureResist},
                {"frostResist", e.frostResist},
                {"shadowResist", e.shadowResist},
                {"arcaneResist", e.arcaneResist},
                {"physicalResistPct", e.physicalResistPct},
                {"ccImmunityMask", e.ccImmunityMask},
                {"ccImmunityNames", ccImmunityString(e.ccImmunityMask)},
                {"mechanicImmunityMask", e.mechanicImmunityMask},
                {"schoolImmunityMask", e.schoolImmunityMask},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCRE: %s.wcre\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  resists : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   creature   holy fire natu fros shad arca   phys%%  schoolImm  CC-immune\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %5u   %4d %4d %4d %4d %4d %4d   %3u    0x%02X     %s\n",
                    e.resistId, e.creatureEntry,
                    e.holyResist, e.fireResist,
                    e.natureResist, e.frostResist,
                    e.shadowResist, e.arcaneResist,
                    e.physicalResistPct,
                    e.schoolImmunityMask,
                    ccImmunityString(e.ccImmunityMask).c_str());
        std::printf("           %s\n", e.name.c_str());
    }
    return 0;
}

// Parse a "+"-joined token bitmask into the
// ccImmunityMask bits. "all" → 0xFFFF, "none" or empty → 0.
// Returns -1 on unknown token (no partial result).
int parseCcImmunityString(const std::string& s) {
    using R = wowee::pipeline::WoweeCreatureResists;
    if (s.empty() || s == "none") return 0;
    if (s == "all")               return 0xFFFF;
    int mask = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t plus = s.find('+', pos);
        std::string tok = (plus == std::string::npos)
            ? s.substr(pos) : s.substr(pos, plus - pos);
        if      (tok == "root")      mask |= R::ImmuneRoot;
        else if (tok == "snare")     mask |= R::ImmuneSnare;
        else if (tok == "stun")      mask |= R::ImmuneStun;
        else if (tok == "fear")      mask |= R::ImmuneFear;
        else if (tok == "sleep")     mask |= R::ImmuneSleep;
        else if (tok == "silence")   mask |= R::ImmuneSilence;
        else if (tok == "charm")     mask |= R::ImmuneCharm;
        else if (tok == "disarm")    mask |= R::ImmuneDisarm;
        else if (tok == "polymorph") mask |= R::ImmunePolymorph;
        else if (tok == "banish")    mask |= R::ImmuneBanish;
        else if (tok == "knockback") mask |= R::ImmuneKnockback;
        else if (tok == "interrupt") mask |= R::ImmuneInterrupt;
        else if (tok == "taunt")     mask |= R::ImmuneTaunt;
        else if (tok == "bleed")     mask |= R::ImmuneBleed;
        else                          return -1;
        if (plus == std::string::npos) break;
        pos = plus + 1;
    }
    return mask;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcre");
    if (out.empty()) out = base + ".wcre.json";
    if (!wowee::pipeline::WoweeCreatureResistsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wcre-json: WCRE not found: %s.wcre\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureResistsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCRE";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"resistId", e.resistId},
            {"name", e.name},
            {"description", e.description},
            {"creatureEntry", e.creatureEntry},
            {"holyResist", e.holyResist},
            {"fireResist", e.fireResist},
            {"natureResist", e.natureResist},
            {"frostResist", e.frostResist},
            {"shadowResist", e.shadowResist},
            {"arcaneResist", e.arcaneResist},
            {"physicalResistPct", e.physicalResistPct},
            {"ccImmunityMask", e.ccImmunityMask},
            {"ccImmunityNames",
                ccImmunityString(e.ccImmunityMask)},
            {"mechanicImmunityMask", e.mechanicImmunityMask},
            {"schoolImmunityMask", e.schoolImmunityMask},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcre-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu resist profiles)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcre");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcre-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcre-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCreatureResists c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcre-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeCreatureResists::Entry e;
        e.resistId = je.value("resistId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.creatureEntry = je.value("creatureEntry", 0u);
        e.holyResist = static_cast<int16_t>(
            je.value("holyResist", 0));
        e.fireResist = static_cast<int16_t>(
            je.value("fireResist", 0));
        e.natureResist = static_cast<int16_t>(
            je.value("natureResist", 0));
        e.frostResist = static_cast<int16_t>(
            je.value("frostResist", 0));
        e.shadowResist = static_cast<int16_t>(
            je.value("shadowResist", 0));
        e.arcaneResist = static_cast<int16_t>(
            je.value("arcaneResist", 0));
        e.physicalResistPct = static_cast<uint8_t>(
            je.value("physicalResistPct", 0u));
        // ccImmunityMask: int OR "+"-joined token string.
        if (je.contains("ccImmunityMask")) {
            const auto& cm = je["ccImmunityMask"];
            if (cm.is_string()) {
                int parsed = parseCcImmunityString(
                    cm.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wcre-json: unknown "
                        "ccImmunityMask token in '%s' on "
                        "entry id=%u\n",
                        cm.get<std::string>().c_str(),
                        e.resistId);
                    return 1;
                }
                e.ccImmunityMask = static_cast<uint16_t>(parsed);
            } else if (cm.is_number_integer()) {
                e.ccImmunityMask = static_cast<uint16_t>(
                    cm.get<int>());
            }
        } else if (je.contains("ccImmunityNames") &&
                   je["ccImmunityNames"].is_string()) {
            int parsed = parseCcImmunityString(
                je["ccImmunityNames"].get<std::string>());
            if (parsed >= 0)
                e.ccImmunityMask = static_cast<uint16_t>(parsed);
        }
        e.mechanicImmunityMask = je.value(
            "mechanicImmunityMask", 0u);
        e.schoolImmunityMask = static_cast<uint8_t>(
            je.value("schoolImmunityMask", 0u));
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeCreatureResistsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcre-json: failed to save %s.wcre\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcre (%zu resist profiles)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcre");
    if (!wowee::pipeline::WoweeCreatureResistsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wcre: WCRE not found: %s.wcre\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureResistsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<uint32_t> creaturesSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.resistId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.resistId == 0)
            errors.push_back(ctx + ": resistId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.creatureEntry == 0) {
            errors.push_back(ctx +
                ": creatureEntry is 0 — resist profile is "
                "not bound to any WCRT entry");
        }
        // Resist values: int16 covers -32768 to 32767.
        // 32767 is the special "full immunity" sentinel.
        // Negative values are unusual but legal (some DR
        // mechanics can negative-resist).
        auto checkResist = [&](int16_t v, const char* school) {
            if (v < -100) {
                warnings.push_back(ctx + ": " + school +
                    " resist " + std::to_string(v) +
                    " < -100 — extreme negative resist "
                    "creates >2x damage taken; verify");
            }
        };
        checkResist(e.holyResist,   "holy");
        checkResist(e.fireResist,   "fire");
        checkResist(e.natureResist, "nature");
        checkResist(e.frostResist,  "frost");
        checkResist(e.shadowResist, "shadow");
        checkResist(e.arcaneResist, "arcane");
        // physicalResistPct cap is 75% (game-engine
        // hard cap). Above that, the stored value is
        // clamped at runtime.
        if (e.physicalResistPct > 75) {
            warnings.push_back(ctx +
                ": physicalResistPct " +
                std::to_string(e.physicalResistPct) +
                " > 75%% — clamped at runtime to game-"
                "engine cap (armor mitigation cap)");
        }
        // schoolImmunityMask uses bottom 6 bits (one per
        // magic school). Bit 7 unused.
        if (e.schoolImmunityMask & 0xC0) {
            warnings.push_back(ctx +
                ": schoolImmunityMask has reserved bits "
                "set (0xC0) — only bits 0-5 (Holy/Fire/"
                "Nature/Frost/Shadow/Arcane) are meaningful");
        }
        // Multiple WCRE entries binding the same
        // creature is ambiguous (which profile applies?).
        if (e.creatureEntry != 0 &&
            !creaturesSeen.insert(e.creatureEntry).second) {
            errors.push_back(ctx +
                ": creatureEntry " +
                std::to_string(e.creatureEntry) +
                " is already bound by another resist "
                "profile — damage-calc lookup would be "
                "ambiguous");
        }
        if (!idsSeen.insert(e.resistId).second) {
            errors.push_back(ctx + ": duplicate resistId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wcre", base, errors, warnings);
    std::printf("validate-wcre: %s.wcre\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu resist profiles, all "
                    "resistIds + creatureEntries unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleCreatureResistsCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-cre") == 0 && i + 1 < argc) {
        outRc = handleGenBosses(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cre-elites") == 0 && i + 1 < argc) {
        outRc = handleGenElites(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cre-immune") == 0 && i + 1 < argc) {
        outRc = handleGenImmunities(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcre") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcre") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcre-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcre-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
