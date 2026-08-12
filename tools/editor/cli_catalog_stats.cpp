#include "cli_catalog_stats.hpp"
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

// Probe a catalog file's header and try to walk a few of
// the entries far enough to learn the first uint32 ID
// field. Stops after a configurable cap so we don't read
// huge files unnecessarily.
struct StatsProbe {
    bool headerOk = false;
    char magic[4] = {0, 0, 0, 0};
    uint32_t version = 0;
    std::string catalogName;
    uint32_t entryCount = 0;
    uintmax_t totalBytes = 0;

    // Header byte layout:
    //   magic(4) + version(4) + nameLen(4) + name(nameLen) + entryCount(4)
    uintmax_t headerBytes = 0;
    uintmax_t entrySectionBytes = 0;
    double averageEntryBytes = 0.0;
    uint32_t catalogNameBytes = 0;

    // First few entry IDs we successfully read by reading
    // each entry's leading uint32. We can't reliably know
    // the per-entry size, so once we read 3 IDs we stop -
    // this is a sample, not an exhaustive enumeration.
    std::vector<uint32_t> firstEntryIds;
};

bool probe(const fs::path& path, StatsProbe& out) {
    std::error_code ec;
    out.totalBytes = fs::file_size(path, ec);
    if (ec) out.totalBytes = 0;
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    if (!is.read(out.magic, 4) || is.gcount() != 4) return false;
    if (!is.read(reinterpret_cast<char*>(&out.version), 4)) return false;
    uint32_t nameLen = 0;
    if (!is.read(reinterpret_cast<char*>(&nameLen), 4)) return false;
    if (nameLen > (1u << 20)) return false;
    out.catalogName.resize(nameLen);
    if (nameLen > 0) {
        is.read(out.catalogName.data(), nameLen);
        if (is.gcount() != static_cast<std::streamsize>(nameLen)) {
            out.catalogName.clear();
            return false;
        }
    }
    out.catalogNameBytes = nameLen;
    if (!is.read(reinterpret_cast<char*>(&out.entryCount), 4))
        return false;
    out.headerOk = true;
    out.headerBytes = 4 + 4 + 4 + nameLen + 4;     // magic+ver+nameLen+name+count
    if (out.totalBytes >= out.headerBytes) {
        out.entrySectionBytes = out.totalBytes - out.headerBytes;
    }
    if (out.entryCount > 0) {
        out.averageEntryBytes =
            static_cast<double>(out.entrySectionBytes) /
            static_cast<double>(out.entryCount);
    }
    // Read just the first entry's leading uint32 as a
    // reliable sample. We can't advance to subsequent
    // entries without knowing the per-format size (each
    // entry has variable-length name+description strings),
    // so multi-sampling produces garbage for most formats.
    // The first id is always at exactly headerBytes - that
    // one we can trust.
    if (out.entryCount > 0 && out.entrySectionBytes >= 4) {
        is.seekg(static_cast<std::streamoff>(out.headerBytes),
                  std::ios::beg);
        uint32_t id = 0;
        if (is.read(reinterpret_cast<char*>(&id), 4) &&
            is.gcount() == 4) {
            out.firstEntryIds.push_back(id);
        }
    }
    return true;
}

int handleStats(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "catalog-stats: file not found: %s\n", path.c_str());
        return 1;
    }
    StatsProbe p;
    if (!probe(path, p) || !p.headerOk) {
        std::fprintf(stderr,
            "catalog-stats: failed to read header from %s\n",
            path.c_str());
        return 1;
    }
    const FormatMagicEntry* fmt = findFormatByMagic(p.magic);
    if (jsonOut) {
        nlohmann::json j;
        j["path"] = path;
        char ms[5] = {p.magic[0], p.magic[1], p.magic[2],
                       p.magic[3], 0};
        j["magic"] = ms;
        if (fmt) {
            j["format"] = fmt->extension;
            j["category"] = fmt->category;
            j["description"] = fmt->description;
        } else {
            j["format"] = nullptr;
        }
        j["version"] = p.version;
        j["catalogName"] = p.catalogName;
        j["entryCount"] = p.entryCount;
        j["totalBytes"] = p.totalBytes;
        j["headerBytes"] = p.headerBytes;
        j["entrySectionBytes"] = p.entrySectionBytes;
        j["catalogNameBytes"] = p.catalogNameBytes;
        j["averageEntryBytes"] = p.averageEntryBytes;
        j["firstEntryId"] = p.firstEntryIds.empty()
                                ? 0u : p.firstEntryIds.front();
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    char ms[5] = {p.magic[0], p.magic[1], p.magic[2], p.magic[3], 0};
    std::printf("catalog-stats: %s\n", path.c_str());
    std::printf("  magic            : '%s'%s\n", ms,
                fmt ? "" : "  (unknown - not in format table)");
    if (fmt) {
        std::printf("  format           : %s (%s, %s)\n",
                    fmt->description, fmt->extension, fmt->category);
    }
    std::printf("  version          : %u\n", p.version);
    std::printf("  catalogName      : %s\n", p.catalogName.c_str());
    std::printf("  entryCount       : %u\n", p.entryCount);
    std::printf("\n");
    std::printf("  totalBytes       : %llu\n",
                static_cast<unsigned long long>(p.totalBytes));
    std::printf("  headerBytes      : %llu  (magic + version + nameLen + name + entryCount)\n",
                static_cast<unsigned long long>(p.headerBytes));
    std::printf("  entrySectionBytes: %llu\n",
                static_cast<unsigned long long>(p.entrySectionBytes));
    std::printf("  catalogNameBytes : %u\n", p.catalogNameBytes);
    if (p.entryCount > 0) {
        std::printf("  avgEntryBytes    : %.1f\n", p.averageEntryBytes);
    }
    if (!p.firstEntryIds.empty()) {
        std::printf("\n  firstEntryId     : %u\n",
                    p.firstEntryIds.front());
    }
    return 0;
}

} // namespace

bool handleCatalogStats(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--catalog-stats") == 0 && i + 1 < argc) {
        outRc = handleStats(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
