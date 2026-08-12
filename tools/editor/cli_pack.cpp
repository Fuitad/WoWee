#include "cli_pack.hpp"

#include "content_pack.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleListWcp(int& i, int argc, char** argv) {
    // Like --info-wcp but prints every file path. Useful for spotting
    // missing or unexpected entries before unpacking.
    std::string path = argv[++i];
    wowee::editor::ContentPackInfo info;
    if (!wowee::editor::ContentPacker::readInfo(path, info)) {
        std::fprintf(stderr, "Failed to read WCP: %s\n", path.c_str());
        return 1;
    }
    std::printf("WCP: %s - %zu files\n", path.c_str(), info.files.size());
    // Sort by path so identical packs produce identical output (the
    // packer order depends on the directory_iterator implementation).
    auto files = info.files;
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    for (const auto& f : files) {
        std::printf("  %-10s %10llu  %s\n",
                    f.category.c_str(),
                    static_cast<unsigned long long>(f.size),
                    f.path.c_str());
    }
    return 0;
}

int handleInfoWcp(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    // Optional --json after the path for machine-readable output.
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::ContentPackInfo info;
    if (!wowee::editor::ContentPacker::readInfo(path, info)) {
        std::fprintf(stderr, "Failed to read WCP: %s\n", path.c_str());
        return 1;
    }
    // Per-category file totals
    std::unordered_map<std::string, size_t> byCat;
    uint64_t totalSize = 0;
    for (const auto& f : info.files) {
        byCat[f.category]++;
        totalSize += f.size;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["wcp"] = path;
        j["name"] = info.name;
        j["author"] = info.author;
        j["description"] = info.description;
        j["version"] = info.version;
        j["format"] = info.format;
        j["mapId"] = info.mapId;
        j["fileCount"] = info.files.size();
        j["totalBytes"] = totalSize;
        nlohmann::json categories = nlohmann::json::object();
        for (const auto& [cat, count] : byCat) categories[cat] = count;
        j["categories"] = categories;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCP: %s\n", path.c_str());
    std::printf("  name        : %s\n", info.name.c_str());
    std::printf("  author      : %s\n", info.author.c_str());
    std::printf("  description : %s\n", info.description.c_str());
    std::printf("  version     : %s\n", info.version.c_str());
    std::printf("  format      : %s\n", info.format.c_str());
    std::printf("  mapId       : %u\n", info.mapId);
    std::printf("  files       : %zu\n", info.files.size());
    for (const auto& [cat, count] : byCat) {
        std::printf("    %-10s : %zu\n", cat.c_str(), count);
    }
    std::printf("  total bytes : %.2f MB\n", totalSize / (1024.0 * 1024.0));
    return 0;
}

int handleInfoPackBudget(int& i, int argc, char** argv) {
    // Per-extension byte breakdown of a WCP archive. --info-wcp
    // gives counts per category; this gives bytes per extension
    // so users can spot what's bloating an archive before
    // shipping. ('Why is my pack 80MB? Oh, the .glb baked
    // outputs got included.')
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::ContentPackInfo info;
    if (!wowee::editor::ContentPacker::readInfo(path, info)) {
        std::fprintf(stderr,
            "info-pack-budget: failed to read %s\n", path.c_str());
        return 1;
    }
    // Sum bytes per extension (lower-cased).
    std::map<std::string, std::pair<int, uint64_t>> byExt;
    uint64_t totalBytes = 0;
    for (const auto& f : info.files) {
        std::string ext;
        auto dot = f.path.find_last_of('.');
        if (dot != std::string::npos) ext = f.path.substr(dot);
        else ext = "(no-ext)";
        std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c) { return std::tolower(c); });
        byExt[ext].first++;
        byExt[ext].second += f.size;
        totalBytes += f.size;
    }
    // Sort by bytes descending.
    std::vector<std::pair<std::string, std::pair<int, uint64_t>>> sorted(
        byExt.begin(), byExt.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  return a.second.second > b.second.second;
              });
    if (jsonOut) {
        nlohmann::json j;
        j["wcp"] = path;
        j["totalFiles"] = info.files.size();
        j["totalBytes"] = totalBytes;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [ext, cb] : sorted) {
            arr.push_back({{"ext", ext},
                            {"count", cb.first},
                            {"bytes", cb.second}});
        }
        j["byExtension"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCP budget: %s\n", path.c_str());
    std::printf("  total: %zu file(s), %.2f MB\n",
                info.files.size(), totalBytes / (1024.0 * 1024.0));
    std::printf("\n  ext           count        bytes      KB    share\n");
    for (const auto& [ext, cb] : sorted) {
        double pct = totalBytes > 0
            ? 100.0 * cb.second / totalBytes : 0.0;
        std::printf("  %-12s %6d  %11llu  %6.1f  %5.1f%%\n",
                    ext.c_str(), cb.first,
                    static_cast<unsigned long long>(cb.second),
                    cb.second / 1024.0, pct);
    }
    return 0;
}

int handleInfoPackTree(int& i, int argc, char** argv) {
    // Tree view of a WCP's directory layout with per-file byte
    // sizes. --list-wcp shows the flat sorted file list;
    // --info-pack-tree gives the hierarchical view that's
    // easier to read for archives with subdirectories (textures
    // under data/, models under buildings/, etc.).
    std::string path = argv[++i];
    wowee::editor::ContentPackInfo info;
    if (!wowee::editor::ContentPacker::readInfo(path, info)) {
        std::fprintf(stderr,
            "info-pack-tree: failed to read %s\n", path.c_str());
        return 1;
    }
    // Build a directory tree from flat file paths. Sub-tree
    // children are sorted alphabetically with files before dirs
    // (by-convention filesystem-tree look).
    struct Node {
        std::map<std::string, std::shared_ptr<Node>> children;
        bool isFile = false;
        uint64_t bytes = 0;
    };
    auto root = std::make_shared<Node>();
    auto split = [](const std::string& p) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : p) {
            if (c == '/' || c == '\\') {
                if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
            } else cur += c;
        }
        if (!cur.empty()) parts.push_back(cur);
        return parts;
    };
    uint64_t totalBytes = 0;
    for (const auto& f : info.files) {
        auto parts = split(f.path);
        if (parts.empty()) continue;
        Node* cur = root.get();
        for (size_t k = 0; k < parts.size(); ++k) {
            auto& child = cur->children[parts[k]];
            if (!child) child = std::make_shared<Node>();
            if (k == parts.size() - 1) {
                child->isFile = true;
                child->bytes = f.size;
            }
            cur = child.get();
        }
        totalBytes += f.size;
    }
    // Recursive renderer with box-drawing connectors. Aggregates
    // child bytes up so directories show their subtotal.
    std::function<uint64_t(const Node*, const std::string&)> render =
        [&](const Node* n, const std::string& prefix) -> uint64_t {
        size_t i = 0;
        size_t total = n->children.size();
        uint64_t subtotal = 0;
        for (const auto& [name, child] : n->children) {
            bool last = (++i == total);
            const char* branch = last ? "└─ " : "├─ ";
            const char* cont   = last ? "   " : "│  ";
            if (child->isFile) {
                std::printf("%s%s%s  (%llu bytes)\n",
                            prefix.c_str(), branch, name.c_str(),
                            static_cast<unsigned long long>(child->bytes));
                subtotal += child->bytes;
            } else {
                // Directory - recurse, then print header with subtotal.
                std::printf("%s%s%s/\n",
                            prefix.c_str(), branch, name.c_str());
                subtotal += render(child.get(), prefix + cont);
            }
        }
        return subtotal;
    };
    std::printf("%s  (%zu files, %.2f KB)\n",
                path.c_str(), info.files.size(), totalBytes / 1024.0);
    render(root.get(), "");
    return 0;
}

int handlePackWcp(int& i, int argc, char** argv) {
    // Pack a zone directory into a .wcp archive.
    // Usage: --pack-wcp <zoneDirOrName> [destPath]
    // If <zoneDirOrName> looks like a path (contains '/' or starts
    // with '.'), use it directly; otherwise resolve under
    // custom_zones/ then output/ (matching the discovery search
    // order).
    std::string nameOrDir = argv[++i];
    std::string destPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        destPath = argv[++i];
    }
    namespace fs = std::filesystem;
    std::string outputDir, mapName;
    if (nameOrDir.find('/') != std::string::npos || nameOrDir[0] == '.') {
        fs::path p = fs::absolute(nameOrDir);
        outputDir = p.parent_path().string();
        mapName = p.filename().string();
    } else {
        mapName = nameOrDir;
        if (fs::exists("custom_zones/" + mapName)) outputDir = "custom_zones";
        else if (fs::exists("output/" + mapName)) outputDir = "output";
        else {
            std::fprintf(stderr,
                "--pack-wcp: zone '%s' not found in custom_zones/ or output/\n",
                mapName.c_str());
            return 1;
        }
    }
    if (destPath.empty()) destPath = mapName + ".wcp";
    wowee::editor::ContentPackInfo info;
    info.name = mapName;
    info.format = "wcp-1.0";
    if (!wowee::editor::ContentPacker::packZone(outputDir, mapName, destPath, info)) {
        std::fprintf(stderr, "WCP pack failed for %s/%s\n",
                     outputDir.c_str(), mapName.c_str());
        return 1;
    }
    std::printf("WCP packed: %s\n", destPath.c_str());
    return 0;
}

int handleUnpackWcp(int& i, int argc, char** argv) {
    std::string wcpPath = argv[++i];
    std::string destDir = "custom_zones";
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        destDir = argv[++i];
    }
    if (!wowee::editor::ContentPacker::unpackZone(wcpPath, destDir)) {
        std::fprintf(stderr, "WCP unpack failed: %s\n", wcpPath.c_str());
        return 1;
    }
    std::printf("WCP unpacked to: %s\n", destDir.c_str());
    return 0;
}


}  // namespace

bool handlePack(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-wcp") == 0 && i + 1 < argc) {
        outRc = handleListWcp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcp") == 0 && i + 1 < argc) {
        outRc = handleInfoWcp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-pack-budget") == 0 && i + 1 < argc) {
        outRc = handleInfoPackBudget(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-pack-tree") == 0 && i + 1 < argc) {
        outRc = handleInfoPackTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--pack-wcp") == 0 && i + 1 < argc) {
        outRc = handlePackWcp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--unpack-wcp") == 0 && i + 1 < argc) {
        outRc = handleUnpackWcp(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
