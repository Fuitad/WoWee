#include "cli_rename_magic.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"

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

bool readMagic(const fs::path& path, char magic[4]) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    is.read(magic, 4);
    return is.gcount() == 4;
}

struct RenamePlan {
    fs::path src;
    fs::path dst;
    const FormatMagicEntry* fmt;
    bool conflict;     // dst already exists
};

int handleBulkRename(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool dryRun = false;
    bool force = false;
    while (i + 1 < argc) {
        std::string a = argv[i + 1];
        if (a == "--dry-run") { dryRun = true; ++i; }
        else if (a == "--force") { force = true; ++i; }
        else break;
    }
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "bulk-rename-by-magic: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::vector<RenamePlan> plans;
    uint64_t skippedAlreadyCorrect = 0;
    uint64_t skippedUnknown = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        char magic[4];
        if (!readMagic(entry.path(), magic)) {
            ++skippedUnknown;
            continue;
        }
        const FormatMagicEntry* fmt = findFormatByMagic(magic);
        if (!fmt) {
            ++skippedUnknown;
            continue;
        }
        fs::path src = entry.path();
        fs::path dst = src;
        dst.replace_extension(fmt->extension);
        if (src == dst) {
            ++skippedAlreadyCorrect;
            continue;
        }
        RenamePlan p;
        p.src = src; p.dst = dst; p.fmt = fmt;
        p.conflict = fs::exists(dst);
        plans.push_back(p);
    }
    uint64_t conflictCount = 0;
    for (const auto& p : plans) if (p.conflict) ++conflictCount;
    std::printf("bulk-rename-by-magic: %s%s\n",
                 dir.c_str(), dryRun ? " (dry-run)" : "");
    std::printf("  candidates       : %zu\n", plans.size());
    std::printf("  conflicts        : %llu%s\n",
                 static_cast<unsigned long long>(conflictCount),
                 force ? "" : " (skipped without --force)");
    std::printf("  already-correct  : %llu (skipped)\n",
                 static_cast<unsigned long long>(skippedAlreadyCorrect));
    std::printf("  unrecognized     : %llu (skipped)\n",
                 static_cast<unsigned long long>(skippedUnknown));
    if (plans.empty()) return 0;
    uint64_t renamed = 0;
    uint64_t skippedConflict = 0;
    uint64_t failed = 0;
    for (const auto& p : plans) {
        if (p.conflict && !force) {
            ++skippedConflict;
            std::printf("  CONFLICT: %s -> %s\n",
                         p.src.string().c_str(), p.dst.string().c_str());
            continue;
        }
        if (dryRun) {
            std::printf("  WOULD: %s -> %s\n",
                         p.src.string().c_str(), p.dst.string().c_str());
            ++renamed;
            continue;
        }
        std::error_code ec;
        fs::rename(p.src, p.dst, ec);
        if (ec) {
            std::fprintf(stderr,
                "  FAIL : %s -> %s (%s)\n",
                p.src.string().c_str(), p.dst.string().c_str(),
                ec.message().c_str());
            ++failed;
            continue;
        }
        std::printf("  OK   : %s -> %s\n",
                     p.src.string().c_str(), p.dst.string().c_str());
        ++renamed;
    }
    std::printf("  %s%llu file%s\n",
                 dryRun ? "would rename: " : "renamed: ",
                 static_cast<unsigned long long>(renamed),
                 renamed == 1 ? "" : "s");
    if (skippedConflict > 0) {
        std::printf("  skipped (conflict): %llu\n",
                     static_cast<unsigned long long>(skippedConflict));
    }
    if (failed > 0) {
        std::fprintf(stderr, "  failed: %llu\n",
                     static_cast<unsigned long long>(failed));
    }
    return failed > 0 ? 1 : 0;
}

int handleRename(int& i, int argc, char** argv) {
    std::string filePath = argv[++i];
    bool dryRun = false;
    bool force = false;
    while (i + 1 < argc) {
        std::string a = argv[i + 1];
        if (a == "--dry-run") { dryRun = true; ++i; }
        else if (a == "--force") { force = true; ++i; }
        else break;
    }
    fs::path src = filePath;
    if (!fs::exists(src) || !fs::is_regular_file(src)) {
        std::fprintf(stderr,
            "rename-by-magic: not a file: %s\n", filePath.c_str());
        return 1;
    }
    char magic[4];
    if (!readMagic(src, magic)) {
        std::fprintf(stderr,
            "rename-by-magic: cannot read 4-byte magic: %s\n",
            filePath.c_str());
        return 1;
    }
    const FormatMagicEntry* fmt = findFormatByMagic(magic);
    if (!fmt) {
        char magicStr[5] = {magic[0], magic[1], magic[2], magic[3], 0};
        std::fprintf(stderr,
            "rename-by-magic: unrecognized magic '%s' in %s\n",
            magicStr, filePath.c_str());
        return 1;
    }
    fs::path dst = src;
    dst.replace_extension(fmt->extension);
    if (src == dst) {
        std::printf("rename-by-magic: %s already has correct "
                     "extension (%s) - no change\n",
                     filePath.c_str(), fmt->extension);
        return 0;
    }
    if (fs::exists(dst) && !force) {
        std::fprintf(stderr,
            "rename-by-magic: target %s already exists "
            "(pass --force to overwrite)\n", dst.string().c_str());
        return 1;
    }
    if (dryRun) {
        std::printf("rename-by-magic (dry-run): %s -> %s\n",
                     filePath.c_str(), dst.string().c_str());
        return 0;
    }
    std::error_code ec;
    fs::rename(src, dst, ec);
    if (ec) {
        std::fprintf(stderr,
            "rename-by-magic: rename failed: %s\n",
            ec.message().c_str());
        return 1;
    }
    std::printf("rename-by-magic: %s -> %s\n",
                 filePath.c_str(), dst.string().c_str());
    return 0;
}

} // namespace

bool handleRenameMagic(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--rename-by-magic") == 0 && i + 1 < argc) {
        outRc = handleRename(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bulk-rename-by-magic") == 0 &&
        i + 1 < argc) {
        outRc = handleBulkRename(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
