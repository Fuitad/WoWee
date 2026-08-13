#include "cli_readmes.hpp"
#include "zone_manifest.hpp"

#include "pipeline/wowee_model.hpp"

#include "cli_paths.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Parse trailing `--out <path>` flag. Returns false on unknown
// double-dash flag (caller should error out).
bool parseOutFlag(int& i, int argc, char** argv,
                  const char* cmdName, std::string& outPath) {
    for (int k = i + 1; k < argc; ++k) {
        std::string flag = argv[k];
        if (flag == "--out" && k + 1 < argc) {
            outPath = argv[++k];
            i = k;
        } else if (flag.rfind("--", 0) == 0) {
            std::fprintf(stderr,
                "%s: unknown flag '%s'\n", cmdName, flag.c_str());
            return false;
        }
    }
    return true;
}

int handleZoneReadme(int& i, int argc, char** argv) {
    // Auto-generate README.md for a zone. Writes a Markdown
    // doc summarizing zone.json metadata and itemizing every
    // texture, mesh, and audio asset (with vert/tri counts
    // for meshes and duration for WAVs).
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (!parseOutFlag(i, argc, argv, "gen-zone-readme", outPath)) return 1;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "gen-zone-readme: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/README.md";
    std::string mapName = fs::path(zoneDir).filename().string();
    std::string biome = "?";
    try {
        std::ifstream zf(zoneDir + "/zone.json");
        if (zf) {
            nlohmann::json zj;
            zf >> zj;
            if (zj.contains("mapName") && zj["mapName"].is_string())
                mapName = zj["mapName"].get<std::string>();
            if (zj.contains("biome") && zj["biome"].is_string())
                biome = zj["biome"].get<std::string>();
        }
    } catch (...) {}
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "gen-zone-readme: cannot open %s for write\n",
            outPath.c_str());
        return 1;
    }
    out << "# " << mapName << "\n\n";
    out << "Auto-generated zone manifest. Re-run `--gen-zone-readme "
        << zoneDir << "` after content changes.\n\n";
    out << "- **Biome**: " << biome << "\n";
    out << "- **Zone path**: `" << zoneDir << "`\n\n";
    // Textures
    std::vector<std::pair<std::string, uint64_t>> texList;
    fs::path texDir = fs::path(zoneDir) / "textures";
    std::error_code ec;
    if (fs::exists(texDir)) {
        for (const auto& e : fs::recursive_directory_iterator(texDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".png") continue;
            texList.push_back({fs::relative(e.path(), zoneDir).string(),
                               e.file_size()});
        }
    }
    std::sort(texList.begin(), texList.end());
    out << "## Textures (" << texList.size() << ")\n\n";
    if (texList.empty()) {
        out << "_None._\n\n";
    } else {
        out << "| File | Bytes |\n|------|-------|\n";
        for (const auto& [path, bytes] : texList) {
            out << "| `" << path << "` | " << bytes << " |\n";
        }
        out << "\n";
    }
    // Meshes
    struct MeshRow {
        std::string path;
        uint64_t bytes; size_t verts, tris, bones, batches;
    };
    std::vector<MeshRow> meshList;
    fs::path meshDir = fs::path(zoneDir) / "meshes";
    if (fs::exists(meshDir)) {
        for (const auto& womFile : findFilesByExtension(meshDir, ".wom")) {
            auto wom = wowee::pipeline::WoweeModelLoader::load(womFile.base);
            meshList.push_back({
                fs::relative(womFile.path, zoneDir).string(),
                womFile.bytes,
                wom.vertices.size(),
                wom.indices.size() / 3,
                wom.bones.size(),
                wom.batches.size(),
            });
        }
    }
    std::sort(meshList.begin(), meshList.end(),
              [](const MeshRow& a, const MeshRow& b) { return a.path < b.path; });
    out << "## Meshes (" << meshList.size() << ")\n\n";
    if (meshList.empty()) {
        out << "_None._\n\n";
    } else {
        out << "| File | Verts | Tris | Bones | Batches | Bytes |\n";
        out << "|------|-------|------|-------|---------|-------|\n";
        for (const auto& r : meshList) {
            out << "| `" << r.path << "` | " << r.verts << " | "
                << r.tris << " | " << r.bones << " | "
                << r.batches << " | " << r.bytes << " |\n";
        }
        out << "\n";
    }
    // Audio
    struct AudRow {
        std::string path;
        uint64_t bytes;
        uint32_t sampleRate;
        float duration;
    };
    std::vector<AudRow> audList;
    fs::path audDir = fs::path(zoneDir) / "audio";
    if (fs::exists(audDir)) {
        for (const auto& e : fs::recursive_directory_iterator(audDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".wav") continue;
            AudRow r{fs::relative(e.path(), zoneDir).string(),
                     e.file_size(), 0, 0.0f};
            FILE* f = std::fopen(e.path().string().c_str(), "rb");
            if (f) {
                char hdr[44];
                if (std::fread(hdr, 1, 44, f) == 44 &&
                    std::memcmp(hdr, "RIFF", 4) == 0 &&
                    std::memcmp(hdr + 8, "WAVE", 4) == 0) {
                    uint16_t channels = 0, bps = 0;
                    uint32_t dataBytes = 0;
                    std::memcpy(&channels, hdr + 22, 2);
                    std::memcpy(&r.sampleRate, hdr + 24, 4);
                    std::memcpy(&bps, hdr + 34, 2);
                    std::memcpy(&dataBytes, hdr + 40, 4);
                    if (r.sampleRate > 0 && channels > 0 && bps > 0) {
                        uint32_t bytesPerSample = channels * (bps / 8);
                        if (bytesPerSample > 0) {
                            r.duration = static_cast<float>(dataBytes) /
                                         (r.sampleRate * bytesPerSample);
                        }
                    }
                }
                std::fclose(f);
            }
            audList.push_back(std::move(r));
        }
    }
    std::sort(audList.begin(), audList.end(),
              [](const AudRow& a, const AudRow& b) { return a.path < b.path; });
    out << "## Audio (" << audList.size() << ")\n\n";
    if (audList.empty()) {
        out << "_None._\n\n";
    } else {
        out << "| File | Sample rate | Duration (s) | Bytes |\n";
        out << "|------|-------------|--------------|-------|\n";
        for (const auto& r : audList) {
            out << "| `" << r.path << "` | " << r.sampleRate
                << " Hz | " << std::fixed << std::setprecision(2)
                << r.duration << " | " << r.bytes << " |\n";
        }
        out << "\n";
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  textures : %zu\n", texList.size());
    std::printf("  meshes   : %zu\n", meshList.size());
    std::printf("  audio    : %zu\n", audList.size());
    return 0;
}

int handleProjectReadme(int& i, int argc, char** argv) {
    // Auto-generate PROJECT.md for a project. Walks every zone,
    // classifies each (BOOTSTRAPPED/PARTIAL/EMPTY), and writes a
    // Markdown table with per-zone counts and a project-level
    // rollup. Pairs with --gen-zone-readme - running both gives
    // self-documenting content at every level.
    std::string projectDir = argv[++i];
    std::string outPath;
    if (!parseOutFlag(i, argc, argv, "gen-project-readme", outPath)) return 1;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "gen-project-readme: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = projectDir + "/PROJECT.md";
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
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
        std::string name, status, biome;
        int texN, mshN, audN;
        uint64_t bytes;
    };
    std::vector<ZRow> rows;
    int totalAssets = 0;
    uint64_t totalBytes = 0;
    for (const auto& z : zones) {
        ZRow r;
        r.name = fs::path(z).filename().string();
        r.biome = "?";
        try {
            std::ifstream zf(z + "/zone.json");
            if (zf) {
                nlohmann::json zj;
                zf >> zj;
                if (zj.contains("biome") && zj["biome"].is_string())
                    r.biome = zj["biome"].get<std::string>();
            }
        } catch (...) {}
        auto [tn, tb] = scan(z, "textures", ".png");
        auto [mn, mb] = scan(z, "meshes", ".wom");
        auto [an, ab] = scan(z, "audio", ".wav");
        r.texN = tn; r.mshN = mn; r.audN = an;
        r.bytes = tb + mb + ab;
        if (tn > 0 && mn > 0 && an > 0)      r.status = "BOOTSTRAPPED";
        else if (tn + mn + an > 0)           r.status = "PARTIAL";
        else                                  r.status = "EMPTY";
        totalAssets += tn + mn + an;
        totalBytes += r.bytes;
        rows.push_back(std::move(r));
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "gen-project-readme: cannot open %s for write\n",
            outPath.c_str());
        return 1;
    }
    std::string projName = fs::path(projectDir).filename().string();
    if (projName.empty()) projName = "Project";
    out << "# " << projName << "\n\n";
    out << "Auto-generated project manifest. Re-run "
        << "`--gen-project-readme " << projectDir
        << "` after content changes.\n\n";
    out << "- **Path**: `" << projectDir << "`\n";
    out << "- **Zones**: " << rows.size() << "\n";
    out << "- **Total assets**: " << totalAssets << "\n";
    out << "- **Total bytes**: " << totalBytes << "\n\n";
    out << "## Zones\n\n";
    if (rows.empty()) {
        out << "_None._\n";
    } else {
        out << "| Zone | Status | Biome | Textures | Meshes | Audio | Bytes |\n";
        out << "|------|--------|-------|----------|--------|-------|-------|\n";
        for (const auto& r : rows) {
            out << "| `" << r.name << "` | " << r.status
                << " | " << r.biome
                << " | " << r.texN << " | " << r.mshN
                << " | " << r.audN << " | " << r.bytes << " |\n";
        }
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  zones        : %zu\n", rows.size());
    std::printf("  total assets : %d\n", totalAssets);
    std::printf("  total bytes  : %llu\n",
                static_cast<unsigned long long>(totalBytes));
    return 0;
}

}  // namespace

bool handleReadmes(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-zone-readme") == 0 && i + 1 < argc) {
        outRc = handleZoneReadme(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--gen-project-readme") == 0 && i + 1 < argc) {
        outRc = handleProjectReadme(i, argc, argv);
        return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
