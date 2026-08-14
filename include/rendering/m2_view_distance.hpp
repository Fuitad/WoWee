#pragma once

#include <algorithm>

namespace wowee {
namespace rendering {

/// The distance at which one M2 instance stops drawing, squared.
///
/// Three separate inputs each push an instance further out than the scene's
/// base distance: a large model gets a factor above 1 so a cathedral does not
/// pop before a shrub does, an animation-disabled model gets a further 2.6x,
/// and a server game object gets an outright floor so a mailbox stays findable.
/// All three are biases within the drawn world, not licence to leave it. The
/// terrain and the WMOs stop at the view distance the player asked for, so an
/// instance past that ceiling is a tree standing on nothing - which is why the
/// clamp comes last, after every widening rather than before it.
inline float m2InstanceMaxDistSq(float baseMaxDistSq,
                                 float perInstanceFactor,
                                 bool isGameObject,
                                 float gameObjectMinDistance,
                                 float viewDistanceAbsolute) {
    float maxDistSq = baseMaxDistSq * perInstanceFactor;
    if (isGameObject) {
        maxDistSq = std::max(maxDistSq, gameObjectMinDistance * gameObjectMinDistance);
    }
    return std::min(maxDistSq, viewDistanceAbsolute * viewDistanceAbsolute);
}

} // namespace rendering
} // namespace wowee
