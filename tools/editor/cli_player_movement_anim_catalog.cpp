#include "cli_player_movement_anim_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_player_movement_anim.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* movementStateName(uint8_t s) {
    using P = wowee::pipeline::WoweePlayerMovementAnim;
    switch (s) {
        case P::StateIdle:  return "idle";
        case P::StateWalk:  return "walk";
        case P::StateRun:   return "run";
        case P::StateSwim:  return "swim";
        case P::StateFly:   return "fly";
        case P::StateSit:   return "sit";
        case P::StateMount: return "mount";
        case P::StateDeath: return "death";
        default:            return "?";
    }
}

const char* raceIdName(uint8_t r) {
    // Vanilla 1.12 ChrRaces ids — display only.
    switch (r) {
        case 1:  return "Human";
        case 2:  return "Orc";
        case 3:  return "Dwarf";
        case 4:  return "NightElf";
        case 5:  return "Undead";
        case 6:  return "Tauren";
        case 7:  return "Gnome";
        case 8:  return "Troll";
        default: return "?";
    }
}

const char* genderName(uint8_t g) {
    return g == 0 ? "M" : (g == 1 ? "F" : "?");
}


void printGenSummary(const wowee::pipeline::WoweePlayerMovementAnim& c,
                     const std::string& base) {
    std::printf("Wrote %s.wphm\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  bindings: %zu\n", c.entries.size());
}

int handleGenHuman(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HumanMovementAnim";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wphm");
    auto c = wowee::pipeline::WoweePlayerMovementAnimLoader::
        makeHumanMovement(name);
    if (!saveOrError<wowee::pipeline::WoweePlayerMovementAnimLoader>(c, base, "gen-phm-human", ".wphm")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenOrc(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "OrcMovementAnim";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wphm");
    auto c = wowee::pipeline::WoweePlayerMovementAnimLoader::
        makeOrcMovement(name);
    if (!saveOrError<wowee::pipeline::WoweePlayerMovementAnimLoader>(c, base, "gen-phm-orc", ".wphm")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUndead(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UndeadMovementAnim";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wphm");
    auto c = wowee::pipeline::WoweePlayerMovementAnimLoader::
        makeUndeadMovement(name);
    if (!saveOrError<wowee::pipeline::WoweePlayerMovementAnimLoader>(c, base, "gen-phm-undead", ".wphm")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wphm");
    if (!wowee::pipeline::WoweePlayerMovementAnimLoader::exists(base)) {
        std::fprintf(stderr, "WPHM not found: %s.wphm\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePlayerMovementAnimLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wphm"] = base + ".wphm";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"mapId", e.mapId},
                {"raceId", e.raceId},
                {"raceName", raceIdName(e.raceId)},
                {"genderId", e.genderId},
                {"genderName", genderName(e.genderId)},
                {"movementState", e.movementState},
                {"movementStateName",
                    movementStateName(e.movementState)},
                {"baseAnimId", e.baseAnimId},
                {"variantAnimId", e.variantAnimId},
                {"transitionMs", e.transitionMs},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPHM: %s.wphm\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  bindings: %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  race      g  state    baseAnim  variant  blendMs\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-8s  %s  %-7s  %8u  %7u  %7u\n",
                    e.mapId,
                    raceIdName(e.raceId),
                    genderName(e.genderId),
                    movementStateName(e.movementState),
                    e.baseAnimId, e.variantAnimId,
                    e.transitionMs);
    }
    return 0;
}

int parseMovementStateToken(const std::string& s) {
    using P = wowee::pipeline::WoweePlayerMovementAnim;
    if (s == "idle")  return P::StateIdle;
    if (s == "walk")  return P::StateWalk;
    if (s == "run")   return P::StateRun;
    if (s == "swim")  return P::StateSwim;
    if (s == "fly")   return P::StateFly;
    if (s == "sit")   return P::StateSit;
    if (s == "mount") return P::StateMount;
    if (s == "death") return P::StateDeath;
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
                    "import-wphm-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wphm");
    if (!wowee::pipeline::WoweePlayerMovementAnimLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wphm: WPHM not found: %s.wphm\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePlayerMovementAnimLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    using KeyTriple = std::tuple<uint8_t, uint8_t, uint8_t>;
    std::set<KeyTriple> tripleSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.mapId) +
                          " " + raceIdName(e.raceId) +
                          " " + genderName(e.genderId) +
                          " " + movementStateName(e.movementState) +
                          ")";
        if (e.mapId == 0)
            errors.push_back(ctx + ": mapId is 0");
        if (e.raceId == 0 || e.raceId > 10) {
            errors.push_back(ctx + ": raceId " +
                std::to_string(e.raceId) +
                " out of vanilla range (1..10)");
        }
        if (e.genderId > 1) {
            errors.push_back(ctx + ": genderId " +
                std::to_string(e.genderId) +
                " out of range (must be 0=male or 1=female)");
        }
        if (e.movementState > 7) {
            errors.push_back(ctx + ": movementState " +
                std::to_string(e.movementState) +
                " out of range (0..7)");
        }
        // baseAnimId 0 IS valid (Stand is anim id 0
        // in the M2 table) BUT only for the Idle
        // state. For any other state, base 0 means
        // "no animation bound" which would freeze the
        // model in the previous frame.
        if (e.baseAnimId == 0 &&
            e.movementState != wowee::pipeline::
                WoweePlayerMovementAnim::StateIdle) {
            errors.push_back(ctx +
                ": baseAnimId 0 on non-Idle state — model "
                "would freeze when entering this state");
        }
        // (race, gender, state) MUST be unique — the
        // renderer dispatches by this triple. Two
        // bindings would non-deterministically tie.
        KeyTriple key{e.raceId, e.genderId, e.movementState};
        if (!tripleSeen.insert(key).second) {
            errors.push_back(ctx +
                ": duplicate (raceId, genderId, "
                "movementState) triple — renderer "
                "dispatch ambiguous");
        }
        if (!idsSeen.insert(e.mapId).second) {
            errors.push_back(ctx + ": duplicate mapId");
        }
        // Self-variant: variantAnimId pointing to the
        // same id as baseAnimId is meaningless overhead
        // — it's still a valid setup but the variant
        // would be a no-op.
        if (e.variantAnimId != 0 &&
            e.variantAnimId == e.baseAnimId) {
            warnings.push_back(ctx +
                ": variantAnimId equals baseAnimId — the "
                "variant would visually equal the base "
                "(no-op overhead)");
        }
        // Excessive transition: more than 2s of blend
        // would feel like an animation hang to the
        // player.
        if (e.transitionMs > 2000) {
            warnings.push_back(ctx +
                ": transitionMs=" +
                std::to_string(e.transitionMs) +
                " exceeds 2000ms — would feel like an "
                "animation hang");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wphm", base, errors, warnings);
    std::printf("validate-wphm: %s.wphm\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu bindings, all mapIds unique, "
                    "(race,gender,state) triple unique, "
                    "raceId 1..10, genderId 0..1, state "
                    "0..7, no non-Idle baseAnim==0\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wphm");
    if (out.empty()) out = base + ".wphm.json";
    if (!wowee::pipeline::WoweePlayerMovementAnimLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wphm-json: WPHM not found: %s.wphm\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePlayerMovementAnimLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WPHM";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"mapId", e.mapId},
            {"raceId", e.raceId},
            {"raceName", raceIdName(e.raceId)},
            {"genderId", e.genderId},
            {"genderName", genderName(e.genderId)},
            {"movementState", e.movementState},
            {"movementStateName",
                movementStateName(e.movementState)},
            {"baseAnimId", e.baseAnimId},
            {"variantAnimId", e.variantAnimId},
            {"transitionMs", e.transitionMs},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wphm-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu bindings)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wphm");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wphm-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wphm-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweePlayerMovementAnim c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wphm-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweePlayerMovementAnim::Entry e;
        e.mapId = je.value("mapId", 0u);
        e.raceId = static_cast<uint8_t>(je.value("raceId", 0));
        e.genderId = static_cast<uint8_t>(je.value("genderId", 0));
        if (!readEnumField(je, "movementState",
                            "movementStateName",
                            parseMovementStateToken,
                            "movementState",
                            e.mapId, e.movementState)) return 1;
        e.baseAnimId = je.value("baseAnimId", 0u);
        e.variantAnimId = je.value("variantAnimId", 0u);
        e.transitionMs = static_cast<uint16_t>(
            je.value("transitionMs", 0));
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweePlayerMovementAnimLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wphm-json: failed to save %s.wphm\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wphm (%zu bindings)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handlePlayerMovementAnimCatalog(int& i, int argc, char** argv,
                                       int& outRc) {
    if (std::strcmp(argv[i], "--gen-phm-human") == 0 &&
        i + 1 < argc) {
        outRc = handleGenHuman(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-phm-orc") == 0 &&
        i + 1 < argc) {
        outRc = handleGenOrc(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-phm-undead") == 0 &&
        i + 1 < argc) {
        outRc = handleGenUndead(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wphm") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wphm") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wphm-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wphm-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
