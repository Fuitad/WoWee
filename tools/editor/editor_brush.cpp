#include "editor_brush.hpp"
#include <algorithm>
#include <cmath>

namespace wowee {
namespace editor {

float EditorBrush::getInfluence(float distance) const {
    // NaN distance must produce 0 influence - comparisons against NaN
    // return false, so without this the function would fall through to
    // the "fully inside" branch and return 1.0 for every queried point.
    if (!std::isfinite(distance) || !std::isfinite(settings_.radius) ||
        settings_.radius <= 0.0f) return 0.0f;
    if (distance >= settings_.radius) return 0.0f;

    float t = distance / settings_.radius;
    float falloff = std::clamp(settings_.falloff, 0.0f, 1.0f);
    float innerRadius = 1.0f - falloff;
    if (t <= innerRadius) return 1.0f;
    if (falloff <= 0.0f) return 1.0f; // hard edge

    float falloffT = (t - innerRadius) / falloff;
    return 1.0f - (falloffT * falloffT);
}

} // namespace editor
} // namespace wowee
