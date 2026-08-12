#include "cli_touch_tree.hpp"
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

// Per-file integrity check result.
struct TouchResult {
    fs::path path;
    const FormatMagicEntry* fmt;
    bool readMagic;
    bool readVersion;
    bool readName;
    bool readEntryCount;
    bool extensionMismatch;     // magic recognized but ext is wrong
    uint32_t version;
    uint32_t entryCount;
    std::string catalogName;
    std::string failReason;     // populated on any failure
};

// Walk the standard catalog header and report the deepest
// stage that succeeded. World/asset formats (WOM/WOB/WHM/WOT/
// WOW) are recognized by magic only - the version+name+count
// probe is skipped since their layouts differ.
TouchResult touchOne(const fs::path& path) {
    TouchResult r;
    r.path = path;
    r.fmt = nullptr;
    r.readMagic = r.readVersion = r.readName = r.readEntryCount = false;
    r.extensionMismatch = false;
    r.version = 0; r.entryCount = 0;
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        r.failReason = "cannot open file";
        return r;
    }
    char magic[4];
    if (!is.read(magic, 4) || is.gcount() != 4) {
        r.failReason = "file too short to read 4-byte magic";
        return r;
    }
    r.readMagic = true;
    r.fmt = findFormatByMagic(magic);
    if (!r.fmt) {
        char ms[5] = {magic[0], magic[1], magic[2], magic[3], 0};
        r.failReason = std::string("unrecognized magic '") + ms + "'";
        return r;
    }
    // Confirm the file's actual extension matches the format
    // - a renamed file with magic WCMS but suffix .wlot is a
    // bug worth flagging (likely the file was hand-renamed
    // away from the truth).
    std::string ext = path.extension().string();
    if (!ext.empty() && ext != r.fmt->extension) {
        r.extensionMismatch = true;
    }
    // World/asset formats stop here - their headers diverge.
    if (r.fmt->infoFlag == nullptr) {
        return r;
    }
    if (!is.read(reinterpret_cast<char*>(&r.version), 4)) {
        r.failReason = "truncated before version field";
        return r;
    }
    r.readVersion = true;
    uint32_t nameLen = 0;
    if (!is.read(reinterpret_cast<char*>(&nameLen), 4)) {
        r.failReason = "truncated before catalog-name length";
        return r;
    }
    if (nameLen > (1u << 20)) {
        r.failReason = "catalog-name length implausible (> 1MB)";
        return r;
    }
    r.catalogName.resize(nameLen);
    if (nameLen > 0) {
        if (!is.read(r.catalogName.data(), nameLen)) {
            r.failReason = "truncated within catalog-name string";
            return r;
        }
    }
    r.readName = true;
    if (!is.read(reinterpret_cast<char*>(&r.entryCount), 4)) {
        r.failReason = "truncated before entryCount field";
        return r;
    }
    r.readEntryCount = true;
    if (r.entryCount > (1u << 20)) {
        r.failReason = "entryCount implausible (> 1M entries)";
        return r;
    }
    return r;
}

bool isFailure(const TouchResult& r) {
    if (!r.failReason.empty()) return true;
    // Extension mismatch is a warning, not a failure.
    return false;
}

int handleTouch(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    bool quiet = false;
    while (i + 1 < argc) {
        std::string a = argv[i + 1];
        if (a == "--quiet") { quiet = true; ++i; }
        else break;
    }
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "touch-tree: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::vector<TouchResult> results;
    uint64_t totalFiles = 0;
    uint64_t skippedUnknown = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        ++totalFiles;
        TouchResult r = touchOne(entry.path());
        if (!r.fmt) {
            ++skippedUnknown;
            continue;
        }
        results.push_back(std::move(r));
    }
    uint64_t okCount = 0;
    uint64_t failCount = 0;
    uint64_t mismatchCount = 0;
    for (const auto& r : results) {
        if (isFailure(r)) ++failCount;
        else ++okCount;
        if (r.extensionMismatch) ++mismatchCount;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["totalFiles"] = totalFiles;
        j["recognized"] = results.size();
        j["unrecognized"] = skippedUnknown;
        j["ok"] = okCount;
        j["failed"] = failCount;
        j["extensionMismatch"] = mismatchCount;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : results) {
            char ms[5] = {r.fmt->magic[0], r.fmt->magic[1],
                           r.fmt->magic[2], r.fmt->magic[3], 0};
            nlohmann::json row;
            row["path"] = fs::relative(r.path, dir).string();
            row["magic"] = ms;
            row["extension"] = r.fmt->extension;
            row["ok"] = !isFailure(r);
            if (r.readVersion) row["version"] = r.version;
            if (r.readName)    row["catalogName"] = r.catalogName;
            if (r.readEntryCount) row["entryCount"] = r.entryCount;
            if (r.extensionMismatch) row["extensionMismatch"] = true;
            if (!r.failReason.empty()) row["failReason"] = r.failReason;
            arr.push_back(row);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return failCount > 0 ? 1 : 0;
    }
    std::printf("touch-tree: %s\n", dir.c_str());
    std::printf("  total files       : %llu\n",
                static_cast<unsigned long long>(totalFiles));
    std::printf("  recognized .w*    : %zu\n", results.size());
    std::printf("  unrecognized      : %llu (skipped)\n",
                static_cast<unsigned long long>(skippedUnknown));
    std::printf("  OK                : %llu\n",
                static_cast<unsigned long long>(okCount));
    std::printf("  FAILED            : %llu\n",
                static_cast<unsigned long long>(failCount));
    std::printf("  ext mismatch      : %llu (warning, not failure)\n",
                static_cast<unsigned long long>(mismatchCount));
    if (failCount == 0 && mismatchCount == 0) {
        if (!quiet) {
            std::printf("\n  all recognized files passed integrity check\n");
        }
        return 0;
    }
    if (!quiet) {
        for (const auto& r : results) {
            if (!isFailure(r) && !r.extensionMismatch) continue;
            char ms[5] = {r.fmt->magic[0], r.fmt->magic[1],
                           r.fmt->magic[2], r.fmt->magic[3], 0};
            const char* tag = isFailure(r) ? "FAIL" : "WARN";
            std::string failPart = isFailure(r)
                ? std::string(": ") + r.failReason
                : std::string();
            std::string mismatchPart = r.extensionMismatch
                ? std::string(" (extension mismatch - expected '") +
                  r.fmt->extension + "')"
                : std::string();
            std::printf("  [%s] %s '%s'%s%s\n",
                         tag,
                         fs::relative(r.path, dir).string().c_str(),
                         ms,
                         failPart.c_str(),
                         mismatchPart.c_str());
        }
    }
    return failCount > 0 ? 1 : 0;
}

} // namespace

bool handleTouchTree(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--touch-tree") == 0 && i + 1 < argc) {
        outRc = handleTouch(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
