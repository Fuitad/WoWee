#include "cli_addon_manifest_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_addon_manifest.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeAddonManifest& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmod\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  addons  : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardAddons";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmod");
    auto c = wowee::pipeline::WoweeAddonManifestLoader::
        makeStandardAddons(name);
    if (!saveOrError<wowee::pipeline::WoweeAddonManifestLoader>(c, base, "gen-mod", ".wmod")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUI(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UIReplacement";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmod");
    auto c = wowee::pipeline::WoweeAddonManifestLoader::
        makeUIReplacement(name);
    if (!saveOrError<wowee::pipeline::WoweeAddonManifestLoader>(c, base, "gen-mod-ui", ".wmod")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUtility(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UtilityAddons";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmod");
    auto c = wowee::pipeline::WoweeAddonManifestLoader::
        makeUtility(name);
    if (!saveOrError<wowee::pipeline::WoweeAddonManifestLoader>(c, base, "gen-mod-util", ".wmod")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmod");
    if (!wowee::pipeline::WoweeAddonManifestLoader::exists(base)) {
        std::fprintf(stderr, "WMOD not found: %s.wmod\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAddonManifestLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmod"] = base + ".wmod";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"addonId", e.addonId},
                {"name", e.name},
                {"description", e.description},
                {"version", e.version},
                {"author", e.author},
                {"minClientBuild", e.minClientBuild},
                {"requiresSavedVariables",
                    e.requiresSavedVariables != 0},
                {"loadOnDemand", e.loadOnDemand != 0},
                {"dependencies", e.dependencies},
                {"optionalDependencies", e.optionalDependencies},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMOD: %s.wmod\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  addons  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  version    sv  lod  deps  optDeps  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-9s  %s   %s   %4zu  %7zu  %s\n",
                    e.addonId, e.version.c_str(),
                    e.requiresSavedVariables ? "Y" : "n",
                    e.loadOnDemand ? "Y" : "n",
                    e.dependencies.size(),
                    e.optionalDependencies.size(),
                    e.name.c_str());
    }
    return 0;
}

// Stack-based DFS cycle detection. Returns the first
// cycle found as a vector of addonIds. Empty if no
// cycle. Considers ONLY required dependencies — optional
// deps don't deadlock.
std::vector<uint32_t> findFirstCycle(
    const wowee::pipeline::WoweeAddonManifest& c) {
    std::map<uint32_t, std::vector<uint32_t>> graph;
    std::set<uint32_t> known;
    for (const auto& e : c.entries) {
        graph[e.addonId] = e.dependencies;
        known.insert(e.addonId);
    }
    enum Color : uint8_t { White = 0, Gray = 1, Black = 2 };
    std::map<uint32_t, Color> color;
    for (uint32_t id : known) color[id] = White;
    std::vector<uint32_t> path;
    std::vector<uint32_t> cycle;
    std::function<bool(uint32_t)> dfs = [&](uint32_t node) -> bool {
        color[node] = Gray;
        path.push_back(node);
        for (uint32_t dep : graph[node]) {
            if (!known.count(dep)) continue;
            if (color[dep] == Gray) {
                // Found back-edge to gray node — extract
                // the cycle starting at dep in path.
                auto it = std::find(path.begin(), path.end(), dep);
                cycle.assign(it, path.end());
                cycle.push_back(dep);   // close the loop
                return true;
            }
            if (color[dep] == White) {
                if (dfs(dep)) return true;
            }
        }
        color[node] = Black;
        path.pop_back();
        return false;
    };
    for (uint32_t id : known) {
        if (color[id] == White && dfs(id)) return cycle;
    }
    return {};
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmod");
    if (!wowee::pipeline::WoweeAddonManifestLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wmod: WMOD not found: %s.wmod\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAddonManifestLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<std::string> namesSeen;
    std::set<uint32_t> knownIds;
    for (const auto& e : c.entries) knownIds.insert(e.addonId);
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.addonId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.addonId == 0)
            errors.push_back(ctx + ": addonId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.version.empty())
            errors.push_back(ctx + ": version is empty "
                "(every addon must declare a version)");
        if (!e.name.empty() &&
            !namesSeen.insert(e.name).second) {
            errors.push_back(ctx +
                ": duplicate addon name '" + e.name +
                "' — addon-loader would dispatch ambiguously");
        }
        if (!idsSeen.insert(e.addonId).second) {
            errors.push_back(ctx + ": duplicate addonId");
        }
        // Self-dependency: addon listing itself in its
        // own deps would deadlock during load.
        for (uint32_t dep : e.dependencies) {
            if (dep == e.addonId) {
                errors.push_back(ctx +
                    ": addon depends on itself "
                    "(deadlock at load)");
            }
            if (!knownIds.count(dep)) {
                errors.push_back(ctx +
                    ": required dependency addonId=" +
                    std::to_string(dep) +
                    " not found in catalog");
            }
        }
        for (uint32_t dep : e.optionalDependencies) {
            if (dep == e.addonId) {
                warnings.push_back(ctx +
                    ": addon optionally depends on "
                    "itself — has no effect, prune");
            }
            // Optional deps to unknown ids are NOT an
            // error — addon may degrade gracefully if
            // the optional dep is absent.
        }
        if (e.minClientBuild != 0 && e.minClientBuild < 4500) {
            warnings.push_back(ctx +
                ": minClientBuild=" +
                std::to_string(e.minClientBuild) +
                " is below the lowest known WoW vanilla "
                "build (4500); likely a typo");
        }
    }
    // DFS cycle detection over required dependencies.
    auto cycle = findFirstCycle(c);
    if (!cycle.empty()) {
        std::string trail;
        for (size_t k = 0; k < cycle.size(); ++k) {
            if (k > 0) trail += " -> ";
            trail += std::to_string(cycle[k]);
        }
        errors.push_back("dependency cycle detected: " +
                          trail +
                          " — addon-loader would deadlock");
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wmod", base, errors, warnings);
    std::printf("validate-wmod: %s.wmod\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu addons, all addonIds + "
                    "names unique, no required-dep cycle, "
                    "no missing required deps, no self-"
                    "deps\n", c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wmod");
    if (out.empty()) out = base + ".wmod.json";
    if (!wowee::pipeline::WoweeAddonManifestLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wmod-json: WMOD not found: %s.wmod\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAddonManifestLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WMOD";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"addonId", e.addonId},
            {"name", e.name},
            {"description", e.description},
            {"version", e.version},
            {"author", e.author},
            {"minClientBuild", e.minClientBuild},
            {"requiresSavedVariables",
                e.requiresSavedVariables != 0},
            {"loadOnDemand", e.loadOnDemand != 0},
            {"dependencies", e.dependencies},
            {"optionalDependencies", e.optionalDependencies},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wmod-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu addons)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wmod");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wmod-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wmod-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeAddonManifest c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wmod-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeAddonManifest::Entry e;
        e.addonId = je.value("addonId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.version = je.value("version", std::string{});
        e.author = je.value("author", std::string{});
        e.minClientBuild = je.value("minClientBuild", 0u);
        e.requiresSavedVariables =
            je.value("requiresSavedVariables", false) ? 1 : 0;
        e.loadOnDemand = je.value("loadOnDemand", false) ? 1 : 0;
        if (je.contains("dependencies") &&
            je["dependencies"].is_array()) {
            for (const auto& d : je["dependencies"]) {
                if (d.is_number_unsigned() ||
                    d.is_number_integer()) {
                    e.dependencies.push_back(d.get<uint32_t>());
                }
            }
        }
        if (je.contains("optionalDependencies") &&
            je["optionalDependencies"].is_array()) {
            for (const auto& d : je["optionalDependencies"]) {
                if (d.is_number_unsigned() ||
                    d.is_number_integer()) {
                    e.optionalDependencies.push_back(
                        d.get<uint32_t>());
                }
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeAddonManifestLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmod-json: failed to save %s.wmod\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmod (%zu addons)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleAddonManifestCatalog(int& i, int argc, char** argv,
                                  int& outRc) {
    if (std::strcmp(argv[i], "--gen-mod") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mod-ui") == 0 && i + 1 < argc) {
        outRc = handleGenUI(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mod-util") == 0 &&
        i + 1 < argc) {
        outRc = handleGenUtility(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmod") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmod") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmod-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmod-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
