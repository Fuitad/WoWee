#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace wowee {
namespace rendering {

/// Where a world position falls in a water surface's own grid.
///
/// A surface is an origin and two step vectors, one per grid axis, so a lake
/// lying at an angle inside a WMO is described the same way as a chunk of open
/// sea. Everything that asks a question about the water under a point starts
/// by projecting that point onto those two axes: the height under the player,
/// the nearest surface, the liquid type, and whether the water belongs to a
/// building. water_renderer.cpp did it four times.
///
/// Answers nothing when the point is outside the surface, and when the surface
/// is degenerate. A step vector of zero length would divide by zero and put
/// every point in the world inside that surface, which is worth a check rather
/// than a NaN: a surface built from a malformed chunk would otherwise claim
/// the whole map and the player would swim through the air over it.
///
/// The bounds are inclusive at both ends, because a grid of `width` cells has
/// `width + 1` corners and the far edge belongs to the surface.
///
/// The result is in grid units, not cells: 2.5 is halfway along the third
/// column, which is what the callers interpolate between corners with.
inline std::optional<glm::vec2> surfaceGridPosition(
        const glm::vec3& origin, const glm::vec3& stepX, const glm::vec3& stepY,
        uint8_t width, uint8_t height, float worldX, float worldY) {
    const glm::vec2 relative(worldX - origin.x, worldY - origin.y);
    const glm::vec2 axisX(stepX.x, stepX.y);
    const glm::vec2 axisY(stepY.x, stepY.y);

    const float lengthSqX = glm::dot(axisX, axisX);
    const float lengthSqY = glm::dot(axisY, axisY);
    if (lengthSqX < 1e-6f || lengthSqY < 1e-6f) return std::nullopt;

    const glm::vec2 grid(glm::dot(relative, axisX) / lengthSqX,
                         glm::dot(relative, axisY) / lengthSqY);
    if (grid.x < 0.0f || grid.x > static_cast<float>(width) ||
        grid.y < 0.0f || grid.y > static_cast<float>(height)) {
        return std::nullopt;
    }
    return grid;
}

}  // namespace rendering
}  // namespace wowee
