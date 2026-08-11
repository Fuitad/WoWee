#include "cli_talent_tabs_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_talent_tabs.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
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


void printGenSummary(const wowee::pipeline::WoweeTalentTab& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtle\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabs    : %zu\n", c.entries.size());
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorTalentTabs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtle");
    auto c = wowee::pipeline::WoweeTalentTabLoader::makeWarrior(name);
    if (!saveOrError<wowee::pipeline::WoweeTalentTabLoader>(c, base, "gen-tle", ".wtle")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageTalentTabs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtle");
    auto c = wowee::pipeline::WoweeTalentTabLoader::makeMage(name);
    if (!saveOrError<wowee::pipeline::WoweeTalentTabLoader>(c, base, "gen-tle-mage", ".wtle")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPaladin(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PaladinTalentTabs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtle");
    auto c = wowee::pipeline::WoweeTalentTabLoader::makePaladin(name);
    if (!saveOrError<wowee::pipeline::WoweeTalentTabLoader>(c, base, "gen-tle-paladin", ".wtle")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtle");
    if (!wowee::pipeline::WoweeTalentTabLoader::exists(base)) {
        std::fprintf(stderr, "WTLE not found: %s.wtle\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTalentTabLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtle"] = base + ".wtle";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"tabId", e.tabId},
                {"name", e.name},
                {"description", e.description},
                {"classMask", e.classMask},
                {"displayOrder", e.displayOrder},
                {"roleHint", e.roleHint},
                {"roleHintName", wowee::pipeline::WoweeTalentTab::roleHintName(e.roleHint)},
                {"iconPath", e.iconPath},
                {"backgroundFile", e.backgroundFile},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTLE: %s.wtle\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabs    : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    classMask    ord  role     name              backgroundFile\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   0x%08x   %u    %-7s  %-15s   %s\n",
                    e.tabId, e.classMask,
                    e.displayOrder,
                    wowee::pipeline::WoweeTalentTab::roleHintName(e.roleHint),
                    e.name.c_str(),
                    e.backgroundFile.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wtle");
    if (!wowee::pipeline::WoweeTalentTabLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtle-json: WTLE not found: %s.wtle\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTalentTabLoader::load(base);
    if (outPath.empty()) outPath = base + ".wtle.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["tabId"] = e.tabId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["classMask"] = e.classMask;
        je["displayOrder"] = e.displayOrder;
        je["roleHint"] = e.roleHint;
        je["roleHintName"] =
            wowee::pipeline::WoweeTalentTab::roleHintName(e.roleHint);
        je["iconPath"] = e.iconPath;
        je["backgroundFile"] = e.backgroundFile;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wtle-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabs    : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseRoleHintToken(const nlohmann::json& jv,
                           uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeTalentTab::PetClass)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "dps")    return wowee::pipeline::WoweeTalentTab::DPS;
        if (s == "tank")   return wowee::pipeline::WoweeTalentTab::Tank;
        if (s == "healer") return wowee::pipeline::WoweeTalentTab::Healer;
        if (s == "hybrid") return wowee::pipeline::WoweeTalentTab::Hybrid;
        if (s == "pet" ||
            s == "petclass") return wowee::pipeline::WoweeTalentTab::PetClass;
    }
    return fallback;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wtle-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wtle-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeTalentTab c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeTalentTab::Entry e;
            if (je.contains("tabId"))         e.tabId = je["tabId"].get<uint32_t>();
            if (je.contains("name"))          e.name = je["name"].get<std::string>();
            if (je.contains("description"))   e.description = je["description"].get<std::string>();
            if (je.contains("classMask"))     e.classMask = je["classMask"].get<uint32_t>();
            if (je.contains("displayOrder"))  e.displayOrder = je["displayOrder"].get<uint8_t>();
            uint8_t role = wowee::pipeline::WoweeTalentTab::DPS;
            if (je.contains("roleHint"))
                role = parseRoleHintToken(je["roleHint"], role);
            else if (je.contains("roleHintName"))
                role = parseRoleHintToken(je["roleHintName"], role);
            e.roleHint = role;
            if (je.contains("iconPath"))       e.iconPath = je["iconPath"].get<std::string>();
            if (je.contains("backgroundFile")) e.backgroundFile = je["backgroundFile"].get<std::string>();
            if (je.contains("iconColorRGBA"))  e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wtle");
    outBase = cli::withoutExt(outBase, ".wtle");
    if (!wowee::pipeline::WoweeTalentTabLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtle-json: failed to save %s.wtle\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtle\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabs    : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtle");
    if (!wowee::pipeline::WoweeTalentTabLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtle: WTLE not found: %s.wtle\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTalentTabLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.tabId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.tabId == 0)
            errors.push_back(ctx + ": tabId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.classMask == 0)
            errors.push_back(ctx +
                ": classMask is 0 — no class can use this tab");
        if (e.roleHint > wowee::pipeline::WoweeTalentTab::PetClass) {
            errors.push_back(ctx + ": roleHint " +
                std::to_string(e.roleHint) + " not in 0..4");
        }
        if (e.displayOrder > 3) {
            warnings.push_back(ctx +
                ": displayOrder " +
                std::to_string(e.displayOrder) +
                " > 3 — talent UI shows at most 4 tabs");
        }
        if (e.iconPath.empty())
            warnings.push_back(ctx +
                ": iconPath is empty — tab will render with "
                "the missing-texture placeholder");
        if (e.backgroundFile.empty())
            warnings.push_back(ctx +
                ": backgroundFile is empty — talent tree "
                "panel will have no background art");
        for (uint32_t prev : idsSeen) {
            if (prev == e.tabId) {
                errors.push_back(ctx + ": duplicate tabId");
                break;
            }
        }
        idsSeen.push_back(e.tabId);
    }
    // Cross-entry: detect duplicate (classMask, displayOrder)
    // for overlapping classMasks — two tabs can't share a UI
    // slot for the same class.
    for (size_t a = 0; a < c.entries.size(); ++a) {
        for (size_t b = a + 1; b < c.entries.size(); ++b) {
            const auto& ea = c.entries[a];
            const auto& eb = c.entries[b];
            if (ea.displayOrder != eb.displayOrder) continue;
            if ((ea.classMask & eb.classMask) == 0) continue;
            warnings.push_back(
                "entries " + std::to_string(a) + " (" +
                ea.name + ") and " + std::to_string(b) + " (" +
                eb.name + ") share displayOrder " +
                std::to_string(ea.displayOrder) +
                " for overlapping classMask — tab UI position collision");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wtle", base, errors, warnings);
    std::printf("validate-wtle: %s.wtle\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu tabs, all tabIds unique, no UI overlaps\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleTalentTabsCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-tle") == 0 && i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tle-mage") == 0 && i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tle-paladin") == 0 && i + 1 < argc) {
        outRc = handleGenPaladin(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtle") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtle") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtle-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtle-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
