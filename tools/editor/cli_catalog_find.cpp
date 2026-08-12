#include "cli_catalog_find.hpp"
#include "cli_arg_parse.hpp"
#include "cli_catalog_entry_key.hpp"
#include "cli_catalog_subprocess.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

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

struct Hit {
    fs::path path;
    std::string magic;             // 4-char as string
    std::string primaryKeyField;
    std::string entryName;
    nlohmann::json entry;
};

int handleFind(int& i, int argc, char** argv) {
    if (i + 2 >= argc) {
        std::fprintf(stderr,
            "catalog-find: usage: --catalog-find "
            "<directory> <id> [--magic <WXXX>] [--json]\n");
        return 1;
    }
    std::string dir = argv[++i];
    std::string idArg = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    // Optional --magic <WXXX> filter to limit search to
    // one format. Useful when an id is a primary key in
    // multiple format families and you only want hits from
    // one (e.g. id 100 matches both WGRP comp 100 and
    // WSCB broadcast 100 - --magic WGRP narrows it).
    std::string magicFilter;
    while (i + 1 < argc && std::strcmp(argv[i + 1], "--magic") == 0 &&
           i + 2 < argc) {
        ++i;
        magicFilter = argv[++i];
    }

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "catalog-find: not a directory: %s\n", dir.c_str());
        return 1;
    }

    uint64_t searchId = 0;
    try {
        searchId = std::stoull(idArg);
    } catch (...) {
        std::fprintf(stderr,
            "catalog-find: <id> must be a numeric literal "
            "(got '%s')\n", idArg.c_str());
        return 1;
    }

    std::vector<Hit> hits;
    size_t scanned = 0;
    size_t skippedNoFlag = 0;
    size_t skippedUnknownMagic = 0;

    // skip_permission_denied prevents the iterator from
    // throwing on unreadable subdirectories (common when
    // walking /tmp or system trees that contain other-user
    // files). Errors are swallowed silently - catalog-find
    // is a best-effort search, not an audit.
    std::error_code walkEc;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied,
        walkEc);
    fs::recursive_directory_iterator end;
    if (walkEc) {
        std::fprintf(stderr,
            "catalog-find: cannot open directory '%s': %s\n",
            dir.c_str(), walkEc.message().c_str());
        return 1;
    }
    for (; it != end; it.increment(walkEc)) {
        if (walkEc) {
            // A subdirectory failed mid-walk; clear and
            // continue. The skip_permission_denied option
            // covers most cases but defensive code stays
            // safer.
            walkEc.clear();
            continue;
        }
        const auto& dirent = *it;
        if (!dirent.is_regular_file(walkEc)) {
            walkEc.clear();
            continue;
        }
        char magic[4]{};
        if (!peekMagic(dirent.path(), magic)) continue;
        const FormatMagicEntry* fmt = findFormatByMagic(magic);
        if (!fmt) {
            ++skippedUnknownMagic;
            continue;
        }
        if (!magicFilter.empty()) {
            std::string m(magic, 4);
            // Pad / strip trailing space - table magics
            // include space chars (e.g. "WOM ").
            if (m != magicFilter) continue;
        }
        if (!fmt->infoFlag) {
            ++skippedNoFlag;
            continue;
        }
        ++scanned;
        const auto read = readCatalogEntries(argv[0], *fmt, dirent.path());
        if (!read.ok()) continue;
        const nlohmann::json& doc = read.doc;
        for (const auto& entry : doc["entries"]) {
            const auto pk =
                entryPrimaryKey(entry, fmt->primaryKey, true);
            if (!pk.found || pk.value != searchId) continue;
            Hit h;
            h.path = dirent.path();
            h.magic = std::string(magic, 4);
            h.primaryKeyField = pk.name;
            if (entry.is_object() && entry.contains("name") &&
                entry["name"].is_string()) {
                h.entryName = entry["name"].get<std::string>();
            }
            h.entry = entry;
            hits.push_back(h);
        }
    }

    if (jsonOut) {
        nlohmann::json out;
        out["directory"] = dir;
        out["searchId"] = searchId;
        if (!magicFilter.empty()) out["magicFilter"] = magicFilter;
        out["scanned"] = scanned;
        out["hits"] = nlohmann::json::array();
        for (const auto& h : hits) {
            out["hits"].push_back({
                {"file", h.path.string()},
                {"magic", h.magic},
                {"primaryKey", h.primaryKeyField},
                {"name", h.entryName},
                {"entry", h.entry},
            });
        }
        std::printf("%s\n", out.dump(2).c_str());
        return hits.empty() ? 1 : 0;
    }

    std::printf("catalog-find: searched %zu catalog files "
                "in '%s' for id=%llu",
                scanned, dir.c_str(),
                static_cast<unsigned long long>(searchId));
    if (!magicFilter.empty()) {
        std::printf(" (magic=%s)", magicFilter.c_str());
    }
    std::printf("\n");
    if (skippedNoFlag > 0) {
        std::printf("  (skipped %zu files: format has no "
                    "--info-* surface)\n", skippedNoFlag);
    }
    if (skippedUnknownMagic > 0) {
        std::printf("  (skipped %zu files: unknown magic)\n",
                    skippedUnknownMagic);
    }
    if (hits.empty()) {
        std::printf("  no hits - id %llu is not a primary "
                    "key in any catalog under this tree\n",
                    static_cast<unsigned long long>(searchId));
        return 1;
    }
    std::printf("  hits (%zu):\n", hits.size());
    for (const auto& h : hits) {
        std::printf("    [%s] %s:%s=%llu",
                    h.magic.c_str(), h.path.string().c_str(),
                    h.primaryKeyField.c_str(),
                    static_cast<unsigned long long>(searchId));
        if (!h.entryName.empty()) {
            std::printf("  \"%s\"", h.entryName.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

} // namespace

bool handleCatalogFind(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--catalog-find") == 0 &&
        i + 2 < argc) {
        outRc = handleFind(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
