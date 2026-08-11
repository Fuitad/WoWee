#include "cli_zone_packs.hpp"
#include "zone_manifest.hpp"
#include "cli_subprocess.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Parse a trailing `--seed N` flag pair. Returns updated `i` past
// the consumed args via the by-ref param. Sets seed if found.
// Returns false if it sees an unknown --flag (caller should error).
bool parseSeedFlag(int& i, int argc, char** argv,
                   const char* cmdName, uint32_t& seed) {
    for (int k = i + 1; k < argc; ++k) {
        std::string flag = argv[k];
        if (flag == "--seed" && k + 1 < argc) {
            try { seed = static_cast<uint32_t>(std::stoul(argv[++k])); }
            catch (...) {}
            i = k;
        } else if (flag.rfind("--", 0) == 0) {
            std::fprintf(stderr,
                "%s: unknown flag '%s'\n", cmdName, flag.c_str());
            return false;
        }
    }
    return true;
}

// Spawn `argv0` with argument list; suppress child stdout/stderr; return
// rc==0. Uses cli_subprocess::runChild so no shell parsing happens — the
// previous std::system path was an `argv[0]` + path concat fed to the
// shell (CodeQL cpp/command-line-injection).
bool runSilently(const std::string& argv0, const std::vector<std::string>& args) {
    return wowee::editor::cli::runChild(argv0, args, /*quiet=*/true) == 0;
}

int handleTexturePack(int& i, int argc, char** argv) {
    // Drop a starter PNG texture pack into <zoneDir>/textures/
    // by fanning out to the procedural --gen-texture-* commands.
    // Saves the user from sourcing proprietary art when bringing
    // up a new zone — six themed textures cover most needs.
    std::string zoneDir = argv[++i];
    uint32_t seed = 1;
    if (!parseSeedFlag(i, argc, argv, "gen-zone-texture-pack", seed)) return 1;
    std::filesystem::path zp(zoneDir);
    if (!std::filesystem::exists(zp / "zone.json")) {
        std::fprintf(stderr,
            "gen-zone-texture-pack: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    std::filesystem::path texDir = zp / "textures";
    std::error_code ec;
    std::filesystem::create_directories(texDir, ec);
    if (ec) {
        std::fprintf(stderr,
            "gen-zone-texture-pack: cannot create %s: %s\n",
            texDir.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::string self = (argc > 0) ? argv[0] : "wowee_editor";
    struct Cmd {
        std::string flag;
        std::string outName;
        std::vector<std::string> args;
    };
    std::vector<Cmd> jobs = {
        {"--gen-texture-noise",   "grass.png",  {"4A7C2E", "5C9B3A", "256", "256"}},
        {"--gen-texture-noise",   "dirt.png",   {"6B4A2A", "8B5E3A", "256", "256"}},
        {"--gen-texture-checker", "stone.png",  {"7A7A7A", "5A5A5A", "32", "256", "256"}},
        {"--gen-texture-brick",   "brick.png",  {"8B4513", "D3D3D3", "64", "24", "4", "256", "256"}},
        {"--gen-texture-wood",    "wood.png",   {"8B5A2B", "4A3216", "12", std::to_string(seed), "256", "256"}},
        {"--gen-texture-radial",  "water.png",  {"3A6FA0", "1E4A78", "256", "256"}},
    };
    int written = 0;
    for (const auto& job : jobs) {
        std::filesystem::path out = texDir / job.outName;
        std::vector<std::string> childArgs = {job.flag, out.string()};
        for (const auto& a : job.args) childArgs.push_back(a);
        if (!runSilently(self, childArgs)) {
            std::fprintf(stderr,
                "gen-zone-texture-pack: %s failed\n", job.flag.c_str());
        } else {
            ++written;
        }
    }
    std::printf("gen-zone-texture-pack: wrote %d of %zu textures to %s\n",
                written, jobs.size(), texDir.string().c_str());
    return written == static_cast<int>(jobs.size()) ? 0 : 1;
}

int handleMeshPack(int& i, int argc, char** argv) {
    // Companion to --gen-zone-texture-pack: drops a starter
    // WOM mesh pack into <zoneDir>/meshes/. Each rock variant
    // uses a different seed so they read as distinct boulders.
    std::string zoneDir = argv[++i];
    uint32_t seed = 1;
    if (!parseSeedFlag(i, argc, argv, "gen-zone-mesh-pack", seed)) return 1;
    std::filesystem::path zp(zoneDir);
    if (!std::filesystem::exists(zp / "zone.json")) {
        std::fprintf(stderr,
            "gen-zone-mesh-pack: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    std::filesystem::path meshDir = zp / "meshes";
    std::error_code ec;
    std::filesystem::create_directories(meshDir, ec);
    if (ec) {
        std::fprintf(stderr,
            "gen-zone-mesh-pack: cannot create %s: %s\n",
            meshDir.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::string self = (argc > 0) ? argv[0] : "wowee_editor";
    struct MeshJob {
        std::string flag;
        std::string outBase;  // no .wom extension
        std::vector<std::string> args;
    };
    std::string s0 = std::to_string(seed);
    std::string s1 = std::to_string(seed + 17);
    std::string s2 = std::to_string(seed + 41);
    std::vector<MeshJob> jobs = {
        // 3 rock variants with distinct seeds for variety
        {"--gen-mesh-rock",  "rock_small",  {"0.6", "0.30", "2", s0}},
        {"--gen-mesh-rock",  "rock_medium", {"1.2", "0.25", "2", s1}},
        {"--gen-mesh-rock",  "rock_large",  {"2.4", "0.35", "3", s2}},
        // Tree placeholder, fence segment for boundaries
        {"--gen-mesh-tree",  "tree",        {"0.15", "3.0", "1.0"}},
        {"--gen-mesh-fence", "fence",       {"5", "1.2", "1.4", "0.06"}},
    };
    int written = 0;
    for (const auto& job : jobs) {
        std::filesystem::path out = meshDir / job.outBase;
        std::vector<std::string> childArgs = {job.flag, out.string()};
        for (const auto& a : job.args) childArgs.push_back(a);
        if (!runSilently(self, childArgs)) {
            std::fprintf(stderr,
                "gen-zone-mesh-pack: %s failed\n", job.flag.c_str());
        } else {
            ++written;
        }
    }
    std::printf("gen-zone-mesh-pack: wrote %d of %zu meshes to %s\n",
                written, jobs.size(), meshDir.string().c_str());
    return written == static_cast<int>(jobs.size()) ? 0 : 1;
}

int handleZoneStarterPack(int& i, int argc, char** argv) {
    // Convenience entry point: run --gen-zone-texture-pack
    // and --gen-zone-mesh-pack in sequence so a fresh zone
    // gets a full open-format asset bootstrap from a single
    // command. Seed propagates to both children.
    std::string zoneDir = argv[++i];
    uint32_t seed = 1;
    if (!parseSeedFlag(i, argc, argv, "gen-zone-starter-pack", seed)) return 1;
    if (!std::filesystem::exists(std::filesystem::path(zoneDir) / "zone.json")) {
        std::fprintf(stderr,
            "gen-zone-starter-pack: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    std::string self = (argc > 0) ? argv[0] : "wowee_editor";
    std::string seedStr = std::to_string(seed);
    std::printf("gen-zone-starter-pack: %s (seed %u)\n",
                zoneDir.c_str(), seed);
    std::printf("  step 1/2: textures\n");
    if (wowee::editor::cli::runChild(self,
            {"--gen-zone-texture-pack", zoneDir, "--seed", seedStr}) != 0) {
        std::fprintf(stderr, "gen-zone-starter-pack: texture step failed\n");
        return 1;
    }
    std::printf("  step 2/2: meshes\n");
    if (wowee::editor::cli::runChild(self,
            {"--gen-zone-mesh-pack", zoneDir, "--seed", seedStr}) != 0) {
        std::fprintf(stderr, "gen-zone-starter-pack: mesh step failed\n");
        return 1;
    }
    std::printf("\ngen-zone-starter-pack: complete\n");
    std::printf("  zone dir : %s\n", zoneDir.c_str());
    std::printf("  textures : 6 PNGs in textures/\n");
    std::printf("  meshes   : 5 WOMs in meshes/\n");
    return 0;
}

int handleProjectStarterPack(int& i, int argc, char** argv) {
    // Project-wide bootstrap. For every zone in <projectDir>,
    // run --gen-zone-starter-pack (textures + meshes) and
    // --gen-zone-audio-pack (audio). Each zone gets a unique
    // sub-seed offset from the base seed so per-zone content
    // looks distinct.
    std::string projectDir = argv[++i];
    uint32_t seed = 1;
    if (!parseSeedFlag(i, argc, argv, "gen-project-starter-pack", seed)) return 1;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "gen-project-starter-pack: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    if (zones.empty()) {
        std::fprintf(stderr,
            "gen-project-starter-pack: %s contains no zones\n",
            projectDir.c_str());
        return 1;
    }
    std::string self = (argc > 0) ? argv[0] : "wowee_editor";
    std::printf("gen-project-starter-pack: %s (base seed %u)\n",
                projectDir.c_str(), seed);
    std::printf("  zones: %zu\n\n", zones.size());
    int passed = 0, failed = 0;
    for (size_t z = 0; z < zones.size(); ++z) {
        std::string zoneSeed = std::to_string(seed + z * 17);
        std::string name = fs::path(zones[z]).filename().string();
        std::printf("  [%zu/%zu] %s (seed %s)\n",
                    z + 1, zones.size(), name.c_str(), zoneSeed.c_str());
        bool ok1 = runSilently(self,
            {"--gen-zone-starter-pack", zones[z], "--seed", zoneSeed});
        bool ok2 = runSilently(self,
            {"--gen-zone-audio-pack", zones[z]});
        if (ok1 && ok2) {
            ++passed;
            std::printf("           OK  textures + meshes + audio\n");
        } else {
            ++failed;
            std::printf("           FAIL  starter ok=%d, audio ok=%d\n",
                        static_cast<int>(ok1), static_cast<int>(ok2));
        }
    }
    std::printf("\n  Total: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace

bool handleZonePacks(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-zone-texture-pack") == 0 && i + 1 < argc) {
        outRc = handleTexturePack(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--gen-zone-mesh-pack") == 0 && i + 1 < argc) {
        outRc = handleMeshPack(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--gen-zone-starter-pack") == 0 && i + 1 < argc) {
        outRc = handleZoneStarterPack(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--gen-project-starter-pack") == 0 && i + 1 < argc) {
        outRc = handleProjectStarterPack(i, argc, argv);
        return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
