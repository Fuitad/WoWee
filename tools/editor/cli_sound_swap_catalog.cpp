#include "cli_sound_swap_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_sound_swap.hpp"
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

const char* conditionKindName(uint8_t k) {
    using S = wowee::pipeline::WoweeSoundSwap;
    switch (k) {
        case S::Always:     return "always";
        case S::ZoneOnly:   return "zoneonly";
        case S::ClassOnly:  return "classonly";
        case S::RaceOnly:   return "raceonly";
        case S::GenderOnly: return "genderonly";
        default:            return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeSoundSwap& c,
                     const std::string& base) {
    std::printf("Wrote %s.wswp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rules   : %zu\n", c.entries.size());
}

int handleGenBosses(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BossSoundOverrides";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wswp");
    auto c = wowee::pipeline::WoweeSoundSwapLoader::
        makeBossOverrides(name);
    if (!saveOrError<wowee::pipeline::WoweeSoundSwapLoader>(c, base, "gen-swp-bosses", ".wswp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRace(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaceVoiceOverrides";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wswp");
    auto c = wowee::pipeline::WoweeSoundSwapLoader::
        makeRaceVoices(name);
    if (!saveOrError<wowee::pipeline::WoweeSoundSwapLoader>(c, base, "gen-swp-race", ".wswp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUI(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "GlobalUISoundOverrides";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wswp");
    auto c = wowee::pipeline::WoweeSoundSwapLoader::
        makeGlobalUI(name);
    if (!saveOrError<wowee::pipeline::WoweeSoundSwapLoader>(c, base, "gen-swp-ui", ".wswp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wswp");
    if (!wowee::pipeline::WoweeSoundSwapLoader::exists(base)) {
        std::fprintf(stderr, "WSWP not found: %s.wswp\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSoundSwapLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wswp"] = base + ".wswp";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"ruleId", e.ruleId},
                {"name", e.name},
                {"originalSoundId", e.originalSoundId},
                {"replacementSoundId", e.replacementSoundId},
                {"conditionKind", e.conditionKind},
                {"conditionKindName",
                    conditionKindName(e.conditionKind)},
                {"priorityIndex", e.priorityIndex},
                {"gainAdjustDb_x10", e.gainAdjustDb_x10},
                {"conditionValue", e.conditionValue},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSWP: %s.wswp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rules   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  origSound  replSound  condition       value  prio  gain    name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %9u  %9u  %-12s   %5u   %3u  %+4d   %s\n",
                    e.ruleId, e.originalSoundId,
                    e.replacementSoundId,
                    conditionKindName(e.conditionKind),
                    e.conditionValue,
                    e.priorityIndex,
                    e.gainAdjustDb_x10,
                    e.name.c_str());
    }
    return 0;
}

int parseConditionKindToken(const std::string& s) {
    using S = wowee::pipeline::WoweeSoundSwap;
    if (s == "always")     return S::Always;
    if (s == "zoneonly")   return S::ZoneOnly;
    if (s == "classonly")  return S::ClassOnly;
    if (s == "raceonly")   return S::RaceOnly;
    if (s == "genderonly") return S::GenderOnly;
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
                    "import-wswp-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wswp");
    if (!wowee::pipeline::WoweeSoundSwapLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wswp: WSWP not found: %s.wswp\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSoundSwapLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    using Triple = std::tuple<uint32_t, uint8_t, uint32_t>;
    std::set<Triple> tripleSeen;
    using PrioPair = std::pair<uint32_t, uint8_t>;
    std::set<PrioPair> prioSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.ruleId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.ruleId == 0)
            errors.push_back(ctx + ": ruleId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.originalSoundId == 0)
            errors.push_back(ctx +
                ": originalSoundId is 0 — no source "
                "sound to swap");
        if (e.replacementSoundId == 0)
            errors.push_back(ctx +
                ": replacementSoundId is 0 — no "
                "replacement to play");
        if (e.conditionKind > 4) {
            errors.push_back(ctx + ": conditionKind " +
                std::to_string(e.conditionKind) +
                " out of range (0..4)");
        }
        // Self-replacement is always a bug — replacing
        // a sound with itself is a no-op that wastes
        // a dispatch slot.
        if (e.originalSoundId != 0 &&
            e.originalSoundId == e.replacementSoundId) {
            errors.push_back(ctx +
                ": originalSoundId == replacementSoundId="
                + std::to_string(e.originalSoundId) +
                " — no-op self-replacement");
        }
        // priorityIndex == 0 means the rule is never
        // picked when any other rule for the same
        // sound matches. Could be intentional
        // (disable rule) but warn.
        if (e.priorityIndex == 0) {
            warnings.push_back(ctx +
                ": priorityIndex=0 — rule never wins "
                "tie-break (effectively disabled); "
                "remove or set priority > 0");
        }
        // Gain range clamp: ±30 dB is the practical
        // limit for client mixers; beyond risks
        // clipping or inaudibility.
        if (e.gainAdjustDb_x10 > 300 ||
            e.gainAdjustDb_x10 < -300) {
            warnings.push_back(ctx +
                ": gainAdjustDb_x10=" +
                std::to_string(e.gainAdjustDb_x10) +
                " (=" +
                std::to_string(e.gainAdjustDb_x10 / 10) +
                " dB) outside ±30 dB practical range "
                "— mixer may clip or sound becomes "
                "inaudible");
        }
        // Condition-value sanity: Always condition
        // should have value=0 (any other value is
        // dead data). Other kinds need value != 0
        // (kind without target = matches everything,
        // duplicating Always semantics).
        using S = wowee::pipeline::WoweeSoundSwap;
        if (e.conditionKind == S::Always &&
            e.conditionValue != 0) {
            warnings.push_back(ctx +
                ": Always condition with non-zero "
                "conditionValue=" +
                std::to_string(e.conditionValue) +
                " — value is ignored at runtime "
                "(dead data)");
        }
        if (e.conditionKind != S::Always &&
            e.conditionValue == 0) {
            errors.push_back(ctx +
                ": non-Always conditionKind=" +
                std::string(conditionKindName(e.conditionKind))
                + " requires non-zero conditionValue");
        }
        // (originalSoundId, conditionKind,
        // conditionValue) MUST be unique — two rules
        // with the same trigger triple at different
        // priorities are still ordered, but two with
        // the SAME priority would tie.
        Triple t{e.originalSoundId, e.conditionKind,
                  e.conditionValue};
        if (e.originalSoundId != 0 &&
            !tripleSeen.insert(t).second) {
            errors.push_back(ctx +
                ": duplicate trigger triple "
                "(originalSoundId=" +
                std::to_string(e.originalSoundId) +
                ", conditionKind=" +
                std::string(conditionKindName(e.conditionKind))
                + ", conditionValue=" +
                std::to_string(e.conditionValue) +
                ") — runtime would have two rules "
                "for the same trigger");
        }
        // Same priority within same originalSoundId
        // is a tie-break ambiguity even if conditions
        // differ.
        PrioPair pp{e.originalSoundId, e.priorityIndex};
        if (e.originalSoundId != 0 &&
            e.priorityIndex != 0 &&
            !prioSeen.insert(pp).second) {
            warnings.push_back(ctx +
                ": originalSoundId=" +
                std::to_string(e.originalSoundId) +
                " has another rule at same "
                "priorityIndex=" +
                std::to_string(e.priorityIndex) +
                " — tie-break order undefined when "
                "both rules' conditions match");
        }
        if (!idsSeen.insert(e.ruleId).second) {
            errors.push_back(ctx + ": duplicate ruleId");
        }
    }
    return cli::reportValidation("wswp", base, jsonOut, errors, warnings,
                                 formatted("%zu rules, all ruleIds unique, "
                    "non-zero original+replacement sound, "
                    "no self-replacement, conditionKind "
                    "0..4, no duplicate trigger triples, "
                    "non-Always kinds have non-zero "
                    "conditionValue", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wswp");
    if (out.empty()) out = base + ".wswp.json";
    if (!wowee::pipeline::WoweeSoundSwapLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wswp-json: WSWP not found: %s.wswp\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSoundSwapLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WSWP";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"ruleId", e.ruleId},
            {"name", e.name},
            {"originalSoundId", e.originalSoundId},
            {"replacementSoundId", e.replacementSoundId},
            {"conditionKind", e.conditionKind},
            {"conditionKindName",
                conditionKindName(e.conditionKind)},
            {"priorityIndex", e.priorityIndex},
            {"gainAdjustDb_x10", e.gainAdjustDb_x10},
            {"conditionValue", e.conditionValue},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wswp-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu rules)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wswp");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wswp-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wswp-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSoundSwap c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wswp-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeSoundSwap::Entry e;
        e.ruleId = je.value("ruleId", 0u);
        e.name = je.value("name", std::string{});
        e.originalSoundId = je.value("originalSoundId", 0u);
        e.replacementSoundId =
            je.value("replacementSoundId", 0u);
        if (!readEnumField(je, "conditionKind",
                            "conditionKindName",
                            parseConditionKindToken,
                            "conditionKind", e.ruleId,
                            e.conditionKind)) return 1;
        e.priorityIndex = static_cast<uint8_t>(
            je.value("priorityIndex", 0));
        e.gainAdjustDb_x10 = static_cast<int16_t>(
            je.value("gainAdjustDb_x10", 0));
        e.conditionValue = je.value("conditionValue", 0u);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeSoundSwapLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wswp-json: failed to save %s.wswp\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wswp (%zu rules)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleSoundSwapCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-swp-bosses") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBosses(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-swp-race") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRace(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-swp-ui") == 0 &&
        i + 1 < argc) {
        outRc = handleGenUI(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wswp") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wswp") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wswp-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wswp-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
