#include "cli_spell_ranges_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_ranges.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeSpellRange& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsrg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranges  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterRanges";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsrg");
    auto c = wowee::pipeline::WoweeSpellRangeLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellRangeLoader>(c, base, "gen-srg", ".wsrg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRanged(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RangedSpellBuckets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsrg");
    auto c = wowee::pipeline::WoweeSpellRangeLoader::makeRanged(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellRangeLoader>(c, base, "gen-srg-ranged", ".wsrg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFriendly(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FriendlyOnlyRanges";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsrg");
    auto c = wowee::pipeline::WoweeSpellRangeLoader::makeFriendly(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellRangeLoader>(c, base, "gen-srg-friendly", ".wsrg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsrg");
    if (!wowee::pipeline::WoweeSpellRangeLoader::exists(base)) {
        std::fprintf(stderr, "WSRG not found: %s.wsrg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellRangeLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsrg"] = base + ".wsrg";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"rangeId", e.rangeId},
                {"name", e.name},
                {"description", e.description},
                {"rangeKind", e.rangeKind},
                {"rangeKindName", wowee::pipeline::WoweeSpellRange::rangeKindName(e.rangeKind)},
                {"minRange", e.minRange},
                {"maxRange", e.maxRange},
                {"minRangeFriendly", e.minRangeFriendly},
                {"maxRangeFriendly", e.maxRangeFriendly},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSRG: %s.wsrg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranges  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind        min-max(hostile)  min-max(friendly)  color       name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-9s   %5.1f - %6.1f   %5.1f - %6.1f   0x%08x  %s\n",
                    e.rangeId,
                    wowee::pipeline::WoweeSpellRange::rangeKindName(e.rangeKind),
                    e.minRange, e.maxRange,
                    e.minRangeFriendly, e.maxRangeFriendly,
                    e.iconColorRGBA, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wsrg");
    if (!wowee::pipeline::WoweeSpellRangeLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsrg-json: WSRG not found: %s.wsrg\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellRangeLoader::load(base);
    if (outPath.empty()) outPath = base + ".wsrg.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["rangeId"] = e.rangeId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["rangeKind"] = e.rangeKind;
        je["rangeKindName"] =
            wowee::pipeline::WoweeSpellRange::rangeKindName(e.rangeKind);
        je["minRange"] = e.minRange;
        je["maxRange"] = e.maxRange;
        je["minRangeFriendly"] = e.minRangeFriendly;
        je["maxRangeFriendly"] = e.maxRangeFriendly;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wsrg-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranges  : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseRangeKindToken(const nlohmann::json& jv,
                            uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellRange::Unlimited)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "self")        return wowee::pipeline::WoweeSpellRange::Self;
        if (s == "melee")       return wowee::pipeline::WoweeSpellRange::Melee;
        if (s == "short" ||
            s == "shortranged") return wowee::pipeline::WoweeSpellRange::ShortRanged;
        if (s == "ranged")      return wowee::pipeline::WoweeSpellRange::Ranged;
        if (s == "long" ||
            s == "longranged")  return wowee::pipeline::WoweeSpellRange::LongRanged;
        if (s == "very-long" ||
            s == "verylong")    return wowee::pipeline::WoweeSpellRange::VeryLong;
        if (s == "unlimited")   return wowee::pipeline::WoweeSpellRange::Unlimited;
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
            "import-wsrg-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wsrg-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellRange c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellRange::Entry e;
            if (je.contains("rangeId"))     e.rangeId = je["rangeId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            // Accept both rangeKind (int) and rangeKindName
            // (string) — falling back to the other when only
            // one form is present, mirroring the dual int+name
            // shape the export emits.
            uint8_t kind = wowee::pipeline::WoweeSpellRange::Ranged;
            if (je.contains("rangeKind"))
                kind = parseRangeKindToken(je["rangeKind"], kind);
            else if (je.contains("rangeKindName"))
                kind = parseRangeKindToken(je["rangeKindName"], kind);
            e.rangeKind = kind;
            if (je.contains("minRange"))         e.minRange = je["minRange"].get<float>();
            if (je.contains("maxRange"))         e.maxRange = je["maxRange"].get<float>();
            if (je.contains("minRangeFriendly")) e.minRangeFriendly = je["minRangeFriendly"].get<float>();
            if (je.contains("maxRangeFriendly")) e.maxRangeFriendly = je["maxRangeFriendly"].get<float>();
            if (je.contains("iconColorRGBA"))    e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wsrg");
    outBase = cli::withoutExt(outBase, ".wsrg");
    if (!wowee::pipeline::WoweeSpellRangeLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsrg-json: failed to save %s.wsrg\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsrg\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranges  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsrg");
    if (!wowee::pipeline::WoweeSpellRangeLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsrg: WSRG not found: %s.wsrg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellRangeLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.rangeId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.rangeId == 0)
            errors.push_back(ctx + ": rangeId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.rangeKind > wowee::pipeline::WoweeSpellRange::Unlimited) {
            errors.push_back(ctx + ": rangeKind " +
                std::to_string(e.rangeKind) + " not in 0..6");
        }
        if (e.minRange < 0.0f || e.maxRange < 0.0f ||
            e.minRangeFriendly < 0.0f ||
            e.maxRangeFriendly < 0.0f) {
            errors.push_back(ctx +
                ": negative range value (ranges must be >= 0)");
        }
        if (e.minRange > e.maxRange) {
            errors.push_back(ctx + ": minRange " +
                std::to_string(e.minRange) +
                " > maxRange " + std::to_string(e.maxRange));
        }
        if (e.minRangeFriendly > e.maxRangeFriendly) {
            errors.push_back(ctx + ": minRangeFriendly " +
                std::to_string(e.minRangeFriendly) +
                " > maxRangeFriendly " +
                std::to_string(e.maxRangeFriendly));
        }
        // Self-kind should have max range = 0; otherwise the
        // engine would treat it as targeted.
        if (e.rangeKind == wowee::pipeline::WoweeSpellRange::Self &&
            (e.maxRange != 0.0f || e.maxRangeFriendly != 0.0f)) {
            warnings.push_back(ctx +
                ": Self kind with non-zero maxRange — engine "
                "treats this as targeted, not self-only");
        }
        // Melee-kind should be 0..5y by canonical convention.
        if (e.rangeKind == wowee::pipeline::WoweeSpellRange::Melee &&
            e.maxRange > 8.0f) {
            warnings.push_back(ctx +
                ": Melee kind with maxRange " +
                std::to_string(e.maxRange) +
                " > 8 (canonical melee is 5y)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.rangeId) {
                errors.push_back(ctx + ": duplicate rangeId");
                break;
            }
        }
        idsSeen.push_back(e.rangeId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsrg", base, errors, warnings);
    std::printf("validate-wsrg: %s.wsrg\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu ranges, all rangeIds unique, all min<=max\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellRangesCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-srg") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-srg-ranged") == 0 && i + 1 < argc) {
        outRc = handleGenRanged(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-srg-friendly") == 0 && i + 1 < argc) {
        outRc = handleGenFriendly(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsrg") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsrg") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsrg-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsrg-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
