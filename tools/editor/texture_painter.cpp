#include "texture_painter.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace wowee {
namespace editor {

void TexturePainter::setActiveTexture(const std::string& texturePath) {
    activeTexture_ = texturePath;
    // Track recent textures (max 10)
    auto it = std::find(recentTextures_.begin(), recentTextures_.end(), texturePath);
    if (it != recentTextures_.end()) recentTextures_.erase(it);
    recentTextures_.insert(recentTextures_.begin(), texturePath);
    if (recentTextures_.size() > 10) recentTextures_.pop_back();
}

uint32_t TexturePainter::ensureTextureInList(const std::string& path) {
    for (uint32_t i = 0; i < terrain_->textures.size(); i++) {
        if (terrain_->textures[i] == path) return i;
    }
    terrain_->textures.push_back(path);
    return static_cast<uint32_t>(terrain_->textures.size() - 1);
}

int TexturePainter::ensureLayerOnChunk(int chunkIdx, uint32_t textureId) {
    auto& chunk = terrain_->chunks[chunkIdx];

    for (int i = 0; i < static_cast<int>(chunk.layers.size()); i++) {
        if (chunk.layers[i].textureId == textureId) return i;
    }

    if (chunk.layers.size() < 4) {
        pipeline::TextureLayer layer{};
        layer.textureId = textureId;
        layer.flags = 0x100;
        layer.offsetMCAL = static_cast<uint32_t>(chunk.alphaMap.size());
        layer.effectId = 0;
        chunk.layers.push_back(layer);
        chunk.alphaMap.resize(chunk.alphaMap.size() + 4096, 0);
        return static_cast<int>(chunk.layers.size() - 1);
    }

    // At 4 layers - find the non-base layer with lowest total alpha and replace it
    int weakest = -1;
    int weakestSum = INT32_MAX;
    for (int i = 1; i < static_cast<int>(chunk.layers.size()); i++) {
        if (chunk.layers[i].textureId == textureId) return i;
        size_t off = chunk.layers[i].offsetMCAL;
        if (off + 4096 > chunk.alphaMap.size()) continue;
        int sum = 0;
        for (int j = 0; j < 4096; j++) sum += chunk.alphaMap[off + j];
        if (sum < weakestSum) { weakestSum = sum; weakest = i; }
    }

    if (weakest < 0) return -1;

    // Replace the weakest layer
    chunk.layers[weakest].textureId = textureId;
    size_t off = chunk.layers[weakest].offsetMCAL;
    std::fill(chunk.alphaMap.begin() + off, chunk.alphaMap.begin() + off + 4096, 0);
    return weakest;
}

glm::vec2 TexturePainter::worldToChunkUV(int chunkIdx, const glm::vec3& worldPos) const {
    int cx = chunkIdx % 16;
    int cy = chunkIdx / 16;
    int tileX = terrain_->coord.x;
    int tileY = terrain_->coord.y;

    float tileNW_X = (32.0f - static_cast<float>(tileY)) * TILE_SIZE;
    float tileNW_Y = (32.0f - static_cast<float>(tileX)) * TILE_SIZE;
    float chunkBaseX = tileNW_X - static_cast<float>(cy) * CHUNK_SIZE;
    float chunkBaseY = tileNW_Y - static_cast<float>(cx) * CHUNK_SIZE;

    // UV: 0,0 at chunk NW corner, 1,1 at SE corner
    float u = (chunkBaseX - worldPos.x) / CHUNK_SIZE;
    float v = (chunkBaseY - worldPos.y) / CHUNK_SIZE;
    return glm::vec2(u, v);
}

void TexturePainter::modifyAlpha(int chunkIdx, int layerIdx, const glm::vec3& center,
                                  float radius, float strength, float falloff, bool erasing) {
    // Reject NaN center / non-positive radius up front. Without this, every
    // dist comparison below becomes NaN-vs-finite (which returns false), so
    // the falloff path falls through and paints full strength on every
    // texel in the chunk - the opposite of what was asked.
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(radius) || radius <= 0.0f) return;
    auto& chunk = terrain_->chunks[chunkIdx];
    auto& layer = chunk.layers[layerIdx];

    // Find alpha data offset for this layer
    size_t alphaOffset = layer.offsetMCAL;
    if (alphaOffset + 4096 > chunk.alphaMap.size()) return;

    int cx = chunkIdx % 16;
    int cy = chunkIdx / 16;
    int tileX = terrain_->coord.x;
    int tileY = terrain_->coord.y;

    float tileNW_X = (32.0f - static_cast<float>(tileY)) * TILE_SIZE;
    float tileNW_Y = (32.0f - static_cast<float>(tileX)) * TILE_SIZE;
    float chunkBaseX = tileNW_X - static_cast<float>(cy) * CHUNK_SIZE;
    float chunkBaseY = tileNW_Y - static_cast<float>(cx) * CHUNK_SIZE;

    float texelSize = CHUNK_SIZE / 64.0f;

    for (int ty = 0; ty < 64; ty++) {
        for (int tx = 0; tx < 64; tx++) {
            // World position of this alpha texel
            float wx = chunkBaseX - (static_cast<float>(ty) + 0.5f) * texelSize;
            float wy = chunkBaseY - (static_cast<float>(tx) + 0.5f) * texelSize;

            float dist = std::sqrt((wx - center.x) * (wx - center.x) +
                                   (wy - center.y) * (wy - center.y));
            if (dist >= radius) continue;

            // Falloff
            float t = dist / radius;
            float innerRadius = 1.0f - falloff;
            float influence = 1.0f;
            if (t > innerRadius && falloff > 0.001f) {
                float ft = (t - innerRadius) / falloff;
                influence = 1.0f - ft * ft;
            }

            size_t idx = alphaOffset + ty * 64 + tx;
            float current = static_cast<float>(chunk.alphaMap[idx]) / 255.0f;
            float delta = strength * influence;

            float newVal;
            if (erasing)
                newVal = std::max(0.0f, current - delta);
            else
                newVal = std::min(1.0f, current + delta);

            chunk.alphaMap[idx] = static_cast<uint8_t>(newVal * 255.0f);
        }
    }
}

std::vector<int> TexturePainter::paint(const glm::vec3& center, float radius,
                                        float strength, float falloff) {
    if (!terrain_ || activeTexture_.empty()) return {};

    uint32_t texId = ensureTextureInList(activeTexture_);
    std::vector<int> modified;

    for (int i = 0; i < 256; i++) {
        if (!terrain_->chunks[i].hasHeightMap()) continue;

        // Quick distance check from chunk center
        int cx = i % 16;
        int cy = i / 16;
        float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * TILE_SIZE;
        float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * TILE_SIZE;
        float chunkCenterX = tileNW_X - (cy + 0.5f) * CHUNK_SIZE;
        float chunkCenterY = tileNW_Y - (cx + 0.5f) * CHUNK_SIZE;
        float dist = std::sqrt((chunkCenterX - center.x) * (chunkCenterX - center.x) +
                               (chunkCenterY - center.y) * (chunkCenterY - center.y));
        if (dist > radius + CHUNK_SIZE) continue;

        int layerIdx = ensureLayerOnChunk(i, texId);
        if (layerIdx < 0) continue; // chunk full

        modifyAlpha(i, layerIdx, center, radius, strength, falloff, false);
        modified.push_back(i);
    }

    return modified;
}

void TexturePainter::autoPaintByHeight(const std::vector<HeightBand>& bands) {
    if (!terrain_ || bands.empty()) return;

    // Ensure all band textures are in the texture list
    for (const auto& band : bands)
        ensureTextureInList(band.texturePath);

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap()) continue;

        // Find average height of this chunk
        float avgH = chunk.position[2];
        float sum = 0;
        for (int v = 0; v < 145; v++) sum += chunk.heightMap.heights[v];
        avgH += sum / 145.0f;

        // Find which band this chunk falls into
        for (const auto& band : bands) {
            if (avgH <= band.maxHeight) {
                uint32_t texId = ensureTextureInList(band.texturePath);
                if (!chunk.layers.empty())
                    chunk.layers[0].textureId = texId;
                break;
            }
        }
    }
}

void TexturePainter::autoPaintBySlope(float slopeThreshold, const std::string& steepTexture) {
    if (!terrain_) return;
    uint32_t steepTexId = ensureTextureInList(steepTexture);

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap() || chunk.layers.empty()) continue;

        // Compute average slope from normals
        float maxSlope = 0.0f;
        for (int v = 0; v < 145; v++) {
            float nz = static_cast<float>(chunk.normals[v * 3 + 2]) / 127.0f;
            float slope = 1.0f - std::abs(nz);
            maxSlope = std::max(maxSlope, slope);
        }

        if (maxSlope > slopeThreshold) {
            // Add steep texture as a layer
            int layerIdx = ensureLayerOnChunk(ci, steepTexId);
            if (layerIdx > 0) {
                size_t off = chunk.layers[layerIdx].offsetMCAL;
                if (off + 4096 <= chunk.alphaMap.size()) {
                    for (int ty = 0; ty < 64; ty++) {
                        for (int tx = 0; tx < 64; tx++) {
                            // Sample slope at this texel
                            int vi = (ty / 8) * 17 + (tx / 8);
                            if (vi >= 145) vi = 144;
                            float nz = static_cast<float>(chunk.normals[vi * 3 + 2]) / 127.0f;
                            float slope = 1.0f - std::abs(nz);
                            float alpha = std::clamp((slope - slopeThreshold * 0.5f) / (1.0f - slopeThreshold * 0.5f), 0.0f, 1.0f);
                            chunk.alphaMap[off + ty * 64 + tx] = static_cast<uint8_t>(alpha * 255.0f);
                        }
                    }
                }
            }
        }
    }
}

void TexturePainter::paintAlongPath(const glm::vec3& start, const glm::vec3& end,
                                     float width, const std::string& texturePath) {
    if (!terrain_ || texturePath.empty()) return;
    if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(end.x) || !std::isfinite(end.y) ||
        !std::isfinite(width) || width <= 0.0f) return;
    uint32_t texId = ensureTextureInList(texturePath);
    glm::vec2 lineStart(start.x, start.y);
    glm::vec2 lineEnd(end.x, end.y);
    float lineLen = glm::length(lineEnd - lineStart);
    // Reject zero-length lines: glm::normalize would produce NaN, then
    // every subsequent dist would be NaN and the chunk-skip check would
    // pass - painting full strength on every chunk in the tile.
    if (lineLen < 1e-4f) return;
    glm::vec2 lineDir = (lineEnd - lineStart) / lineLen;

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap() || chunk.layers.empty()) continue;

        int cx = ci % 16, cy = ci / 16;
        float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * 533.33333f;
        float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * 533.33333f;
        float chunkCenterX = tileNW_X - (cy + 0.5f) * (533.33333f / 16.0f);
        float chunkCenterY = tileNW_Y - (cx + 0.5f) * (533.33333f / 16.0f);

        // Quick distance check
        glm::vec2 cc(chunkCenterX, chunkCenterY);
        glm::vec2 toCC = cc - lineStart;
        float proj = glm::dot(toCC, lineDir);
        proj = std::clamp(proj, 0.0f, lineLen);
        glm::vec2 closest = lineStart + lineDir * proj;
        if (glm::length(cc - closest) > width + 40.0f) continue;

        int layerIdx = ensureLayerOnChunk(ci, texId);
        if (layerIdx < 0) continue;

        size_t alphaOffset = chunk.layers[layerIdx].offsetMCAL;
        if (alphaOffset + 4096 > chunk.alphaMap.size()) continue;

        float texelSize = (533.33333f / 16.0f) / 64.0f;
        for (int ty = 0; ty < 64; ty++) {
            for (int tx = 0; tx < 64; tx++) {
                float wx = tileNW_X - cy * (533.33333f / 16.0f) - (ty + 0.5f) * texelSize;
                float wy = tileNW_Y - cx * (533.33333f / 16.0f) - (tx + 0.5f) * texelSize;
                glm::vec2 p(wx, wy);
                glm::vec2 toP = p - lineStart;
                float t = glm::dot(toP, lineDir);
                t = std::clamp(t, 0.0f, lineLen);
                glm::vec2 near = lineStart + lineDir * t;
                float dist = glm::length(p - near);
                if (dist < width) {
                    float falloff = 1.0f - (dist / width);
                    falloff = falloff * falloff;
                    uint8_t alpha = static_cast<uint8_t>(std::min(255.0f, falloff * 255.0f));
                    chunk.alphaMap[alphaOffset + ty * 64 + tx] = std::max(
                        chunk.alphaMap[alphaOffset + ty * 64 + tx], alpha);
                }
            }
        }
    }
}

void TexturePainter::gradientBlend(const std::string& tex1, const std::string& tex2, bool horizontal) {
    if (!terrain_) return;
    uint32_t id1 = ensureTextureInList(tex1);
    uint32_t id2 = ensureTextureInList(tex2);

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap() || chunk.layers.empty()) continue;
        int cx = ci % 16, cy = ci / 16;

        // Set base texture to tex1
        chunk.layers[0].textureId = id1;

        // Add tex2 as blended layer
        int layerIdx = ensureLayerOnChunk(ci, id2);
        if (layerIdx < 0) continue;
        size_t off = chunk.layers[layerIdx].offsetMCAL;
        if (off + 4096 > chunk.alphaMap.size()) continue;

        for (int ty = 0; ty < 64; ty++) {
            for (int tx = 0; tx < 64; tx++) {
                float t;
                if (horizontal)
                    t = (cx * 64.0f + tx) / (16.0f * 64.0f);
                else
                    t = (cy * 64.0f + ty) / (16.0f * 64.0f);
                chunk.alphaMap[off + ty * 64 + tx] = static_cast<uint8_t>(t * 255.0f);
            }
        }
    }
}

void TexturePainter::scatterPatches(const std::string& texturePath, int count,
                                     float minRadius, float maxRadius, uint32_t seed) {
    if (!terrain_ || texturePath.empty()) return;
    ensureTextureInList(texturePath);

    float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * 533.33333f;
    float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * 533.33333f;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distX(tileNW_X - 533.33333f, tileNW_X);
    std::uniform_real_distribution<float> distY(tileNW_Y - 533.33333f, tileNW_Y);
    std::uniform_real_distribution<float> distR(minRadius, maxRadius);

    for (int p = 0; p < count; p++) {
        float px = distX(rng), py = distY(rng), pr = distR(rng);
        paint(glm::vec3(px, py, 0), pr, 0.8f, 0.5f);
    }
}

std::vector<int> TexturePainter::erase(const glm::vec3& center, float radius,
                                        float strength, float falloff) {
    if (!terrain_ || activeTexture_.empty()) return {};

    std::vector<int> modified;

    for (int i = 0; i < 256; i++) {
        if (!terrain_->chunks[i].hasHeightMap()) continue;
        auto& chunk = terrain_->chunks[i];

        int cx = i % 16;
        int cy = i / 16;
        float tileNW_X = (32.0f - static_cast<float>(terrain_->coord.y)) * TILE_SIZE;
        float tileNW_Y = (32.0f - static_cast<float>(terrain_->coord.x)) * TILE_SIZE;
        float chunkCenterX = tileNW_X - (cy + 0.5f) * CHUNK_SIZE;
        float chunkCenterY = tileNW_Y - (cx + 0.5f) * CHUNK_SIZE;
        float dist = std::sqrt((chunkCenterX - center.x) * (chunkCenterX - center.x) +
                               (chunkCenterY - center.y) * (chunkCenterY - center.y));
        if (dist > radius + CHUNK_SIZE) continue;

        // Erase all non-base layers in range
        for (int l = 1; l < static_cast<int>(chunk.layers.size()); l++) {
            modifyAlpha(i, l, center, radius, strength, falloff, true);
        }
        modified.push_back(i);
    }

    return modified;
}

std::string TexturePainter::pickTextureAt(const glm::vec3& worldPos) const {
    if (!terrain_) return "";

    for (int ci = 0; ci < 256; ci++) {
        const auto& chunk = terrain_->chunks[ci];
        if (!chunk.hasHeightMap() || chunk.layers.empty()) continue;

        glm::vec2 uv = worldToChunkUV(ci, worldPos);
        if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) continue;

        if (chunk.layers.size() == 1) {
            if (chunk.layers[0].textureId < terrain_->textures.size())
                return terrain_->textures[chunk.layers[0].textureId];
            continue;
        }

        int px = std::clamp(static_cast<int>(uv.x * 64.0f), 0, 63);
        int py = std::clamp(static_cast<int>(uv.y * 64.0f), 0, 63);
        int pixelIdx = py * 64 + px;

        uint32_t bestLayer = 0;
        uint8_t bestAlpha = 0;
        for (size_t li = 1; li < chunk.layers.size(); li++) {
            size_t alphaOffset = (li - 1) * 4096 + pixelIdx;
            if (alphaOffset < chunk.alphaMap.size()) {
                uint8_t alpha = chunk.alphaMap[alphaOffset];
                if (alpha > bestAlpha) {
                    bestAlpha = alpha;
                    bestLayer = static_cast<uint32_t>(li);
                }
            }
        }

        if (bestAlpha < 128) bestLayer = 0;

        if (bestLayer < chunk.layers.size() &&
            chunk.layers[bestLayer].textureId < terrain_->textures.size()) {
            return terrain_->textures[chunk.layers[bestLayer].textureId];
        }
    }
    return "";
}

} // namespace editor
} // namespace wowee
