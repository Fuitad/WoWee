// The uniform grid that narrows a collision query.
//
// This was two copies, one in the M2 renderer and one in the WMO renderer,
// both private and both reachable only through a Vulkan context, so neither
// had ever been tested. Its failure mode is not a crash: hand back too few
// candidates and the caller finds no floor and no wall, which reads as falling
// through the world rather than as a bug in a hash table.
//
// The values below are worked out by hand from the cell size, not recorded
// from the implementation.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "rendering/spatial_grid.hpp"

using namespace wowee::rendering;

namespace {

/// Every id the grid offers for a query box.
std::vector<uint32_t> query(const SpatialGrid& grid, const glm::vec3& lo,
                            const glm::vec3& hi) {
    std::unordered_set<uint32_t> seen;
    std::vector<uint32_t> out;
    gatherIds(grid, lo, hi, seen, [&](uint32_t id) { out.push_back(id); });
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("a position maps to the cell that contains it", "[spatialgrid]") {
    // Cells are 64 wide, so 0..63.99 is cell 0 and 64 begins cell 1.
    CHECK(toSpatialCell({0.0f, 0.0f, 0.0f}) == GridCell{0, 0, 0});
    CHECK(toSpatialCell({63.9f, 0.0f, 0.0f}) == GridCell{0, 0, 0});
    CHECK(toSpatialCell({64.0f, 0.0f, 0.0f}) == GridCell{1, 0, 0});
    CHECK(toSpatialCell({130.0f, 200.0f, 5.0f}) == GridCell{2, 3, 0});
}

TEST_CASE("negative positions floor rather than truncate", "[spatialgrid]") {
    // The trap. A cast to int truncates towards zero, which would put -0.5 and
    // +0.5 in the same cell and make cell 0 twice as wide as every other. Half
    // of every map is at negative coordinates, so this is not an edge case.
    CHECK(toSpatialCell({-0.5f, 0.0f, 0.0f}) == GridCell{-1, 0, 0});
    CHECK(toSpatialCell({-63.9f, 0.0f, 0.0f}) == GridCell{-1, 0, 0});
    CHECK(toSpatialCell({-64.0f, 0.0f, 0.0f}) == GridCell{-1, 0, 0});
    CHECK(toSpatialCell({-64.1f, 0.0f, 0.0f}) == GridCell{-2, 0, 0});
    CHECK(toSpatialCell({-1.0f, -1.0f, -1.0f}) == GridCell{-1, -1, -1});
}

TEST_CASE("an instance is filed under every cell its bounds reach", "[spatialgrid]") {
    SpatialGrid grid;
    // Spans two cells in x and two in y, one in z: four cells.
    insertBounds(grid, {10.0f, 10.0f, 10.0f}, {70.0f, 70.0f, 20.0f}, 7);

    CHECK(grid.size() == 4);
    for (int y = 0; y <= 1; ++y) {
        for (int x = 0; x <= 1; ++x) {
            INFO("cell " << x << "," << y);
            auto it = grid.find(GridCell{x, y, 0});
            REQUIRE(it != grid.end());
            CHECK(it->second == std::vector<uint32_t>{7});
        }
    }
}

TEST_CASE("a query finds what overlaps it and nothing else", "[spatialgrid]") {
    SpatialGrid grid;
    insertBounds(grid, {0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 10.0f}, 1);       // cell 0,0,0
    insertBounds(grid, {200.0f, 0.0f, 0.0f}, {210.0f, 10.0f, 10.0f}, 2);    // cell 3,0,0
    insertBounds(grid, {-10.0f, 0.0f, 0.0f}, {-5.0f, 10.0f, 10.0f}, 3);     // cell -1,0,0

    CHECK(query(grid, {1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 2.0f}) == std::vector<uint32_t>{1});
    CHECK(query(grid, {-8.0f, 1.0f, 1.0f}, {-7.0f, 2.0f, 2.0f}) == std::vector<uint32_t>{3});
    // A box wide enough to touch the first and last cells picks up all three,
    // including the one in the empty cell between them, which is skipped.
    CHECK(query(grid, {-10.0f, 1.0f, 1.0f}, {205.0f, 2.0f, 2.0f}) ==
          std::vector<uint32_t>{1, 2, 3});
    CHECK(query(grid, {1000.0f, 1000.0f, 1000.0f}, {1001.0f, 1001.0f, 1001.0f}).empty());
}

TEST_CASE("an instance spanning several cells is offered once", "[spatialgrid]") {
    // Without the seen set the caller would test the same wall four times, and
    // a raycast that counts hits would count each of them.
    SpatialGrid grid;
    insertBounds(grid, {10.0f, 10.0f, 0.0f}, {70.0f, 70.0f, 0.0f}, 9);
    CHECK(query(grid, {0.0f, 0.0f, 0.0f}, {128.0f, 128.0f, 0.0f}) ==
          std::vector<uint32_t>{9});
}

TEST_CASE("a query box crossing the origin covers both sides", "[spatialgrid]") {
    // Cell -1 and cell 0 are different cells, so a box straddling zero has to
    // walk both. If toSpatialCell truncated, minCell and maxCell would both be
    // 0 and everything to the west would be invisible.
    SpatialGrid grid;
    insertBounds(grid, {-20.0f, 0.0f, 0.0f}, {-20.0f, 0.0f, 0.0f}, 1);
    insertBounds(grid, {20.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}, 2);
    CHECK(query(grid, {-5.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}) ==
          std::vector<uint32_t>{1, 2});
}

TEST_CASE("the scratch set is cleared by the query, not by the caller",
          "[spatialgrid]") {
    // Both callers hold one thread_local set and reuse it for every query. If
    // it kept its contents, the second query on a thread would drop every id
    // the first one saw.
    SpatialGrid grid;
    insertBounds(grid, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 5);

    std::unordered_set<uint32_t> seen{5, 6, 7};
    std::vector<uint32_t> out;
    gatherIds(grid, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, seen,
              [&](uint32_t id) { out.push_back(id); });
    CHECK(out == std::vector<uint32_t>{5});
}

TEST_CASE("inverted bounds file nothing rather than looping backwards",
          "[spatialgrid]") {
    SpatialGrid grid;
    insertBounds(grid, {100.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 1);
    CHECK(grid.empty());
    CHECK(query(grid, {100.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}).empty());
}

TEST_CASE("erasing takes an instance out of every cell it was in", "[spatialgrid]") {
    SpatialGrid grid;
    insertBounds(grid, {10.0f, 10.0f, 0.0f}, {70.0f, 70.0f, 0.0f}, 1);
    insertBounds(grid, {10.0f, 10.0f, 0.0f}, {70.0f, 70.0f, 0.0f}, 2);
    eraseBounds(grid, {10.0f, 10.0f, 0.0f}, {70.0f, 70.0f, 0.0f}, 1);

    CHECK(query(grid, {0.0f, 0.0f, 0.0f}, {128.0f, 128.0f, 0.0f}) ==
          std::vector<uint32_t>{2});
    // The cells stay, because the zone will fill them again.
    CHECK(grid.size() == 4);
}

TEST_CASE("erasing with the bounds an instance has moved to strands it",
          "[spatialgrid]") {
    // The reason erase takes bounds rather than reading the instance: file at
    // A, move to B, then erase with B, and the entry at A is still there. Both
    // callers erase with the old box for exactly this reason, and this pins
    // what goes wrong if one stops.
    SpatialGrid grid;
    insertBounds(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, 1);   // cell 0
    eraseBounds(grid, {200.0f, 0.0f, 0.0f}, {200.0f, 0.0f, 0.0f}, 1);  // cell 3
    CHECK(query(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}) ==
          std::vector<uint32_t>{1});

    eraseBounds(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, 1);
    CHECK(query(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}).empty());
}

TEST_CASE("erasing an id that was never filed changes nothing", "[spatialgrid]") {
    SpatialGrid grid;
    insertBounds(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, 1);
    eraseBounds(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, 99);
    eraseBounds(grid, {5000.0f, 0.0f, 0.0f}, {5000.0f, 0.0f, 0.0f}, 1);
    CHECK(query(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}) ==
          std::vector<uint32_t>{1});
}

TEST_CASE("a move inside one cell refiles nothing", "[spatialgrid]") {
    SpatialGrid grid;
    insertBounds(grid, {10.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}, 1);
    refileBounds(grid, {10.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f},
                 {11.0f, 0.0f, 0.0f}, {21.0f, 0.0f, 0.0f}, 1);
    // Listed once, not twice: refiling without the check would insert a second
    // entry in the cell it never left.
    auto it = grid.find(GridCell{0, 0, 0});
    REQUIRE(it != grid.end());
    CHECK(it->second == std::vector<uint32_t>{1});
}

TEST_CASE("a move across a cell boundary refiles", "[spatialgrid]") {
    SpatialGrid grid;
    insertBounds(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, 1);
    refileBounds(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f},
                 {100.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}, 1);

    CHECK(query(grid, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}).empty());
    CHECK(query(grid, {100.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}) ==
          std::vector<uint32_t>{1});
}

TEST_CASE("growing within the same min cell still refiles", "[spatialgrid]") {
    // The max corner crossing a boundary is enough on its own: the instance
    // now reaches a cell it is not filed under.
    SpatialGrid grid;
    insertBounds(grid, {10.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}, 1);
    refileBounds(grid, {10.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f},
                 {10.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}, 1);
    CHECK(query(grid, {70.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}) ==
          std::vector<uint32_t>{1});
}
