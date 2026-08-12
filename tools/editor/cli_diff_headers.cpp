#include "cli_diff_headers.hpp"
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

struct HeaderInfo {
    fs::path path;
    char magic[4];
    bool magicOk = false;
    const FormatMagicEntry* fmt = nullptr;
    uint32_t version = 0;
    bool versionOk = false;
    std::string catalogName;
    bool catalogNameOk = false;
    uint32_t entryCount = 0;
    bool entryCountOk = false;
    uintmax_t fileBytes = 0;
};

bool readHeader(const fs::path& path, HeaderInfo& out) {
    out.path = path;
    if (!fs::exists(path) || !fs::is_regular_file(path)) return false;
    out.fileBytes = fs::file_size(path);
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    if (!is.read(out.magic, 4) || is.gcount() != 4) return false;
    out.magicOk = true;
    out.fmt = findFormatByMagic(out.magic);
    // Asset / world formats don't have the standard catalog
    // header, so stop after magic for those.
    if (!out.fmt || out.fmt->infoFlag == nullptr) return true;
    if (!is.read(reinterpret_cast<char*>(&out.version), 4)) return true;
    out.versionOk = true;
    uint32_t nameLen = 0;
    if (!is.read(reinterpret_cast<char*>(&nameLen), 4)) return true;
    if (nameLen > (1u << 20)) return true;
    out.catalogName.resize(nameLen);
    if (nameLen > 0) {
        if (!is.read(out.catalogName.data(), nameLen)) {
            out.catalogName.clear();
            return true;
        }
    }
    out.catalogNameOk = true;
    if (!is.read(reinterpret_cast<char*>(&out.entryCount), 4)) {
        return true;
    }
    out.entryCountOk = true;
    return true;
}

const char* sameOrDiffMarker(bool same) {
    return same ? "  =" : "  ≠";
}

int handleDiff(int& i, int argc, char** argv) {
    std::string fileA = argv[++i];
    if (i + 1 >= argc) {
        std::fprintf(stderr,
            "diff-headers: missing second file argument\n");
        return 1;
    }
    std::string fileB = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    HeaderInfo a, b;
    if (!readHeader(fileA, a)) {
        std::fprintf(stderr,
            "diff-headers: cannot read %s\n", fileA.c_str());
        return 1;
    }
    if (!readHeader(fileB, b)) {
        std::fprintf(stderr,
            "diff-headers: cannot read %s\n", fileB.c_str());
        return 1;
    }
    bool magicSame = std::memcmp(a.magic, b.magic, 4) == 0;
    bool versionSame = magicSame && a.versionOk && b.versionOk &&
                        a.version == b.version;
    bool nameSame = magicSame && a.catalogNameOk && b.catalogNameOk &&
                     a.catalogName == b.catalogName;
    bool countSame = magicSame && a.entryCountOk && b.entryCountOk &&
                      a.entryCount == b.entryCount;
    bool bytesSame = a.fileBytes == b.fileBytes;
    bool allSame = magicSame && versionSame && nameSame &&
                    countSame && bytesSame;
    if (jsonOut) {
        nlohmann::json j;
        j["fileA"] = fileA;
        j["fileB"] = fileB;
        j["magicSame"] = magicSame;
        j["versionSame"] = versionSame;
        j["catalogNameSame"] = nameSame;
        j["entryCountSame"] = countSame;
        j["bytesSame"] = bytesSame;
        j["identicalHeaders"] = allSame;
        char ma[5] = {a.magic[0], a.magic[1], a.magic[2], a.magic[3], 0};
        char mb[5] = {b.magic[0], b.magic[1], b.magic[2], b.magic[3], 0};
        j["a"] = {
            {"magic", ma},
            {"version", a.version},
            {"catalogName", a.catalogName},
            {"entryCount", a.entryCount},
            {"fileBytes", a.fileBytes},
        };
        j["b"] = {
            {"magic", mb},
            {"version", b.version},
            {"catalogName", b.catalogName},
            {"entryCount", b.entryCount},
            {"fileBytes", b.fileBytes},
        };
        std::printf("%s\n", j.dump(2).c_str());
        return allSame ? 0 : 1;
    }
    char ma[5] = {a.magic[0], a.magic[1], a.magic[2], a.magic[3], 0};
    char mb[5] = {b.magic[0], b.magic[1], b.magic[2], b.magic[3], 0};
    std::printf("diff-headers:\n  A: %s\n  B: %s\n",
                 fileA.c_str(), fileB.c_str());
    std::printf("\n");
    std::printf("  field         A                         B\n");
    std::printf("  ----------    ------------------------  ------------------------\n");
    std::printf("%s magic         '%s'%s                       '%s'\n",
                 sameOrDiffMarker(magicSame), ma,
                 (a.fmt ? "" : " (unknown)"), mb);
    if (magicSame && a.versionOk && b.versionOk) {
        std::printf("%s version       %-24u  %u\n",
                     sameOrDiffMarker(versionSame),
                     a.version, b.version);
    }
    if (magicSame && a.catalogNameOk && b.catalogNameOk) {
        std::printf("%s catalogName   %-24s  %s\n",
                     sameOrDiffMarker(nameSame),
                     a.catalogName.c_str(), b.catalogName.c_str());
    }
    if (magicSame && a.entryCountOk && b.entryCountOk) {
        std::printf("%s entryCount    %-24u  %u\n",
                     sameOrDiffMarker(countSame),
                     a.entryCount, b.entryCount);
    }
    std::printf("%s fileBytes     %-24llu  %llu\n",
                 sameOrDiffMarker(bytesSame),
                 static_cast<unsigned long long>(a.fileBytes),
                 static_cast<unsigned long long>(b.fileBytes));
    std::printf("\n  ");
    if (allSame) {
        std::printf("identical at the header level (and same byte size - "
                     "possibly byte-equal, run cmp(1) to confirm)\n");
    } else if (magicSame && versionSame && nameSame && countSame &&
                !bytesSame) {
        std::printf("same format / version / name / entry count, "
                     "but different byte sizes - entry payloads differ\n");
    } else if (!magicSame) {
        std::printf("DIFFERENT FORMATS - files are unrelated\n");
    } else {
        std::printf("header fields differ - see ≠ markers above\n");
    }
    return allSame ? 0 : 1;
}

} // namespace

bool handleDiffHeaders(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--diff-headers") == 0 && i + 2 < argc) {
        outRc = handleDiff(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
