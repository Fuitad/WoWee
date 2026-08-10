#include "cli_item_suffixes_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_item_suffixes.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeItemSuffix& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeItemSuffixLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wsuf\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeItemSuffix& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsuf\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  suffixes : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterSuffixes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsuf");
    auto c = wowee::pipeline::WoweeItemSuffixLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-suf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMagical(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MagicalSuffixes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsuf");
    auto c = wowee::pipeline::WoweeItemSuffixLoader::makeMagical(name);
    if (!saveOrError(c, base, "gen-suf-magical")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPvP(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvPSuffixes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsuf");
    auto c = wowee::pipeline::WoweeItemSuffixLoader::makePvP(name);
    if (!saveOrError(c, base, "gen-suf-pvp")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendEntryJson(nlohmann::json& arr,
                     const wowee::pipeline::WoweeItemSuffix::Entry& e) {
    nlohmann::json stats = nlohmann::json::array();
    for (size_t k = 0;
         k < wowee::pipeline::WoweeItemSuffix::kMaxStats; ++k) {
        if (e.statKind[k] == 0 && e.statValuePoints[k] == 0) continue;
        stats.push_back({
            {"statKind", e.statKind[k]},
            {"statValuePoints", e.statValuePoints[k]},
        });
    }
    arr.push_back({
        {"suffixId", e.suffixId},
        {"name", e.name},
        {"description", e.description},
        {"itemQualityFloor", e.itemQualityFloor},
        {"itemQualityCeiling", e.itemQualityCeiling},
        {"suffixCategory", e.suffixCategory},
        {"suffixCategoryName", wowee::pipeline::WoweeItemSuffix::suffixCategoryName(e.suffixCategory)},
        {"restrictedSlotMask", e.restrictedSlotMask},
        {"stats", stats},
    });
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsuf");
    if (!wowee::pipeline::WoweeItemSuffixLoader::exists(base)) {
        std::fprintf(stderr, "WSUF not found: %s.wsuf\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemSuffixLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsuf"] = base + ".wsuf";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) appendEntryJson(arr, e);
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSUF: %s.wsuf\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  suffixes : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    category    quality   slotMask     stats        name\n");
    for (const auto& e : c.entries) {
        size_t statCount = 0;
        for (size_t k = 0;
             k < wowee::pipeline::WoweeItemSuffix::kMaxStats; ++k) {
            if (e.statKind[k] != 0 || e.statValuePoints[k] != 0)
                ++statCount;
        }
        std::printf("  %4u   %-10s  %u-%u       0x%08x   %zu/%zu        %s\n",
                    e.suffixId,
                    wowee::pipeline::WoweeItemSuffix::suffixCategoryName(e.suffixCategory),
                    e.itemQualityFloor, e.itemQualityCeiling,
                    e.restrictedSlotMask, statCount,
                    wowee::pipeline::WoweeItemSuffix::kMaxStats,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wsuf");
    if (outPath.empty()) outPath = base + ".wsuf.json";
    if (!wowee::pipeline::WoweeItemSuffixLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsuf-json: WSUF not found: %s.wsuf\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemSuffixLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) appendEntryJson(arr, e);
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wsuf-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.wsuf\n", base.c_str());
    std::printf("  suffixes : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wsuf");
    outBase = cli::withoutExt(outBase, ".wsuf");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wsuf-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wsuf-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto categoryFromName = [](const std::string& s) -> uint8_t {
        if (s == "generic")   return wowee::pipeline::WoweeItemSuffix::Generic;
        if (s == "elemental") return wowee::pipeline::WoweeItemSuffix::Elemental;
        if (s == "defensive") return wowee::pipeline::WoweeItemSuffix::Defensive;
        if (s == "pvp")       return wowee::pipeline::WoweeItemSuffix::PvPSuffix;
        if (s == "crafted")   return wowee::pipeline::WoweeItemSuffix::Crafted;
        return wowee::pipeline::WoweeItemSuffix::Generic;
    };
    wowee::pipeline::WoweeItemSuffix c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeItemSuffix::Entry e;
            e.suffixId = je.value("suffixId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.itemQualityFloor = static_cast<uint8_t>(
                je.value("itemQualityFloor", 0));
            e.itemQualityCeiling = static_cast<uint8_t>(
                je.value("itemQualityCeiling", 4));
            if (je.contains("suffixCategory") &&
                je["suffixCategory"].is_number_integer()) {
                e.suffixCategory = static_cast<uint8_t>(
                    je["suffixCategory"].get<int>());
            } else if (je.contains("suffixCategoryName") &&
                       je["suffixCategoryName"].is_string()) {
                e.suffixCategory = categoryFromName(
                    je["suffixCategoryName"].get<std::string>());
            }
            e.restrictedSlotMask = je.value("restrictedSlotMask", 0u);
            for (size_t k = 0;
                 k < wowee::pipeline::WoweeItemSuffix::kMaxStats; ++k) {
                e.statKind[k] = 0;
                e.statValuePoints[k] = 0;
            }
            if (je.contains("stats") && je["stats"].is_array()) {
                size_t slot = 0;
                for (const auto& js : je["stats"]) {
                    if (slot >= wowee::pipeline::WoweeItemSuffix::kMaxStats)
                        break;
                    e.statKind[slot] = static_cast<uint8_t>(
                        js.value("statKind", 0));
                    e.statValuePoints[slot] = static_cast<uint16_t>(
                        js.value("statValuePoints", 0));
                    ++slot;
                }
            }
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeItemSuffixLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsuf-json: failed to save %s.wsuf\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsuf\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  suffixes : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsuf");
    if (!wowee::pipeline::WoweeItemSuffixLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsuf: WSUF not found: %s.wsuf\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemSuffixLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.suffixId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.suffixId == 0)
            errors.push_back(ctx + ": suffixId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.suffixCategory > wowee::pipeline::WoweeItemSuffix::Crafted) {
            errors.push_back(ctx + ": suffixCategory " +
                std::to_string(e.suffixCategory) + " not in 0..4");
        }
        if (e.itemQualityFloor > e.itemQualityCeiling) {
            errors.push_back(ctx + ": itemQualityFloor " +
                std::to_string(e.itemQualityFloor) +
                " > itemQualityCeiling " +
                std::to_string(e.itemQualityCeiling));
        }
        if (e.itemQualityCeiling > 7) {
            errors.push_back(ctx + ": itemQualityCeiling " +
                std::to_string(e.itemQualityCeiling) +
                " not in 0..7 (poor / common / uncommon / rare / "
                "epic / legendary / artifact / heirloom)");
        }
        // A suffix with no stats is mechanically meaningless —
        // it would just rename the item without changing it.
        bool anyStat = false;
        for (size_t s = 0;
             s < wowee::pipeline::WoweeItemSuffix::kMaxStats; ++s) {
            if (e.statKind[s] != 0 || e.statValuePoints[s] != 0) {
                anyStat = true;
                // statKind must be paired with non-zero
                // statValuePoints (and vice versa).
                if (e.statKind[s] != 0 && e.statValuePoints[s] == 0) {
                    errors.push_back(ctx + ": stat slot " +
                        std::to_string(s) + " has statKind=" +
                        std::to_string(e.statKind[s]) +
                        " but statValuePoints=0");
                }
                if (e.statKind[s] == 0 && e.statValuePoints[s] != 0) {
                    errors.push_back(ctx + ": stat slot " +
                        std::to_string(s) +
                        " has statValuePoints=" +
                        std::to_string(e.statValuePoints[s]) +
                        " but statKind=0");
                }
            }
        }
        if (!anyStat) {
            warnings.push_back(ctx +
                ": no stats — suffix renames item but adds nothing");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.suffixId) {
                errors.push_back(ctx + ": duplicate suffixId");
                break;
            }
        }
        idsSeen.push_back(e.suffixId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsuf", base, errors, warnings);
    std::printf("validate-wsuf: %s.wsuf\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu suffixes, all suffixIds unique, all stat slots paired\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleItemSuffixesCatalog(int& i, int argc, char** argv,
                               int& outRc) {
    if (std::strcmp(argv[i], "--gen-suf") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-suf-magical") == 0 && i + 1 < argc) {
        outRc = handleGenMagical(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-suf-pvp") == 0 && i + 1 < argc) {
        outRc = handleGenPvP(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsuf") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsuf") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsuf-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsuf-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
