#include "cli_cinematics_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_cinematics.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCinematic& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcms\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  cinematics : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCinematics";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcms");
    auto c = wowee::pipeline::WoweeCinematicLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCinematicLoader>(c, base, "gen-cinematics", ".wcms")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenIntros(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ClassIntros";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcms");
    auto c = wowee::pipeline::WoweeCinematicLoader::makeIntros(name);
    if (!saveOrError<wowee::pipeline::WoweeCinematicLoader>(c, base, "gen-cinematics-intros", ".wcms")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenQuests(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestCinematics";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcms");
    auto c = wowee::pipeline::WoweeCinematicLoader::makeQuestCinematics(name);
    if (!saveOrError<wowee::pipeline::WoweeCinematicLoader>(c, base, "gen-cinematics-quests", ".wcms")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcms");
    if (!wowee::pipeline::WoweeCinematicLoader::exists(base)) {
        return reportMissing("WCMS", base, ".wcms");
    }
    auto c = wowee::pipeline::WoweeCinematicLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcms"] = base + ".wcms";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"cinematicId", e.cinematicId},
                {"name", e.name},
                {"description", e.description},
                {"mediaPath", e.mediaPath},
                {"kind", e.kind},
                {"kindName", wowee::pipeline::WoweeCinematic::kindName(e.kind)},
                {"triggerKind", e.triggerKind},
                {"triggerKindName", wowee::pipeline::WoweeCinematic::triggerKindName(e.triggerKind)},
                {"triggerTargetId", e.triggerTargetId},
                {"durationSeconds", e.durationSeconds},
                {"skippable", e.skippable},
                {"soundtrackId", e.soundtrackId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCMS: %s.wcms\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  cinematics : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind        trigger          target  dur  skip  snd  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s  %-15s  %5u   %3us   %u    %3u  %s\n",
                    e.cinematicId,
                    wowee::pipeline::WoweeCinematic::kindName(e.kind),
                    wowee::pipeline::WoweeCinematic::triggerKindName(e.triggerKind),
                    e.triggerTargetId,
                    e.durationSeconds,
                    e.skippable,
                    e.soundtrackId,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each cinematic emits all 9 scalar fields
    // plus dual int + name forms for kind and triggerKind so
    // hand-edits can use either representation.
    return cli::exportCatalogJson<wowee::pipeline::WoweeCinematicLoader>(
        i, argc, argv, "wcms", "WCMS", "cinematics ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"cinematicId", e.cinematicId},
                {"name", e.name},
                {"description", e.description},
                {"mediaPath", e.mediaPath},
                {"kind", e.kind},
                {"kindName", wowee::pipeline::WoweeCinematic::kindName(e.kind)},
                {"triggerKind", e.triggerKind},
                {"triggerKindName", wowee::pipeline::WoweeCinematic::triggerKindName(e.triggerKind)},
                {"triggerTargetId", e.triggerTargetId},
                {"durationSeconds", e.durationSeconds},
                {"skippable", e.skippable},
                {"soundtrackId", e.soundtrackId},
            });
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wcms");
    outBase = cli::withoutExt(outBase, ".wcms");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wcms-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wcms-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "video")      return wowee::pipeline::WoweeCinematic::PreRenderedVideo;
        if (s == "camera")     return wowee::pipeline::WoweeCinematic::CameraFlythrough;
        if (s == "text-crawl") return wowee::pipeline::WoweeCinematic::TextCrawl;
        if (s == "image")      return wowee::pipeline::WoweeCinematic::StillImage;
        if (s == "slideshow")  return wowee::pipeline::WoweeCinematic::Slideshow;
        return wowee::pipeline::WoweeCinematic::PreRenderedVideo;
    };
    auto triggerFromName = [](const std::string& s) -> uint8_t {
        if (s == "manual")        return wowee::pipeline::WoweeCinematic::Manual;
        if (s == "quest-start")   return wowee::pipeline::WoweeCinematic::QuestStart;
        if (s == "quest-end")     return wowee::pipeline::WoweeCinematic::QuestEnd;
        if (s == "class-start")   return wowee::pipeline::WoweeCinematic::ClassStart;
        if (s == "zone-entry")    return wowee::pipeline::WoweeCinematic::ZoneEntry;
        if (s == "dungeon-clear") return wowee::pipeline::WoweeCinematic::DungeonClear;
        if (s == "login")         return wowee::pipeline::WoweeCinematic::Login;
        if (s == "achievement")   return wowee::pipeline::WoweeCinematic::AchievementGained;
        if (s == "level-up")      return wowee::pipeline::WoweeCinematic::LevelUp;
        return wowee::pipeline::WoweeCinematic::Manual;
    };
    wowee::pipeline::WoweeCinematic c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCinematic::Entry e;
            e.cinematicId = je.value("cinematicId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.mediaPath = je.value("mediaPath", std::string{});
            if (je.contains("kind") && je["kind"].is_number_integer()) {
                e.kind = static_cast<uint8_t>(je["kind"].get<int>());
            } else if (je.contains("kindName") &&
                       je["kindName"].is_string()) {
                e.kind = kindFromName(je["kindName"].get<std::string>());
            }
            if (je.contains("triggerKind") &&
                je["triggerKind"].is_number_integer()) {
                e.triggerKind = static_cast<uint8_t>(
                    je["triggerKind"].get<int>());
            } else if (je.contains("triggerKindName") &&
                       je["triggerKindName"].is_string()) {
                e.triggerKind = triggerFromName(
                    je["triggerKindName"].get<std::string>());
            }
            e.triggerTargetId = je.value("triggerTargetId", 0u);
            e.durationSeconds = je.value("durationSeconds", 0u);
            e.skippable = static_cast<uint8_t>(je.value("skippable", 1));
            e.soundtrackId = je.value("soundtrackId", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeCinematicLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcms-json: failed to save %s.wcms\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcms\n", outBase.c_str());
    std::printf("  source     : %s\n", jsonPath.c_str());
    std::printf("  cinematics : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeCinematicLoader>(
        i, argc, argv, "wcms", "WCMS",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.cinematicId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.cinematicId == 0)
                errors.push_back(ctx + ": cinematicId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.mediaPath.empty())
                errors.push_back(ctx + ": mediaPath is empty");
            if (e.kind > wowee::pipeline::WoweeCinematic::Slideshow) {
                errors.push_back(ctx + ": kind " +
                    std::to_string(e.kind) + " not in 0..4");
            }
            if (e.triggerKind > wowee::pipeline::WoweeCinematic::LevelUp) {
                errors.push_back(ctx + ": triggerKind " +
                    std::to_string(e.triggerKind) + " not in 0..8");
            }
            // Triggers other than Manual/Login require a non-zero
            // target id (questId, mapId, classId, achievementId etc).
            if (e.triggerKind != wowee::pipeline::WoweeCinematic::Manual &&
                e.triggerKind != wowee::pipeline::WoweeCinematic::Login &&
                e.triggerKind != wowee::pipeline::WoweeCinematic::LevelUp &&
                e.triggerTargetId == 0) {
                errors.push_back(ctx +
                    ": triggerKind " +
                    wowee::pipeline::WoweeCinematic::triggerKindName(e.triggerKind) +
                    " requires a non-zero triggerTargetId");
            }
            if (e.durationSeconds == 0) {
                warnings.push_back(ctx + ": durationSeconds=0 "
                    "(cinematic will be skipped instantly)");
            }
            if (e.kind == wowee::pipeline::WoweeCinematic::PreRenderedVideo &&
                e.skippable == 0) {
                warnings.push_back(ctx + ": pre-rendered video is "
                    "non-skippable (player can't escape)");
            }
            if (!idsSeen.add(e.cinematicId)) errors.push_back(ctx + ": duplicate cinematicId");
        }
            return formatted("%zu cinematics, all cinematicIds unique", c.entries.size());
        });
}

} // namespace

bool handleCinematicsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-cinematics") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cinematics-intros") == 0 && i + 1 < argc) {
        outRc = handleGenIntros(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cinematics-quests") == 0 && i + 1 < argc) {
        outRc = handleGenQuests(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcms") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcms") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcms-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcms-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
