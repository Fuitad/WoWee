#include "wowee_terrain.hpp"
#include "cli_catalog_paths.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "core/logger.hpp"
#include "stb_image_write.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cmath>

namespace wowee {
namespace editor {

bool WoweeTerrain::exportOpen(const pipeline::ADTTerrain& terrain,
                               const std::string& basePath, int tileX, int tileY) {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(basePath).parent_path());

    // Export binary heightmap (.whm = Wowee HeightMap)
    // Format: 256 chunks × 145 floats = 37120 floats (148480 bytes)
    std::string hmPath = basePath + ".whm";
    {
        std::ofstream f(hmPath, std::ios::binary);
        if (!f) return false;
        // Header: "WHM1" + chunkCount(4) + vertsPerChunk(4)
        uint32_t magic = 0x314D4857; // "WHM1"
        uint32_t chunks = 256, verts = 145;
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&chunks), 4);
        f.write(reinterpret_cast<const char*>(&verts), 4);
        // Per-chunk: baseHeight(4) + heights[145](580) + alphaSize(4) + alphaData(N)
        for (int ci = 0; ci < 256; ci++) {
            const auto& chunk = terrain.chunks[ci];
            // Sanitize base + heights on save so a corrupt in-memory terrain
            // (e.g. mid-edit NaN spike) doesn't get persisted into the WHM
            // and require the load-time guard to clean up forever after.
            float base = chunk.position[2];
            if (!std::isfinite(base)) base = 0.0f;
            f.write(reinterpret_cast<const char*>(&base), 4);
            float clean[145];
            for (int i = 0; i < 145; i++) {
                clean[i] = chunk.heightMap.heights[i];
                if (!std::isfinite(clean[i])) clean[i] = 0.0f;
            }
            f.write(reinterpret_cast<const char*>(clean), 145 * 4);
            // Cap alpha size at 64KB (matches loader cap) - alphaMap is
            // bounded in practice but defensive truncation prevents a
            // stale memory state from producing an unloadable WHM.
            uint32_t alphaSize = std::min<uint32_t>(
                static_cast<uint32_t>(chunk.alphaMap.size()), 65536);
            f.write(reinterpret_cast<const char*>(&alphaSize), 4);
            if (alphaSize > 0) {
                f.write(reinterpret_cast<const char*>(chunk.alphaMap.data()), alphaSize);
            }
        }
    }

    // Export JSON metadata (.wot = Wowee Open Terrain)
    std::string jsonPath = basePath + ".wot";
    {
        nlohmann::json j;
        j["format"] = "wot-1.0";
        j["editor"] = "wowee-editor-1.0.0";
        j["tileX"] = tileX;
        j["tileY"] = tileY;
        j["chunkGrid"] = {16, 16};
        j["vertsPerChunk"] = 145;
        j["heightmapFile"] = fs::path(hmPath).filename().string();
        j["tileSize"] = 533.33333f;
        j["chunkSize"] = 33.33333f;

        nlohmann::json texArr = nlohmann::json::array();
        for (const auto& tex : terrain.textures) texArr.push_back(tex);
        j["textures"] = texArr;

        nlohmann::json chunkArr = nlohmann::json::array();
        for (int ci = 0; ci < 256; ci++) {
            const auto& chunk = terrain.chunks[ci];
            nlohmann::json cl;
            nlohmann::json layerIds = nlohmann::json::array();
            for (const auto& layer : chunk.layers) layerIds.push_back(layer.textureId);
            cl["layers"] = layerIds;
            cl["holes"] = chunk.holes;
            bool hasAlpha = false;
            for (size_t li = 1; li < chunk.layers.size(); li++)
                if (chunk.layers[li].useAlpha()) { hasAlpha = true; break; }
            cl["hasAlpha"] = hasAlpha;
            chunkArr.push_back(cl);
        }
        j["chunkLayers"] = chunkArr;

        nlohmann::json waterArr = nlohmann::json::array();
        for (int ci = 0; ci < 256; ci++) {
            const auto& water = terrain.waterData[ci];
            if (water.hasWater()) {
                waterArr.push_back({{"chunk", ci},
                    {"type", water.layers[0].liquidType},
                    {"height", water.layers[0].maxHeight}});
            } else {
                waterArr.push_back(nullptr);
            }
        }
        j["water"] = waterArr;

        // Doodad placements (M2 models on terrain)
        nlohmann::json doodadNames = nlohmann::json::array();
        for (const auto& n : terrain.doodadNames) doodadNames.push_back(n);
        j["doodadNames"] = doodadNames;

        // nlohmann::json throws on NaN/inf serialization; scrub before
        // emit so a stale in-memory NaN doesn't kill the whole WOT save.
        auto san = [](float x) { return std::isfinite(x) ? x : 0.0f; };
        nlohmann::json doodads = nlohmann::json::array();
        for (const auto& dp : terrain.doodadPlacements) {
            doodads.push_back({
                {"nameId", dp.nameId}, {"uniqueId", dp.uniqueId},
                {"pos", {san(dp.position[0]), san(dp.position[1]), san(dp.position[2])}},
                {"rot", {san(dp.rotation[0]), san(dp.rotation[1]), san(dp.rotation[2])}},
                {"scale", dp.scale}, {"flags", dp.flags}
            });
        }
        j["doodads"] = doodads;

        // WMO placements (buildings on terrain)
        nlohmann::json wmoNames = nlohmann::json::array();
        for (const auto& n : terrain.wmoNames) wmoNames.push_back(n);
        j["wmoNames"] = wmoNames;

        nlohmann::json wmos = nlohmann::json::array();
        for (const auto& wp : terrain.wmoPlacements) {
            wmos.push_back({
                {"nameId", wp.nameId}, {"uniqueId", wp.uniqueId},
                {"pos", {san(wp.position[0]), san(wp.position[1]), san(wp.position[2])}},
                {"rot", {san(wp.rotation[0]), san(wp.rotation[1]), san(wp.rotation[2])}},
                {"flags", wp.flags}, {"doodadSet", wp.doodadSet}
            });
        }
        j["wmos"] = wmos;

        std::ofstream f(jsonPath);
        if (!f) return false;
        f << j.dump(2) << "\n";
    }

    LOG_INFO("Open terrain exported: ", basePath, " (.wot + .whm)");
    return true;
}

bool WoweeTerrain::exportNormalMap(const pipeline::ADTTerrain& terrain,
                                    const std::string& path) {
    // Export 129x129 normal map as RGB PNG
    constexpr int res = 129;
    std::vector<uint8_t> pixels(res * res * 3);

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            const auto& chunk = terrain.chunks[cy * 16 + cx];
            if (!chunk.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue;
                int px = cx * 8 + col, py = cy * 8 + row;
                if (px >= res || py >= res) continue;

                int ni = v * 3;
                float nx = static_cast<float>(chunk.normals[ni]) / 127.0f;
                float ny = static_cast<float>(chunk.normals[ni + 1]) / 127.0f;
                float nz = static_cast<float>(chunk.normals[ni + 2]) / 127.0f;

                int idx = (py * res + px) * 3;
                pixels[idx] = static_cast<uint8_t>((nx * 0.5f + 0.5f) * 255);
                pixels[idx + 1] = static_cast<uint8_t>((ny * 0.5f + 0.5f) * 255);
                pixels[idx + 2] = static_cast<uint8_t>((nz * 0.5f + 0.5f) * 255);
            }
        }
    }

    stbi_write_png(path.c_str(), res, res, 3, pixels.data(), res * 3);
    return true;
}

bool WoweeTerrain::exportHeightmapPreview(const pipeline::ADTTerrain& terrain,
                                           const std::string& path) {
    constexpr int res = 129;
    std::vector<uint8_t> pixels(res * res);

    float minH = 1e30f, maxH = -1e30f;
    for (int ci = 0; ci < 256; ci++) {
        const auto& chunk = terrain.chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            float h = chunk.position[2] + chunk.heightMap.heights[v];
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }
    float range = std::max(1.0f, maxH - minH);

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            const auto& chunk = terrain.chunks[cy * 16 + cx];
            if (!chunk.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue;
                int px = cx * 8 + col, py = cy * 8 + row;
                if (px >= res || py >= res) continue;
                float h = chunk.position[2] + chunk.heightMap.heights[v];
                float t = (h - minH) / range;
                pixels[py * res + px] = static_cast<uint8_t>(t * 255.0f);
            }
        }
    }

    cli::ensureParentDirectory(path);
    stbi_write_png(path.c_str(), res, res, 1, pixels.data(), res);
    return true;
}

bool WoweeTerrain::exportWaterMask(const pipeline::ADTTerrain& terrain,
                                    const std::string& path) {
    constexpr int res = 16; // One pixel per chunk
    std::vector<uint8_t> pixels(res * res);
    for (int ci = 0; ci < 256; ci++)
        pixels[ci] = terrain.waterData[ci].hasWater() ? 255 : 0;

    cli::ensureParentDirectory(path);
    stbi_write_png(path.c_str(), res, res, 1, pixels.data(), res);
    return true;
}

bool WoweeTerrain::exportHoleMask(const pipeline::ADTTerrain& terrain,
                                   const std::string& path) {
    constexpr int res = 16;
    std::vector<uint8_t> pixels(res * res);
    for (int ci = 0; ci < 256; ci++)
        pixels[ci] = (terrain.chunks[ci].holes != 0) ? 255 : 0;

    cli::ensureParentDirectory(path);
    stbi_write_png(path.c_str(), res, res, 1, pixels.data(), res);
    return true;
}

int WoweeTerrain::exportAlphaMaps(const pipeline::ADTTerrain& terrain,
                                    const std::string& outputDir) {
    namespace fs = std::filesystem;
    fs::create_directories(outputDir);
    int exported = 0;

    for (int ci = 0; ci < 256; ci++) {
        const auto& chunk = terrain.chunks[ci];
        for (size_t li = 1; li < chunk.layers.size(); li++) {
            if (!chunk.layers[li].useAlpha()) continue;
            size_t off = chunk.layers[li].offsetMCAL;
            if (off + 4096 > chunk.alphaMap.size()) continue;

            std::string path = outputDir + "/chunk_" + std::to_string(ci) +
                               "_layer_" + std::to_string(li) + ".png";
            stbi_write_png(path.c_str(), 64, 64, 1,
                           chunk.alphaMap.data() + off, 64);
            exported++;
        }
    }
    return exported;
}

bool WoweeTerrain::exportZoneMap(const pipeline::ADTTerrain& terrain,
                                  const std::string& path, int resolution) {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());

    std::vector<uint8_t> pixels(
        static_cast<size_t>(resolution) * static_cast<size_t>(resolution) * 3u, 0);

    // Find height range
    float minH = 1e30f, maxH = -1e30f;
    for (int ci = 0; ci < 256; ci++) {
        const auto& c = terrain.chunks[ci];
        if (!c.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            float h = c.position[2] + c.heightMap.heights[v];
            minH = std::min(minH, h); maxH = std::max(maxH, h);
        }
    }
    float range = std::max(maxH - minH, 1.0f);

    // Render terrain colors
    for (int py = 0; py < resolution; py++) {
        for (int px = 0; px < resolution; px++) {
            float u = static_cast<float>(px) / resolution;
            float v = static_cast<float>(py) / resolution;

            int cx = static_cast<int>(u * 16); cx = std::clamp(cx, 0, 15);
            int cy = static_cast<int>(v * 16); cy = std::clamp(cy, 0, 15);
            int ci = cy * 16 + cx;

            const auto& chunk = terrain.chunks[ci];
            if (!chunk.hasHeightMap()) continue;

            float localU = (u * 16 - cx) * 8;
            float localV = (v * 16 - cy) * 8;
            int gx = std::clamp(static_cast<int>(localU), 0, 7);
            int gy = std::clamp(static_cast<int>(localV), 0, 7);
            float h = chunk.position[2] + chunk.heightMap.heights[gy * 17 + gx];
            float t = (h - minH) / range;

            // Terrain coloring: blue(low) -> green(mid) -> brown(high) -> white(peak)
            float r, g, b;
            if (t < 0.15f) {
                r = 0.2f; g = 0.3f; b = 0.6f;
            } else if (t < 0.4f) {
                float tt = (t - 0.15f) / 0.25f;
                r = 0.2f * (1-tt) + 0.3f * tt;
                g = 0.3f * (1-tt) + 0.6f * tt;
                b = 0.6f * (1-tt) + 0.2f * tt;
            } else if (t < 0.7f) {
                float tt = (t - 0.4f) / 0.3f;
                r = 0.3f + tt * 0.4f; g = 0.6f - tt * 0.1f; b = 0.2f - tt * 0.1f;
            } else {
                float tt = (t - 0.7f) / 0.3f;
                r = 0.7f + tt * 0.2f; g = 0.5f + tt * 0.3f; b = 0.1f + tt * 0.6f;
            }

            // Water overlay
            if (terrain.waterData[ci].hasWater()) {
                float wh = terrain.waterData[ci].layers[0].maxHeight;
                if (h < wh) { r = 0.15f; g = 0.3f; b = 0.7f; }
            }

            // Hole overlay
            if (chunk.holes) {
                int hx = gx / 2, hy = gy / 2;
                if (chunk.holes & (1 << (hy * 4 + hx))) {
                    r = 0.1f; g = 0.1f; b = 0.1f;
                }
            }

            int idx = (py * resolution + px) * 3;
            pixels[idx]   = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255);
            pixels[idx+1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255);
            pixels[idx+2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255);
        }
    }

    // Draw doodad positions as yellow dots
    float tileNW_X = (32.0f - terrain.coord.y) * 533.33333f;
    float tileNW_Y = (32.0f - terrain.coord.x) * 533.33333f;
    for (const auto& dp : terrain.doodadPlacements) {
        float u = (tileNW_X - dp.position[1]) / 533.33333f;
        float vv = (tileNW_Y - dp.position[0]) / 533.33333f;
        int px = static_cast<int>(vv * resolution);
        int py = static_cast<int>(u * resolution);
        if (px >= 0 && px < resolution && py >= 0 && py < resolution) {
            int idx = (py * resolution + px) * 3;
            pixels[idx] = 255; pixels[idx+1] = 220; pixels[idx+2] = 50;
        }
    }

    if (!stbi_write_png(path.c_str(), resolution, resolution, 3, pixels.data(), resolution * 3)) {
        LOG_ERROR("Failed to write zone map: ", path);
        return false;
    }
    LOG_INFO("Zone map exported: ", path, " (", resolution, "x", resolution, ")");
    return true;
}

bool WoweeTerrain::importOpen(const std::string& basePath, pipeline::ADTTerrain& terrain) {
    return pipeline::WoweeTerrainLoader::load(basePath, terrain);
}

} // namespace editor
} // namespace wowee
