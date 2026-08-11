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
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

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

private:
    std::unordered_set<uint32_t> seen_;
};

}  // namespace cli
}  // namespace editor
}  // namespace wowee
