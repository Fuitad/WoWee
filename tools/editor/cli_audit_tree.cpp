#include "cli_audit_tree.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace fs = std::filesystem;

// Issue categories surfaced by the audit. Order matters
// only for deterministic output - a single file may
// belong to at most one category (the worst-fit wins).
enum class IssueKind {
    TooSmall,            // file < 16 bytes - can't hold a header
    UnknownMagic,        // magic not in kFormats
    ExtensionMismatch,   // extension says X but magic says Y
    MagicWithoutExt,     // magic recognized but file has no .w* extension
    HeaderTruncated,     // header parses but truncated mid-string
};

struct Issue {
    fs::path path;
    IssueKind kind;
    std::string detail;          // human-readable extra info
    const FormatMagicEntry* expectedFmt = nullptr;
    const FormatMagicEntry* actualFmt = nullptr;
};

const char* issueKindLabel(IssueKind k) {
    switch (k) {
        case IssueKind::TooSmall:           return "too-small";
        case IssueKind::UnknownMagic:       return "unknown-magic";
        case IssueKind::ExtensionMismatch:  return "ext-mismatch";
        case IssueKind::MagicWithoutExt:    return "magic-no-ext";
        case IssueKind::HeaderTruncated:    return "header-trunc";
    }
    return "?";
}

bool extensionLooksLikeWowee(const fs::path& p) {
    std::string ext = p.extension().string();
    if (ext.size() < 2 || ext[0] != '.') return false;
    return ext[1] == 'w' || ext[1] == 'W';
}

// Read the leading 16+nameLen bytes and report whether the
// header parses cleanly. Fills magic + format on success.
struct PeekResult {
    bool readMagic = false;
    bool readHeader = false;        // magic+version+nameLen+name+entryCount all present
    char magic[4] = {0, 0, 0, 0};
    uint32_t version = 0;
    uint32_t nameLen = 0;
    uint32_t entryCount = 0;
    uintmax_t fileSize = 0;
};

PeekResult peekFile(const fs::path& path) {
    PeekResult r;
    std::error_code ec;
    r.fileSize = fs::file_size(path, ec);
    if (ec) r.fileSize = 0;
    std::ifstream is(path, std::ios::binary);
    if (!is) return r;
    if (!is.read(r.magic, 4) || is.gcount() != 4) return r;
    r.readMagic = true;
    if (!is.read(reinterpret_cast<char*>(&r.version), 4)) return r;
    if (!is.read(reinterpret_cast<char*>(&r.nameLen), 4)) return r;
    // Reject implausible name lengths up front - these usually
    // indicate the file is not actually a Wowee catalog.
    if (r.nameLen > (1u << 20)) return r;
    is.seekg(r.nameLen, std::ios::cur);
    if (!is.read(reinterpret_cast<char*>(&r.entryCount), 4)) return r;
    r.readHeader = true;
    return r;
}

int handleAudit(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "audit-tree: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::vector<Issue> issues;
    uint64_t totalFiles = 0;
    uint64_t cleanFiles = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        ++totalFiles;
        const fs::path& path = entry.path();
        std::string ext = path.extension().string();
        const FormatMagicEntry* extFmt = findFormatByExtension(ext.c_str());
        bool extLooksWowee = extensionLooksLikeWowee(path);
        // For files that don't look Wowee-related at all,
        // skip them silently - only audit candidates that
        // either have a wowee-shaped extension or actually
        // start with a known magic.
        PeekResult pr = peekFile(path);
        if (pr.fileSize < 16) {
            // Anything under 16 bytes can't even hold the
            // 4-byte magic + 4-byte version + 4-byte
            // nameLen + 4-byte entryCount minimum. Only
            // flag if the file has a wowee extension -
            // sub-16-byte unrelated files are noise.
            if (extLooksWowee) {
                Issue iss;
                iss.path = path;
                iss.kind = IssueKind::TooSmall;
                iss.detail = std::to_string(pr.fileSize) +
                             " bytes - header needs at least 16";
                iss.expectedFmt = extFmt;
                issues.push_back(std::move(iss));
            }
            continue;
        }
        const FormatMagicEntry* magicFmt = nullptr;
        if (pr.readMagic) magicFmt = findFormatByMagic(pr.magic);
        if (!magicFmt && !extLooksWowee) continue;   // not ours
        if (!magicFmt && extLooksWowee) {
            Issue iss;
            iss.path = path;
            iss.kind = IssueKind::UnknownMagic;
            char ms[5] = {pr.magic[0], pr.magic[1],
                           pr.magic[2], pr.magic[3], 0};
            // Filter non-printable characters from the
            // displayed magic to keep terminal output safe.
            for (char& c : ms) {
                if (c != 0 && (c < 0x20 || c >= 0x7F)) c = '?';
            }
            iss.detail = std::string("magic '") + ms +
                         "' not in format table";
            iss.expectedFmt = extFmt;
            issues.push_back(std::move(iss));
            continue;
        }
        if (magicFmt && !extLooksWowee) {
            Issue iss;
            iss.path = path;
            iss.kind = IssueKind::MagicWithoutExt;
            iss.detail = std::string("magic '") + magicFmt->magic[0] +
                         magicFmt->magic[1] + magicFmt->magic[2] +
                         magicFmt->magic[3] + "' detected but file " +
                         "has no .w* extension";
            iss.actualFmt = magicFmt;
            issues.push_back(std::move(iss));
            continue;
        }
        if (magicFmt && extFmt && magicFmt != extFmt) {
            Issue iss;
            iss.path = path;
            iss.kind = IssueKind::ExtensionMismatch;
            iss.detail = std::string("extension ") + extFmt->extension +
                         " says " + extFmt->category +
                         " but magic says " + magicFmt->category +
                         " (" + magicFmt->extension + ")";
            iss.expectedFmt = extFmt;
            iss.actualFmt = magicFmt;
            issues.push_back(std::move(iss));
            continue;
        }
        if (magicFmt && !pr.readHeader) {
            // Reaching here means the magic byte matched but
            // the header was truncated mid-string or before
            // entryCount. The file is corrupt.
            Issue iss;
            iss.path = path;
            iss.kind = IssueKind::HeaderTruncated;
            iss.detail = "header parses past magic but is "
                         "truncated before entryCount";
            iss.actualFmt = magicFmt;
            issues.push_back(std::move(iss));
            continue;
        }
        ++cleanFiles;
    }
    bool ok = issues.empty();
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["totalFiles"] = totalFiles;
        j["cleanFiles"] = cleanFiles;
        j["issueCount"] = issues.size();
        j["ok"] = ok;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& iss : issues) {
            arr.push_back({
                {"path", fs::relative(iss.path, dir).string()},
                {"kind", issueKindLabel(iss.kind)},
                {"detail", iss.detail},
            });
        }
        j["issues"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return ok ? 0 : 1;
    }
    std::printf("audit-tree: %s\n", dir.c_str());
    std::printf("  total files     : %llu\n",
                static_cast<unsigned long long>(totalFiles));
    std::printf("  clean wowee     : %llu\n",
                static_cast<unsigned long long>(cleanFiles));
    std::printf("  issues found    : %zu\n", issues.size());
    if (ok) {
        std::printf("  OK - no extension/magic mismatches, no truncated headers\n");
        return 0;
    }
    // Group by issue kind for readable output.
    auto printGroup = [&](IssueKind k, const char* heading) {
        size_t n = 0;
        for (const auto& iss : issues) if (iss.kind == k) ++n;
        if (n == 0) return;
        std::printf("\n  %s (%zu):\n", heading, n);
        for (const auto& iss : issues) {
            if (iss.kind != k) continue;
            std::printf("    %s\n",
                fs::relative(iss.path, dir).string().c_str());
            std::printf("        %s\n", iss.detail.c_str());
        }
    };
    printGroup(IssueKind::TooSmall,
               "Files too small to contain a header");
    printGroup(IssueKind::UnknownMagic,
               "Files with .w* extension but unrecognized magic");
    printGroup(IssueKind::ExtensionMismatch,
               "Extension/magic mismatch (renamed files?)");
    printGroup(IssueKind::MagicWithoutExt,
               "Wowee magic detected but no .w* extension");
    printGroup(IssueKind::HeaderTruncated,
               "Truncated headers (corrupted files)");
    return 1;
}

} // namespace

bool handleAuditTree(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--audit-tree") == 0 && i + 1 < argc) {
        outRc = handleAudit(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
