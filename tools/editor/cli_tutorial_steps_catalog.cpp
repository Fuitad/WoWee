#include "cli_tutorial_steps_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_tutorial_steps.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* triggerEventName(uint8_t e) {
    using T = wowee::pipeline::WoweeTutorialSteps;
    switch (e) {
        case T::Login:      return "login";
        case T::ZoneEnter:  return "zoneenter";
        case T::LevelUp:    return "levelup";
        case T::ItemPickup: return "itempickup";
        case T::SkillTrain: return "skilltrain";
        default:            return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeTutorialSteps& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtur\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  steps   : %zu\n", c.entries.size());
}

int handleGenNewbie(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "NewbieTutorialFlow";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtur");
    auto c = wowee::pipeline::WoweeTutorialStepsLoader::
        makeNewbieFlow(name);
    if (!saveOrError<wowee::pipeline::WoweeTutorialStepsLoader>(c, base, "gen-tut-newbie", ".wtur")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenLevelUp(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "LevelUpTutorialFlow";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtur");
    auto c = wowee::pipeline::WoweeTutorialStepsLoader::
        makeLevelUpFlow(name);
    if (!saveOrError<wowee::pipeline::WoweeTutorialStepsLoader>(c, base, "gen-tut-levelup", ".wtur")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBg(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BattlegroundTutorialFlow";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtur");
    auto c = wowee::pipeline::WoweeTutorialStepsLoader::
        makeBgFlow(name);
    if (!saveOrError<wowee::pipeline::WoweeTutorialStepsLoader>(c, base, "gen-tut-bg", ".wtur")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtur");
    if (!wowee::pipeline::WoweeTutorialStepsLoader::exists(base)) {
        std::fprintf(stderr, "WTUR not found: %s.wtur\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTutorialStepsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtur"] = base + ".wtur";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"tutId", e.tutId},
                {"name", e.name},
                {"stepIndex", e.stepIndex},
                {"triggerEvent", e.triggerEvent},
                {"triggerEventName",
                    triggerEventName(e.triggerEvent)},
                {"triggerValue", e.triggerValue},
                {"iconIndex", e.iconIndex},
                {"hideAfterSec", e.hideAfterSec},
                {"title", e.title},
                {"body", e.body},
                {"targetUIElementName",
                    e.targetUIElementName},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTUR: %s.wtur\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  steps   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  step  event           value  hideS  bodyLen  title\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %3u  %-12s   %5u   %4u   %5zu  %s\n",
                    e.tutId, e.stepIndex,
                    triggerEventName(e.triggerEvent),
                    e.triggerValue, e.hideAfterSec,
                    e.body.size(), e.title.c_str());
    }
    return 0;
}

int parseTriggerEventToken(const std::string& s) {
    using T = wowee::pipeline::WoweeTutorialSteps;
    if (s == "login")      return T::Login;
    if (s == "zoneenter")  return T::ZoneEnter;
    if (s == "levelup")    return T::LevelUp;
    if (s == "itempickup") return T::ItemPickup;
    if (s == "skilltrain") return T::SkillTrain;
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
                    "import-wtur-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wtur");
    if (!wowee::pipeline::WoweeTutorialStepsLoader::exists(base)) {
        return reportMissing("validate-wtur", "WTUR", base, ".wtur");
    }
    auto c = wowee::pipeline::WoweeTutorialStepsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    using T = wowee::pipeline::WoweeTutorialSteps;
    std::set<uint32_t> idsSeen;
    using Triple = std::tuple<uint8_t, uint32_t, uint8_t>;
    std::set<Triple> tripleSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (tutId=" + std::to_string(e.tutId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.tutId == 0)
            errors.push_back(ctx + ": tutId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.title.empty())
            errors.push_back(ctx +
                ": title is empty - popup would be "
                "headerless");
        if (e.body.empty())
            errors.push_back(ctx +
                ": body is empty - popup would have "
                "no instructional content");
        if (e.triggerEvent > 4) {
            errors.push_back(ctx + ": triggerEvent " +
                std::to_string(e.triggerEvent) +
                " out of range (0..4)");
        }
        if (e.stepIndex == 0) {
            errors.push_back(ctx +
                ": stepIndex is 0 - sequence ordering "
                "starts at 1");
        }
        // (triggerEvent, triggerValue, stepIndex)
        // MUST be unique. Two steps at same position
        // in same trigger group = ambiguous order.
        Triple t{e.triggerEvent, e.triggerValue,
                  e.stepIndex};
        if (!tripleSeen.insert(t).second) {
            errors.push_back(ctx +
                ": duplicate (triggerEvent=" +
                std::string(triggerEventName(e.triggerEvent))
                + ", triggerValue=" +
                std::to_string(e.triggerValue) +
                ", stepIndex=" +
                std::to_string(e.stepIndex) +
                ") - sequence ordering ambiguous");
        }
        // Login event with non-zero triggerValue is
        // dead data (Login fires once, no value
        // discrimination).
        if (e.triggerEvent == T::Login &&
            e.triggerValue != 0) {
            warnings.push_back(ctx +
                ": Login triggerEvent with non-zero "
                "triggerValue=" +
                std::to_string(e.triggerValue) +
                " - value is ignored at runtime "
                "(Login is unconditional)");
        }
        // Non-Login events without triggerValue would
        // fire on every event of that kind without
        // discrimination - usually unintended.
        if (e.triggerEvent != T::Login &&
            e.triggerValue == 0) {
            warnings.push_back(ctx +
                ": triggerEvent=" +
                std::string(triggerEventName(e.triggerEvent))
                + " with triggerValue=0 - would fire "
                "for ALL events of this kind (any "
                "zone / any level / any item / any "
                "skill)");
        }
        // hideAfterSec < 5 means the popup vanishes
        // before the player can read it. > 0 (=
        // auto-dismiss enabled) AND < 5 = error.
        // hideAfterSec=0 is fine (no auto-dismiss,
        // user clicks to dismiss).
        if (e.hideAfterSec > 0 && e.hideAfterSec < 5) {
            errors.push_back(ctx +
                ": hideAfterSec=" +
                std::to_string(e.hideAfterSec) +
                " is below 5s - popup vanishes before "
                "the player can read it");
        }
        // Body length sanity: under 10 chars usually
        // means an empty / placeholder template.
        if (e.body.size() > 0 && e.body.size() < 10) {
            warnings.push_back(ctx +
                ": body length " +
                std::to_string(e.body.size()) +
                " is under 10 chars - likely "
                "placeholder text");
        }
        if (!idsSeen.insert(e.tutId).second) {
            errors.push_back(ctx + ": duplicate tutId");
        }
    }
    return cli::reportValidation("wtur", base, jsonOut, errors, warnings,
                                 formatted("%zu steps, all tutIds unique, "
                    "title+body non-empty, triggerEvent "
                    "0..4, no duplicate (event,value,step) "
                    "triples, hideAfterSec >= 5 when set", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wtur");
    if (out.empty()) out = base + ".wtur.json";
    if (!wowee::pipeline::WoweeTutorialStepsLoader::exists(base)) {
        return reportMissing("export-wtur-json", "WTUR", base, ".wtur");
    }
    auto c = wowee::pipeline::WoweeTutorialStepsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WTUR";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"tutId", e.tutId},
            {"name", e.name},
            {"stepIndex", e.stepIndex},
            {"triggerEvent", e.triggerEvent},
            {"triggerEventName",
                triggerEventName(e.triggerEvent)},
            {"triggerValue", e.triggerValue},
            {"iconIndex", e.iconIndex},
            {"hideAfterSec", e.hideAfterSec},
            {"title", e.title},
            {"body", e.body},
            {"targetUIElementName",
                e.targetUIElementName},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wtur-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu steps)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wtur");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wtur-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wtur-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeTutorialSteps c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wtur-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeTutorialSteps::Entry e;
        e.tutId = je.value("tutId", 0u);
        e.name = je.value("name", std::string{});
        e.stepIndex = static_cast<uint8_t>(
            je.value("stepIndex", 0));
        if (!readEnumField(je, "triggerEvent",
                            "triggerEventName",
                            parseTriggerEventToken,
                            "triggerEvent", e.tutId,
                            e.triggerEvent)) return 1;
        e.triggerValue = je.value("triggerValue", 0u);
        e.iconIndex = je.value("iconIndex", 0u);
        e.hideAfterSec = je.value("hideAfterSec", 0u);
        e.title = je.value("title", std::string{});
        e.body = je.value("body", std::string{});
        e.targetUIElementName =
            je.value("targetUIElementName", std::string{});
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeTutorialStepsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtur-json: failed to save %s.wtur\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtur (%zu steps)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleTutorialStepsCatalog(int& i, int argc, char** argv,
                                  int& outRc) {
    if (std::strcmp(argv[i], "--gen-tut-newbie") == 0 &&
        i + 1 < argc) {
        outRc = handleGenNewbie(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tut-levelup") == 0 &&
        i + 1 < argc) {
        outRc = handleGenLevelUp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tut-bg") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBg(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtur") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtur") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtur-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtur-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
