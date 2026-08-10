#include "cli_animations_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_animations.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeAnimation& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeAnimationLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wani\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeAnimation& c,
                     const std::string& base) {
    std::printf("Wrote %s.wani\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  animations : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterAnimations";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wani");
    auto c = wowee::pipeline::WoweeAnimationLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-animations")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCombat(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CombatAnimations";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wani");
    auto c = wowee::pipeline::WoweeAnimationLoader::makeCombat(name);
    if (!saveOrError(c, base, "gen-animations-combat")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMovement(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MovementAnimations";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wani");
    auto c = wowee::pipeline::WoweeAnimationLoader::makeMovement(name);
    if (!saveOrError(c, base, "gen-animations-movement")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wani");
    if (!wowee::pipeline::WoweeAnimationLoader::exists(base)) {
        std::fprintf(stderr, "WANI not found: %s.wani\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAnimationLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wani"] = base + ".wani";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"animationId", e.animationId},
                {"name", e.name},
                {"description", e.description},
                {"fallbackId", e.fallbackId},
                {"behaviorId", e.behaviorId},
                {"behaviorTier", e.behaviorTier},
                {"behaviorTierName", wowee::pipeline::WoweeAnimation::behaviorTierName(e.behaviorTier)},
                {"flags", e.flags},
                {"weaponFlags", e.weaponFlags},
                {"loopDurationMs", e.loopDurationMs},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WANI: %s.wani\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  animations : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    tier        flags       weapons     dur(ms)  fallback  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %-9s   0x%08x  0x%08x   %5u     %4u    %s\n",
                    e.animationId,
                    wowee::pipeline::WoweeAnimation::behaviorTierName(e.behaviorTier),
                    e.flags, e.weaponFlags, e.loopDurationMs,
                    e.fallbackId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each animation emits all 8 scalar fields
    // plus a dual int + name form for behaviorTier so
    // hand-edits can use either representation.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wani");
    if (outPath.empty()) outPath = base + ".wani.json";
    if (!wowee::pipeline::WoweeAnimationLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wani-json: WANI not found: %s.wani\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAnimationLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"animationId", e.animationId},
            {"name", e.name},
            {"description", e.description},
            {"fallbackId", e.fallbackId},
            {"behaviorId", e.behaviorId},
            {"behaviorTier", e.behaviorTier},
            {"behaviorTierName", wowee::pipeline::WoweeAnimation::behaviorTierName(e.behaviorTier)},
            {"flags", e.flags},
            {"weaponFlags", e.weaponFlags},
            {"loopDurationMs", e.loopDurationMs},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wani-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source     : %s.wani\n", base.c_str());
    std::printf("  animations : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wani.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wani");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wani-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wani-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto tierFromName = [](const std::string& s) -> uint8_t {
        if (s == "default")  return wowee::pipeline::WoweeAnimation::Default;
        if (s == "mounted")  return wowee::pipeline::WoweeAnimation::Mounted;
        if (s == "sitting")  return wowee::pipeline::WoweeAnimation::Sitting;
        if (s == "aerial")   return wowee::pipeline::WoweeAnimation::Aerial;
        if (s == "swimming") return wowee::pipeline::WoweeAnimation::Swimming;
        return wowee::pipeline::WoweeAnimation::Default;
    };
    wowee::pipeline::WoweeAnimation c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeAnimation::Entry e;
            e.animationId = je.value("animationId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.fallbackId = je.value("fallbackId", 0u);
            e.behaviorId = je.value("behaviorId", 0u);
            if (je.contains("behaviorTier") &&
                je["behaviorTier"].is_number_integer()) {
                e.behaviorTier = static_cast<uint8_t>(
                    je["behaviorTier"].get<int>());
            } else if (je.contains("behaviorTierName") &&
                       je["behaviorTierName"].is_string()) {
                e.behaviorTier = tierFromName(
                    je["behaviorTierName"].get<std::string>());
            }
            e.flags = je.value("flags", 0u);
            e.weaponFlags = je.value("weaponFlags",
                wowee::pipeline::WoweeAnimation::kWeaponAny);
            e.loopDurationMs = je.value("loopDurationMs", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeAnimationLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wani-json: failed to save %s.wani\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wani\n", outBase.c_str());
    std::printf("  source     : %s\n", jsonPath.c_str());
    std::printf("  animations : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wani");
    if (!wowee::pipeline::WoweeAnimationLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wani: WANI not found: %s.wani\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAnimationLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    // First pass — collect all animationIds for fallback
    // resolution.
    std::vector<uint32_t> allIds;
    for (const auto& e : c.entries) allIds.push_back(e.animationId);
    auto idExists = [&](uint32_t id) {
        for (uint32_t a : allIds) if (a == id) return true;
        return false;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.animationId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.behaviorTier > wowee::pipeline::WoweeAnimation::Swimming) {
            errors.push_back(ctx + ": behaviorTier " +
                std::to_string(e.behaviorTier) + " not in 0..4");
        }
        if (e.weaponFlags == 0) {
            warnings.push_back(ctx +
                ": weaponFlags=0 (animation never selected for any weapon)");
        }
        // fallbackId == animationId would create an infinite
        // loop. fallback to id 0 (Stand) is conventional and
        // always valid even if Stand isn't in this catalog.
        if (e.fallbackId == e.animationId &&
            e.animationId != 0) {
            errors.push_back(ctx +
                ": fallbackId equals animationId (infinite loop)");
        }
        if (e.fallbackId != 0 && !idExists(e.fallbackId)) {
            warnings.push_back(ctx + ": fallbackId=" +
                std::to_string(e.fallbackId) +
                " not found in this catalog (resolved at runtime)");
        }
        // Looped animations must have a non-zero duration —
        // otherwise the renderer divides by zero stepping the
        // animation cursor.
        if ((e.flags & wowee::pipeline::WoweeAnimation::kFlagLooped) &&
            e.loopDurationMs == 0) {
            errors.push_back(ctx +
                ": kFlagLooped set but loopDurationMs=0 "
                "(animation cursor would divide by zero)");
        }
        // Mutually exclusive — Looped + OneShot is contradictory.
        if ((e.flags & wowee::pipeline::WoweeAnimation::kFlagLooped) &&
            (e.flags & wowee::pipeline::WoweeAnimation::kFlagOneShot)) {
            errors.push_back(ctx +
                ": both kFlagLooped and kFlagOneShot set "
                "(mutually exclusive)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.animationId) {
                errors.push_back(ctx + ": duplicate animationId");
                break;
            }
        }
        idsSeen.push_back(e.animationId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wani", base, errors, warnings);
    std::printf("validate-wani: %s.wani\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu animations, all animationIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleAnimationsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-animations") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-animations-combat") == 0 && i + 1 < argc) {
        outRc = handleGenCombat(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-animations-movement") == 0 && i + 1 < argc) {
        outRc = handleGenMovement(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wani") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wani") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wani-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wani-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
