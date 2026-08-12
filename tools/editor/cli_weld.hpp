#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

// Vertex weld pass shared by --info-mesh-stats / --info-wob-stats /
// --bake-wom-collision / --audit-watertight. Positions are quantized
// onto a 1/eps grid; every vertex sharing a cell with a previously-
// seen vertex is remapped to that vertex's index. Returns canon[v]
// giving the canonical (lowest-index) representative of v's
// equivalence class and writes the count of distinct cells to
// `uniqueOut`.
//
// Implementation uses std::map<tuple<int64,int64,int64>, uint32_t>
// for exact equality on the quantized key - a hash-based key would
// risk false-positive collisions that incorrectly merge distinct
// corners (e.g. a unit cube's 8 corners all hashing to 2 buckets).
std::vector<uint32_t> buildWeldMap(
    const std::vector<glm::vec3>& positions,
    float eps,
    std::size_t& uniqueOut);

// Edge classification result from walking a triangle list with a
// canon[] map (typically built by buildWeldMap above, but the
// identity mapping also works for "as-authored" edge counts).
struct EdgeStats {
    std::size_t total = 0;       // distinct edges seen
    std::size_t boundary = 0;    // shared by exactly 1 triangle (open seam)
    std::size_t manifold = 0;    // shared by exactly 2 (closed surface)
    std::size_t nonManifold = 0; // shared by 3+ (branching surface)
    bool watertight() const {
        return boundary == 0 && nonManifold == 0;
    }
};

// Walk every triangle in `indices` (must be a multiple of 3),
// remap each corner through canon[], and count edge uses. An
// edge whose two canonical endpoints are equal is dropped (it
// became a self-loop after welding and isn't a real edge).
EdgeStats classifyEdges(const std::vector<uint32_t>& indices,
                        const std::vector<uint32_t>& canon);

} // namespace cli
} // namespace editor
} // namespace wowee
