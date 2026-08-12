#include "cli_zone_export.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "wowee_terrain.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/adt_loader.hpp"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleExportPng(int& i, int argc, char** argv) {
    // Render heightmap, normal-map, and zone-map PNG previews for a
    // terrain. Useful for portfolio screenshots, ground-truth map
    // comparison, and quick visual validation without launching GUI.
    std::string base = argv[++i];
    for (const char* ext : {".wot", ".whm"}) {
        if (base.size() >= 4 && base.substr(base.size() - 4) == ext) {
            base = base.substr(0, base.size() - 4);
            break;
        }
    }
    if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) {
        std::fprintf(stderr, "WOT/WHM not found at base: %s\n", base.c_str());
        return 1;
    }
    wowee::pipeline::ADTTerrain terrain;
    if (!wowee::pipeline::WoweeTerrainLoader::load(base, terrain)) {
        std::fprintf(stderr, "Failed to load terrain: %s\n", base.c_str());
        return 1;
    }
    wowee::editor::WoweeTerrain::exportHeightmapPreview(terrain, base + "_heightmap.png");
    wowee::editor::WoweeTerrain::exportNormalMap(terrain, base + "_normals.png");
    wowee::editor::WoweeTerrain::exportZoneMap(terrain, base + "_zone.png", 512);
    std::printf("Exported PNGs: %s_{heightmap,normals,zone}.png\n", base.c_str());
    return 0;
}

int handleExportZoneDepsMd(int& i, int argc, char** argv) {
    // Markdown counterpart to --list-zone-deps. Writes a sortable
    // GitHub-rendered table of every external model the zone
    // references plus on-disk presence (so PR reviewers see at a
    // glance whether dependencies are accounted for in the
    // accompanying asset bundle).
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "export-zone-deps-md: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    zm.load(zoneDir + "/zone.json");
    if (outPath.empty()) outPath = zoneDir + "/DEPS.md";
    // Same dep-collection pass as --list-zone-deps.
    std::map<std::string, int> directM2;
    std::map<std::string, int> directWMO;
    std::map<std::string, int> doodadM2;
    wowee::editor::ObjectPlacer op;
    if (op.loadFromFile(zoneDir + "/objects.json")) {
        for (const auto& o : op.getObjects()) {
            if (o.type == wowee::editor::PlaceableType::M2)  directM2[o.path]++;
            else if (o.type == wowee::editor::PlaceableType::WMO) directWMO[o.path]++;
        }
    }
    int wobCount = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file() ||
            e.path().extension() != ".wob") continue;
        wobCount++;
        std::string base = e.path().string();
        if (base.size() >= 4) base = base.substr(0, base.size() - 4);
        auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
        for (const auto& d : bld.doodads) {
            if (!d.modelPath.empty()) doodadM2[d.modelPath]++;
        }
    }
    // Resolve dep on disk. Same heuristic as --check-zone-refs:
    // try both open + proprietary in conventional roots.
    auto stripExt = [](const std::string& p, const char* ext) {
        size_t n = std::strlen(ext);
        if (p.size() >= n) {
            std::string tail = p.substr(p.size() - n);
            std::string lower = tail;
            for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
            if (lower == ext) return p.substr(0, p.size() - n);
        }
        return p;
    };
    auto resolveStatus = [&](const std::string& path, bool isWMO) {
        std::string base, openExt, propExt;
        if (isWMO) {
            base = stripExt(path, ".wmo");
            openExt = ".wob"; propExt = ".wmo";
        } else {
            base = stripExt(path, ".m2");
            openExt = ".wom"; propExt = ".m2";
        }
        std::vector<std::string> roots = {
            "", zoneDir + "/", "output/", "custom_zones/", "Data/"
        };
        bool hasOpen = false, hasProp = false;
        for (const auto& root : roots) {
            if (fs::exists(root + base + openExt)) hasOpen = true;
            if (fs::exists(root + base + propExt)) hasProp = true;
        }
        if (hasOpen && hasProp) return "open + proprietary";
        if (hasOpen) return "open only";
        if (hasProp) return "proprietary only";
        return "MISSING";
    };
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-zone-deps-md: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << "# Dependencies - " <<
        (zm.displayName.empty() ? zm.mapName : zm.displayName) << "\n\n";
    out << "*Auto-generated by `wowee_editor --export-zone-deps-md`. "
           "Status is best-effort - checks zone-local, output/, "
           "custom_zones/, Data/ roots in that order.*\n\n";
    auto emitTable = [&](const char* heading,
                          const std::map<std::string,int>& m,
                          bool isWMO) {
        out << "## " << heading << " (" << m.size() << ")\n\n";
        if (m.empty()) {
            out << "*None.*\n\n";
            return;
        }
        out << "| Refs | Path | Status |\n";
        out << "|---:|---|---|\n";
        for (const auto& [path, count] : m) {
            out << "| " << count << " | `" << path << "` | "
                << resolveStatus(path, isWMO) << " |\n";
        }
        out << "\n";
    };
    emitTable("Direct M2 placements",  directM2,  false);
    emitTable("Direct WMO placements", directWMO, true);
    emitTable("WOB doodad M2 refs",    doodadM2,  false);
    out << "## Summary\n\n";
    out << "- Zone: `" << zm.mapName << "`\n";
    out << "- WOBs scanned: " << wobCount << "\n";
    out << "- Unique dependencies: " <<
        directM2.size() + directWMO.size() + doodadM2.size() << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  %zu M2 placements, %zu WMO placements, %zu WOB doodad refs\n",
                directM2.size(), directWMO.size(), doodadM2.size());
    return 0;
}

int handleExportZoneSpawnPng(int& i, int argc, char** argv) {
    // Top-down PNG of spawn positions colored by type. Bound by
    // the zone's tile range so the image is properly framed.
    // Useful for design review (does the spawn distribution
    // match the intended encounter design?) and for showing
    // collaborators 'where are the mobs'.
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "export-zone-spawn-png: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "export-zone-spawn-png: parse failed\n");
        return 1;
    }
    if (zm.tiles.empty()) {
        std::fprintf(stderr, "export-zone-spawn-png: zone has no tiles\n");
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/" + zm.mapName + "_spawns.png";
    // Compute world-space bounds from manifest tiles. Same math
    // as --info-zone-extents.
    constexpr float kTileSize = 533.33333f;
    int tileMinX = 64, tileMaxX = -1;
    int tileMinY = 64, tileMaxY = -1;
    for (const auto& [tx, ty] : zm.tiles) {
        tileMinX = std::min(tileMinX, tx);
        tileMaxX = std::max(tileMaxX, tx);
        tileMinY = std::min(tileMinY, ty);
        tileMaxY = std::max(tileMaxY, ty);
    }
    float worldMinX = (32.0f - tileMaxY - 1) * kTileSize;
    float worldMaxX = (32.0f - tileMinY)     * kTileSize;
    float worldMinY = (32.0f - tileMaxX - 1) * kTileSize;
    float worldMaxY = (32.0f - tileMinX)     * kTileSize;
    // Image dimensions: 256px per tile so detail is visible
    // without inflating per-pixel cost.
    int tilesX = tileMaxY - tileMinY + 1;  // tile.y maps to world.x
    int tilesY = tileMaxX - tileMinX + 1;
    const int kPxPerTile = 256;
    int imgW = tilesX * kPxPerTile;
    int imgH = tilesY * kPxPerTile;
    // Cap output size - 16-tile-wide projects shouldn't exceed
    // 4096 wide. Scale down if needed.
    int maxDim = std::max(imgW, imgH);
    if (maxDim > 4096) {
        int divisor = (maxDim + 4095) / 4096;
        imgW = std::max(64, imgW / divisor);
        imgH = std::max(64, imgH / divisor);
    }
    std::vector<uint8_t> img(imgW * imgH * 3, 32);  // dark grey background
    // Tile-grid lines so the boundary is visible.
    for (int t = 1; t < tilesX; ++t) {
        int x = (t * imgW) / tilesX;
        if (x >= 0 && x < imgW) {
            for (int y = 0; y < imgH; ++y) {
                size_t off = (y * imgW + x) * 3;
                img[off] = img[off+1] = img[off+2] = 64;
            }
        }
    }
    for (int t = 1; t < tilesY; ++t) {
        int y = (t * imgH) / tilesY;
        if (y >= 0 && y < imgH) {
            for (int x = 0; x < imgW; ++x) {
                size_t off = (y * imgW + x) * 3;
                img[off] = img[off+1] = img[off+2] = 64;
            }
        }
    }
    // Plot spawn points. Map world (X, Y) to image (px, py):
    //   px = (worldMaxX - X) / (worldMaxX - worldMinX) * imgW
    //   py = (worldMaxY - Y) / (worldMaxY - worldMinY) * imgH
    // since +X world is north (up) and +Y world is west (left)
    // in WoW coords.
    float wRangeX = worldMaxX - worldMinX;
    float wRangeY = worldMaxY - worldMinY;
    auto plotPoint = [&](float wx, float wy, uint8_t r, uint8_t g, uint8_t b) {
        if (wRangeX <= 0 || wRangeY <= 0) return;
        int px = static_cast<int>((worldMaxX - wx) / wRangeX * imgW);
        int py = static_cast<int>((worldMaxY - wy) / wRangeY * imgH);
        // 3×3 dot.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int x = px + dx, y = py + dy;
                if (x < 0 || x >= imgW || y < 0 || y >= imgH) continue;
                size_t off = (y * imgW + x) * 3;
                img[off] = r; img[off+1] = g; img[off+2] = b;
            }
        }
    };
    // Creatures = red.
    wowee::editor::NpcSpawner sp;
    int creaturesPlotted = 0;
    if (sp.loadFromFile(zoneDir + "/creatures.json")) {
        for (const auto& s : sp.getSpawns()) {
            plotPoint(s.position.x, s.position.y, 220, 60, 60);
            creaturesPlotted++;
        }
    }
    // Objects = green (M2) / blue (WMO).
    wowee::editor::ObjectPlacer op;
    int objectsPlotted = 0;
    if (op.loadFromFile(zoneDir + "/objects.json")) {
        for (const auto& o : op.getObjects()) {
            if (o.type == wowee::editor::PlaceableType::M2) {
                plotPoint(o.position.x, o.position.y, 60, 200, 60);
            } else {
                plotPoint(o.position.x, o.position.y, 60, 120, 220);
            }
            objectsPlotted++;
        }
    }
    if (!stbi_write_png(outPath.c_str(), imgW, imgH, 3,
                         img.data(), imgW * 3)) {
        std::fprintf(stderr,
            "export-zone-spawn-png: stbi_write_png failed\n");
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  %dx%d px, tile grid %dx%d, %d creatures (red), %d objects (green/blue)\n",
                imgW, imgH, tilesX, tilesY, creaturesPlotted, objectsPlotted);
    return 0;
}


}  // namespace

bool handleZoneExport(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--export-png") == 0 && i + 1 < argc) {
        outRc = handleExportPng(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-zone-deps-md") == 0 && i + 1 < argc) {
        outRc = handleExportZoneDepsMd(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-zone-spawn-png") == 0 && i + 1 < argc) {
        outRc = handleExportZoneSpawnPng(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
