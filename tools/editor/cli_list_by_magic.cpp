#include "cli_list_by_magic.hpp"
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

struct Match {
    fs::path path;
    uintmax_t size = 0;
    uint32_t entryCount = 0;
    std::string catalogName;
};

// Read magic + version + name + entryCount. Same shape as
// the helper in cli_summary_dir - kept local to avoid
// header-only utility ping-pong.
bool peekHeader(const fs::path& path, char magic[4],
                std::string& nameOut, uint32_t& entryCountOut) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    if (!is.read(magic, 4) || is.gcount() != 4) return false;
    uint32_t version = 0;
    if (!is.read(reinterpret_cast<char*>(&version), 4)) return false;
    uint32_t nameLen = 0;
    if (!is.read(reinterpret_cast<char*>(&nameLen), 4)) return false;
    if (nameLen > (1u << 20)) return false;
    nameOut.resize(nameLen);
    if (nameLen > 0) {
        is.read(nameOut.data(), nameLen);
        if (is.gcount() != static_cast<std::streamsize>(nameLen)) {
            nameOut.clear();
            return false;
        }
    }
    if (!is.read(reinterpret_cast<char*>(&entryCountOut), 4))
        return false;
    return true;
}

// Normalize a 4-character magic input. Accepts both
// "WSPL" and "wspl" (case-insensitive uppercase).
bool parseMagicArg(const char* arg, char magic[4]) {
    if (!arg) return false;
    size_t n = std::strlen(arg);
    if (n != 4) return false;
    for (int i = 0; i < 4; ++i) {
        char c = arg[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        magic[i] = c;
    }
    return true;
}

int handleList(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    std::string magicArg = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "list-by-magic: not a directory: %s\n", dir.c_str());
        return 1;
    }
    char wantedMagic[4];
    if (!parseMagicArg(magicArg.c_str(), wantedMagic)) {
        std::fprintf(stderr,
            "list-by-magic: magic must be exactly 4 characters: %s\n",
            magicArg.c_str());
        return 1;
    }
    const FormatMagicEntry* fmt = findFormatByMagic(wantedMagic);
    // fmt is allowed to be null - we still match files
    // with that magic, just won't have format metadata for
    // the report header.
    std::vector<Match> matches;
    uintmax_t totalBytes = 0;
    uint64_t totalEntries = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        char magic[4];
        std::string nameField;
        uint32_t entryCount = 0;
        if (!peekHeader(entry.path(), magic, nameField, entryCount))
            continue;
        if (std::memcmp(magic, wantedMagic, 4) != 0) continue;
        Match m;
        m.path = entry.path();
        m.size = entry.file_size();
        m.entryCount = entryCount;
        m.catalogName = nameField;
        totalBytes += m.size;
        totalEntries += entryCount;
        matches.push_back(std::move(m));
    }
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        char ms[5] = {wantedMagic[0], wantedMagic[1],
                       wantedMagic[2], wantedMagic[3], 0};
        j["magic"] = ms;
        if (fmt) {
            j["format"] = fmt->extension;
            j["category"] = fmt->category;
            j["description"] = fmt->description;
        } else {
            j["format"] = nullptr;
        }
        j["count"] = matches.size();
        j["totalBytes"] = totalBytes;
        j["totalEntries"] = totalEntries;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : matches) {
            arr.push_back({
                {"path", fs::relative(m.path, dir).string()},
                {"size", m.size},
                {"entryCount", m.entryCount},
                {"catalogName", m.catalogName},
            });
        }
        j["matches"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return matches.empty() ? 1 : 0;
    }
    char ms[5] = {wantedMagic[0], wantedMagic[1],
                   wantedMagic[2], wantedMagic[3], 0};
    std::printf("list-by-magic: '%s' in %s\n", ms, dir.c_str());
    if (fmt) {
        std::printf("  format    : %s (%s, %s)\n",
                    fmt->description, fmt->extension, fmt->category);
    } else {
        std::printf("  format    : (unknown magic - no metadata)\n");
    }
    std::printf("  matches   : %zu\n", matches.size());
    std::printf("  total     : %llu bytes, %llu entries\n",
                static_cast<unsigned long long>(totalBytes),
                static_cast<unsigned long long>(totalEntries));
    if (matches.empty()) {
        std::printf("  no files with magic '%s' under %s\n",
                    ms, dir.c_str());
        return 1;
    }
    std::printf("\n");
    std::printf("    bytes      entries  catalogName               path\n");
    for (const auto& m : matches) {
        std::printf("    %8llu   %6u   %-25s  %s\n",
                    static_cast<unsigned long long>(m.size),
                    m.entryCount,
                    m.catalogName.c_str(),
                    fs::relative(m.path, dir).string().c_str());
    }
    return 0;
}

} // namespace

bool handleListByMagic(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-by-magic") == 0 && i + 2 < argc) {
        outRc = handleList(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
