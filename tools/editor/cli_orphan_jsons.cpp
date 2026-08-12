#include "cli_orphan_jsons.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace fs = std::filesystem;

struct Orphan {
    fs::path jsonPath;          // the sidecar with no binary
    fs::path expectedBinary;    // the binary it expected
    const FormatMagicEntry* fmt = nullptr;
};

int handleScan(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "orphan-jsons: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::vector<Orphan> orphans;
    size_t totalSidecars = 0;
    size_t pairedSidecars = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const fs::path& p = entry.path();
        // Only inspect .wXXX.json sidecars. Filename must
        // end in ".json" and the ".wXXX" before it must
        // match a known format extension.
        std::string fname = p.filename().string();
        if (fname.size() < 6) continue;
        if (fname.compare(fname.size() - 5, 5, ".json") != 0) continue;
        std::string stem = fname.substr(0, fname.size() - 5);
        size_t dot = stem.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = stem.substr(dot);
        const FormatMagicEntry* fmt = findFormatByExtension(ext.c_str());
        if (!fmt) continue;     // .json that isn't ours
        ++totalSidecars;
        // Expected binary = same path with the ".json"
        // suffix stripped (so foo.wsrg.json -> foo.wsrg).
        fs::path expectedBin = p.parent_path() / stem;
        std::error_code ec;
        if (fs::exists(expectedBin, ec)) {
            ++pairedSidecars;
            continue;
        }
        Orphan o;
        o.jsonPath = p;
        o.expectedBinary = expectedBin;
        o.fmt = fmt;
        orphans.push_back(std::move(o));
    }
    bool anyOrphans = !orphans.empty();
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["totalSidecars"] = totalSidecars;
        j["paired"] = pairedSidecars;
        j["orphans"] = orphans.size();
        j["allPaired"] = !anyOrphans;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& o : orphans) {
            arr.push_back({
                {"jsonPath", fs::relative(o.jsonPath, dir).string()},
                {"expectedBinary", fs::relative(o.expectedBinary, dir).string()},
                {"format", std::string(o.fmt->extension)},
            });
        }
        j["orphanList"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return anyOrphans ? 1 : 0;
    }
    std::printf("orphan-jsons: %s\n", dir.c_str());
    std::printf("  total sidecars : %zu\n", totalSidecars);
    std::printf("  paired         : %zu\n", pairedSidecars);
    std::printf("  orphans        : %zu\n", orphans.size());
    if (!anyOrphans) {
        std::printf("  every .wXXX.json sidecar has a matching binary\n");
        return 0;
    }
    std::printf("\n  orphan sidecars (binary missing):\n");
    for (const auto& o : orphans) {
        std::printf("    %s\n      expected: %s   [%s]\n",
                    fs::relative(o.jsonPath, dir).string().c_str(),
                    fs::relative(o.expectedBinary, dir).string().c_str(),
                    o.fmt->extension);
    }
    std::printf("\n  to repair: re-run --bulk-import-json on the "
                "directory, or remove the orphan sidecars manually\n");
    return 1;
}

} // namespace

bool handleOrphanJsons(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--orphan-jsons") == 0 && i + 1 < argc) {
        outRc = handleScan(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
