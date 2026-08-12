#pragma once

#include "editor_brush.hpp"
#include "editor_history.hpp"
#include "terrain_biomes.hpp"
#include "pipeline/adt_loader.hpp"
#include "pipeline/terrain_mesh.hpp"
#include "rendering/camera.hpp"
#include <vector>
#include <functional>

namespace wowee {
namespace editor {

class TerrainEditor {
public:
    TerrainEditor();

    void setTerrain(pipeline::ADTTerrain* terrain) { terrain_ = terrain; }
    pipeline::ADTTerrain* getTerrain() { return terrain_; }
    const pipeline::ADTTerrain* getTerrain() const { return terrain_; }

    EditorBrush& brush() { return brush_; }
    const EditorBrush& brush() const { return brush_; }
    EditorHistory& history() { return history_; }

    static pipeline::ADTTerrain createBlankTerrain(int tileX, int tileY, float baseHeight = 100.0f,
                                                     Biome biome = Biome::Grassland);

    // Raycast against terrain, returns true if hit
    bool raycastTerrain(const rendering::Ray& ray, glm::vec3& hitPos) const;

    // Sample terrain normal at a world XY position (for object alignment)
    glm::vec3 sampleTerrainNormal(const glm::vec3& worldPos) const;

    // Apply brush at current position (call per-frame while painting)
    void applyBrush(float deltaTime);

    // Begin/end a paint stroke (for undo grouping)
    void beginStroke();
    void endStroke();
    bool isStrokeActive() const { return strokeActive_; }

    // Get chunks modified since last call (for re-upload)
    std::vector<int> consumeDirtyChunks();
    void markDirty(int chunkIdx) { dirtyChunks_.push_back(chunkIdx); dirty_ = true; }
    void stitchChunkEdges(int chunkIdx) { stitchEdges(chunkIdx); }
    glm::vec3 getChunkVertexWorldPos(int ci, int vi) const { return chunkVertexWorldPos(ci, vi); }
    void beginGeneratorUndo() { recordGeneratorUndo(); }
    void endGeneratorUndo() { commitGeneratorUndo(); }

    // Regenerate mesh for specific chunks
    pipeline::TerrainMesh regenerateMesh() const;
    pipeline::ChunkMesh regenerateChunkMesh(int chunkIndex) const;

    void undo();
    void redo();

    // Recalculate normals for modified chunks (improves lighting after sculpt)
    void recalcNormals(const std::vector<int>& chunkIndices);

    // Noise generator: applies procedural height noise to the terrain
    void applyNoise(float frequency, float amplitude, int octaves, uint32_t seed);

    // Global smooth pass across entire tile (N iterations)
    void smoothEntireTile(int iterations);

    // Clamp all heights to a min/max range
    void clampHeights(float minH, float maxH);

    // Reset all heights to zero (flat terrain)
    void resetToFlat();

    // Scale all heights by a factor (useful for exaggerating or flattening)
    void scaleHeights(float factor);

    // Terrain stamp: copy heights from source area, paste at destination
    void copyStamp(const glm::vec3& center, float radius);
    void pasteStamp(const glm::vec3& center);
    bool saveStamp(const std::string& path) const;
    bool loadStamp(const std::string& path);
    bool hasStamp() const { return !stampData_.empty(); }

    // Mirror terrain along X or Y axis through tile center
    void mirrorX();
    void mirrorY();

    // Carve a river/path between two points (lowers terrain along line)
    void carveRiver(const glm::vec3& start, const glm::vec3& end, float width, float depth);

    // Flatten a road between two points (smooths to average height along path)
    void flattenRoad(const glm::vec3& start, const glm::vec3& end, float width);

    // Create a crater at a position (bowl shape with raised rim)
    void createCrater(const glm::vec3& center, float radius, float depth, float rimHeight);

    // Create a mesa/plateau (raised flat area with steep cliff edges)
    void createMesa(const glm::vec3& center, float radius, float height, float edgeSteepness);

    // Create a smooth hill/mountain
    void createHill(const glm::vec3& center, float radius, float height);

    // Create a ridge/mountain range between two points
    void createRidge(const glm::vec3& start, const glm::vec3& end, float width, float height);

    // Create an island shape (raised center, dropping to base at edges)
    void createIsland(float centerHeight, float edgeDropoff);

    // Create a winding canyon across the tile
    void createCanyon(float width, float depth, uint32_t seed);

    // Terrace/quantize heights into N steps
    void terraceHeights(int steps);

    // Thermal erosion: material falls downhill based on angle of repose
    void thermalErosion(int iterations, float talusAngle);

    // Invert terrain (flip heights around midpoint)
    void invertHeights();

    // Offset all heights by a constant
    void offsetHeights(float amount);

    // Voronoi cell noise - creates cell-like terrain patterns
    void applyVoronoiNoise(int cellCount, float amplitude, uint32_t seed);

    // Fill entire tile with water at a height
    void fillWater(float height, uint16_t liquidType);

    // Add a water layer only to chunks the line segment passes through
    // (within `width`). Used by the river tool so rivers actually fill
    // with water without flooding the rest of the tile. Per-chunk water
    // height is computed from the post-carve terrain so the water sits
    // in the carved channel.
    void fillWaterAlongPath(const glm::vec3& start, const glm::vec3& end,
                              float width, uint16_t liquidType);

    // Smooth terrain near water level to create natural beaches
    void smoothBeaches(float waterHeight, float beachWidth);

    // Ramp tile edges to a target height for seamless multi-tile joins
    void rampEdges(float targetHeight, float rampWidth);

    // Add random detail noise to existing terrain (preserves shape, adds roughness)
    void addDetailNoise(float amplitude, float frequency, uint32_t seed);

    // Rotate terrain 90 degrees clockwise
    void rotateTerrain90();

    // Create rolling sand dune pattern
    void createDunes(float wavelength, float amplitude, float direction, uint32_t seed);

    // Import/export heightmap (raw 16-bit grayscale, 129x129)
    bool importHeightmap(const std::string& path, float heightScale);
    bool importHeightmapImage(const std::string& path, float heightScale);
    bool exportHeightmap(const std::string& path, float heightScale);

    // Water editing
    void setWaterLevel(const glm::vec3& center, float radius, float waterHeight, uint16_t liquidType = 0);
    void removeWater(const glm::vec3& center, float radius);

    // Hole editing (4x4 bitmask per chunk - cave entrances, mine shafts)
    void punchHole(const glm::vec3& center, float radius);
    void fillHole(const glm::vec3& center, float radius);

    bool hasUnsavedChanges() const { return dirty_; }
    void markSaved() { dirty_ = false; }

private:
    void applyRaise(float dt);
    void applySmooth(float dt);
    void applyFlatten(float dt);
    void applyErode(float dt);
    void stitchEdges(int chunkIdx);

    std::vector<int> getAffectedChunks(const glm::vec3& center, float radius) const;
    glm::vec3 chunkVertexWorldPos(int chunkIdx, int vertIdx) const;
    float getVertexHeight(int chunkIdx, int vertIdx) const;
    void setVertexHeight(int chunkIdx, int vertIdx, float height);

    struct StampVertex { float dx, dy, height; };
    std::vector<StampVertex> stampData_;
    glm::vec3 stampCenter_{0};

    void recordGeneratorUndo();
    void commitGeneratorUndo();

    pipeline::ADTTerrain* terrain_ = nullptr;
    EditorBrush brush_;
    EditorHistory history_;

    bool strokeActive_ = false;
    bool dirty_ = false;
    std::vector<int> dirtyChunks_;

    static constexpr float TILE_SIZE = 533.33333f;
    static constexpr float CHUNK_SIZE = TILE_SIZE / 16.0f;
    static constexpr float GRID_STEP = CHUNK_SIZE / 8.0f;
};

} // namespace editor
} // namespace wowee
