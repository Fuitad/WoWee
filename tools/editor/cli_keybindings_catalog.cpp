#include "cli_keybindings_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_keybindings.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeKeyBinding& c,
                     const std::string& base) {
    std::printf("Wrote %s.wkbd\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterKeybindings";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wkbd");
    auto c = wowee::pipeline::WoweeKeyBindingLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeKeyBindingLoader>(c, base, "gen-kbd", ".wkbd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMovement(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MovementKeybindings";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wkbd");
    auto c = wowee::pipeline::WoweeKeyBindingLoader::makeMovement(name);
    if (!saveOrError<wowee::pipeline::WoweeKeyBindingLoader>(c, base, "gen-kbd-movement", ".wkbd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUI(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UIPanelKeybindings";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wkbd");
    auto c = wowee::pipeline::WoweeKeyBindingLoader::makeUIPanels(name);
    if (!saveOrError<wowee::pipeline::WoweeKeyBindingLoader>(c, base, "gen-kbd-ui", ".wkbd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wkbd");
    if (!wowee::pipeline::WoweeKeyBindingLoader::exists(base)) {
        std::fprintf(stderr, "WKBD not found: %s.wkbd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeKeyBindingLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wkbd"] = base + ".wkbd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"bindingId", e.bindingId},
                {"actionName", e.actionName},
                {"description", e.description},
                {"defaultKey", e.defaultKey},
                {"alternateKey", e.alternateKey},
                {"category", e.category},
                {"categoryName", wowee::pipeline::WoweeKeyBinding::categoryName(e.category)},
                {"isUserOverridable", e.isUserOverridable},
                {"sortOrder", e.sortOrder},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WKBD: %s.wkbd\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    category    user   sort   default       alt          actionName\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-9s    %u     %3u   %-12s  %-10s   %s\n",
                    e.bindingId,
                    wowee::pipeline::WoweeKeyBinding::categoryName(e.category),
                    e.isUserOverridable, e.sortOrder,
                    e.defaultKey.c_str(),
                    e.alternateKey.empty() ? "-" : e.alternateKey.c_str(),
                    e.actionName.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each binding emits all 7 scalar fields
    // plus a dual int + name form for category so hand-edits
    // can use either representation.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wkbd");
    if (outPath.empty()) outPath = base + ".wkbd.json";
    if (!wowee::pipeline::WoweeKeyBindingLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wkbd-json: WKBD not found: %s.wkbd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeKeyBindingLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"bindingId", e.bindingId},
            {"actionName", e.actionName},
            {"description", e.description},
            {"defaultKey", e.defaultKey},
            {"alternateKey", e.alternateKey},
            {"category", e.category},
            {"categoryName", wowee::pipeline::WoweeKeyBinding::categoryName(e.category)},
            {"isUserOverridable", e.isUserOverridable},
            {"sortOrder", e.sortOrder},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wkbd-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.wkbd\n", base.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wkbd");
    outBase = cli::withoutExt(outBase, ".wkbd");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wkbd-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wkbd-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto catFromName = [](const std::string& s) -> uint8_t {
        if (s == "movement")  return wowee::pipeline::WoweeKeyBinding::Movement;
        if (s == "combat")    return wowee::pipeline::WoweeKeyBinding::Combat;
        if (s == "targeting") return wowee::pipeline::WoweeKeyBinding::Targeting;
        if (s == "camera")    return wowee::pipeline::WoweeKeyBinding::Camera;
        if (s == "ui-panels") return wowee::pipeline::WoweeKeyBinding::UIPanels;
        if (s == "chat")      return wowee::pipeline::WoweeKeyBinding::Chat;
        if (s == "macro")     return wowee::pipeline::WoweeKeyBinding::Macro;
        if (s == "bar")       return wowee::pipeline::WoweeKeyBinding::Bar;
        if (s == "other")     return wowee::pipeline::WoweeKeyBinding::Other;
        return wowee::pipeline::WoweeKeyBinding::Movement;
    };
    wowee::pipeline::WoweeKeyBinding c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeKeyBinding::Entry e;
            e.bindingId = je.value("bindingId", 0u);
            e.actionName = je.value("actionName", std::string{});
            e.description = je.value("description", std::string{});
            e.defaultKey = je.value("defaultKey", std::string{});
            e.alternateKey = je.value("alternateKey", std::string{});
            if (je.contains("category") &&
                je["category"].is_number_integer()) {
                e.category = static_cast<uint8_t>(
                    je["category"].get<int>());
            } else if (je.contains("categoryName") &&
                       je["categoryName"].is_string()) {
                e.category = catFromName(
                    je["categoryName"].get<std::string>());
            }
            // isUserOverridable defaults to 1 (overridable)
            // when omitted — matches the typical case for
            // game-action bindings.
            e.isUserOverridable = static_cast<uint8_t>(
                je.value("isUserOverridable", 1));
            e.sortOrder = static_cast<uint8_t>(
                je.value("sortOrder", 0));
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeKeyBindingLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wkbd-json: failed to save %s.wkbd\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wkbd\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wkbd");
    if (!wowee::pipeline::WoweeKeyBindingLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wkbd: WKBD not found: %s.wkbd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeKeyBindingLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    std::set<std::string> actionsSeen;
    std::set<std::string> primaryKeysSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.bindingId);
        if (!e.actionName.empty()) ctx += " " + e.actionName;
        ctx += ")";
        if (e.bindingId == 0)
            errors.push_back(ctx + ": bindingId is 0");
        if (e.actionName.empty())
            errors.push_back(ctx + ": actionName is empty");
        if (e.defaultKey.empty())
            errors.push_back(ctx + ": defaultKey is empty");
        if (e.category > wowee::pipeline::WoweeKeyBinding::Other) {
            errors.push_back(ctx + ": category " +
                std::to_string(e.category) + " not in 0..8");
        }
        if (e.alternateKey == e.defaultKey && !e.defaultKey.empty()) {
            errors.push_back(ctx +
                ": alternateKey == defaultKey (no point in alt)");
        }
        // Action name should be SCREAMING_SNAKE — anything
        // with lowercase is suspect.
        for (char ch : e.actionName) {
            if (ch >= 'a' && ch <= 'z') {
                warnings.push_back(ctx +
                    ": actionName contains lowercase chars "
                    "(convention is SCREAMING_SNAKE)");
                break;
            }
        }
        // Duplicate primary keys would conflict at runtime —
        // last one binding loaded wins, leaving the first
        // silently shadowed.
        if (!e.defaultKey.empty()) {
            if (primaryKeysSeen.count(e.defaultKey)) {
                errors.push_back(ctx + ": defaultKey '" +
                    e.defaultKey +
                    "' already bound by an earlier entry "
                    "(would shadow that binding)");
            }
            primaryKeysSeen.insert(e.defaultKey);
        }
        if (!e.actionName.empty()) {
            if (actionsSeen.count(e.actionName)) {
                errors.push_back(ctx +
                    ": duplicate actionName '" + e.actionName + "'");
            }
            actionsSeen.insert(e.actionName);
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.bindingId) {
                errors.push_back(ctx + ": duplicate bindingId");
                break;
            }
        }
        idsSeen.push_back(e.bindingId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wkbd", base, errors, warnings);
    std::printf("validate-wkbd: %s.wkbd\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu bindings, all bindingIds unique, "
                    "no key conflicts\n", c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleKeybindingsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-kbd") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-kbd-movement") == 0 && i + 1 < argc) {
        outRc = handleGenMovement(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-kbd-ui") == 0 && i + 1 < argc) {
        outRc = handleGenUI(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wkbd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wkbd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wkbd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wkbd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
