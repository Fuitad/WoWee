#include "cli_spell_visuals_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_visuals.hpp"
#include <nlohmann/json.hpp>

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

bool saveOrError(const wowee::pipeline::WoweeSpellVisualKit& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpellVisualKitLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wsvk\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpellVisualKit& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsvk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  visuals : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterVisualKits";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsvk");
    auto c = wowee::pipeline::WoweeSpellVisualKitLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-svk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCombat(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CombatVisualKits";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsvk");
    auto c = wowee::pipeline::WoweeSpellVisualKitLoader::makeCombat(name);
    if (!saveOrError(c, base, "gen-svk-combat")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUtility(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UtilityVisualKits";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsvk");
    auto c = wowee::pipeline::WoweeSpellVisualKitLoader::makeUtility(name);
    if (!saveOrError(c, base, "gen-svk-utility")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsvk");
    if (!wowee::pipeline::WoweeSpellVisualKitLoader::exists(base)) {
        std::fprintf(stderr, "WSVK not found: %s.wsvk\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellVisualKitLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsvk"] = base + ".wsvk";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"visualKitId", e.visualKitId},
                {"name", e.name},
                {"description", e.description},
                {"castEffectModelPath", e.castEffectModelPath},
                {"projectileModelPath", e.projectileModelPath},
                {"impactEffectModelPath", e.impactEffectModelPath},
                {"handEffectModelPath", e.handEffectModelPath},
                {"precastAnimId", e.precastAnimId},
                {"castAnimId", e.castAnimId},
                {"impactAnimId", e.impactAnimId},
                {"castSoundId", e.castSoundId},
                {"impactSoundId", e.impactSoundId},
                {"projectileSpeed", e.projectileSpeed},
                {"projectileGravity", e.projectileGravity},
                {"castDurationMs", e.castDurationMs},
                {"impactRadius", e.impactRadius},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSVK: %s.wsvk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  visuals : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    castAnim  impAnim   speed   grav   dur(ms)   AoE   castSnd  impSnd  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %5u    %5u    %5.1f  %4.2f   %5u   %4.1f   %5u    %5u   %s\n",
                    e.visualKitId, e.castAnimId, e.impactAnimId,
                    e.projectileSpeed, e.projectileGravity,
                    e.castDurationMs, e.impactRadius,
                    e.castSoundId, e.impactSoundId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each visual kit emits all 12 scalar fields
    // and 4 model-path strings — there are no enums to widen
    // with name forms, so the mapping is straightforward.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wsvk");
    if (outPath.empty()) outPath = base + ".wsvk.json";
    if (!wowee::pipeline::WoweeSpellVisualKitLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsvk-json: WSVK not found: %s.wsvk\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellVisualKitLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"visualKitId", e.visualKitId},
            {"name", e.name},
            {"description", e.description},
            {"castEffectModelPath", e.castEffectModelPath},
            {"projectileModelPath", e.projectileModelPath},
            {"impactEffectModelPath", e.impactEffectModelPath},
            {"handEffectModelPath", e.handEffectModelPath},
            {"precastAnimId", e.precastAnimId},
            {"castAnimId", e.castAnimId},
            {"impactAnimId", e.impactAnimId},
            {"castSoundId", e.castSoundId},
            {"impactSoundId", e.impactSoundId},
            {"projectileSpeed", e.projectileSpeed},
            {"projectileGravity", e.projectileGravity},
            {"castDurationMs", e.castDurationMs},
            {"impactRadius", e.impactRadius},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wsvk-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wsvk\n", base.c_str());
    std::printf("  visuals : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wsvk.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wsvk");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wsvk-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wsvk-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellVisualKit c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellVisualKit::Entry e;
            e.visualKitId = je.value("visualKitId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.castEffectModelPath = je.value("castEffectModelPath", std::string{});
            e.projectileModelPath = je.value("projectileModelPath", std::string{});
            e.impactEffectModelPath = je.value("impactEffectModelPath", std::string{});
            e.handEffectModelPath = je.value("handEffectModelPath", std::string{});
            e.precastAnimId = je.value("precastAnimId", 0u);
            e.castAnimId = je.value("castAnimId", 0u);
            e.impactAnimId = je.value("impactAnimId", 0u);
            e.castSoundId = je.value("castSoundId", 0u);
            e.impactSoundId = je.value("impactSoundId", 0u);
            e.projectileSpeed = je.value("projectileSpeed", 0.0f);
            e.projectileGravity = je.value("projectileGravity", 0.0f);
            e.castDurationMs = je.value("castDurationMs", 0u);
            e.impactRadius = je.value("impactRadius", 0.0f);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeSpellVisualKitLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsvk-json: failed to save %s.wsvk\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsvk\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  visuals : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsvk");
    if (!wowee::pipeline::WoweeSpellVisualKitLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsvk: WSVK not found: %s.wsvk\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellVisualKitLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.visualKitId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.visualKitId == 0)
            errors.push_back(ctx + ": visualKitId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.projectileSpeed < 0.0f)
            errors.push_back(ctx + ": projectileSpeed " +
                std::to_string(e.projectileSpeed) +
                " negative (use 0 for instant)");
        if (e.projectileGravity < 0.0f)
            errors.push_back(ctx + ": projectileGravity " +
                std::to_string(e.projectileGravity) +
                " negative (use 0 for straight line)");
        if (e.impactRadius < 0.0f)
            errors.push_back(ctx + ": impactRadius " +
                std::to_string(e.impactRadius) +
                " negative (use 0 for single-target)");
        // Projectile model + zero speed = projectile defined
        // but never travels. Inverse: speed > 0 + no model =
        // invisible projectile. Both are usually mistakes.
        if (!e.projectileModelPath.empty() &&
            e.projectileSpeed == 0.0f) {
            warnings.push_back(ctx +
                ": projectileModelPath set but projectileSpeed=0 "
                "(model never travels)");
        }
        if (e.projectileModelPath.empty() &&
            e.projectileSpeed > 0.0f) {
            warnings.push_back(ctx +
                ": projectileSpeed > 0 but no projectileModelPath "
                "(invisible projectile)");
        }
        // No effect model AND no animation AND no sound = the
        // visual kit has no observable effect at all.
        if (e.castEffectModelPath.empty() &&
            e.impactEffectModelPath.empty() &&
            e.handEffectModelPath.empty() &&
            e.castAnimId == 0 && e.impactAnimId == 0 &&
            e.castSoundId == 0 && e.impactSoundId == 0) {
            warnings.push_back(ctx +
                ": no models, animations, or sounds — visual kit has no observable effect");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.visualKitId) {
                errors.push_back(ctx + ": duplicate visualKitId");
                break;
            }
        }
        idsSeen.push_back(e.visualKitId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsvk", base, errors, warnings);
    std::printf("validate-wsvk: %s.wsvk\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu visual kits, all visualKitIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellVisualsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-svk") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-svk-combat") == 0 && i + 1 < argc) {
        outRc = handleGenCombat(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-svk-utility") == 0 && i + 1 < argc) {
        outRc = handleGenUtility(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsvk") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsvk") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsvk-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsvk-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
