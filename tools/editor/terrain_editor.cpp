#include "terrain_editor.hpp"
#include "core/logger.hpp"
#include "stb_image.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>

namespace wowee {
namespace editor {

TerrainEditor::TerrainEditor() = default;

pipeline::ADTTerrain TerrainEditor::createBlankTerrain(int tileX, int tileY, float baseHeight,
                                                        Biome biome) {
    pipeline::ADTTerrain terrain;
    terrain.loaded = true;
    terrain.version = 18;
    terrain.coord = {tileX, tileY};

    const auto& biomeTextures = getBiomeTextures(biome);

    // Integer grid noise - guarantees shared edge vertices get identical heights
    auto gridNoise = [](int gx, int gy) -> float {
        uint32_t h = static_cast<uint32_t>(gx * 374761393 + gy * 668265263);
        h = (h ^ (h >> 13)) * 1274126177;
        h = h ^ (h >> 16);
        return (static_cast<float>(h & 0xFFFF) / 65535.0f - 0.5f) * 3.0f;
    };

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            auto& chunk = terrain.chunks[cy * 16 + cx];
            chunk.flags = 0;
            chunk.indexX = cx;
            chunk.indexY = cy;
            chunk.holes = 0;

            chunk.position[0] = (32.0f - tileX) * TILE_SIZE - cx * CHUNK_SIZE;
            chunk.position[1] = (32.0f - tileY) * TILE_SIZE - cy * CHUNK_SIZE;
            chunk.position[2] = baseHeight;

            chunk.heightMap.loaded = true;

            for (int i = 0; i < 145; i++) {
                int row = i / 17;
                int col = i % 17;

                if (col <= 8) {
                    // Outer vertex - shared at chunk edges
                    int globalRow = cy * 8 + row;
                    int globalCol = cx * 8 + col;
                    chunk.heightMap.heights[i] = gridNoise(globalRow, globalCol);
                } else {
                    // Inner vertex (quad center) - not shared, offset grid
                    int innerCol = col - 9;
                    int globalRow = cy * 16 + row * 2 + 1;
                    int globalCol = cx * 16 + innerCol * 2 + 1;
                    chunk.heightMap.heights[i] = gridNoise(globalRow, globalCol);
                }
            }

            // Normals pointing up (will be recalculated by renderer)
            for (int i = 0; i < 145; i++) {
                chunk.normals[i * 3 + 0] = 0;
                chunk.normals[i * 3 + 1] = 0;
                chunk.normals[i * 3 + 2] = 127;
            }

            // Base texture layer
            pipeline::TextureLayer layer{};
            layer.textureId = 0;
            layer.flags = 0;
            layer.offsetMCAL = 0;
            layer.effectId = 0;
            chunk.layers.push_back(layer);
        }
    }

    // Biome textures
    terrain.textures.push_back(biomeTextures.base);
    terrain.textures.push_back(biomeTextures.secondary);
    terrain.textures.push_back(biomeTextures.accent);
    terrain.textures.push_back(biomeTextures.detail);

    return terrain;
}

glm::vec3 TerrainEditor::chunkVertexWorldPos(int chunkIdx, int vertIdx) const {
    const auto& chunk = terrain_->chunks[chunkIdx];
    int tileX = terrain_->coord.x;
    int tileY = terrain_->coord.y;
    int cx = chunkIdx % 16;
    int cy = chunkIdx / 16;

    float tileNW_renderX = (32.0f - static_cast<float>(tileY)) * TILE_SIZE;
    float tileNW_renderY = (32.0f - static_cast<float>(tileX)) * TILE_SIZE;
    float chunkBaseX = tileNW_renderX - static_cast<float>(cy) * CHUNK_SIZE;
    float chunkBaseY = tileNW_renderY - static_cast<float>(cx) * CHUNK_SIZE;
    float chunkBaseZ = chunk.position[2];

    int row = vertIdx / 17;
    int col = vertIdx % 17;
    float offsetX = static_cast<float>(col);
    float offsetY = static_cast<float>(row);
    if (col > 8) {
        offsetY += 0.5f;
        offsetX -= 8.5f;
    }

    float unitSize = CHUNK_SIZE / 8.0f;
    float x = chunkBaseX - offsetY * unitSize;
    float y = chunkBaseY - offsetX * unitSize;
    float z = chunkBaseZ + chunk.heightMap.heights[vertIdx];

    return glm::vec3(x, y, z);
}

float TerrainEditor::getVertexHeight(int chunkIdx, int vertIdx) const {
    return terrain_->chunks[chunkIdx].heightMap.heights[vertIdx];
}

void TerrainEditor::setVertexHeight(int chunkIdx, int vertIdx, float height) {
    terrain_->chunks[chunkIdx].heightMap.heights[vertIdx] = height;
}

bool TerrainEditor::raycastTerrain(const rendering::Ray& ray, glm::vec3& hitPos) const {
    if (!terrain_) return false;
    // Reject NaN ray. The AABB test divides by ray.direction[i], so NaN
    // would propagate through tmin/tmax and the result would be undefined.
    if (!std::isfinite(ray.origin.x) || !std::isfinite(ray.origin.y) ||
        !std::isfinite(ray.origin.z) || !std::isfinite(ray.direction.x) ||
        !std::isfinite(ray.direction.y) || !std::isfinite(ray.direction.z)) {
        return false;
    }

    float bestT = 1e30f;
    bool hit = false;

    for (int chunkIdx = 0; chunkIdx < 256; chunkIdx++) {
        const auto& chunk = terrain_->chunks[chunkIdx];
        if (!chunk.hasHeightMap()) continue;

        // Quick AABB check using actual vertex extent
        glm::vec3 corner0 = chunkVertexWorldPos(chunkIdx, 0);
        glm::vec3 corner1 = chunkVertexWorldPos(chunkIdx, 144);
        glm::vec3 minB = glm::min(corner0, corner1);
        glm::vec3 maxB = glm::max(corner0, corner1);
        // Expand Z by actual height range in chunk
        float minH = chunk.heightMap.heights[0], maxH = minH;
        for (int h = 1; h < 145; h++) {
            minH = std::min(minH, chunk.heightMap.heights[h]);
            maxH = std::max(maxH, chunk.heightMap.heights[h]);
        }
        minB.z = chunk.position[2] + minH - 10.0f;
        maxB.z = chunk.position[2] + maxH + 10.0f;

        // Simple AABB-ray test
        float tmin = -1e30f, tmax = 1e30f;
        for (int i = 0; i < 3; i++) {
            if (std::abs(ray.direction[i]) < 1e-8f) {
                if (ray.origin[i] < minB[i] || ray.origin[i] > maxB[i]) { tmin = 1e30f; break; }
            } else {
                float t1 = (minB[i] - ray.origin[i]) / ray.direction[i];
                float t2 = (maxB[i] - ray.origin[i]) / ray.direction[i];
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
            }
        }
        if (tmin > tmax || tmax < 0) continue;

        // Triangle intersection for each quad
        for (int qy = 0; qy < 8; qy++) {
            for (int qx = 0; qx < 8; qx++) {
                int center = 9 + qy * 17 + qx;
                int tl = center - 9;
                int tr = center - 8;
                int bl = center + 8;
                int br = center + 9;

                int tris[4][3] = {{center, tl, tr}, {center, tr, br}, {center, br, bl}, {center, bl, tl}};
                for (auto& tri : tris) {
                    glm::vec3 v0 = chunkVertexWorldPos(chunkIdx, tri[0]);
                    glm::vec3 v1 = chunkVertexWorldPos(chunkIdx, tri[1]);
                    glm::vec3 v2 = chunkVertexWorldPos(chunkIdx, tri[2]);

                    // Moller-Trumbore intersection
                    glm::vec3 e1 = v1 - v0;
                    glm::vec3 e2 = v2 - v0;
                    glm::vec3 h = glm::cross(ray.direction, e2);
                    float a = glm::dot(e1, h);
                    if (std::abs(a) < 1e-8f) continue;

                    float f = 1.0f / a;
                    glm::vec3 s = ray.origin - v0;
                    float u = f * glm::dot(s, h);
                    if (u < 0.0f || u > 1.0f) continue;

                    glm::vec3 q = glm::cross(s, e1);
                    float v = f * glm::dot(ray.direction, q);
                    if (v < 0.0f || u + v > 1.0f) continue;

                    float t = f * glm::dot(e2, q);
                    if (t > 0.001f && t < bestT) {
                        bestT = t;
                        hitPos = ray.origin + ray.direction * t;
                        hit = true;
                    }
                }
            }
        }
    }

    return hit;
}

std::vector<int> TerrainEditor::getAffectedChunks(const glm::vec3& center, float radius) const {
    std::vector<int> result;
    for (int i = 0; i < 256; i++) {
        if (!terrain_->chunks[i].hasHeightMap()) continue;
        // Check if any vertex in chunk is within radius
        glm::vec3 c0 = chunkVertexWorldPos(i, 0);
        glm::vec3 c1 = chunkVertexWorldPos(i, 144);
        glm::vec3 chunkCenter = (c0 + c1) * 0.5f;
        float chunkRadius = glm::length(c1 - c0) * 0.5f;
        if (glm::length(glm::vec2(chunkCenter.x - center.x, chunkCenter.y - center.y)) < radius + chunkRadius)
            result.push_back(i);
    }
    return result;
}

glm::vec3 TerrainEditor::sampleTerrainNormal(const glm::vec3& worldPos) const {
    if (!terrain_) return glm::vec3(0, 0, 1);

    auto sampleH = [&](float x, float y) -> float {
        rendering::Ray ray;
        ray.origin = glm::vec3(x, y, 10000.0f);
        ray.direction = glm::vec3(0, 0, -1);
        glm::vec3 hit;
        if (const_cast<TerrainEditor*>(this)->raycastTerrain(ray, hit))
            return hit.z;
        return worldPos.z;
    };

    float step = 2.0f;
    float hL = sampleH(worldPos.x - step, worldPos.y);
    float hR = sampleH(worldPos.x + step, worldPos.y);
    float hD = sampleH(worldPos.x, worldPos.y - step);
    float hU = sampleH(worldPos.x, worldPos.y + step);

    glm::vec3 dx(2.0f * step, 0, hR - hL);
    glm::vec3 dy(0, 2.0f * step, hU - hD);
    glm::vec3 n = glm::normalize(glm::cross(dx, dy));
    if (n.z < 0) n = -n;
    return n;
}

void TerrainEditor::beginStroke() {
    if (!terrain_ || strokeActive_) return;
    strokeActive_ = true;

    auto affected = getAffectedChunks(brush_.getPosition(), brush_.settings().radius);
    // Capture all chunks that could be affected during the entire stroke
    std::vector<int> allChunks(256);
    std::iota(allChunks.begin(), allChunks.end(), 0);
    std::vector<int> valid;
    for (int i : allChunks) {
        if (terrain_->chunks[i].hasHeightMap()) valid.push_back(i);
    }
    history_.beginEdit(*terrain_, valid);
}

void TerrainEditor::endStroke() {
    if (!strokeActive_) return;
    strokeActive_ = false;
    history_.endEdit(*terrain_);
}

void TerrainEditor::recordGeneratorUndo() {
    if (!terrain_) return;
    std::vector<int> valid;
    for (int i = 0; i < 256; i++) {
        if (terrain_->chunks[i].hasHeightMap()) valid.push_back(i);
    }
    history_.beginEdit(*terrain_, valid);
}

void TerrainEditor::commitGeneratorUndo() {
    if (!terrain_) return;
    history_.endEdit(*terrain_);
}

void TerrainEditor::applyBrush(float deltaTime) {
    if (!terrain_ || !brush_.isActive()) return;
    // Reject NaN brush position / non-positive radius / NaN strength.
    // dist = length(pos - center) goes NaN if center has NaN, and
    // brush_.getInfluence(NaN) returns full strength on every vertex
    // because NaN comparisons always return false.
    const auto& bs = brush_.settings();
    auto bp = brush_.getPosition();
    if (!std::isfinite(bp.x) || !std::isfinite(bp.y) || !std::isfinite(bp.z) ||
        !std::isfinite(bs.radius) || bs.radius <= 0.0f ||
        !std::isfinite(bs.strength)) return;

    switch (bs.mode) {
        case BrushMode::Raise: applyRaise(deltaTime); break;
        case BrushMode::Lower: applyRaise(deltaTime); break;
        case BrushMode::Smooth: applySmooth(deltaTime); break;
        case BrushMode::Flatten:
        case BrushMode::Level: applyFlatten(deltaTime); break;
        case BrushMode::Erode: applyErode(deltaTime); break;
    }
}

void TerrainEditor::applyRaise(float dt) {
    float amount = brush_.settings().strength * dt;
    if (brush_.settings().mode == BrushMode::Lower) amount = -amount;

    auto affected = getAffectedChunks(brush_.getPosition(), brush_.settings().radius);
    for (int chunkIdx : affected) {
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(chunkIdx, v);
            float dist = glm::length(glm::vec2(pos.x - brush_.getPosition().x,
                                                pos.y - brush_.getPosition().y));
            float influence = brush_.getInfluence(dist);
            if (influence > 0.0f) {
                float h = getVertexHeight(chunkIdx, v);
                setVertexHeight(chunkIdx, v, h + amount * influence);
                modified = true;
            }
        }
        if (modified) {
            stitchEdges(chunkIdx);
            if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), chunkIdx) == dirtyChunks_.end())
                dirtyChunks_.push_back(chunkIdx);
            dirty_ = true;
        }
    }
}

void TerrainEditor::applySmooth(float dt) {
    float factor = std::min(1.0f, brush_.settings().strength * dt * 0.5f);

    auto affected = getAffectedChunks(brush_.getPosition(), brush_.settings().radius);

    // Build a snapshot of all heights so we read from consistent state
    std::array<std::array<float, 145>, 256> snapshot;
    for (int ci : affected)
        for (int v = 0; v < 145; v++)
            snapshot[ci][v] = getVertexHeight(ci, v);

    // Helper: get height of vertex at global outer grid position,
    // looking across chunk boundaries
    auto getGlobalOuterHeight = [&](int chunkIdx, int row, int col) -> float {
        int cx = chunkIdx % 16;
        int cy = chunkIdx / 16;

        // If within chunk bounds, return directly
        if (row >= 0 && row <= 8 && col >= 0 && col <= 8) {
            int vi = row * 17 + col;
            return snapshot[chunkIdx][vi];
        }

        // Cross into adjacent chunk
        int ncx = cx, ncy = cy;
        int nr = row, nc = col;
        if (row < 0) { ncy = cy - 1; nr = 8; }
        if (row > 8) { ncy = cy + 1; nr = 0; }
        if (col < 0) { ncx = cx - 1; nc = 8; }
        if (col > 8) { ncx = cx + 1; nc = 0; }

        if (ncx < 0 || ncx > 15 || ncy < 0 || ncy > 15)
            return snapshot[chunkIdx][std::clamp(row, 0, 8) * 17 + std::clamp(col, 0, 8)];

        int nci = ncy * 16 + ncx;
        if (!terrain_->chunks[nci].hasHeightMap())
            return snapshot[chunkIdx][std::clamp(row, 0, 8) * 17 + std::clamp(col, 0, 8)];

        int vi = nr * 17 + nc;
        return snapshot[nci][vi];
    };

    for (int chunkIdx : affected) {
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(chunkIdx, v);
            float dist = glm::length(glm::vec2(pos.x - brush_.getPosition().x,
                                                pos.y - brush_.getPosition().y));
            float influence = brush_.getInfluence(dist);
            if (influence <= 0.0f) continue;

            int row = v / 17;
            int col = v % 17;

            float sum = 0.0f;
            int count = 0;

            if (col <= 8) {
                // Outer vertex - sample 4 neighbors, crossing chunk borders
                int dirs[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (auto& d : dirs) {
                    sum += getGlobalOuterHeight(chunkIdx, row + d[0], col + d[1]);
                    count++;
                }
            } else {
                // Inner vertex - use same-chunk neighbors only
                int neighbors[] = {v - 17, v + 17, v - 1, v + 1};
                for (int n : neighbors) {
                    if (n >= 0 && n < 145) {
                        sum += snapshot[chunkIdx][n];
                        count++;
                    }
                }
            }

            if (count > 0) {
                float avg = sum / static_cast<float>(count);
                float h = snapshot[chunkIdx][v];
                float newH = h + (avg - h) * factor * influence;
                if (newH != h) {
                    setVertexHeight(chunkIdx, v, newH);
                    modified = true;
                }
            }
        }

        if (modified) {
            stitchEdges(chunkIdx);
            if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), chunkIdx) == dirtyChunks_.end())
                dirtyChunks_.push_back(chunkIdx);
            dirty_ = true;
        }
    }
}

void TerrainEditor::stitchEdges(int chunkIdx) {
    int cx = chunkIdx % 16;
    int cy = chunkIdx / 16;

    auto pushDirty = [&](int idx) {
        if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), idx) == dirtyChunks_.end())
            dirtyChunks_.push_back(idx);
    };

    if (cx < 15) {
        int n = cy * 16 + cx + 1;
        if (terrain_->chunks[n].hasHeightMap()) {
            for (int r = 0; r <= 8; r++)
                setVertexHeight(n, r * 17, getVertexHeight(chunkIdx, r * 17 + 8));
            pushDirty(n);
        }
    }
    if (cx > 0) {
        int n = cy * 16 + cx - 1;
        if (terrain_->chunks[n].hasHeightMap()) {
            for (int r = 0; r <= 8; r++)
                setVertexHeight(n, r * 17 + 8, getVertexHeight(chunkIdx, r * 17));
            pushDirty(n);
        }
    }
    if (cy < 15) {
        int n = (cy + 1) * 16 + cx;
        if (terrain_->chunks[n].hasHeightMap()) {
            for (int c = 0; c <= 8; c++)
                setVertexHeight(n, c, getVertexHeight(chunkIdx, 8 * 17 + c));
            pushDirty(n);
        }
    }
    if (cy > 0) {
        int n = (cy - 1) * 16 + cx;
        if (terrain_->chunks[n].hasHeightMap()) {
            for (int c = 0; c <= 8; c++)
                setVertexHeight(n, 8 * 17 + c, getVertexHeight(chunkIdx, c));
            pushDirty(n);
        }
    }
}

void TerrainEditor::applyFlatten(float dt) {
    float factor = std::min(1.0f, brush_.settings().strength * dt * 0.3f);
    float targetH = brush_.settings().flattenHeight;

    auto affected = getAffectedChunks(brush_.getPosition(), brush_.settings().radius);
    for (int chunkIdx : affected) {
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(chunkIdx, v);
            float dist = glm::length(glm::vec2(pos.x - brush_.getPosition().x,
                                                pos.y - brush_.getPosition().y));
            float influence = brush_.getInfluence(dist);
            if (influence <= 0.0f) continue;

            float h = getVertexHeight(chunkIdx, v);
            // targetH is absolute world Z; heights are relative to chunk base
            float relTarget = targetH - terrain_->chunks[chunkIdx].position[2];
            float newH = h + (relTarget - h) * factor * influence;
            if (newH != h) {
                setVertexHeight(chunkIdx, v, newH);
                modified = true;
            }
        }
        if (modified) {
            stitchEdges(chunkIdx);
            if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), chunkIdx) == dirtyChunks_.end())
                dirtyChunks_.push_back(chunkIdx);
            dirty_ = true;
        }
    }
}

std::vector<int> TerrainEditor::consumeDirtyChunks() {
    std::vector<int> result;
    result.swap(dirtyChunks_);
    return result;
}

pipeline::TerrainMesh TerrainEditor::regenerateMesh() const {
    if (!terrain_) return {};
    return pipeline::TerrainMeshGenerator::generate(*terrain_);
}

pipeline::ChunkMesh TerrainEditor::regenerateChunkMesh(int chunkIndex) const {
    if (!terrain_) return {};
    auto mesh = pipeline::TerrainMeshGenerator::generate(*terrain_);
    return mesh.chunks[chunkIndex];
}

void TerrainEditor::undo() {
    if (!terrain_) return;
    history_.undo(*terrain_);
    for (int idx : history_.lastAffectedChunks()) {
        if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), idx) == dirtyChunks_.end())
            dirtyChunks_.push_back(idx);
    }
}

void TerrainEditor::recalcNormals(const std::vector<int>& chunkIndices) {
    if (!terrain_) return;
    float unitSize = CHUNK_SIZE / 8.0f;

    for (int ci : chunkIndices) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;

        for (int i = 0; i < 145; i++) {
            int row = i / 17;
            int col = i % 17;

            // Get heights of neighbors
            float hC = chunk.heightMap.heights[i];
            float hL = hC, hR = hC, hU = hC, hD = hC;

            if (col <= 8) {
                // Outer vertex
                int li = row * 17 + std::max(0, col - 1);
                int ri = row * 17 + std::min(8, col + 1);
                int ui = std::max(0, row - 1) * 17 + col;
                int di = std::min(8, row + 1) * 17 + col;
                hL = chunk.heightMap.heights[li];
                hR = chunk.heightMap.heights[ri];
                hU = chunk.heightMap.heights[ui];
                hD = chunk.heightMap.heights[di];
            } else {
                // Inner vertex - use adjacent outer verts
                int innerCol = col - 9;
                int tl = row * 17 + innerCol;
                int tr = row * 17 + innerCol + 1;
                int bl = (row + 1) * 17 + innerCol;
                int br = (row + 1) * 17 + innerCol + 1;
                if (tl >= 0 && tl < 145) hU = chunk.heightMap.heights[tl];
                if (tr >= 0 && tr < 145) hR = chunk.heightMap.heights[tr];
                if (bl >= 0 && bl < 145) hD = chunk.heightMap.heights[bl];
                if (br >= 0 && br < 145) hL = chunk.heightMap.heights[br];
            }

            // Compute normal from height differences
            float dx = (hL - hR) / (2.0f * unitSize);
            float dy = (hU - hD) / (2.0f * unitSize);
            float len = std::sqrt(dx * dx + dy * dy + 1.0f);
            glm::vec3 n(dx / len, dy / len, 1.0f / len);

            chunk.normals[i * 3 + 0] = static_cast<int8_t>(n.x * 127.0f);
            chunk.normals[i * 3 + 1] = static_cast<int8_t>(n.y * 127.0f);
            chunk.normals[i * 3 + 2] = static_cast<int8_t>(n.z * 127.0f);
        }
    }
}

void TerrainEditor::redo() {
    if (!terrain_) return;
    history_.redo(*terrain_);
    for (int idx : history_.lastAffectedChunks()) {
        if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), idx) == dirtyChunks_.end())
            dirtyChunks_.push_back(idx);
    }
}

void TerrainEditor::setWaterLevel(const glm::vec3& center, float radius,
                                   float waterHeight, uint16_t liquidType) {
    if (!terrain_) return;

    auto affected = getAffectedChunks(center, radius);
    for (int chunkIdx : affected) {
        auto& water = terrain_->waterData[chunkIdx];

        if (water.layers.empty()) {
            pipeline::ADTTerrain::WaterLayer wl;
            wl.liquidType = liquidType;
            wl.flags = 0;
            wl.minHeight = waterHeight;
            wl.maxHeight = waterHeight;
            wl.x = 0;
            wl.y = 0;
            wl.width = 9;
            wl.height = 9;
            wl.heights.assign(81, waterHeight);
            wl.mask.assign(8, 0xFF);
            water.layers.push_back(wl);
        } else {
            auto& wl = water.layers[0];
            wl.minHeight = waterHeight;
            wl.maxHeight = waterHeight;
            wl.liquidType = liquidType;
            std::fill(wl.heights.begin(), wl.heights.end(), waterHeight);
        }

        if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), chunkIdx) == dirtyChunks_.end())
            dirtyChunks_.push_back(chunkIdx);
        dirty_ = true;
    }
}

void TerrainEditor::removeWater(const glm::vec3& center, float radius) {
    if (!terrain_) return;

    auto affected = getAffectedChunks(center, radius);
    for (int chunkIdx : affected) {
        terrain_->waterData[chunkIdx].layers.clear();
        if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), chunkIdx) == dirtyChunks_.end())
            dirtyChunks_.push_back(chunkIdx);
        dirty_ = true;
    }
}

void TerrainEditor::smoothEntireTile(int iterations) {
    if (!terrain_) return;

    for (int iter = 0; iter < iterations; iter++) {
        // Snapshot all heights
        std::array<std::array<float, 145>, 256> snap;
        for (int ci = 0; ci < 256; ci++)
            for (int v = 0; v < 145; v++)
                snap[ci][v] = terrain_->chunks[ci].heightMap.heights[v];

        for (int ci = 0; ci < 256; ci++) {
            auto& chunk = terrain_->chunks[ci];
            if (!chunk.hasHeightMap()) continue;
            int cx = ci % 16, cy = ci / 16;

            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue; // smooth outer vertices only

                float sum = snap[ci][v];
                int count = 1;

                // Same-chunk neighbors
                if (col > 0) { sum += snap[ci][row * 17 + col - 1]; count++; }
                if (col < 8) { sum += snap[ci][row * 17 + col + 1]; count++; }
                if (row > 0) { sum += snap[ci][(row - 1) * 17 + col]; count++; }
                if (row < 8) { sum += snap[ci][(row + 1) * 17 + col]; count++; }

                // Cross-chunk neighbors at edges
                if (col == 0 && cx > 0) { sum += snap[cy * 16 + cx - 1][row * 17 + 8]; count++; }
                if (col == 8 && cx < 15) { sum += snap[cy * 16 + cx + 1][row * 17 + 0]; count++; }
                if (row == 0 && cy > 0) { sum += snap[(cy - 1) * 16 + cx][8 * 17 + col]; count++; }
                if (row == 8 && cy < 15) { sum += snap[(cy + 1) * 16 + cx][0 * 17 + col]; count++; }

                chunk.heightMap.heights[v] = sum / static_cast<float>(count);
            }

            // Update inner vertices from smoothed outer vertices
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col <= 8) continue;
                int innerCol = col - 9;
                // Average of 4 surrounding outer vertices
                int tl = row * 17 + innerCol;
                int tr = row * 17 + innerCol + 1;
                int bl = (row + 1) * 17 + innerCol;
                int br = (row + 1) * 17 + innerCol + 1;
                if (tl < 145 && tr < 145 && bl < 145 && br < 145)
                    chunk.heightMap.heights[v] = (chunk.heightMap.heights[tl] +
                        chunk.heightMap.heights[tr] + chunk.heightMap.heights[bl] +
                        chunk.heightMap.heights[br]) * 0.25f;
            }
        }

        // Stitch all edges
        for (int ci = 0; ci < 256; ci++)
            stitchEdges(ci);
    }

    for (int ci = 0; ci < 256; ci++)
        dirtyChunks_.push_back(ci);
    dirty_ = true;
}

void TerrainEditor::resetToFlat() {
    if (!terrain_) return;
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        chunk.heightMap.heights.fill(0.0f);
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
}

void TerrainEditor::scaleHeights(float factor) {
    if (!terrain_) return;
    recordGeneratorUndo();
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++)
            chunk.heightMap.heights[v] *= factor;
        dirtyChunks_.push_back(ci);
    }
    // Re-stitch all edges after scaling
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::mirrorX() {
    if (!terrain_) return;
    recordGeneratorUndo();
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 8; cx++) {
            int srcIdx = cy * 16 + cx;
            int dstIdx = cy * 16 + (15 - cx);
            auto& src = terrain_->chunks[srcIdx];
            auto& dst = terrain_->chunks[dstIdx];
            if (!src.hasHeightMap() || !dst.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue;
                int mirrorCol = 8 - col;
                int mirrorV = row * 17 + mirrorCol;
                dst.heightMap.heights[mirrorV] = src.heightMap.heights[v];
            }
            dirtyChunks_.push_back(dstIdx);
        }
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::mirrorY() {
    if (!terrain_) return;
    recordGeneratorUndo();
    for (int cy = 0; cy < 8; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            int srcIdx = cy * 16 + cx;
            int dstIdx = (15 - cy) * 16 + cx;
            auto& src = terrain_->chunks[srcIdx];
            auto& dst = terrain_->chunks[dstIdx];
            if (!src.hasHeightMap() || !dst.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue;
                int mirrorRow = 8 - row;
                int mirrorV = mirrorRow * 17 + col;
                dst.heightMap.heights[mirrorV] = src.heightMap.heights[v];
            }
            dirtyChunks_.push_back(dstIdx);
        }
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::carveRiver(const glm::vec3& start, const glm::vec3& end,
                                float width, float depth) {
    if (!terrain_) return;
    if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(end.x) || !std::isfinite(end.y) ||
        !std::isfinite(width) || width <= 0.0f ||
        !std::isfinite(depth)) return;
    recordGeneratorUndo();
    glm::vec2 lineStart(start.x, start.y);
    glm::vec2 lineEnd(end.x, end.y);
    float lineLen = glm::length(lineEnd - lineStart);
    if (lineLen < 1e-4f) return;
    glm::vec2 lineDir = (lineEnd - lineStart) / lineLen;

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;

        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            glm::vec2 p(pos.x, pos.y);

            // Project point onto line segment
            glm::vec2 toP = p - lineStart;
            float t = glm::dot(toP, lineDir);
            t = std::clamp(t, 0.0f, lineLen);
            glm::vec2 closest = lineStart + lineDir * t;
            float dist = glm::length(p - closest);

            if (dist < width) {
                float falloff = 1.0f - (dist / width);
                falloff = falloff * falloff; // smooth edges
                float carve = depth * falloff;
                chunk.heightMap.heights[v] -= carve;
                modified = true;
            }
        }
        if (modified) {
            stitchEdges(ci);
            dirtyChunks_.push_back(ci);
        }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::createCrater(const glm::vec3& center, float radius, float depth, float rimHeight) {
    if (!terrain_) return;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(depth) || !std::isfinite(rimHeight)) return;
    recordGeneratorUndo();

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;

        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            float dist = glm::length(glm::vec2(pos.x - center.x, pos.y - center.y));
            if (dist > radius * 1.3f) continue;

            float t = dist / radius;
            float delta = 0.0f;

            if (t < 0.8f) {
                // Bowl interior: parabolic depression
                float bowlT = t / 0.8f;
                delta = -depth * (1.0f - bowlT * bowlT);
            } else if (t < 1.0f) {
                // Rim: raised edge
                float rimT = (t - 0.8f) / 0.2f;
                delta = rimHeight * std::sin(rimT * 3.14159f);
            } else if (t < 1.3f) {
                // Outer falloff
                float fallT = (t - 1.0f) / 0.3f;
                delta = rimHeight * (1.0f - fallT) * 0.3f;
            }

            chunk.heightMap.heights[v] += delta;
            modified = true;
        }
        if (modified) {
            stitchEdges(ci);
            dirtyChunks_.push_back(ci);
        }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::createMesa(const glm::vec3& center, float radius, float height, float edgeSteepness) {
    if (!terrain_) return;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height) || !std::isfinite(edgeSteepness)) return;
    recordGeneratorUndo();
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;

        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            float dist = glm::length(glm::vec2(pos.x - center.x, pos.y - center.y));
            if (dist > radius * 1.5f) continue;

            float t = dist / radius;
            float blend;
            if (t < 0.7f) {
                blend = 1.0f; // flat top
            } else if (t < 1.0f) {
                float edgeT = (t - 0.7f) / 0.3f;
                blend = 1.0f - std::pow(edgeT, 1.0f / std::max(0.1f, edgeSteepness));
            } else {
                blend = 0.0f;
            }

            chunk.heightMap.heights[v] += height * blend;
            modified = true;
        }
        if (modified) {
            stitchEdges(ci);
            dirtyChunks_.push_back(ci);
        }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::createHill(const glm::vec3& center, float radius, float height) {
    if (!terrain_) return;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height)) return;
    recordGeneratorUndo();
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            float dist = glm::length(glm::vec2(pos.x - center.x, pos.y - center.y));
            if (dist >= radius) continue;
            float t = dist / radius;
            float blend = (1.0f - t * t) * (1.0f - t * t); // smooth bell curve
            chunk.heightMap.heights[v] += height * blend;
            modified = true;
        }
        if (modified) { stitchEdges(ci); dirtyChunks_.push_back(ci); }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::applyVoronoiNoise(int cellCount, float amplitude, uint32_t seed) {
    if (!terrain_) return;
    recordGeneratorUndo();

    float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * TILE_SIZE;
    float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * TILE_SIZE;

    // Generate random cell centers
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distX(tileNW_X - TILE_SIZE, tileNW_X);
    std::uniform_real_distribution<float> distY(tileNW_Y - TILE_SIZE, tileNW_Y);
    std::uniform_real_distribution<float> distH(0.0f, amplitude);

    struct Cell { float x, y, h; };
    std::vector<Cell> cells(cellCount);
    for (auto& c : cells) { c.x = distX(rng); c.y = distY(rng); c.h = distH(rng); }

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            // Find nearest two cells
            float d1 = 1e30f, d2 = 1e30f;
            float h1 = 0;
            for (const auto& c : cells) {
                float d = (pos.x - c.x) * (pos.x - c.x) + (pos.y - c.y) * (pos.y - c.y);
                if (d < d1) { d2 = d1; d1 = d; h1 = c.h; }
                else if (d < d2) { d2 = d; }
            }
            // F2-F1 creates ridge patterns at cell boundaries
            float edge = std::sqrt(d2) - std::sqrt(d1);
            float edgeNorm = std::min(edge / 30.0f, 1.0f);
            chunk.heightMap.heights[v] += h1 * (1.0f - edgeNorm * 0.5f);
        }
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::createDunes(float wavelength, float amplitude, float direction, uint32_t seed) {
    if (!terrain_) return;
    recordGeneratorUndo();
    float dirRad = direction * 3.14159f / 180.0f;
    float dx = std::cos(dirRad), dy = std::sin(dirRad);

    // Secondary wave for variation
    auto hash = [](int x, uint32_t s) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + s * 668265263);
        h = (h ^ (h >> 13)) * 1274126177;
        return (static_cast<float>(h & 0xFFFF) / 65535.0f - 0.5f) * 2.0f;
    };

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            float proj = pos.x * dx + pos.y * dy;
            float wave = std::sin(proj / wavelength * 6.2832f) * amplitude;
            float secondary = std::sin(proj / (wavelength * 2.3f) * 6.2832f + seed * 0.1f) * amplitude * 0.3f;
            float perp = pos.x * dy - pos.y * dx;
            float variation = hash(static_cast<int>(perp * 0.1f), seed) * amplitude * 0.15f;
            chunk.heightMap.heights[v] += wave + secondary + variation;
        }
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::rotateTerrain90() {
    if (!terrain_) return;
    recordGeneratorUndo();
    // Snapshot all outer vertex heights into a 129x129 grid
    std::array<std::array<float, 129>, 129> grid{};
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            auto& chunk = terrain_->chunks[cy * 16 + cx];
            if (!chunk.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue;
                grid[cy * 8 + row][cx * 8 + col] = chunk.heightMap.heights[v];
            }
        }
    }
    // Rotate 90 degrees CW: new[x][128-y] = old[y][x]
    std::array<std::array<float, 129>, 129> rotated{};
    for (int y = 0; y < 129; y++)
        for (int x = 0; x < 129; x++)
            rotated[x][128 - y] = grid[y][x];
    // Write back
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            auto& chunk = terrain_->chunks[cy * 16 + cx];
            if (!chunk.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) {
                    // Inner vertex: average of surrounding outer
                    int innerCol = col - 9;
                    int gy = cy * 8 + row, gx = cx * 8 + innerCol;
                    if (gy < 128 && gx < 128)
                        chunk.heightMap.heights[v] = (rotated[gy][gx] + rotated[gy][gx+1] +
                                                       rotated[gy+1][gx] + rotated[gy+1][gx+1]) * 0.25f;
                } else {
                    chunk.heightMap.heights[v] = rotated[cy * 8 + row][cx * 8 + col];
                }
            }
            dirtyChunks_.push_back(cy * 16 + cx);
        }
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::offsetHeights(float amount) {
    if (!terrain_) return;
    recordGeneratorUndo();
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++)
            chunk.heightMap.heights[v] += amount;
        dirtyChunks_.push_back(ci);
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::invertHeights() {
    if (!terrain_) return;
    recordGeneratorUndo();
    // Find midpoint
    float minH = 1e30f, maxH = -1e30f;
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            minH = std::min(minH, chunk.heightMap.heights[v]);
            maxH = std::max(maxH, chunk.heightMap.heights[v]);
        }
    }
    float mid = (minH + maxH) * 0.5f;
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++)
            chunk.heightMap.heights[v] = mid - (chunk.heightMap.heights[v] - mid);
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::fillWater(float height, uint16_t liquidType) {
    if (!terrain_) return;
    for (int ci = 0; ci < 256; ci++) {
        auto& water = terrain_->waterData[ci];
        if (water.layers.empty()) {
            pipeline::ADTTerrain::WaterLayer wl;
            wl.liquidType = liquidType;
            wl.flags = 0;
            wl.minHeight = height;
            wl.maxHeight = height;
            wl.x = 0; wl.y = 0; wl.width = 9; wl.height = 9;
            wl.heights.assign(81, height);
            wl.mask.assign(8, 0xFF);
            water.layers.push_back(wl);
        } else {
            auto& wl = water.layers[0];
            wl.liquidType = liquidType;
            wl.minHeight = height;
            wl.maxHeight = height;
            std::fill(wl.heights.begin(), wl.heights.end(), height);
        }
        dirtyChunks_.push_back(ci);
    }
    dirty_ = true;
}

void TerrainEditor::fillWaterAlongPath(const glm::vec3& start, const glm::vec3& end,
                                          float width, uint16_t liquidType) {
    if (!terrain_) return;
    if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(end.x) || !std::isfinite(end.y) ||
        !std::isfinite(width) || width <= 0.0f) return;
    glm::vec2 lineStart(start.x, start.y);
    glm::vec2 lineEnd(end.x, end.y);
    float lineLen = glm::length(lineEnd - lineStart);
    if (lineLen < 1e-4f) return;
    glm::vec2 lineDir = (lineEnd - lineStart) / lineLen;

    // Each chunk is 33.33 yards across, so a chunk's diagonal half is
    // ~23.6 yards. Treat any chunk whose center lies within
    // `width + chunkHalfDiag` of the river segment as "intersected" so
    // the water layer covers any cell the carved channel touches.
    constexpr float kChunkSize = 33.33333f;
    const float chunkHalfDiag = kChunkSize * 0.7071f;  // sqrt(2)/2 of size

    int filled = 0;
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        // Chunk center in world coords. position[0]=wowY, [1]=wowX so the
        // 2D vector here is (X, Y) for the chunk's center.
        glm::vec2 chunkCenter(chunk.position[1] + kChunkSize * 0.5f,
                               chunk.position[0] + kChunkSize * 0.5f);
        glm::vec2 toC = chunkCenter - lineStart;
        float t = glm::dot(toC, lineDir);
        t = std::clamp(t, 0.0f, lineLen);
        glm::vec2 closest = lineStart + lineDir * t;
        float dist = glm::length(chunkCenter - closest);
        if (dist > width + chunkHalfDiag) continue;

        // Find min terrain height in this chunk after carving - water
        // goes to that level + a small offset so it visibly fills the
        // channel without overflowing onto banks.
        float minH = 1e30f;
        for (int v = 0; v < 145; v++) {
            float absH = chunk.position[2] + chunk.heightMap.heights[v];
            if (absH < minH) minH = absH;
        }
        if (minH > 1e29f) continue;
        float waterH = minH + 0.5f;

        auto& water = terrain_->waterData[ci];
        if (water.layers.empty()) {
            pipeline::ADTTerrain::WaterLayer wl;
            wl.liquidType = liquidType;
            wl.flags = 0;
            wl.minHeight = waterH;
            wl.maxHeight = waterH;
            wl.x = 0; wl.y = 0; wl.width = 9; wl.height = 9;
            wl.heights.assign(81, waterH);
            wl.mask.assign(8, 0xFF);
            water.layers.push_back(wl);
        } else {
            auto& wl = water.layers[0];
            wl.liquidType = liquidType;
            wl.minHeight = waterH;
            wl.maxHeight = waterH;
            std::fill(wl.heights.begin(), wl.heights.end(), waterH);
        }
        dirtyChunks_.push_back(ci);
        filled++;
    }
    if (filled > 0) dirty_ = true;
}

void TerrainEditor::smoothBeaches(float waterHeight, float beachWidth) {
    if (!terrain_) return;
    recordGeneratorUndo();
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            float absH = chunk.position[2] + chunk.heightMap.heights[v];
            float distToWater = std::abs(absH - waterHeight);
            if (distToWater < beachWidth) {
                // Smooth toward water level with gentle slope
                float t = distToWater / beachWidth;
                float target = waterHeight + (absH > waterHeight ? 1.0f : -1.0f) * beachWidth * t * t;
                float relTarget = target - chunk.position[2];
                chunk.heightMap.heights[v] = chunk.heightMap.heights[v] * 0.3f + relTarget * 0.7f;
                modified = true;
            }
        }
        if (modified) { stitchEdges(ci); dirtyChunks_.push_back(ci); }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::addDetailNoise(float amplitude, float frequency, uint32_t seed) {
    recordGeneratorUndo();
    if (!terrain_) return;
    auto hash2d = [](int x, int y, uint32_t s) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263 + s);
        h = (h ^ (h >> 13)) * 1274126177;
        h = h ^ (h >> 16);
        return (static_cast<float>(h & 0xFFFF) / 65535.0f - 0.5f) * 2.0f;
    };
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            int ix = static_cast<int>(std::floor(pos.x * frequency));
            int iy = static_cast<int>(std::floor(pos.y * frequency));
            chunk.heightMap.heights[v] += hash2d(ix, iy, seed) * amplitude;
        }
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
}

void TerrainEditor::rampEdges(float targetHeight, float rampWidth) {
    if (!terrain_) return;
    float relTarget = targetHeight - terrain_->chunks[0].position[2];
    // Note: only affects terrain heights, not water levels

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        int cx = ci % 16, cy = ci / 16;

        for (int v = 0; v < 145; v++) {
            int row = v / 17, col = v % 17;
            if (col > 8) continue;

            // Distance to nearest tile edge (in chunk units)
            float edgeDistX = std::min(static_cast<float>(cx * 8 + col),
                                        static_cast<float>(128 - cx * 8 - col)) / 128.0f;
            float edgeDistY = std::min(static_cast<float>(cy * 8 + row),
                                        static_cast<float>(128 - cy * 8 - row)) / 128.0f;
            float edgeDist = std::min(edgeDistX, edgeDistY);
            float rampNorm = rampWidth / 128.0f;

            if (edgeDist < rampNorm) {
                float t = edgeDist / rampNorm;
                float blend = t * t; // smooth start
                chunk.heightMap.heights[v] = chunk.heightMap.heights[v] * blend +
                                              relTarget * (1.0f - blend);
            }
        }
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::thermalErosion(int iterations, float talusAngle) {
    recordGeneratorUndo();
    if (!terrain_) return;
    float unitSize = CHUNK_SIZE / 8.0f;
    float maxDelta = std::tan(talusAngle * 3.14159f / 180.0f) * unitSize;

    for (int iter = 0; iter < iterations; iter++) {
        for (int ci = 0; ci < 256; ci++) {
            auto& chunk = terrain_->chunks[ci];
            if (!chunk.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue;
                float h = chunk.heightMap.heights[v];
                int neighbors[] = {v - 17, v + 17, v - 1, v + 1};
                for (int n : neighbors) {
                    if (n < 0 || n >= 145) continue;
                    int nRow = n / 17, nCol = n % 17;
                    if (nCol > 8 || std::abs(nRow - row) > 1 || std::abs(nCol - col) > 1) continue;
                    float nh = chunk.heightMap.heights[n];
                    float delta = h - nh;
                    if (delta > maxDelta) {
                        float transfer = (delta - maxDelta) * 0.25f;
                        chunk.heightMap.heights[v] -= transfer;
                        chunk.heightMap.heights[n] += transfer;
                    }
                }
            }
        }
    }
    for (int ci = 0; ci < 256; ci++) {
        stitchEdges(ci);
        dirtyChunks_.push_back(ci);
    }
    dirty_ = true;
}

void TerrainEditor::terraceHeights(int steps) {
    if (!terrain_ || steps < 2) return;

    // Find height range
    float minH = 1e30f, maxH = -1e30f;
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            float h = chunk.position[2] + chunk.heightMap.heights[v];
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }
    float range = maxH - minH;
    if (range < 1.0f) return;
    float stepSize = range / steps;

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            float absH = chunk.position[2] + chunk.heightMap.heights[v];
            float quantized = std::floor((absH - minH) / stepSize) * stepSize + minH;
            chunk.heightMap.heights[v] = quantized - chunk.position[2];
        }
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::createCanyon(float width, float depth, uint32_t seed) {
    recordGeneratorUndo();
    if (!terrain_) return;

    float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * TILE_SIZE;
    float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * TILE_SIZE;
    float tileCenter_X = tileNW_X - TILE_SIZE * 0.5f;
    float tileCenter_Y = tileNW_Y - TILE_SIZE * 0.5f;

    // Generate a winding path using sine waves
    auto canyonPath = [&](float t) -> glm::vec2 {
        float px = tileCenter_X + (t - 0.5f) * TILE_SIZE * 0.9f;
        float py = tileCenter_Y + std::sin(t * 6.28f * 1.5f + seed * 0.1f) * TILE_SIZE * 0.2f
                   + std::sin(t * 6.28f * 3.0f + seed * 0.3f) * TILE_SIZE * 0.05f;
        return glm::vec2(px, py);
    };

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            glm::vec2 p(pos.x, pos.y);

            // Find nearest point on canyon path
            float bestDist = 1e30f;
            for (float t = 0.0f; t <= 1.0f; t += 0.005f) {
                glm::vec2 cp = canyonPath(t);
                float d = glm::length(p - cp);
                bestDist = std::min(bestDist, d);
            }

            if (bestDist < width) {
                float falloff = 1.0f - (bestDist / width);
                falloff = falloff * falloff;
                chunk.heightMap.heights[v] -= depth * falloff;
                modified = true;
            }
        }
        if (modified) { stitchEdges(ci); dirtyChunks_.push_back(ci); }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::createIsland(float centerHeight, float edgeDropoff) {
    if (!terrain_) return;
    recordGeneratorUndo();

    // Island shape: distance from tile center determines height
    // Center is high, edges drop below base height (underwater)
    float tileCenterX = 0, tileCenterY = 0;
    // Compute tile center from first chunk
    {
        float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * TILE_SIZE;
        float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * TILE_SIZE;
        tileCenterX = tileNW_X - TILE_SIZE * 0.5f;
        tileCenterY = tileNW_Y - TILE_SIZE * 0.5f;
    }
    float maxDist = TILE_SIZE * 0.45f; // island radius slightly smaller than tile

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            float dist = glm::length(glm::vec2(pos.x - tileCenterX, pos.y - tileCenterY));
            float t = std::clamp(dist / maxDist, 0.0f, 1.0f);

            // Smooth island falloff: high center, gradual drop, steep beach
            float islandH;
            if (t < 0.6f) {
                islandH = centerHeight; // flat interior
            } else if (t < 0.85f) {
                float beachT = (t - 0.6f) / 0.25f;
                islandH = centerHeight * (1.0f - beachT * beachT);
            } else {
                float dropT = (t - 0.85f) / 0.15f;
                islandH = centerHeight * (1.0f - 0.85f * 0.85f) * (1.0f - dropT) - edgeDropoff * dropT;
            }

            chunk.heightMap.heights[v] = islandH + chunk.heightMap.heights[v] * 0.3f;
        }
        dirtyChunks_.push_back(ci);
    }
    for (int ci = 0; ci < 256; ci++) stitchEdges(ci);
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::createRidge(const glm::vec3& start, const glm::vec3& end,
                                 float width, float height) {
    if (!terrain_) return;
    if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(end.x) || !std::isfinite(end.y) ||
        !std::isfinite(width) || width <= 0.0f ||
        !std::isfinite(height)) return;
    recordGeneratorUndo();
    glm::vec2 lineStart(start.x, start.y);
    glm::vec2 lineEnd(end.x, end.y);
    float lineLen = glm::length(lineEnd - lineStart);
    if (lineLen < 1e-4f) return;
    glm::vec2 lineDir = (lineEnd - lineStart) / lineLen;

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            glm::vec2 p(pos.x, pos.y);
            glm::vec2 toP = p - lineStart;
            float along = glm::dot(toP, lineDir);
            along = std::clamp(along, 0.0f, lineLen);
            glm::vec2 closest = lineStart + lineDir * along;
            float dist = glm::length(p - closest);
            if (dist >= width) continue;

            float crossFalloff = 1.0f - (dist / width);
            crossFalloff = crossFalloff * crossFalloff;
            float alongFalloff = 1.0f - 2.0f * std::abs(along / lineLen - 0.5f);
            alongFalloff = std::max(0.0f, alongFalloff);
            float h = height * crossFalloff * std::sqrt(alongFalloff);
            chunk.heightMap.heights[v] += h;
            modified = true;
        }
        if (modified) { stitchEdges(ci); dirtyChunks_.push_back(ci); }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::flattenRoad(const glm::vec3& start, const glm::vec3& end, float width) {
    if (!terrain_) return;
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(start.z) ||
        !std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z) ||
        !std::isfinite(width) || width <= 0.0f) return;
    recordGeneratorUndo();
    glm::vec2 lineStart(start.x, start.y);
    glm::vec2 lineEnd(end.x, end.y);
    float lineLen = glm::length(lineEnd - lineStart);
    // Zero-length road would normalize to NaN and propagate NaN through
    // every dist comparison - paint the height delta on every vertex.
    if (lineLen < 1e-4f) return;
    glm::vec2 lineDir = (lineEnd - lineStart) / lineLen;

    // Interpolate height along the path
    auto heightAtT = [&](float t) -> float {
        return start.z + (end.z - start.z) * (t / lineLen);
    };

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;

        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            glm::vec2 p(pos.x, pos.y);
            glm::vec2 toP = p - lineStart;
            float t = glm::dot(toP, lineDir);
            t = std::clamp(t, 0.0f, lineLen);
            glm::vec2 closest = lineStart + lineDir * t;
            float dist = glm::length(p - closest);

            if (dist < width) {
                float targetH = heightAtT(t);
                float relTarget = targetH - chunk.position[2];
                float falloff = 1.0f - (dist / width);
                falloff = falloff * falloff;
                float h = chunk.heightMap.heights[v];
                chunk.heightMap.heights[v] = h + (relTarget - h) * falloff;
                modified = true;
            }
        }
        if (modified) {
            stitchEdges(ci);
            dirtyChunks_.push_back(ci);
        }
    }
    dirty_ = true;
    commitGeneratorUndo();
}

void TerrainEditor::copyStamp(const glm::vec3& center, float radius) {
    if (!terrain_) return;
    stampData_.clear();
    stampCenter_ = center;

    for (int ci = 0; ci < 256; ci++) {
        if (!terrain_->chunks[ci].hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(ci, v);
            float dx = pos.x - center.x;
            float dy = pos.y - center.y;
            if (std::sqrt(dx * dx + dy * dy) <= radius) {
                StampVertex sv;
                sv.dx = dx;
                sv.dy = dy;
                sv.height = terrain_->chunks[ci].heightMap.heights[v];
                stampData_.push_back(sv);
            }
        }
    }
    LOG_INFO("Stamp copied: ", stampData_.size(), " vertices in radius ", radius);
}

void TerrainEditor::pasteStamp(const glm::vec3& center) {
    if (!terrain_ || stampData_.empty()) return;

    for (const auto& sv : stampData_) {
        float wx = center.x + sv.dx;
        float wy = center.y + sv.dy;

        // Find nearest vertex and set its height
        float bestDist = 1e30f;
        int bestChunk = -1, bestVert = -1;
        for (int ci = 0; ci < 256; ci++) {
            if (!terrain_->chunks[ci].hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                glm::vec3 pos = chunkVertexWorldPos(ci, v);
                float d = std::sqrt((pos.x - wx) * (pos.x - wx) + (pos.y - wy) * (pos.y - wy));
                if (d < bestDist && d < 3.0f) {
                    bestDist = d;
                    bestChunk = ci;
                    bestVert = v;
                }
            }
        }
        if (bestChunk >= 0) {
            terrain_->chunks[bestChunk].heightMap.heights[bestVert] = sv.height;
            if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), bestChunk) == dirtyChunks_.end())
                dirtyChunks_.push_back(bestChunk);
        }
    }

    for (int ci : dirtyChunks_) stitchEdges(ci);
    dirty_ = true;
    LOG_INFO("Stamp pasted at (", center.x, ",", center.y, ")");
}

bool TerrainEditor::saveStamp(const std::string& path) const {
    if (stampData_.empty()) return false;
    nlohmann::json j;
    j["format"] = "wowee-stamp-1.0";
    j["vertexCount"] = stampData_.size();
    // Scrub NaN samples on save - nlohmann::json throws on non-finite
    // serialization, which would abort the entire save.
    auto san = [](float x) { return std::isfinite(x) ? x : 0.0f; };
    nlohmann::json verts = nlohmann::json::array();
    for (const auto& sv : stampData_)
        verts.push_back({san(sv.dx), san(sv.dy), san(sv.height)});
    j["vertices"] = verts;

    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2) << "\n";
    LOG_INFO("Stamp saved: ", path, " (", stampData_.size(), " vertices)");
    return true;
}

bool TerrainEditor::loadStamp(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    try {
        auto j = nlohmann::json::parse(f);
        if (!j.contains("vertices") || !j["vertices"].is_array()) return false;

        stampData_.clear();
        // Cap stamp size - stamps blast directly into chunk heights, so a
        // huge or NaN-laden stamp would corrupt the terrain irreversibly.
        constexpr size_t kMaxStampVerts = 1'000'000;
        if (j["vertices"].size() > kMaxStampVerts) {
            LOG_ERROR("Stamp vertexCount too large: ", j["vertices"].size());
            return false;
        }
        for (const auto& v : j["vertices"]) {
            if (!v.is_array() || v.size() < 3) continue;
            StampVertex sv;
            sv.dx = v[0].get<float>();
            sv.dy = v[1].get<float>();
            sv.height = v[2].get<float>();
            // Skip non-finite samples - they'd propagate through brush blends
            // and produce permanent NaN holes in the heightmap.
            if (!std::isfinite(sv.dx) || !std::isfinite(sv.dy) ||
                !std::isfinite(sv.height)) continue;
            stampData_.push_back(sv);
        }
        stampCenter_ = glm::vec3(0);
        LOG_INFO("Stamp loaded: ", path, " (", stampData_.size(), " vertices)");
        return !stampData_.empty();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load stamp: ", e.what());
        return false;
    }
}

void TerrainEditor::clampHeights(float minH, float maxH) {
    if (!terrain_) return;
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        bool modified = false;
        for (int v = 0; v < 145; v++) {
            float absH = chunk.position[2] + chunk.heightMap.heights[v];
            if (absH < minH) {
                chunk.heightMap.heights[v] = minH - chunk.position[2];
                modified = true;
            } else if (absH > maxH) {
                chunk.heightMap.heights[v] = maxH - chunk.position[2];
                modified = true;
            }
        }
        if (modified) dirtyChunks_.push_back(ci);
    }
    dirty_ = true;
}

void TerrainEditor::applyErode(float dt) {
    float factor = std::min(1.0f, brush_.settings().strength * dt * 0.3f);

    auto affected = getAffectedChunks(brush_.getPosition(), brush_.settings().radius);
    for (int chunkIdx : affected) {
        bool modified = false;
        auto& chunk = terrain_->chunks[chunkIdx];
        for (int v = 0; v < 145; v++) {
            glm::vec3 pos = chunkVertexWorldPos(chunkIdx, v);
            float dist = glm::length(glm::vec2(pos.x - brush_.getPosition().x,
                                                pos.y - brush_.getPosition().y));
            float influence = brush_.getInfluence(dist);
            if (influence <= 0.0f) continue;

            float h = chunk.heightMap.heights[v];
            int col = v % 17;

            // Find lowest neighbor (same chunk)
            float lowestH = h;
            if (col <= 8) {
                int neighbors[] = {v - 17, v + 17, v - 1, v + 1};
                for (int n : neighbors) {
                    if (n >= 0 && n < 145)
                        lowestH = std::min(lowestH, chunk.heightMap.heights[n]);
                }
            }

            // Move height toward lowest neighbor (erosion)
            float slope = h - lowestH;
            if (slope > 0.1f) {
                float erosion = slope * factor * influence * 0.3f;
                chunk.heightMap.heights[v] -= erosion;
                modified = true;
            }
        }
        if (modified) {
            stitchEdges(chunkIdx);
            if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), chunkIdx) == dirtyChunks_.end())
                dirtyChunks_.push_back(chunkIdx);
            dirty_ = true;
        }
    }
}

void TerrainEditor::applyNoise(float frequency, float amplitude, int octaves, uint32_t seed) {
    recordGeneratorUndo();
    if (!terrain_) return;

    // Simple value noise with octaves
    auto hash2d = [](int x, int y, uint32_t s) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263 + s * 1274126177);
        h = (h ^ (h >> 13)) * 1274126177;
        h = h ^ (h >> 16);
        return static_cast<float>(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
    };

    auto smoothNoise = [&](float fx, float fy, uint32_t s) -> float {
        int ix = static_cast<int>(std::floor(fx));
        int iy = static_cast<int>(std::floor(fy));
        float fracX = fx - ix;
        float fracY = fy - iy;
        // Smoothstep
        fracX = fracX * fracX * (3.0f - 2.0f * fracX);
        fracY = fracY * fracY * (3.0f - 2.0f * fracY);
        float v00 = hash2d(ix, iy, s);
        float v10 = hash2d(ix + 1, iy, s);
        float v01 = hash2d(ix, iy + 1, s);
        float v11 = hash2d(ix + 1, iy + 1, s);
        float i0 = v00 + (v10 - v00) * fracX;
        float i1 = v01 + (v11 - v01) * fracX;
        return i0 + (i1 - i0) * fracY;
    };

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;
        for (int v = 0; v < 145; v++) {
            glm::vec3 wpos = chunkVertexWorldPos(ci, v);

            float total = 0.0f;
            float amp = amplitude;
            float freq = frequency;
            for (int o = 0; o < octaves; o++) {
                total += smoothNoise(wpos.x * freq, wpos.y * freq, seed + o * 97) * amp;
                freq *= 2.0f;
                amp *= 0.5f;
            }
            chunk.heightMap.heights[v] += total;
        }
        dirtyChunks_.push_back(ci);
    }
    dirty_ = true;
    commitGeneratorUndo();
}

bool TerrainEditor::importHeightmap(const std::string& path, float heightScale) {
    if (!terrain_) return false;
    recordGeneratorUndo();
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { return false; }
    auto fileSize = f.tellg();
    f.seekg(0);

    // Determine resolution from file size
    // 129x129 x 2 bytes = 33282 (one chunk row+1 per tile row+1)
    // 257x257 x 2 bytes = 132098 (2 samples per chunk quad)
    int res = 0;
    if (fileSize >= 132098) res = 257;
    else if (fileSize >= 33282) res = 129;
    else if (fileSize >= 16641) { res = 129; } // 8-bit 129x129
    else return false;

    bool is16bit = (fileSize >= res * res * 2);
    std::vector<float> heightData(res * res);

    if (is16bit) {
        std::vector<uint16_t> raw(res * res);
        f.read(reinterpret_cast<char*>(raw.data()), res * res * 2);
        for (int i = 0; i < res * res; i++)
            heightData[i] = static_cast<float>(raw[i]) / 65535.0f;
    } else {
        std::vector<uint8_t> raw(res * res);
        f.read(reinterpret_cast<char*>(raw.data()), res * res);
        for (int i = 0; i < res * res; i++)
            heightData[i] = static_cast<float>(raw[i]) / 255.0f;
    }

    // Map heightmap pixels to terrain vertices
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            auto& chunk = terrain_->chunks[cy * 16 + cx];
            if (!chunk.hasHeightMap()) continue;

            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                float offX = static_cast<float>(col);
                float offY = static_cast<float>(row);
                if (col > 8) { offY += 0.5f; offX -= 8.5f; }

                // Map to pixel coords
                float px = (cx * 8.0f + offX) / 128.0f * (res - 1);
                float py = (cy * 8.0f + offY) / 128.0f * (res - 1);
                int ix = std::clamp(static_cast<int>(px), 0, res - 1);
                int iy = std::clamp(static_cast<int>(py), 0, res - 1);

                chunk.heightMap.heights[v] = heightData[iy * res + ix] * heightScale;
            }
            dirtyChunks_.push_back(cy * 16 + cx);
        }
    }
    dirty_ = true;
    commitGeneratorUndo();
    return true;
}

bool TerrainEditor::importHeightmapImage(const std::string& path, float heightScale) {
    if (!terrain_) return false;
    recordGeneratorUndo();

    int w = 0, h = 0, channels = 0;
    bool is16 = false;
    std::vector<float> heightData;

    // Try 16-bit first for precision
    unsigned short* data16 = stbi_load_16(path.c_str(), &w, &h, &channels, 1);
    // Pre-multiply as size_t so an attacker-supplied (or just very large)
    // 65535×65535 image can't overflow int before the resize is sized.
    const size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (data16) {
        is16 = true;
        heightData.resize(pixelCount);
        for (size_t i = 0; i < pixelCount; i++)
            heightData[i] = static_cast<float>(data16[i]) / 65535.0f;
        stbi_image_free(data16);
    } else {
        unsigned char* data8 = stbi_load(path.c_str(), &w, &h, &channels, 1);
        if (!data8) {
            LOG_ERROR("Failed to load heightmap image: ", path);
            commitGeneratorUndo();
            return false;
        }
        const size_t pixelCount8 = static_cast<size_t>(w) * static_cast<size_t>(h);
        heightData.resize(pixelCount8);
        for (size_t i = 0; i < pixelCount8; i++)
            heightData[i] = static_cast<float>(data8[i]) / 255.0f;
        stbi_image_free(data8);
    }

    LOG_INFO("Heightmap image loaded: ", path, " (", w, "x", h,
             is16 ? " 16-bit" : " 8-bit", ")");

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            auto& chunk = terrain_->chunks[cy * 16 + cx];
            if (!chunk.hasHeightMap()) continue;

            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                float offX = static_cast<float>(col);
                float offY = static_cast<float>(row);
                if (col > 8) { offY += 0.5f; offX -= 8.5f; }

                float u = (cx * 8.0f + offX) / 128.0f;
                float vv = (cy * 8.0f + offY) / 128.0f;
                int px = std::clamp(static_cast<int>(u * (w - 1)), 0, w - 1);
                int py = std::clamp(static_cast<int>(vv * (h - 1)), 0, h - 1);

                chunk.heightMap.heights[v] = heightData[py * w + px] * heightScale;
            }
            stitchEdges(cy * 16 + cx);
            dirtyChunks_.push_back(cy * 16 + cx);
        }
    }
    dirty_ = true;
    commitGeneratorUndo();
    LOG_INFO("Heightmap applied: scale=", heightScale);
    return true;
}

bool TerrainEditor::exportHeightmap(const std::string& path, float heightScale) {
    if (!terrain_) return false;
    constexpr int res = 129;
    std::vector<uint16_t> data(res * res, 0);

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            auto& chunk = terrain_->chunks[cy * 16 + cx];
            if (!chunk.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                int row = v / 17, col = v % 17;
                if (col > 8) continue; // outer vertices only for 129x129
                int px = cx * 8 + col;
                int py = cy * 8 + row;
                if (px >= res || py >= res) continue;
                float h = (chunk.position[2] + chunk.heightMap.heights[v]) / heightScale;
                h = std::clamp(h, 0.0f, 1.0f);
                data[py * res + px] = static_cast<uint16_t>(h * 65535.0f);
            }
        }
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), data.size() * 2);
    return true;
}

void TerrainEditor::punchHole(const glm::vec3& center, float radius) {
    if (!terrain_) return;
    auto affected = getAffectedChunks(center, radius);
    for (int ci : affected) {
        auto& chunk = terrain_->chunks[ci];
        // Each chunk has 8x8 quads, holes use a 4x4 bitmask (each bit covers 2x2 quads)
        for (int hy = 0; hy < 4; hy++) {
            for (int hx = 0; hx < 4; hx++) {
                // Center of this 2x2 quad group
                int cx = ci % 16, cy = ci / 16;
                float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * TILE_SIZE;
                float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * TILE_SIZE;
                float qx = tileNW_X - cy * CHUNK_SIZE - (hy * 2 + 1) * CHUNK_SIZE / 8.0f;
                float qy = tileNW_Y - cx * CHUNK_SIZE - (hx * 2 + 1) * CHUNK_SIZE / 8.0f;
                float dist = std::sqrt((qx - center.x) * (qx - center.x) +
                                       (qy - center.y) * (qy - center.y));
                if (dist < radius) {
                    int bit = 1 << (hy * 4 + hx);
                    chunk.holes |= static_cast<uint16_t>(bit);
                }
            }
        }
        if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), ci) == dirtyChunks_.end())
            dirtyChunks_.push_back(ci);
        dirty_ = true;
    }
}

void TerrainEditor::fillHole(const glm::vec3& center, float radius) {
    if (!terrain_) return;
    auto affected = getAffectedChunks(center, radius);
    for (int ci : affected) {
        auto& chunk = terrain_->chunks[ci];
        for (int hy = 0; hy < 4; hy++) {
            for (int hx = 0; hx < 4; hx++) {
                int cx = ci % 16, cy = ci / 16;
                float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * TILE_SIZE;
                float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * TILE_SIZE;
                float qx = tileNW_X - cy * CHUNK_SIZE - (hy * 2 + 1) * CHUNK_SIZE / 8.0f;
                float qy = tileNW_Y - cx * CHUNK_SIZE - (hx * 2 + 1) * CHUNK_SIZE / 8.0f;
                float dist = std::sqrt((qx - center.x) * (qx - center.x) +
                                       (qy - center.y) * (qy - center.y));
                if (dist < radius) {
                    int bit = 1 << (hy * 4 + hx);
                    chunk.holes &= ~static_cast<uint16_t>(bit);
                }
            }
        }
        if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), ci) == dirtyChunks_.end())
            dirtyChunks_.push_back(ci);
        dirty_ = true;
    }
}

} // namespace editor
} // namespace wowee
