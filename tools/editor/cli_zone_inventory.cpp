#include "cli_zone_inventory.hpp"

#include "pipeline/wowee_model.hpp"

#include "cli_paths.hpp"
#include "wom_mesh_stats.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Match `[--json]` trailing flag and consume it. Returns true if
// --json was present (caller emits JSON instead of human table).
bool consumeJsonFlag(int& i, int argc, char** argv) {
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--json") == 0) {
        i++;
        return true;
    }
    return false;
}

int handleZoneMeshes(int& i, int argc, char** argv) {
    // Inventory every WOM in a zone with quick stats: file size,
    // vert/tri/bone/anim/batch counts. Companion to
    // --list-zone-textures (which counts inbound texture refs).
    std::string zoneDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "list-zone-meshes: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    struct Row : WomMeshStats {
        std::string path;
        uint64_t bytes = 0;
    };
    std::vector<Row> rows;
    std::error_code ec;
    for (const auto& womFile : findFilesByExtension(zoneDir, ".wom")) {
        Row r;
        r.path = womFile.relative;
        r.bytes = womFile.bytes;
        setMeshStats(r, wowee::pipeline::WoweeModelLoader::load(womFile.base));
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.path < b.path; });
    uint64_t totalBytes = 0;
    size_t totalVerts = 0, totalTris = 0;
    for (const auto& r : rows) {
        totalBytes += r.bytes;
        totalVerts += r.verts;
        totalTris += r.tris;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["meshCount"] = rows.size();
        j["totalBytes"] = totalBytes;
        j["totalVerts"] = totalVerts;
        j["totalTris"] = totalTris;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({
                {"path", r.path},
                {"bytes", r.bytes},
                {"verts", r.verts},
                {"tris", r.tris},
                {"bones", r.bones},
                {"anims", r.anims},
                {"batches", r.batches},
                {"textures", r.textures},
            });
        }
        j["meshes"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone meshes: %s\n", zoneDir.c_str());
    std::printf("  meshes      : %zu\n", rows.size());
    std::printf("  total bytes : %llu\n",
                static_cast<unsigned long long>(totalBytes));
    std::printf("  total verts : %zu\n", totalVerts);
    std::printf("  total tris  : %zu\n", totalTris);
    if (rows.empty()) {
        std::printf("  *no .wom files found*\n");
        return 0;
    }
    std::printf("\n  %8s %7s %7s %4s %4s %4s %4s  %s\n",
                "bytes", "verts", "tris", "bone", "anim", "batc", "tex", "path");
    for (const auto& r : rows) {
        std::printf("  %8llu %7zu %7zu %4zu %4zu %4zu %4zu  %s\n",
                    static_cast<unsigned long long>(r.bytes),
                    r.verts, r.tris,
                    r.bones, r.anims, r.batches, r.textures,
                    r.path.c_str());
    }
    return 0;
}

int handleZoneAudio(int& i, int argc, char** argv) {
    // Inventory every WAV under <zoneDir>/audio/ with stats parsed
    // from the RIFF/WAVE header: sample rate, channels, bits per
    // sample, duration. Limited to audio/ subdir to avoid walking
    // the whole zone tree.
    std::string zoneDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "list-zone-audio: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    struct Row {
        std::string path;
        uint64_t bytes = 0;
        uint32_t sampleRate = 0;
        uint16_t channels = 0;
        uint16_t bitsPerSample = 0;
        float duration = 0.0f;
        bool valid = false;
    };
    std::vector<Row> rows;
    std::error_code ec;
    fs::path audioDir = fs::path(zoneDir) / "audio";
    if (!fs::exists(audioDir)) {
        std::printf("Zone audio: %s\n", zoneDir.c_str());
        std::printf("  *no audio/ subdirectory*\n");
        return 0;
    }
    for (const auto& e : fs::recursive_directory_iterator(audioDir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".wav") continue;
        Row r;
        r.path = fs::relative(e.path(), zoneDir).string();
        r.bytes = static_cast<uint64_t>(e.file_size());
        FILE* f = std::fopen(e.path().string().c_str(), "rb");
        if (f) {
            char hdr[44];
            if (std::fread(hdr, 1, 44, f) == 44 &&
                std::memcmp(hdr, "RIFF", 4) == 0 &&
                std::memcmp(hdr + 8, "WAVE", 4) == 0 &&
                std::memcmp(hdr + 12, "fmt ", 4) == 0) {
                std::memcpy(&r.channels, hdr + 22, 2);
                std::memcpy(&r.sampleRate, hdr + 24, 4);
                std::memcpy(&r.bitsPerSample, hdr + 34, 2);
                uint32_t dataBytes = 0;
                std::memcpy(&dataBytes, hdr + 40, 4);
                if (r.sampleRate > 0 && r.channels > 0 && r.bitsPerSample > 0) {
                    uint32_t bytesPerSample =
                        static_cast<uint32_t>(r.channels) *
                        (r.bitsPerSample / 8);
                    if (bytesPerSample > 0) {
                        r.duration =
                            static_cast<float>(dataBytes) /
                            (r.sampleRate * bytesPerSample);
                    }
                    r.valid = true;
                }
            }
            std::fclose(f);
        }
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.path < b.path; });
    uint64_t totalBytes = 0;
    float totalDuration = 0.0f;
    for (const auto& r : rows) {
        totalBytes += r.bytes;
        totalDuration += r.duration;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["wavCount"] = rows.size();
        j["totalBytes"] = totalBytes;
        j["totalDuration"] = totalDuration;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({
                {"path", r.path},
                {"bytes", r.bytes},
                {"sampleRate", r.sampleRate},
                {"channels", r.channels},
                {"bitsPerSample", r.bitsPerSample},
                {"duration", r.duration},
                {"valid", r.valid},
            });
        }
        j["audio"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone audio: %s\n", zoneDir.c_str());
    std::printf("  WAVs           : %zu\n", rows.size());
    std::printf("  total bytes    : %llu\n",
                static_cast<unsigned long long>(totalBytes));
    std::printf("  total duration : %.2f sec\n", totalDuration);
    if (rows.empty()) {
        std::printf("  *no .wav files found in audio/*\n");
        return 0;
    }
    std::printf("\n  %8s %6s %4s %4s %7s  %s\n",
                "bytes", "rate", "ch", "bit", "sec", "path");
    for (const auto& r : rows) {
        if (r.valid) {
            std::printf("  %8llu %6u %4u %4u %7.2f  %s\n",
                        static_cast<unsigned long long>(r.bytes),
                        r.sampleRate,
                        static_cast<unsigned>(r.channels),
                        static_cast<unsigned>(r.bitsPerSample),
                        r.duration, r.path.c_str());
        } else {
            std::printf("  %8llu  ?      ?    ?       ?  %s (invalid header)\n",
                        static_cast<unsigned long long>(r.bytes),
                        r.path.c_str());
        }
    }
    return 0;
}

int handleZoneTextures(int& i, int argc, char** argv) {
    // Aggregate texture references across every WOM model in a
    // zone directory. Lists the textures those models pull in
    // with reference counts, useful for ship-list verification.
    std::string zoneDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "list-zone-textures: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    std::map<std::string, int> texHist;  // path -> count of WOMs that ref it
    int womCount = 0;
    std::error_code ec;
    for (const auto& womFile : findFilesByExtension(zoneDir, ".wom")) {
        womCount++;
        auto wom = wowee::pipeline::WoweeModelLoader::load(womFile.base);
        std::unordered_set<std::string> seenInThisWom;
        for (const auto& tp : wom.texturePaths) {
            if (tp.empty()) continue;
            if (seenInThisWom.insert(tp).second) {
                texHist[tp]++;
            }
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["womCount"] = womCount;
        j["uniqueTextures"] = texHist.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [path, count] : texHist) {
            arr.push_back({{"path", path}, {"refCount", count}});
        }
        j["textures"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone textures: %s\n", zoneDir.c_str());
    std::printf("  WOMs scanned    : %d\n", womCount);
    std::printf("  unique textures : %zu\n", texHist.size());
    if (texHist.empty()) {
        std::printf("  *no texture references*\n");
        return 0;
    }
    std::printf("\n  refs  path\n");
    for (const auto& [path, count] : texHist) {
        std::printf("  %4d  %s\n", count, path.c_str());
    }
    return 0;
}

int handleZoneSummary(int& i, int argc, char** argv) {
    // One-glance health digest for a zone. Combines per-category
    // counts/bytes with a quick BOOTSTRAPPED/PARTIAL/EMPTY status.
    std::string zoneDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "info-zone-summary: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    std::string mapName = "?";
    try {
        std::ifstream zf(zoneDir + "/zone.json");
        if (zf) {
            nlohmann::json zj;
            zf >> zj;
            if (zj.contains("mapName") && zj["mapName"].is_string()) {
                mapName = zj["mapName"].get<std::string>();
            }
        }
    } catch (...) { /* tolerated - leave as ? */ }
    auto scan = [&](const std::string& sub, const std::string& ext)
        -> std::pair<int, uint64_t> {
        int n = 0;
        uint64_t b = 0;
        fs::path p = fs::path(zoneDir) / sub;
        if (!fs::exists(p)) return {0, 0};
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(p, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ext) continue;
            n++;
            b += e.file_size();
        }
        return {n, b};
    };
    auto [texN, texB] = scan("textures", ".png");
    auto [mshN, mshB] = scan("meshes", ".wom");
    auto [audN, audB] = scan("audio", ".wav");
    std::string status;
    if (texN > 0 && mshN > 0 && audN > 0) status = "BOOTSTRAPPED";
    else if (texN + mshN + audN > 0) status = "PARTIAL";
    else status = "EMPTY";
    uint64_t totalBytes = texB + mshB + audB;
    int totalAssets = texN + mshN + audN;
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["mapName"] = mapName;
        j["status"] = status;
        j["totalAssets"] = totalAssets;
        j["totalBytes"] = totalBytes;
        j["textures"] = {{"count", texN}, {"bytes", texB}};
        j["meshes"]   = {{"count", mshN}, {"bytes", mshB}};
        j["audio"]    = {{"count", audN}, {"bytes", audB}};
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone: %s (%s)\n", mapName.c_str(), zoneDir.c_str());
    std::printf("  status   : %s\n", status.c_str());
    std::printf("  textures : %d (%llu bytes)\n",
                texN, static_cast<unsigned long long>(texB));
    std::printf("  meshes   : %d (%llu bytes)\n",
                mshN, static_cast<unsigned long long>(mshB));
    std::printf("  audio    : %d (%llu bytes)\n",
                audN, static_cast<unsigned long long>(audB));
    std::printf("  TOTAL    : %d assets, %llu bytes\n",
                totalAssets,
                static_cast<unsigned long long>(totalBytes));
    return 0;
}

}  // namespace

bool handleZoneInventory(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-zone-meshes") == 0 && i + 1 < argc) {
        outRc = handleZoneMeshes(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--list-zone-audio") == 0 && i + 1 < argc) {
        outRc = handleZoneAudio(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--list-zone-textures") == 0 && i + 1 < argc) {
        outRc = handleZoneTextures(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--info-zone-summary") == 0 && i + 1 < argc) {
        outRc = handleZoneSummary(i, argc, argv);
        return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
