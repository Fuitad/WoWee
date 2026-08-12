#pragma once

#include <array>
#include <cstddef>

#include "pipeline/terrain_mesh.hpp"
#include "rendering/vertex_layout.hpp"

namespace wowee {
namespace rendering {

/// How the terrain vertex is read, stated once.
///
/// terrain_renderer.cpp described this three times: twice for the two pipeline
/// builders, and once more for the shadow pass with the offsets as literals
/// beside a comment asserting a stride of 44. That stride is not a number
/// anyone chose; it is what the trailing `chunkIndex` byte pads out to, so a
/// field added or reordered in TerrainVertex moves it without touching the
/// comment.

/// What assets/shaders/terrain.vert.glsl declares, in its order.
inline constexpr std::array<VertexAttribute, 4> kTerrainVertexAttributes = {{
    {0, 3, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, position))},
    {1, 3, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, normal))},
    {2, 2, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, texCoord))},
    {3, 2, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, layerUV))},
}};

/// The same geometry through the shared shadow shader, which declares bone
/// inputs terrain has none of.
///
/// They point at the position, which is the earliest field and so always in
/// bounds. useBones is 0 in the shadow pass, so nothing reads them; a declared
/// input with no description would make the pipeline invalid.
inline constexpr std::array<VertexAttribute, 4> kTerrainShadowVertexAttributes = {{
    {0, 3, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, position))},
    {1, 2, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, texCoord))},
    {2, 4, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, position))},
    {3, 4, static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, position))},
}};

}  // namespace rendering
}  // namespace wowee
