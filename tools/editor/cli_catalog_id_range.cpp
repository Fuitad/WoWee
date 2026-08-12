#include "cli_catalog_id_range.hpp"
#include "cli_arg_parse.hpp"
#include "cli_catalog_entry_key.hpp"
#include "cli_catalog_subprocess.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace fs = std::filesystem;

struct FileSummary {
    fs::path path;
    std::string magic;
    size_t entryCount = 0;
    uint64_t minId = 0;
    uint64_t maxId = 0;
    size_t gapCount = 0;        // missing IDs in
                                  // [minId, maxId] range
    uint64_t firstGap = 0;      // smallest unused id in
                                  // the range, 0 if none
    uint64_t recommendedNextId = 1;
};

int handleIdRange(int& i, int argc, char** argv) {
    if (i + 1 >= argc) {
        std::fprintf(stderr,
            "catalog-id-range: usage: --catalog-id-range "
            "<directory> [--magic <WXXX>] [--json]\n");
        return 1;
    }
    std::string dir = argv[++i];
    bool jsonOut = false;
    std::string magicFilter;
    while (i + 1 < argc) {
        if (std::strcmp(argv[i + 1], "--json") == 0) {
            ++i; jsonOut = true;
        } else if (std::strcmp(argv[i + 1], "--magic") == 0 &&
                   i + 2 < argc) {
            ++i;
            magicFilter = argv[++i];
        } else {
            break;
        }
    }
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "catalog-id-range: not a directory: %s\n",
            dir.c_str());
        return 1;
    }

    std::vector<FileSummary> summaries;
    size_t skipped = 0;

    std::error_code walkEc;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied,
        walkEc);
    fs::recursive_directory_iterator end;
    if (walkEc) {
        std::fprintf(stderr,
            "catalog-id-range: cannot open directory: %s\n",
            walkEc.message().c_str());
        return 1;
    }
    for (; it != end; it.increment(walkEc)) {
        if (walkEc) { walkEc.clear(); continue; }
        const auto& dirent = *it;
        if (!dirent.is_regular_file(walkEc)) {
            walkEc.clear(); continue;
        }
        char magic[4]{};
        if (!peekMagic(dirent.path(), magic)) continue;
        const FormatMagicEntry* fmt = findFormatByMagic(magic);
        if (!fmt || !fmt->infoFlag) {
            ++skipped; continue;
        }
        if (!magicFilter.empty()) {
            std::string m(magic, 4);
            if (m != magicFilter) continue;
        }
        const auto read = readCatalogEntries(argv[0], *fmt, dirent.path());
        if (!read.ok()) continue;
        const nlohmann::json& doc = read.doc;

        FileSummary s;
        s.path = dirent.path();
        s.magic = std::string(magic, 4);
        s.entryCount = doc["entries"].size();

        std::set<uint64_t> ids;
        for (const auto& entry : doc["entries"]) {
            const auto pk = entryPrimaryKey(entry, fmt->primaryKey);
            if (pk.found) ids.insert(pk.value);
        }
        if (!ids.empty()) {
            s.minId = *ids.begin();
            s.maxId = *ids.rbegin();
            // Count gaps in the [min, max] range.
            for (uint64_t k = s.minId + 1; k < s.maxId; ++k) {
                if (ids.find(k) == ids.end()) {
                    if (s.firstGap == 0) s.firstGap = k;
                    ++s.gapCount;
                }
            }
            // Recommendation: smallest gap in range if any,
            // else max+1.
            s.recommendedNextId = (s.firstGap != 0)
                ? s.firstGap : (s.maxId + 1);
        } else {
            s.recommendedNextId = 1;
        }
        summaries.push_back(s);
    }

    // Sort by path for deterministic output.
    std::sort(summaries.begin(), summaries.end(),
              [](const FileSummary& a, const FileSummary& b) {
                  return a.path < b.path;
              });

    if (jsonOut) {
        nlohmann::json out;
        out["directory"] = dir;
        if (!magicFilter.empty()) out["magicFilter"] = magicFilter;
        out["scanned"] = summaries.size();
        out["skippedNoFlag"] = skipped;
        out["files"] = nlohmann::json::array();
        for (const auto& s : summaries) {
            out["files"].push_back({
                {"path", s.path.string()},
                {"magic", s.magic},
                {"entryCount", s.entryCount},
                {"minId", s.minId},
                {"maxId", s.maxId},
                {"gapCount", s.gapCount},
                {"firstGap", s.firstGap},
                {"recommendedNextId", s.recommendedNextId},
            });
        }
        std::printf("%s\n", out.dump(2).c_str());
        return summaries.empty() ? 1 : 0;
    }

    std::printf("catalog-id-range: %zu catalog files in '%s'",
                summaries.size(), dir.c_str());
    if (!magicFilter.empty()) {
        std::printf(" (magic=%s)", magicFilter.c_str());
    }
    std::printf("\n");
    if (skipped > 0) {
        std::printf("  (skipped %zu files: no --info-* "
                    "surface)\n", skipped);
    }
    if (summaries.empty()) {
        std::printf("  no catalog files found\n");
        return 1;
    }
    std::printf("    magic   count    minId   maxId   gaps  firstGap  recNext  file\n");
    for (const auto& s : summaries) {
        std::printf("    [%s]   %4zu  %7llu  %7llu  %4zu   %7llu  %7llu  %s\n",
                    s.magic.c_str(), s.entryCount,
                    (unsigned long long)s.minId,
                    (unsigned long long)s.maxId,
                    s.gapCount,
                    (unsigned long long)s.firstGap,
                    (unsigned long long)s.recommendedNextId,
                    s.path.string().c_str());
    }
    return 0;
}

} // namespace

bool handleCatalogIdRange(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--catalog-id-range") == 0 &&
        i + 1 < argc) {
        outRc = handleIdRange(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
