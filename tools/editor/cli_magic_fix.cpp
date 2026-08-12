#include "cli_magic_fix.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"
#include "cli_paths.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace fs = std::filesystem;

struct ProposedRename {
    fs::path from;
    fs::path to;
    const FormatMagicEntry* fmt = nullptr;
    bool collision = false;        // 'to' already exists
    std::string reason;            // ext-mismatch / magic-no-ext
};

// Match an extension against the format table case-
// insensitively. Mirrors cli_audit_tree's helper - kept
// local to avoid a header-only utility ping-pong.
const FormatMagicEntry* findFormatByExtension(const std::string& ext) {
    if (ext.empty()) return nullptr;
    for (const FormatMagicEntry* p = formatTableBegin();
         p != formatTableEnd(); ++p) {
        const char* a = p->extension;
        const char* b = ext.c_str();
        bool match = true;
        while (*a && *b) {
            char ca = *a; char cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) { match = false; break; }
            ++a; ++b;
        }
        if (match && *a == 0 && *b == 0) return p;
    }
    return nullptr;
}

// Build the destination path: same parent + filename stem
// + the magic-correct extension. If the source already has
// an extension, replace it; otherwise append.
fs::path proposeRenameTarget(const fs::path& from,
                              const FormatMagicEntry* fmt) {
    fs::path target = from;
    target.replace_extension(fmt->extension);
    return target;
}

int handleFix(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    bool apply = false;
    // Walk remaining args for --apply (separate from --json
    // so the two don't collide in option parsing).
    for (int k = i + 1; k < argc; ++k) {
        if (std::strcmp(argv[k], "--apply") == 0) {
            apply = true;
            // remove --apply from the arg list so the
            // outer dispatch loop doesn't try to handle it
            for (int m = k; m + 1 < argc; ++m) argv[m] = argv[m + 1];
            --argc;
            break;
        }
    }
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "magic-fix: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::vector<ProposedRename> proposals;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const fs::path& path = entry.path();
        char magic[4] = {0, 0, 0, 0};
        if (!peekMagic(path, magic)) continue;
        const FormatMagicEntry* magicFmt = findFormatByMagic(magic);
        if (!magicFmt) continue;       // unknown magic - leave alone
        std::string ext = path.extension().string();
        const FormatMagicEntry* extFmt = findFormatByExtension(ext);
        if (extFmt == magicFmt) continue;   // already matches
        // Either the extension is wrong or absent - propose
        // a rename to the canonical extension for this magic.
        ProposedRename pr;
        pr.from = path;
        pr.to = proposeRenameTarget(path, magicFmt);
        pr.fmt = magicFmt;
        pr.reason = extFmt ? "ext-mismatch" : "magic-no-ext";
        // Refuse to overwrite - flag collision so the user
        // can resolve manually. fs::exists() is cheap; the
        // walk visits each file once.
        std::error_code ec;
        if (fs::exists(pr.to, ec) && pr.to != pr.from) {
            pr.collision = true;
        }
        proposals.push_back(std::move(pr));
    }
    size_t applied = 0;
    size_t skipped = 0;
    if (apply) {
        for (auto& pr : proposals) {
            if (pr.collision) { ++skipped; continue; }
            std::error_code ec;
            fs::rename(pr.from, pr.to, ec);
            if (ec) {
                ++skipped;
                if (!jsonOut) {
                    std::fprintf(stderr,
                        "magic-fix: rename failed for %s -> %s: %s\n",
                        pr.from.string().c_str(),
                        pr.to.string().c_str(),
                        ec.message().c_str());
                }
                continue;
            }
            ++applied;
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["proposals"] = nlohmann::json::array();
        for (const auto& pr : proposals) {
            j["proposals"].push_back({
                {"from", fs::relative(pr.from, dir).string()},
                {"to", fs::relative(pr.to, dir).string()},
                {"reason", pr.reason},
                {"collision", pr.collision},
            });
        }
        j["proposalCount"] = proposals.size();
        j["applied"] = applied;
        j["skipped"] = skipped;
        j["dryRun"] = !apply;
        std::printf("%s\n", j.dump(2).c_str());
        return proposals.empty() ? 0 : (apply && skipped == 0 ? 0 : 1);
    }
    std::printf("magic-fix: %s\n", dir.c_str());
    std::printf("  mode           : %s\n",
                apply ? "APPLY (renaming)" : "dry-run (--apply to commit)");
    std::printf("  proposals      : %zu\n", proposals.size());
    if (apply) {
        std::printf("  applied        : %zu\n", applied);
        std::printf("  skipped        : %zu\n", skipped);
    }
    if (proposals.empty()) {
        std::printf("  no extension/magic mismatches found - tree is clean\n");
        return 0;
    }
    std::printf("\n");
    for (const auto& pr : proposals) {
        const char* mark = pr.collision ? "!" : " ";
        std::printf("  %s %s\n      -> %s   [%s]\n",
                    mark,
                    fs::relative(pr.from, dir).string().c_str(),
                    fs::relative(pr.to, dir).string().c_str(),
                    pr.collision ? "COLLISION (target exists, skipped)"
                                 : pr.reason.c_str());
    }
    if (!apply) {
        std::printf("\n  re-run with --apply to commit these renames\n");
    }
    return apply && skipped == 0 ? 0 : 1;
}

} // namespace

bool handleMagicFix(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--magic-fix") == 0 && i + 1 < argc) {
        outRc = handleFix(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
