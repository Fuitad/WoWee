#include "cli_project_inventory.hpp"
#include "zone_manifest.hpp"

#include "pipeline/wowee_model.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

bool consumeJsonFlag(int& i, int argc, char** argv) {
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--json") == 0) {
        i++;
        return true;
    }
    return false;
}

// Walk every direct subdirectory of <projectDir> that contains a
// zone.json. All five handlers below need this enumeration.
std::vector<std::string> enumerateZones(const std::string& projectDir) {
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    return zones;
}

int handleProjectMeshes(int& i, int argc, char** argv) {
    // Per-zone aggregate: WOM count + total bytes/verts/tris
    // for each zone, plus a project grand total.
    std::string projectDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "list-project-meshes: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    auto zones = enumerateZones(projectDir);
    struct ZRow {
        std::string name;
        int meshCount = 0;
        uint64_t bytes = 0;
        size_t verts = 0;
        size_t tris = 0;
    };
    std::vector<ZRow> rows;
    std::error_code ec;
    for (const auto& z : zones) {
        ZRow r;
        r.name = fs::path(z).filename().string();
        fs::path meshDir = fs::path(z) / "meshes";
        if (fs::exists(meshDir)) {
            for (const auto& e : fs::recursive_directory_iterator(meshDir, ec)) {
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != ".wom") continue;
                r.meshCount++;
                r.bytes += e.file_size();
                std::string base = e.path().string();
                base = base.substr(0, base.size() - 4);
                auto wom = wowee::pipeline::WoweeModelLoader::load(base);
                r.verts += wom.vertices.size();
                r.tris += wom.indices.size() / 3;
            }
        }
        rows.push_back(std::move(r));
    }
    int totalMeshes = 0;
    uint64_t totalBytes = 0;
    size_t totalVerts = 0, totalTris = 0;
    for (const auto& r : rows) {
        totalMeshes += r.meshCount;
        totalBytes += r.bytes;
        totalVerts += r.verts;
        totalTris += r.tris;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = rows.size();
        j["totalMeshes"] = totalMeshes;
        j["totalBytes"] = totalBytes;
        j["totalVerts"] = totalVerts;
        j["totalTris"] = totalTris;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({
                {"zone", r.name},
                {"meshes", r.meshCount},
                {"bytes", r.bytes},
                {"verts", r.verts},
                {"tris", r.tris},
            });
        }
        j["zones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project meshes: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu\n", rows.size());
    std::printf("  total meshes : %d\n", totalMeshes);
    std::printf("  total bytes  : %llu\n",
                static_cast<unsigned long long>(totalBytes));
    std::printf("  total verts  : %zu\n", totalVerts);
    std::printf("  total tris   : %zu\n", totalTris);
    if (rows.empty()) {
        std::printf("  *no zones found*\n");
        return 0;
    }
    std::printf("\n  %5s %8s %8s %8s  %s\n",
                "wom", "bytes", "verts", "tris", "zone");
    for (const auto& r : rows) {
        std::printf("  %5d %8llu %8zu %8zu  %s\n",
                    r.meshCount,
                    static_cast<unsigned long long>(r.bytes),
                    r.verts, r.tris, r.name.c_str());
    }
    return 0;
}

int handleProjectMeshesDetail(int& i, int argc, char** argv) {
    // Per-mesh listing across an entire project, sorted by
    // triangle count descending. Useful for outlier detection
    // and mesh-sharing audits.
    std::string projectDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "list-project-meshes-detail: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    auto zones = enumerateZones(projectDir);
    struct Row {
        std::string zone, path;
        size_t verts, tris, bones, batches, textures;
        uint64_t bytes;
        uint32_t version;
    };
    std::vector<Row> rows;
    for (const auto& zoneDir : zones) {
        std::string zoneName = fs::path(zoneDir).filename().string();
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".wom") continue;
            std::string base = e.path().string();
            if (base.size() >= 4) base = base.substr(0, base.size() - 4);
            auto wom = wowee::pipeline::WoweeModelLoader::load(base);
            Row r;
            r.zone = zoneName;
            r.path = fs::relative(e.path(), zoneDir, ec).string();
            if (ec) r.path = e.path().filename().string();
            r.verts = wom.vertices.size();
            r.tris = wom.indices.size() / 3;
            r.bones = wom.bones.size();
            r.batches = wom.batches.size();
            r.textures = wom.texturePaths.size();
            r.bytes = e.file_size(ec);
            if (ec) r.bytes = 0;
            r.version = wom.version;
            rows.push_back(r);
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.tris > b.tris; });
    uint64_t totVerts = 0, totTris = 0, totBones = 0, totBytes = 0;
    for (const auto& r : rows) {
        totVerts += r.verts; totTris += r.tris;
        totBones += r.bones; totBytes += r.bytes;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["meshCount"] = rows.size();
        j["totals"] = {{"vertices", totVerts},
                        {"triangles", totTris},
                        {"bones", totBones},
                        {"bytes", totBytes}};
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({{"zone", r.zone},
                            {"path", r.path},
                            {"version", r.version},
                            {"vertices", r.verts},
                            {"triangles", r.tris},
                            {"bones", r.bones},
                            {"batches", r.batches},
                            {"textures", r.textures},
                            {"bytes", r.bytes}});
        }
        j["meshes"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project meshes: %s\n", projectDir.c_str());
    std::printf("  zones  : %zu\n", zones.size());
    std::printf("  meshes : %zu\n", rows.size());
    std::printf("  totals : %llu verts, %llu tris, %llu bones, %.1f KB\n",
                static_cast<unsigned long long>(totVerts),
                static_cast<unsigned long long>(totTris),
                static_cast<unsigned long long>(totBones),
                totBytes / 1024.0);
    if (rows.empty()) {
        std::printf("\n  *no .wom files in any zone*\n");
        return 0;
    }
    std::printf("\n  zone                    v    verts    tris   bones    bytes  path\n");
    for (const auto& r : rows) {
        std::printf("  %-22s v%u %6zu  %6zu  %5zu  %7llu  %s\n",
                    r.zone.substr(0, 22).c_str(),
                    r.version, r.verts, r.tris, r.bones,
                    static_cast<unsigned long long>(r.bytes),
                    r.path.c_str());
    }
    return 0;
}

int handleProjectAudio(int& i, int argc, char** argv) {
    // Per-zone WAV count + total bytes + total duration via the
    // same RIFF header parse as list-zone-audio.
    std::string projectDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "list-project-audio: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    auto zones = enumerateZones(projectDir);
    struct ZRow {
        std::string name;
        int wavCount = 0;
        uint64_t bytes = 0;
        float duration = 0.0f;
    };
    std::vector<ZRow> rows;
    std::error_code ec;
    for (const auto& z : zones) {
        ZRow r;
        r.name = fs::path(z).filename().string();
        fs::path audDir = fs::path(z) / "audio";
        if (fs::exists(audDir)) {
            for (const auto& e : fs::recursive_directory_iterator(audDir, ec)) {
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != ".wav") continue;
                r.wavCount++;
                r.bytes += e.file_size();
                FILE* f = std::fopen(e.path().string().c_str(), "rb");
                if (f) {
                    char hdr[44];
                    if (std::fread(hdr, 1, 44, f) == 44 &&
                        std::memcmp(hdr, "RIFF", 4) == 0 &&
                        std::memcmp(hdr + 8, "WAVE", 4) == 0) {
                        uint16_t channels = 0, bps = 0;
                        uint32_t rate = 0, dataBytes = 0;
                        std::memcpy(&channels, hdr + 22, 2);
                        std::memcpy(&rate, hdr + 24, 4);
                        std::memcpy(&bps, hdr + 34, 2);
                        std::memcpy(&dataBytes, hdr + 40, 4);
                        if (rate > 0 && channels > 0 && bps > 0) {
                            uint32_t bytesPerSample =
                                static_cast<uint32_t>(channels) * (bps / 8);
                            if (bytesPerSample > 0) {
                                r.duration += static_cast<float>(dataBytes) /
                                              (rate * bytesPerSample);
                            }
                        }
                    }
                    std::fclose(f);
                }
            }
        }
        rows.push_back(std::move(r));
    }
    int totalWavs = 0;
    uint64_t totalBytes = 0;
    float totalDuration = 0.0f;
    for (const auto& r : rows) {
        totalWavs += r.wavCount;
        totalBytes += r.bytes;
        totalDuration += r.duration;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = rows.size();
        j["totalWavs"] = totalWavs;
        j["totalBytes"] = totalBytes;
        j["totalDuration"] = totalDuration;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({
                {"zone", r.name},
                {"wavs", r.wavCount},
                {"bytes", r.bytes},
                {"duration", r.duration},
            });
        }
        j["zones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project audio: %s\n", projectDir.c_str());
    std::printf("  zones          : %zu\n", rows.size());
    std::printf("  total wavs     : %d\n", totalWavs);
    std::printf("  total bytes    : %llu\n",
                static_cast<unsigned long long>(totalBytes));
    std::printf("  total duration : %.2f sec\n", totalDuration);
    if (rows.empty()) {
        std::printf("  *no zones found*\n");
        return 0;
    }
    std::printf("\n  %5s %8s %8s  %s\n",
                "wavs", "bytes", "sec", "zone");
    for (const auto& r : rows) {
        std::printf("  %5d %8llu %8.2f  %s\n",
                    r.wavCount,
                    static_cast<unsigned long long>(r.bytes),
                    r.duration, r.name.c_str());
    }
    return 0;
}

int handleProjectTextures(int& i, int argc, char** argv) {
    // Per-zone WOM/texture counts + global deduped texture set
    // with usage counts. Helps answer "how many textures do I
    // need to ship across the whole project?" — texture sharing
    // across zones often makes the global set smaller than the
    // per-zone sum.
    std::string projectDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "list-project-textures: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    auto zones = enumerateZones(projectDir);
    struct ZRow {
        std::string name;
        int womCount = 0;
        int uniqueTextures = 0;
    };
    std::vector<ZRow> rows;
    std::map<std::string, int> globalHist;
    int totalWoms = 0;
    for (const auto& zoneDir : zones) {
        ZRow r;
        r.name = fs::path(zoneDir).filename().string();
        std::unordered_set<std::string> zoneSet;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".wom") continue;
            r.womCount++;
            std::string base = e.path().string();
            if (base.size() >= 4) base = base.substr(0, base.size() - 4);
            auto wom = wowee::pipeline::WoweeModelLoader::load(base);
            std::unordered_set<std::string> seenInThisWom;
            for (const auto& tp : wom.texturePaths) {
                if (tp.empty()) continue;
                if (seenInThisWom.insert(tp).second) {
                    globalHist[tp]++;
                    zoneSet.insert(tp);
                }
            }
        }
        r.uniqueTextures = static_cast<int>(zoneSet.size());
        totalWoms += r.womCount;
        rows.push_back(r);
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["totalWoms"] = totalWoms;
        j["uniqueTextures"] = globalHist.size();
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& r : rows) {
            zarr.push_back({{"name", r.name},
                            {"womCount", r.womCount},
                            {"uniqueTextures", r.uniqueTextures}});
        }
        j["zones"] = zarr;
        nlohmann::json tarr = nlohmann::json::array();
        for (const auto& [p, c] : globalHist) {
            tarr.push_back({{"path", p}, {"refCount", c}});
        }
        j["textures"] = tarr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project textures: %s\n", projectDir.c_str());
    std::printf("  zones           : %zu\n", zones.size());
    std::printf("  WOMs scanned    : %d\n", totalWoms);
    std::printf("  unique textures : %zu (deduped project-wide)\n",
                globalHist.size());
    std::printf("\n  zone                       WOMs   uniq-tex\n");
    for (const auto& r : rows) {
        std::printf("  %-26s  %4d   %7d\n",
                    r.name.substr(0, 26).c_str(),
                    r.womCount, r.uniqueTextures);
    }
    if (globalHist.empty()) {
        std::printf("\n  *no texture references*\n");
        return 0;
    }
    std::printf("\n  refs  texture path (project-global)\n");
    for (const auto& [path, count] : globalHist) {
        std::printf("  %4d  %s\n", count, path.c_str());
    }
    return 0;
}

int handleProjectSummary(int& i, int argc, char** argv) {
    // Per-zone status row (BOOTSTRAPPED / PARTIAL / EMPTY) +
    // per-category counts, plus a project total at the bottom.
    std::string projectDir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-summary: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    auto zones = enumerateZones(projectDir);
    auto scan = [](const std::string& base, const std::string& sub,
                   const std::string& ext) -> std::pair<int, uint64_t> {
        int n = 0;
        uint64_t b = 0;
        fs::path p = fs::path(base) / sub;
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
    struct ZRow {
        std::string name;
        std::string status;
        int texN = 0, mshN = 0, audN = 0;
        uint64_t bytes = 0;
    };
    std::vector<ZRow> rows;
    int bootstrapped = 0, partial = 0, empty = 0;
    uint64_t totalBytes = 0;
    int totalAssets = 0;
    for (const auto& z : zones) {
        ZRow r;
        r.name = fs::path(z).filename().string();
        auto [tn, tb] = scan(z, "textures", ".png");
        auto [mn, mb] = scan(z, "meshes", ".wom");
        auto [an, ab] = scan(z, "audio", ".wav");
        r.texN = tn; r.mshN = mn; r.audN = an;
        r.bytes = tb + mb + ab;
        if (tn > 0 && mn > 0 && an > 0) {
            r.status = "BOOTSTRAPPED";
            ++bootstrapped;
        } else if (tn + mn + an > 0) {
            r.status = "PARTIAL";
            ++partial;
        } else {
            r.status = "EMPTY";
            ++empty;
        }
        totalBytes += r.bytes;
        totalAssets += tn + mn + an;
        rows.push_back(std::move(r));
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = rows.size();
        j["bootstrapped"] = bootstrapped;
        j["partial"] = partial;
        j["empty"] = empty;
        j["totalAssets"] = totalAssets;
        j["totalBytes"] = totalBytes;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({
                {"zone", r.name},
                {"status", r.status},
                {"textures", r.texN},
                {"meshes", r.mshN},
                {"audio", r.audN},
                {"bytes", r.bytes},
            });
        }
        j["zones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu\n", rows.size());
    std::printf("  bootstrapped : %d\n", bootstrapped);
    std::printf("  partial      : %d\n", partial);
    std::printf("  empty        : %d\n", empty);
    std::printf("  total assets : %d\n", totalAssets);
    std::printf("  total bytes  : %llu\n",
                static_cast<unsigned long long>(totalBytes));
    if (rows.empty()) {
        std::printf("  *no zones found*\n");
        return 0;
    }
    std::printf("\n  %-14s %4s %4s %4s %10s  %s\n",
                "status", "tex", "msh", "aud", "bytes", "zone");
    for (const auto& r : rows) {
        std::printf("  %-14s %4d %4d %4d %10llu  %s\n",
                    r.status.c_str(), r.texN, r.mshN, r.audN,
                    static_cast<unsigned long long>(r.bytes),
                    r.name.c_str());
    }
    return 0;
}

}  // namespace

bool handleProjectInventory(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-project-meshes") == 0 && i + 1 < argc) {
        outRc = handleProjectMeshes(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--list-project-meshes-detail") == 0 && i + 1 < argc) {
        outRc = handleProjectMeshesDetail(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--list-project-audio") == 0 && i + 1 < argc) {
        outRc = handleProjectAudio(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--list-project-textures") == 0 && i + 1 < argc) {
        outRc = handleProjectTextures(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--info-project-summary") == 0 && i + 1 < argc) {
        outRc = handleProjectSummary(i, argc, argv);
        return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
