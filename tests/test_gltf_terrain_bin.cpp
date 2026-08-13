// The BIN chunk a terrain .glb carries.
//
// Two exporters packed this byte for byte, the zone bake and the WHM export,
// and every accessor and bufferView in the JSON beside it is written from the
// three offsets it computes. Getting one wrong produces a file the editor
// reports as exported and no viewer will draw - the failure is total on the
// other side and invisible on this one.
//
// The oracle is the glTF 2.0 spec rather than the code: a POSITION accessor is
// VEC3 of float32, tightly packed, so twelve bytes a vertex; indices here are
// unsigned int, four bytes each; and a bufferView's byteOffset is an offset
// into the buffer, so the runs have to sit end to end in the order the offsets
// claim.
#include <catch_amalgamated.hpp>

#include <cstring>
#include <vector>

#include "gltf_glb.hpp"

using wowee::editor::cli::packTerrainBin;

namespace {

float floatAt(const std::vector<uint8_t>& bytes, size_t offset) {
    float value = 0.0f;
    std::memcpy(&value, &bytes[offset], 4);
    return value;
}

uint32_t wordAt(const std::vector<uint8_t>& bytes, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, &bytes[offset], 4);
    return value;
}

}  // namespace

TEST_CASE("the three runs sit end to end in the order the offsets claim",
          "[gltf]") {
    const std::vector<glm::vec3> positions(4, glm::vec3(0.0f));
    const std::vector<uint32_t> indices(6, 0);
    const auto bin = packTerrainBin(positions, indices);

    // Twelve bytes a vertex for positions, the same again for normals, four a
    // index. Nothing between them: a bufferView byteLength is the run's own
    // size and the next byteOffset is where it ends.
    CHECK(bin.positionOffset == 0);
    CHECK(bin.normalOffset == 4 * 12);
    CHECK(bin.indexOffset == 4 * 12 + 4 * 12);
    CHECK(bin.bytes.size() == 4 * 12 + 4 * 12 + 6 * 4);
}

TEST_CASE("a position round-trips through the buffer", "[gltf]") {
    const std::vector<glm::vec3> positions{{1.5f, -2.25f, 300.0f},
                                           {0.0f, 0.0f, 0.0f}};
    const auto bin = packTerrainBin(positions, {});

    CHECK(floatAt(bin.bytes, bin.positionOffset + 0) == 1.5f);
    CHECK(floatAt(bin.bytes, bin.positionOffset + 4) == -2.25f);
    CHECK(floatAt(bin.bytes, bin.positionOffset + 8) == 300.0f);
    // x, y, z in that order and nothing else between vertices.
    CHECK(floatAt(bin.bytes, bin.positionOffset + 12) == 0.0f);
}

TEST_CASE("every normal is +Z", "[gltf]") {
    // Terrain is Z-up here, and a real per-vertex normal would need a
    // smoothing pass across chunk boundaries. A viewer that cares computes its
    // own; what matters is that the run is there and the right size, because
    // the NORMAL accessor's count is the vertex count.
    const std::vector<glm::vec3> positions(3, glm::vec3(7.0f));
    const auto bin = packTerrainBin(positions, {});

    for (uint32_t v = 0; v < 3; ++v) {
        INFO("vertex " << v);
        CHECK(floatAt(bin.bytes, bin.normalOffset + v * 12 + 0) == 0.0f);
        CHECK(floatAt(bin.bytes, bin.normalOffset + v * 12 + 4) == 0.0f);
        CHECK(floatAt(bin.bytes, bin.normalOffset + v * 12 + 8) == 1.0f);
    }
}

TEST_CASE("indices are written as they came, four bytes each", "[gltf]") {
    const std::vector<glm::vec3> positions(3, glm::vec3(0.0f));
    const std::vector<uint32_t> indices{0, 2, 1, 900000};
    const auto bin = packTerrainBin(positions, indices);

    for (size_t i = 0; i < indices.size(); ++i) {
        INFO("index " << i);
        CHECK(wordAt(bin.bytes, bin.indexOffset + i * 4) == indices[i]);
    }
}

TEST_CASE("an empty mesh packs to nothing rather than misreporting",
          "[gltf]") {
    // A zone bake with no tiles loaded reaches here. The buffer has to be
    // empty and the offsets consistent, because byteLength values built from
    // them go into the JSON either way.
    const auto bin = packTerrainBin({}, {});
    CHECK(bin.bytes.empty());
    CHECK(bin.positionOffset == 0);
    CHECK(bin.normalOffset == 0);
    CHECK(bin.indexOffset == 0);
}

TEST_CASE("vertices without indices still pack", "[gltf]") {
    // The index run is allowed to be empty: a point cloud is a valid glTF.
    const std::vector<glm::vec3> positions(2, glm::vec3(1.0f));
    const auto bin = packTerrainBin(positions, {});
    CHECK(bin.bytes.size() == 2 * 12 + 2 * 12);
    CHECK(bin.indexOffset == bin.bytes.size());
}
