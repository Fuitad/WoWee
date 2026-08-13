#include "cli_catalog_by_name.hpp"
#include "cli_arg_parse.hpp"
#include "cli_catalog_entry_key.hpp"
#include "cli_catalog_subprocess.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

struct Hit {
    fs::path path;
    std::string magic;
    uint64_t id;
    std::string entryName;
};

int handleByName(int& i, int argc, char** argv) {
    if (i + 2 >= argc) {
        std::fprintf(stderr,
            "catalog-by-name: usage: --catalog-by-name "
            "<directory> <name-substring> [--magic <WXXX>] "
            "[--ignore-case] [--json]\n");
        return 1;
    }
    std::string dir = argv[++i];
    std::string pattern = argv[++i];
    bool jsonOut = false;
    bool ignoreCase = false;
    std::string magicFilter;
    // Parse trailing flags in any order.
    while (i + 1 < argc) {
        if (std::strcmp(argv[i + 1], "--json") == 0) {
            ++i; jsonOut = true;
        } else if (std::strcmp(argv[i + 1], "--ignore-case") == 0) {
            ++i; ignoreCase = true;
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
            "catalog-by-name: not a directory: %s\n", dir.c_str());
        return 1;
    }

    std::string lcPattern = ignoreCase ? toLower(pattern) : pattern;
    std::vector<Hit> hits;
    size_t scanned = 0;

    std::error_code walkEc;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied,
        walkEc);
    fs::recursive_directory_iterator end;
    if (walkEc) {
        std::fprintf(stderr,
            "catalog-by-name: cannot open directory '%s': %s\n",
            dir.c_str(), walkEc.message().c_str());
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
        if (!fmt || !fmt->infoFlag) continue;
        if (!magicFilter.empty()) {
            std::string m(magic, 4);
            if (m != magicFilter) continue;
        }
        ++scanned;
        const auto read = readCatalogEntries(argv[0], *fmt, dirent.path());
        if (!read.ok()) continue;
        const nlohmann::json& doc = read.doc;
        for (const auto& entry : doc["entries"]) {
            if (!entry.is_object()) continue;
            if (!entry.contains("name") ||
                !entry["name"].is_string()) continue;
            std::string entryName =
                entry["name"].get<std::string>();
            std::string haystack = ignoreCase
                ? toLower(entryName) : entryName;
            if (haystack.find(lcPattern) == std::string::npos)
                continue;
            Hit h;
            h.path = dirent.path();
            h.magic = std::string(magic, 4);
            h.id = entryPrimaryKey(entry, fmt->primaryKey).value;
            h.entryName = entryName;
            hits.push_back(h);
        }
    }

    if (jsonOut) {
        nlohmann::json out;
        out["directory"] = dir;
        out["pattern"] = pattern;
        out["ignoreCase"] = ignoreCase;
        if (!magicFilter.empty()) out["magicFilter"] = magicFilter;
        out["scanned"] = scanned;
        out["hits"] = nlohmann::json::array();
        for (const auto& h : hits) {
            out["hits"].push_back({
                {"file", h.path.string()},
                {"magic", h.magic},
                {"id", h.id},
                {"name", h.entryName},
            });
        }
        std::printf("%s\n", out.dump(2).c_str());
        return hits.empty() ? 1 : 0;
    }

    std::printf("catalog-by-name: searched %zu catalog files "
                "in '%s' for name~='%s'%s",
                scanned, dir.c_str(), pattern.c_str(),
                ignoreCase ? " (case-insensitive)" : "");
    if (!magicFilter.empty()) {
        std::printf(" (magic=%s)", magicFilter.c_str());
    }
    std::printf("\n");
    if (hits.empty()) {
        std::printf("  no hits - no entry name matched the "
                    "pattern in any catalog under this tree\n");
        return 1;
    }
    std::printf("  hits (%zu):\n", hits.size());
    for (const auto& h : hits) {
        std::printf("    [%s] %s id=%llu  \"%s\"\n",
                    h.magic.c_str(), h.path.string().c_str(),
                    static_cast<unsigned long long>(h.id),
                    h.entryName.c_str());
    }
    return 0;
}

} // namespace

bool handleCatalogByName(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--catalog-by-name") == 0 &&
        i + 2 < argc) {
        outRc = handleByName(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
