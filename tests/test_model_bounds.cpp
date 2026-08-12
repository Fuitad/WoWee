// The box and radius a .wom carries for its geometry.
//
// Eighteen places computed them and three formulas were in use: half the box
// diagonal in fifteen, the furthest vertex from the box centre in two, and the
// furthest vertex from the model origin in one.
//
// The oracle is what the field means rather than what most callers did.
// M2Loader sets boundRadius from the M2 header's own boundingRadius, which is
// measured from the model origin, and WoweeBuildingLoader::fromWMO measures
// glm::length(position) to match. A sphere around the box centre is a
// different fact in the same field, and the two agree only for a model that
// happens to sit centred on its origin.
//
// Nothing about a wrong value fails. It under-margins the shadow cascade,
// frames the model wrongly in the editor viewport, and costs culling.
#include <catch_amalgamated.hpp>

#include <vector>

#include "pipeline/model_bounds.hpp"

using wowee::pipeline::modelBounds;
using wowee::pipeline::modelBoundsOf;

TEST_CASE("the radius is measured from the model origin", "[bounds]") {
    // Geometry sitting away from its origin is the case the three formulas
    // disagree about, and it is the common one: a doodad modelled standing on
    // the ground has its origin at its feet.
    const std::vector<glm::vec3> offset{
        {10.0f, 0.0f, 0.0f}, {12.0f, 0.0f, 0.0f}, {11.0f, 1.0f, 0.0f},
    };
    const auto bounds = modelBounds(offset);

    // Twelve from the origin, not one from the box centre and not the box's
    // half-diagonal.
    CHECK(bounds.radius == Catch::Approx(12.0f).epsilon(1e-5));
    CHECK(bounds.min.x == Catch::Approx(10.0f));
    CHECK(bounds.max.x == Catch::Approx(12.0f));
}

TEST_CASE("the sphere contains every vertex", "[bounds]") {
    // The property the field exists for, stated directly.
    const std::vector<glm::vec3> shape{
        {-3.0f, 1.0f, 2.0f}, {4.0f, -5.0f, 0.5f}, {0.0f, 0.0f, 0.0f},
        {1.5f, 1.5f, -6.0f},
    };
    const auto bounds = modelBounds(shape);
    for (const glm::vec3& p : shape) {
        INFO("vertex " << p.x << "," << p.y << "," << p.z);
        CHECK(glm::length(p) <= bounds.radius + 1e-4f);
        CHECK(p.x >= bounds.min.x);
        CHECK(p.y >= bounds.min.y);
        CHECK(p.z >= bounds.min.z);
        CHECK(p.x <= bounds.max.x);
        CHECK(p.y <= bounds.max.y);
        CHECK(p.z <= bounds.max.z);
    }
}

TEST_CASE("the sphere is no larger than it has to be", "[bounds]") {
    // Some vertex sits on it. Half the box diagonal does not have that
    // property: for a sphere of radius one it returns 1.73, so a third of the
    // volume of the cull sphere contains nothing.
    const std::vector<glm::vec3> corners{
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
    };
    const auto bounds = modelBounds(corners);
    CHECK(bounds.radius == Catch::Approx(1.0f).epsilon(1e-5));

    const float halfDiagonal = glm::length(bounds.max - bounds.min) * 0.5f;
    CHECK(halfDiagonal == Catch::Approx(1.7320508f).epsilon(1e-5));
}

TEST_CASE("a model centred on its origin is where the formulas agree",
          "[bounds]") {
    // A cube from -1 to 1: the furthest vertex from the origin, the furthest
    // from the box centre and half the diagonal are all the same number, which
    // is why fifteen wrong callers went unnoticed. Every generated shape that
    // happens to be built around the origin reads correctly.
    const std::vector<glm::vec3> cube{
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
    };
    const auto bounds = modelBounds(cube);
    const float halfDiagonal = glm::length(bounds.max - bounds.min) * 0.5f;
    CHECK(bounds.radius == Catch::Approx(halfDiagonal).epsilon(1e-5));
    CHECK(bounds.radius == Catch::Approx(1.7320508f).epsilon(1e-5));
}

TEST_CASE("stairs are the case that showed it", "[bounds]") {
    // The editor's stair generator builds upward and forward from the origin,
    // so the geometry occupies one octant. The box-centred radius was 1.19
    // where the model reaches 2.22, and the box-diagonal one is no better
    // here: both describe a sphere that stops short of the top step.
    const std::vector<glm::vec3> stairs{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {1.0f, 2.0f, 1.0f}, {0.0f, 2.0f, 1.0f},
    };
    const auto bounds = modelBounds(stairs);
    const glm::vec3 centre = (bounds.min + bounds.max) * 0.5f;
    float furthestFromCentre = 0.0f;
    for (const glm::vec3& p : stairs) {
        furthestFromCentre = glm::max(furthestFromCentre, glm::length(p - centre));
    }
    CHECK(bounds.radius > furthestFromCentre);
    CHECK(bounds.radius == Catch::Approx(glm::length(glm::vec3(1, 2, 1)))
                               .epsilon(1e-5));
}

TEST_CASE("an empty model is a point, not an inverted box", "[bounds]") {
    // Seeding min with +1e30 and max with -1e30 and finding nothing to fold in
    // leaves a box whose min is above its max. That reaches the validator,
    // which reports it, and the viewport, which frames it.
    const auto bounds = modelBounds({});
    CHECK(bounds.radius == 0.0f);
    CHECK(bounds.min == glm::vec3(0.0f));
    CHECK(bounds.max == glm::vec3(0.0f));
    CHECK(bounds.min.x <= bounds.max.x);
    CHECK(bounds.min.y <= bounds.max.y);
    CHECK(bounds.min.z <= bounds.max.z);
}

TEST_CASE("one vertex is a box with no volume", "[bounds]") {
    const std::vector<glm::vec3> single{{2.0f, 3.0f, 6.0f}};
    const auto bounds = modelBounds(single);
    CHECK(bounds.min == single[0]);
    CHECK(bounds.max == single[0]);
    CHECK(bounds.radius == Catch::Approx(7.0f).epsilon(1e-5));
}

TEST_CASE("a vertex at the origin does not shrink anything", "[bounds]") {
    const std::vector<glm::vec3> withOrigin{
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 5.0f},
    };
    const auto bounds = modelBounds(withOrigin);
    CHECK(bounds.radius == Catch::Approx(5.0f).epsilon(1e-5));
    CHECK(bounds.min == glm::vec3(0.0f));
}

TEST_CASE("vertices of any shape can be measured", "[bounds]") {
    // The .wom and .wob vertex types are different structs holding a position
    // among other fields, so the accessor is supplied by the caller.
    struct Vertex {
        glm::vec3 position;
        glm::vec2 uv;
    };
    const std::vector<Vertex> verts{
        {{0.0f, 0.0f, 4.0f}, {}}, {{0.0f, 3.0f, 0.0f}, {}},
    };
    const auto bounds =
        modelBoundsOf(verts, [](const Vertex& v) { return v.position; });
    CHECK(bounds.radius == Catch::Approx(4.0f).epsilon(1e-5));
    CHECK(bounds.max == glm::vec3(0.0f, 3.0f, 4.0f));
}
