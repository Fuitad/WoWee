#include "cli_macros_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_macros.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeMacro& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmac\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  macros  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterMacros";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmac");
    auto c = wowee::pipeline::WoweeMacroLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeMacroLoader>(c, base, "gen-mac", ".wmac")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCombat(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CombatMacros";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmac");
    auto c = wowee::pipeline::WoweeMacroLoader::makeCombat(name);
    if (!saveOrError<wowee::pipeline::WoweeMacroLoader>(c, base, "gen-mac-combat", ".wmac")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUtility(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UtilityMacros";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmac");
    auto c = wowee::pipeline::WoweeMacroLoader::makeUtility(name);
    if (!saveOrError<wowee::pipeline::WoweeMacroLoader>(c, base, "gen-mac-utility", ".wmac")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmac");
    if (!wowee::pipeline::WoweeMacroLoader::exists(base)) {
        return reportMissing("WMAC", base, ".wmac");
    }
    auto c = wowee::pipeline::WoweeMacroLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmac"] = base + ".wmac";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"macroId", e.macroId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"macroBody", e.macroBody},
                {"bindKey", e.bindKey},
                {"macroKind", e.macroKind},
                {"macroKindName", wowee::pipeline::WoweeMacro::macroKindName(e.macroKind)},
                {"requiredClassMask", e.requiredClassMask},
                {"maxLength", e.maxLength},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMAC: %s.wmac\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  macros  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind             classMask    bindKey   maxLen  bodyLen  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-15s  0x%08x   %-8s   %5u   %6zu  %s\n",
                    e.macroId,
                    wowee::pipeline::WoweeMacro::macroKindName(e.macroKind),
                    e.requiredClassMask,
                    e.bindKey.empty() ? "-" : e.bindKey.c_str(),
                    e.maxLength, e.macroBody.size(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each macro emits all 8 scalar fields
    // plus a dual int + name form for macroKind. The
    // macroBody string is dumped verbatim — multi-line
    // bodies preserve '\n' as JSON-escape sequences which
    // most editors render readably.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wmac");
    if (outPath.empty()) outPath = base + ".wmac.json";
    if (!wowee::pipeline::WoweeMacroLoader::exists(base)) {
        return reportMissing("export-wmac-json", "WMAC", base, ".wmac");
    }
    auto c = wowee::pipeline::WoweeMacroLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"macroId", e.macroId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"macroBody", e.macroBody},
            {"bindKey", e.bindKey},
            {"macroKind", e.macroKind},
            {"macroKindName", wowee::pipeline::WoweeMacro::macroKindName(e.macroKind)},
            {"requiredClassMask", e.requiredClassMask},
            {"maxLength", e.maxLength},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wmac-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wmac\n", base.c_str());
    std::printf("  macros : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wmac");
    outBase = cli::withoutExt(outBase, ".wmac");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wmac-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wmac-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "system-slash")    return wowee::pipeline::WoweeMacro::SystemSlash;
        if (s == "default-macro")   return wowee::pipeline::WoweeMacro::DefaultMacro;
        if (s == "player-template") return wowee::pipeline::WoweeMacro::PlayerTemplate;
        if (s == "guild-macro")     return wowee::pipeline::WoweeMacro::GuildMacro;
        if (s == "shared-macro")    return wowee::pipeline::WoweeMacro::SharedMacro;
        return wowee::pipeline::WoweeMacro::SystemSlash;
    };
    wowee::pipeline::WoweeMacro c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeMacro::Entry e;
            e.macroId = je.value("macroId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            e.macroBody = je.value("macroBody", std::string{});
            e.bindKey = je.value("bindKey", std::string{});
            if (je.contains("macroKind") &&
                je["macroKind"].is_number_integer()) {
                e.macroKind = static_cast<uint8_t>(
                    je["macroKind"].get<int>());
            } else if (je.contains("macroKindName") &&
                       je["macroKindName"].is_string()) {
                e.macroKind = kindFromName(
                    je["macroKindName"].get<std::string>());
            }
            e.requiredClassMask = je.value("requiredClassMask", 0u);
            // maxLength defaults to 255 (the WoW canonical UI
            // cap) when omitted.
            e.maxLength = static_cast<uint16_t>(
                je.value("maxLength", 255));
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeMacroLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmac-json: failed to save %s.wmac\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmac\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  macros : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeMacroLoader>(
        i, argc, argv, "wmac", "WMAC",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.macroId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.macroId == 0)
                errors.push_back(ctx + ": macroId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.macroBody.empty())
                errors.push_back(ctx +
                    ": macroBody is empty (macro does nothing)");
            if (e.macroKind > wowee::pipeline::WoweeMacro::SharedMacro) {
                errors.push_back(ctx + ": macroKind " +
                    std::to_string(e.macroKind) + " not in 0..4");
            }
            if (e.maxLength != 0 && e.macroBody.size() > e.maxLength) {
                errors.push_back(ctx +
                    ": macroBody length " +
                    std::to_string(e.macroBody.size()) +
                    " exceeds maxLength " +
                    std::to_string(e.maxLength));
            }
            // Macro body must start with `/` (slash command) — any
            // line that doesn't is a stray comment / orphan text.
            if (!e.macroBody.empty() && e.macroBody[0] != '/' &&
                e.macroBody[0] != '#') {
                warnings.push_back(ctx +
                    ": macroBody doesn't start with '/' or '#' "
                    "(likely missing slash on first line)");
            }
            // SystemSlash macros are run by the engine, not by the
            // player — assigning a class restriction makes no sense
            // since the slash command is shared by all classes.
            if (e.macroKind == wowee::pipeline::WoweeMacro::SystemSlash &&
                e.requiredClassMask != 0) {
                warnings.push_back(ctx +
                    ": SystemSlash kind with requiredClassMask != 0 "
                    "(slash commands are class-agnostic)");
            }
            if (!idsSeen.add(e.macroId)) errors.push_back(ctx + ": duplicate macroId");
        }
            return formatted("%zu macros, all macroIds unique, "
                    "all bodies within length cap", c.entries.size());
        });
}

} // namespace

bool handleMacrosCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-mac") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mac-combat") == 0 && i + 1 < argc) {
        outRc = handleGenCombat(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mac-utility") == 0 && i + 1 < argc) {
        outRc = handleGenUtility(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmac") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmac") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmac-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmac-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
