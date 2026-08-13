#pragma once

/**
 * wom_mesh_stats.hpp - the numbers every mesh listing reports about a .wom.
 *
 * Vertices, triangles, bones, batches, textures and the format version, read
 * off a loaded model. Two commands built the same six fields the same way -
 * the zone listing and the project listing - and four more read some subset
 * inline while totalling a tree.
 *
 * Only one of the six is arithmetic rather than a count, and it is the one
 * worth writing down once: a triangle is three indices, so the count is
 * indices/3. A model whose index buffer is not a multiple of three is
 * malformed, and integer division reports the whole triangles rather than
 * refusing, which is what a listing wants: the file still appears, with a
 * count that does not match its bytes.
 */

#include <cstdint>

#include "pipeline/wowee_model.hpp"

namespace wowee {
namespace editor {
namespace cli {

/// What a mesh listing says about one model.
struct WomMeshStats {
    size_t verts = 0;
    size_t tris = 0;
    size_t bones = 0;
    size_t anims = 0;
    size_t batches = 0;
    size_t textures = 0;
    uint32_t version = 0;
};

inline WomMeshStats womMeshStats(const pipeline::WoweeModel& model) {
    WomMeshStats out;
    out.verts = model.vertices.size();
    out.tris = model.indices.size() / 3;
    out.bones = model.bones.size();
    out.anims = model.animations.size();
    out.batches = model.batches.size();
    out.textures = model.texturePaths.size();
    out.version = model.version;
    return out;
}

/// Fill the stats of a row that carries other columns beside them.
inline void setMeshStats(WomMeshStats& out, const pipeline::WoweeModel& model) {
    out = womMeshStats(model);
}

} // namespace cli
} // namespace editor
} // namespace wowee
