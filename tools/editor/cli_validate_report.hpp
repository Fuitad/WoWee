#pragma once

/**
 * cli_validate_report.hpp — how a --validate-* handler reports what it found.
 *
 * Every catalog format validates the same way: collect errors, collect
 * warnings, then say so either as JSON for a script or as text for a person,
 * and exit non-zero if there were errors.
 *
 * Only two things differ between the 138 formats — the file's extension, and
 * the one line describing what "OK" means for that format ("3 slots, all
 * slotIds unique, no UI overlaps"). Everything around those was copied into
 * every handler, which is why a format's JSON report and its text report could
 * ever disagree about anything.
 */

#include <cstdio>
#include <fstream>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_arg_parse.hpp"
#include "cli_catalog_paths.hpp"

namespace wowee {
namespace editor {
namespace cli {

/// The machine-readable report. Answers the process exit code.
///
/// `tag` is the extension without its dot, and is used as the JSON key holding
/// the file path — which is how a caller tells which format answered.
inline int printValidationJson(const std::string& tag, const std::string& base,
                               const std::vector<std::string>& errors,
                               const std::vector<std::string>& warnings) {
    const bool ok = errors.empty();
    nlohmann::json j;
    j[tag] = base + "." + tag;
    j["ok"] = ok;
    j["errors"] = errors;
    j["warnings"] = warnings;
    std::printf("%s\n", j.dump(2).c_str());
    return ok ? 0 : 1;
}

/// The human-readable list of what was wrong. Answers the process exit code.
///
/// Called only when there is something to say: a handler prints its own "OK"
/// line when there are neither errors nor warnings, because what counts as OK
/// is the one thing each format knows and this does not.
inline int printValidationIssues(const std::vector<std::string>& errors,
                                 const std::vector<std::string>& warnings) {
    if (!warnings.empty()) {
        std::printf("  warnings (%zu):\n", warnings.size());
        for (const auto& w : warnings) std::printf("    - %s\n", w.c_str());
    }
    if (!errors.empty()) {
        std::printf("  ERRORS (%zu):\n", errors.size());
        for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    }
    return errors.empty() ? 0 : 1;
}

/// printf into a std::string, for the one line a format writes about itself.
template <typename... Args>
inline std::string formatted(const char* fmt, Args... args) {
    const int n = std::snprintf(nullptr, 0, fmt, args...);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    std::snprintf(out.data(), static_cast<size_t>(n) + 1, fmt, args...);
    return out;
}

/// The end of every --validate-* handler: the report, in whichever form was
/// asked for, and the process exit code.
///
/// All 138 of them wrote this out: test whether there were errors, answer JSON
/// if --json was given, print the file's name, print one line saying what OK
/// means for this format when there is nothing to report, and otherwise list
/// what there is. Only the tag and that one line differ, and the line is the
/// one thing each format knows that this does not — so it is passed in already
/// formatted.
inline int reportValidation(const std::string& tag, const std::string& base, bool jsonOut,
                            const std::vector<std::string>& errors,
                            const std::vector<std::string>& warnings,
                            const std::string& okLine) {
    if (jsonOut) return printValidationJson(tag, base, errors, warnings);
    std::printf("validate-%s: %s.%s\n", tag.c_str(), base.c_str(), tag.c_str());
    if (errors.empty() && warnings.empty()) {
        std::printf("  OK — %s\n", okLine.c_str());
        return 0;
    }
    return printValidationIssues(errors, warnings);
}

/// Run a format's --validate-* handler: resolve the path, refuse if the file is
/// not there, load it, note an empty catalog, run the format's own checks, and
/// report.
///
/// Everything but those checks was written out in each of the 138 handlers —
/// twelve lines of preamble and eight of report around the part that actually
/// knows something about the format. `check` fills the two lists and answers
/// the one line that describes what OK means for it.
template <typename Loader, typename Check>
int validateCatalog(int& i, int argc, char** argv, const char* tag, const char* label,
                    Check check) {
    std::string base = argv[++i];
    const bool jsonOut = consumeJsonFlag(i, argc, argv);
    const std::string extension = std::string(".") + tag;
    base = withoutExt(base, extension);
    if (!Loader::exists(base)) {
        return reportMissing((std::string("validate-") + tag).c_str(), label,
                             base, extension.c_str());
    }
    const auto catalog = Loader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    // A file that read as nothing is a warning rather than an error: an empty
    // catalog is a legal file, and it is also what a truncated one comes back
    // as, so saying so is the only way to tell them apart.
    if (catalog.entries.empty()) warnings.push_back("catalog has zero entries");
    const std::string okLine = check(catalog, errors, warnings);
    return reportValidation(tag, base, jsonOut, errors, warnings, okLine);
}



/// Run a format's --export-*-json handler: resolve the paths, refuse if the
/// file is not there, load it, build the JSON, write it, and say what was
/// written.
///
/// `build` is the only part that knows the format. `countLabel` is the word
/// each handler prints beside the entry count, with whatever padding it used —
/// "mechanics ", "slots   " — so the output is unchanged down to the column the
/// colon lands in.
template <typename Loader, typename Build>
int exportCatalogJson(int& i, int argc, char** argv, const char* tag, const char* label,
                      const char* countLabel, Build build) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    const std::string extension = std::string(".") + tag;
    base = withoutExt(base, extension);
    if (outPath.empty()) outPath = base + extension + ".json";
    if (!Loader::exists(base)) {
        return reportMissing((std::string("export-") + tag + "-json").c_str(), label,
                             base, extension.c_str());
    }
    const auto catalog = Loader::load(base);
    const nlohmann::json j = build(catalog);
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr, "export-%s-json: cannot write %s\n", tag, outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source    : %s%s\n", base.c_str(), extension.c_str());
    std::printf("  %s: %zu\n", countLabel, catalog.entries.size());
    return 0;
}

/// Read a flag mask that a sidecar may have written either as a number or as
/// the names joined by bars — "meat|fish|raw".
///
/// The splitting is the same in every format that does this: take up to the
/// next bar, fold it to lower case, look it up, or it in. Six importers wrote
/// that loop out, and getting the last token wrong — the one with no bar after
/// it — silently drops a flag.
///
/// `tokenToFlag` is the only part that is each format's own, and it stays
/// there: the words are that format's vocabulary and the bits are its own
/// numbering. It answers 0 for a word it does not know, which is how an
/// unrecognised name is ignored rather than guessed at.
template <typename TokenToFlag>
uint32_t flagMaskFromJson(const nlohmann::json& jv, TokenToFlag tokenToFlag) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) return jv.get<uint32_t>();
    if (!jv.is_string()) return 0;
    const std::string joined = jv.get<std::string>();
    uint32_t mask = 0;
    std::size_t pos = 0;
    while (pos < joined.size()) {
        std::size_t end = joined.find('|', pos);
        if (end == std::string::npos) end = joined.size();
        std::string token = joined.substr(pos, end - pos);
        for (char& c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        mask |= tokenToFlag(token);
        pos = end + 1;
    }
    return mask;
}

/// Ids already seen while validating, so the second one can be reported.
///
/// 84 handlers kept a std::vector and walked the whole of it for every entry,
/// which is a quadratic scan of a list that only ever answers one question. The
/// message each of them writes is its own — the field has a different name in
/// every format — so only the bookkeeping is here.
class DuplicateIdCheck {
public:
    /// True the first time an id is offered, false once it has been seen.
    bool add(uint32_t id) { return seen_.insert(id).second; }

    /// Room for this many ids up front.
    ///
    /// The callers were reserving against the std::vector this replaced and
    /// kept the call, which is still the right thing to ask — an unordered_set
    /// that knows its size ahead of time does not rehash. Its absence is what
    /// stopped the editor building, in twelve handlers at once.
    void reserve(std::size_t count) { seen_.reserve(count); }

private:
    std::unordered_set<uint32_t> seen_;
};

/// Run a format's --import-*-json handler: read the JSON, build the catalog
/// from it, write it, and say what was written.
///
/// `build` is the only part that knows the format — it turns the parsed JSON
/// into a catalog. Everything around it is the same in all 139 handlers: the
/// path arithmetic, opening the file, parsing it, the two failure messages, the
/// save and its failure message, and the three lines of summary.
template <typename Loader, typename Catalog, typename Build>
int importCatalogJson(int& i, int argc, char** argv, const char* tag,
                      const char* countLabel, Build build) {
    const std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    const std::string extension = std::string(".") + tag;
    if (outBase.empty()) outBase = baseFromJsonPath(jsonPath, extension);
    outBase = withoutExt(outBase, extension);

    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr, "import-%s-json: cannot read %s\n", tag, jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "import-%s-json: bad JSON in %s: %s\n",
                     tag, jsonPath.c_str(), e.what());
        return 1;
    }

    const Catalog c = build(j);
    if (!Loader::save(c, outBase)) {
        std::fprintf(stderr, "import-%s-json: failed to save %s%s\n",
                     tag, outBase.c_str(), extension.c_str());
        return 1;
    }
    std::printf("Wrote %s%s\n", outBase.c_str(), extension.c_str());
    std::printf("  source    : %s\n", jsonPath.c_str());
    std::printf("  %s: %zu\n", countLabel, c.entries.size());
    return 0;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
