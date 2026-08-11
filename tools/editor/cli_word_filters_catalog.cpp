#include "cli_word_filters_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_word_filters.hpp"
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

const char* filterKindName(uint8_t k) {
    using F = wowee::pipeline::WoweeWordFilters;
    switch (k) {
        case F::Spam:         return "spam";
        case F::GoldSeller:   return "goldseller";
        case F::AllCaps:      return "allcaps";
        case F::RepeatChar:   return "repeatchar";
        case F::URL:          return "url";
        case F::AdvertReward: return "advertreward";
        case F::Misc:         return "misc";
        default:              return "unknown";
    }
}

const char* severityName(uint8_t s) {
    using F = wowee::pipeline::WoweeWordFilters;
    switch (s) {
        case F::Warn:    return "warn";
        case F::Replace: return "replace";
        case F::Drop:    return "drop";
        case F::Mute:    return "mute";
        default:         return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeWordFilters& c,
                     const std::string& base) {
    std::printf("Wrote %s.wwfl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  filters : %zu\n", c.entries.size());
}

int handleGenSpam(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SpamRMTFilters";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wwfl");
    auto c = wowee::pipeline::WoweeWordFiltersLoader::makeSpamRMT(name);
    if (!saveOrError<wowee::pipeline::WoweeWordFiltersLoader>(c, base, "gen-wfl", ".wwfl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCaps(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllCapsFilters";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wwfl");
    auto c = wowee::pipeline::WoweeWordFiltersLoader::makeAllCaps(name);
    if (!saveOrError<wowee::pipeline::WoweeWordFiltersLoader>(c, base, "gen-wfl-caps", ".wwfl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenURL(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "URLDetectFilters";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wwfl");
    auto c = wowee::pipeline::WoweeWordFiltersLoader::makeURLDetect(name);
    if (!saveOrError<wowee::pipeline::WoweeWordFiltersLoader>(c, base, "gen-wfl-url", ".wwfl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wwfl");
    if (!wowee::pipeline::WoweeWordFiltersLoader::exists(base)) {
        std::fprintf(stderr, "WWFL not found: %s.wwfl\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeWordFiltersLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wwfl"] = base + ".wwfl";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"filterId", e.filterId},
                {"name", e.name},
                {"description", e.description},
                {"pattern", e.pattern},
                {"replacement", e.replacement},
                {"filterKind", e.filterKind},
                {"filterKindName", filterKindName(e.filterKind)},
                {"severity", e.severity},
                {"severityName", severityName(e.severity)},
                {"caseSensitive", e.caseSensitive != 0},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WWFL: %s.wwfl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  filters : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind          severity   caseS   pattern -> replacement   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-11s   %-7s    %s     '%s' -> '%s'   %s\n",
                    e.filterId, filterKindName(e.filterKind),
                    severityName(e.severity),
                    e.caseSensitive ? "yes" : "no ",
                    e.pattern.c_str(), e.replacement.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int parseFilterKindToken(const std::string& s) {
    using F = wowee::pipeline::WoweeWordFilters;
    if (s == "spam")         return F::Spam;
    if (s == "goldseller")   return F::GoldSeller;
    if (s == "allcaps")      return F::AllCaps;
    if (s == "repeatchar")   return F::RepeatChar;
    if (s == "url")          return F::URL;
    if (s == "advertreward") return F::AdvertReward;
    if (s == "misc")         return F::Misc;
    return -1;
}

int parseSeverityToken(const std::string& s) {
    using F = wowee::pipeline::WoweeWordFilters;
    if (s == "warn")    return F::Warn;
    if (s == "replace") return F::Replace;
    if (s == "drop")    return F::Drop;
    if (s == "mute")    return F::Mute;
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
                    "import-wwfl-json: unknown %s token "
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

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wwfl");
    if (out.empty()) out = base + ".wwfl.json";
    if (!wowee::pipeline::WoweeWordFiltersLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wwfl-json: WWFL not found: %s.wwfl\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeWordFiltersLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WWFL";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"filterId", e.filterId},
            {"name", e.name},
            {"description", e.description},
            {"pattern", e.pattern},
            {"replacement", e.replacement},
            {"filterKind", e.filterKind},
            {"filterKindName", filterKindName(e.filterKind)},
            {"severity", e.severity},
            {"severityName", severityName(e.severity)},
            {"caseSensitive", e.caseSensitive != 0},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wwfl-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu filters)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wwfl");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wwfl-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wwfl-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeWordFilters c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wwfl-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeWordFilters::Entry e;
        e.filterId = je.value("filterId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.pattern = je.value("pattern", std::string{});
        e.replacement = je.value("replacement", std::string{});
        if (!readEnumField(je, "filterKind", "filterKindName",
                            parseFilterKindToken, "filterKind",
                            e.filterId, e.filterKind)) return 1;
        if (!readEnumField(je, "severity", "severityName",
                            parseSeverityToken, "severity",
                            e.filterId, e.severity)) return 1;
        if (je.contains("caseSensitive")) {
            const auto& v = je["caseSensitive"];
            if (v.is_boolean())
                e.caseSensitive = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.caseSensitive = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeWordFiltersLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wwfl-json: failed to save %s.wwfl\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wwfl (%zu filters)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wwfl");
    if (!wowee::pipeline::WoweeWordFiltersLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wwfl: WWFL not found: %s.wwfl\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeWordFiltersLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<std::string> patternsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.filterId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.filterId == 0)
            errors.push_back(ctx + ": filterId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.pattern.empty()) {
            errors.push_back(ctx +
                ": pattern is empty — filter would match "
                "nothing (or every message, depending on "
                "the matcher's empty-string semantics)");
        }
        if (e.filterKind > 5 && e.filterKind != 255) {
            errors.push_back(ctx + ": filterKind " +
                std::to_string(e.filterKind) +
                " out of range (must be 0..5 or 255 Misc)");
        }
        if (e.severity > 3) {
            errors.push_back(ctx + ": severity " +
                std::to_string(e.severity) +
                " out of range (must be 0..3)");
        }
        // Per-severity validity: Replace severity REQUIRES
        // a non-empty replacement (else the substitution
        // would just delete the matched portion, which is
        // Drop semantics).
        using F = wowee::pipeline::WoweeWordFilters;
        if (e.severity == F::Replace && e.replacement.empty()) {
            warnings.push_back(ctx +
                ": Replace severity with empty "
                "replacement — message would silently lose "
                "the matched substring (effectively Drop "
                "semantics for that span). Use severity="
                "Drop explicitly if that's the intent.");
        }
        // Pattern uniqueness — two filters with the same
        // pattern would fire ambiguously.
        if (!e.pattern.empty() &&
            !patternsSeen.insert(e.pattern).second) {
            errors.push_back(ctx +
                ": pattern '" + e.pattern +
                "' already used by another filter — "
                "preprocessor dispatch would be "
                "non-deterministic");
        }
        if (!idsSeen.insert(e.filterId).second) {
            errors.push_back(ctx + ": duplicate filterId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wwfl", base, errors, warnings);
    std::printf("validate-wwfl: %s.wwfl\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu filters, all filterIds + "
                    "patterns unique\n", c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleWordFiltersCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-wfl") == 0 && i + 1 < argc) {
        outRc = handleGenSpam(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-wfl-caps") == 0 && i + 1 < argc) {
        outRc = handleGenCaps(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-wfl-url") == 0 && i + 1 < argc) {
        outRc = handleGenURL(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wwfl") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wwfl") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wwfl-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wwfl-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
