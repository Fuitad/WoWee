#pragma once

#include <glm/glm.hpp>
#include <cmath>

namespace wowee {
namespace editor {

enum class BrushMode {
    Raise,
    Lower,
    Smooth,
    Flatten,
    Level,
    Erode
};

struct BrushSettings {
    BrushMode mode = BrushMode::Raise;
    float radius = 30.0f;
    float strength = 5.0f;
    float falloff = 0.5f;   // 0 = hard edge, 1 = full falloff
    float flattenHeight = 0.0f;
};

class EditorBrush {
public:
    BrushSettings& settings() { return settings_; }
    const BrushSettings& settings() const { return settings_; }

    bool isActive() const { return active_; }
    void setActive(bool a) { active_ = a; }

    const glm::vec3& getPosition() const { return worldPos_; }
    void setPosition(const glm::vec3& pos) {
        // applyBrush already early-outs on NaN, but rejecting at the
        // setter keeps the stored state itself sane - handy for UI
        // panels that read the current brush position back.
        if (std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z))
            worldPos_ = pos;
    }

    float getInfluence(float distance) const;

private:
    BrushSettings settings_;
    glm::vec3 worldPos_{0.0f};
    bool active_ = false;
};

} // namespace editor
} // namespace wowee
