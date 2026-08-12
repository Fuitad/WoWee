#pragma once

/// Setting a .wom's bounds from its own vertices.
///
/// The editor's generators and mesh edits each recomputed the box and radius
/// after changing geometry, and between them used two formulas that are not
/// the field's: half the box diagonal, and the furthest vertex from the box
/// centre. Both describe a sphere around the box. The field is a sphere at the
/// model origin, which is what M2Loader reads out of the M2 header and what
/// WoweeBuildingLoader::fromWMO measures. See pipeline/model_bounds.hpp.
///
/// Some generators also set the box analytically from the shape's own
/// parameters rather than from the vertices they had just written. That is the
/// same number until something displaces a vertex, and then it is not.

#include "pipeline/model_bounds.hpp"
#include "pipeline/wowee_model.hpp"

namespace wowee {
namespace editor {
namespace cli {

/// Recompute `model`'s box and radius from its vertices.
inline void setModelBounds(pipeline::WoweeModel& model) {
    const pipeline::ModelBounds bounds = pipeline::modelBoundsOf(
        model.vertices,
        [](const pipeline::WoweeModel::Vertex& v) { return v.position; });
    model.boundMin = bounds.min;
    model.boundMax = bounds.max;
    model.boundRadius = bounds.radius;
}

} // namespace cli
} // namespace editor
} // namespace wowee
