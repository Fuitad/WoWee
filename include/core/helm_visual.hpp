#pragma once

// Where a helmet's model and texture live, given its ItemDisplayInfo id.
//
// Head gear is race and gender specific — Helm_Plate_B_01_HuM.m2 is the human
// male cut of the same helm — with a base model as the fallback for the pieces
// that do not vary. Both the local player and every other player need the same
// answer, and they resolve appearance through different classes, so the rule
// lives here rather than in either of them. The cape texture is the standing
// example of what happens otherwise: it was applied in one path and not the
// other, and showed up on the paperdoll while the character in the world wore
// nothing on their back.

#include <cstdint>
#include <string>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace core {

struct HelmVisual {
    /// Candidate model paths, most specific first. The caller loads them in
    /// order because only it knows how to load an M2, and stops at the first
    /// that parses.
    std::string racialModelPath;   ///< Empty when the race has no known suffix.
    std::string baseModelPath;
    std::string texturePath;       ///< Already resolved against what exists.

    bool valid() const { return !baseModelPath.empty(); }
};

/// Resolve the head model and texture for an ItemDisplayInfo id. Returns an
/// empty result when the id is unknown or carries no model.
HelmVisual resolveHelmVisual(pipeline::AssetManager& assets,
                             uint32_t itemDisplayInfoId,
                             uint8_t raceId,
                             uint8_t genderId);

} // namespace core
} // namespace wowee
