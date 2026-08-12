#include "cli_summary_dir.hpp"
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

// Per-format counters populated as we walk a directory.
struct FormatBucket {
    const FormatMagicEntry* fmt = nullptr;
    uint64_t fileCount = 0;
    uint64_t totalEntries = 0;
    uint64_t totalBytes = 0;
};

// Read magic+version+name+entryCount from a candidate file.
// Returns true on success and fills out entryCount; the
// version+name fields are skipped - we only need the count.
bool peekEntryCount(const fs::path& path, char magic[4],
                    uint32_t& entryCountOut) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    if (!is.read(magic, 4) || is.gcount() != 4) return false;
    uint32_t version = 0;
    if (!is.read(reinterpret_cast<char*>(&version), 4)) return false;
    uint32_t nameLen = 0;
    if (!is.read(reinterpret_cast<char*>(&nameLen), 4)) return false;
    if (nameLen > (1u << 20)) return false;
    is.seekg(nameLen, std::ios::cur);
    if (!is.read(reinterpret_cast<char*>(&entryCountOut), 4)) return false;
    return true;
}

int handleSummary(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "summary-dir: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::vector<FormatBucket> buckets;
    buckets.reserve(formatTableSize());
    for (const FormatMagicEntry* p = formatTableBegin();
         p != formatTableEnd(); ++p) {
        FormatBucket b;
        b.fmt = p;
        buckets.push_back(b);
    }
    uint64_t totalFiles = 0;
    uint64_t totalUnknown = 0;
    uint64_t totalBytes = 0;
    std::vector<std::string> unknownFiles;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        ++totalFiles;
        char magic[4];
        uint32_t entryCount = 0;
        bool readOk = peekEntryCount(entry.path(), magic, entryCount);
        const FormatMagicEntry* fmt = nullptr;
        if (readOk) fmt = findFormatByMagic(magic);
        if (!fmt) {
            ++totalUnknown;
            // Only collect the first 20 unknown files to
            // keep the output manageable on large dirs.
            if (unknownFiles.size() < 20) {
                unknownFiles.push_back(
                    fs::relative(entry.path(), dir).string());
            }
            continue;
        }
        uintmax_t fileSize = entry.file_size();
        totalBytes += fileSize;
        for (auto& b : buckets) {
            if (b.fmt == fmt) {
                ++b.fileCount;
                // Only catalog formats (those with an
                // --info-* flag) follow the standard
                // header - for asset/world formats we
                // still count the file but leave entries
                // at 0.
                if (b.fmt->infoFlag != nullptr) {
                    b.totalEntries += entryCount;
                }
                b.totalBytes += fileSize;
                break;
            }
        }
    }
    uint64_t recognized = totalFiles - totalUnknown;
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["totalFiles"] = totalFiles;
        j["recognized"] = recognized;
        j["unknown"] = totalUnknown;
        j["totalBytes"] = totalBytes;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& b : buckets) {
            if (b.fileCount == 0) continue;
            char magicStr[5] = {b.fmt->magic[0], b.fmt->magic[1],
                                 b.fmt->magic[2], b.fmt->magic[3], 0};
            arr.push_back({
                {"magic", magicStr},
                {"extension", b.fmt->extension},
                {"category", b.fmt->category},
                {"description", b.fmt->description},
                {"fileCount", b.fileCount},
                {"totalEntries", b.totalEntries},
                {"totalBytes", b.totalBytes},
            });
        }
        j["formats"] = arr;
        if (!unknownFiles.empty()) {
            j["unknownSample"] = unknownFiles;
        }
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("summary-dir: %s\n", dir.c_str());
    std::printf("  total files       : %llu\n",
                static_cast<unsigned long long>(totalFiles));
    std::printf("  recognized .w*    : %llu\n",
                static_cast<unsigned long long>(recognized));
    std::printf("  unrecognized      : %llu\n",
                static_cast<unsigned long long>(totalUnknown));
    std::printf("  recognized bytes  : %llu\n",
                static_cast<unsigned long long>(totalBytes));
    if (recognized == 0) {
        std::printf("  (no Wowee open format files found)\n");
        return 0;
    }
    std::printf("\n");
    std::printf("    magic    ext      category     files    entries    bytes\n");
    std::printf("    -------  -------  -----------  ------   --------   --------\n");
    for (const auto& b : buckets) {
        if (b.fileCount == 0) continue;
        char magicStr[5] = {b.fmt->magic[0], b.fmt->magic[1],
                             b.fmt->magic[2], b.fmt->magic[3], 0};
        std::printf("    %-7s  %-7s  %-11s  %5llu    %7llu    %8llu\n",
                    magicStr, b.fmt->extension, b.fmt->category,
                    static_cast<unsigned long long>(b.fileCount),
                    static_cast<unsigned long long>(b.totalEntries),
                    static_cast<unsigned long long>(b.totalBytes));
    }
    if (!unknownFiles.empty()) {
        std::printf("\n  sample of unrecognized files (up to 20):\n");
        for (const auto& f : unknownFiles) {
            std::printf("    - %s\n", f.c_str());
        }
    }
    return 0;
}

} // namespace

bool handleSummaryDir(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--summary-dir") == 0 && i + 1 < argc) {
        outRc = handleSummary(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
