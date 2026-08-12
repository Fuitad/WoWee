// The WMO and terrain vertex layouts, against the shaders that read them.
//
// A vertex layout is agreed between parties that cannot check each other: the
// struct the renderer fills, the attribute descriptions given to the pipeline,
// and the shader's `layout(location = N) in` lines. wmo_renderer.cpp stated it
// four times, once with the stride and every offset written as a literal.
//
// Nothing about a disagreement raises. The pipeline is valid either way and
// the geometry draws, reading its fields from the wrong bytes: a wall lit from
// underneath, a texture crawling across a surface, a shadow somewhere else.
//
// The oracle here is real and outside the C++: assets/shaders/*.glsl is parsed
// and the C++ description is compared against what the shader declares. If
// somebody adds an input to the shader without adding an attribute, this fails
// with the name of the input.
#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "rendering/terrain_vertex.hpp"
#include "rendering/wmo_vertex.hpp"

using wowee::rendering::kTerrainShadowVertexAttributes;
using wowee::rendering::kTerrainVertexAttributes;
using wowee::rendering::kWmoShadowVertexAttributes;
using wowee::rendering::kWmoVertexAttributes;
using wowee::rendering::VertexAttribute;
using wowee::rendering::WMOVertex;

namespace {

struct ShaderInput {
    uint32_t location = 0;
    uint32_t componentCount = 0;
    std::string name;
};

/// The vertex inputs a GLSL source declares, in location order.
///
/// Deliberately a separate reading of the file rather than anything shared
/// with the code under test; that is the whole point of it.
std::vector<ShaderInput> readShaderInputs(const std::string& path) {
    std::ifstream file(path);
    if (!file) return {};

    // layout(location = 0) in vec3 aPos;
    const std::regex pattern(
        R"(layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*in\s+vec([234])\s+(\w+)\s*;)");
    std::vector<ShaderInput> inputs;
    std::string line;
    while (std::getline(file, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, pattern)) continue;
        inputs.push_back({static_cast<uint32_t>(std::stoul(match[1].str())),
                          static_cast<uint32_t>(std::stoul(match[2].str())),
                          match[3].str()});
    }
    return inputs;
}

/// The shaders live beside the sources, and the test may be run from anywhere.
std::string shaderPath(const std::string& name) {
#ifdef WOWEE_SOURCE_DIR
    return std::string(WOWEE_SOURCE_DIR) + "/assets/shaders/" + name;
#else
    return "assets/shaders/" + name;
#endif
}

}  // namespace

TEST_CASE("the WMO vertex is what the layout comment claims", "[wmovertex]") {
    // 3+3+2+4+4 floats. The shadow pipeline used to spell this 64 as a
    // literal, so it is worth stating once where a change would be noticed.
    STATIC_REQUIRE(sizeof(WMOVertex) == 64);
    STATIC_REQUIRE(offsetof(WMOVertex, position) == 0);
    STATIC_REQUIRE(offsetof(WMOVertex, normal) == 12);
    STATIC_REQUIRE(offsetof(WMOVertex, texCoord) == 24);
    STATIC_REQUIRE(offsetof(WMOVertex, color) == 32);
    STATIC_REQUIRE(offsetof(WMOVertex, tangent) == 48);
}

TEST_CASE("every attribute sits inside the vertex", "[wmovertex]") {
    for (const VertexAttribute& attribute : kWmoVertexAttributes) {
        INFO("location " << attribute.location);
        CHECK(attribute.offset + attribute.componentCount * sizeof(float) <=
              sizeof(WMOVertex));
    }
    for (const VertexAttribute& attribute : kWmoShadowVertexAttributes) {
        INFO("shadow location " << attribute.location);
        CHECK(attribute.offset + attribute.componentCount * sizeof(float) <=
              sizeof(WMOVertex));
    }
}

TEST_CASE("the attributes match what wmo.vert.glsl declares", "[wmovertex]") {
    const auto inputs = readShaderInputs(shaderPath("wmo.vert.glsl"));
    if (inputs.empty()) {
        WARN("assets/shaders/wmo.vert.glsl not readable from here, skipping");
        return;
    }

    REQUIRE(inputs.size() == kWmoVertexAttributes.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        INFO("shader input " << inputs[i].name);
        CHECK(kWmoVertexAttributes[i].location == inputs[i].location);
        CHECK(kWmoVertexAttributes[i].componentCount == inputs[i].componentCount);
    }
}

TEST_CASE("the shadow attributes match what shadow.vert.glsl declares",
          "[wmovertex]") {
    const auto inputs = readShaderInputs(shaderPath("shadow.vert.glsl"));
    if (inputs.empty()) {
        WARN("assets/shaders/shadow.vert.glsl not readable from here, skipping");
        return;
    }

    // Every declared input needs a description even though this geometry has
    // no bones: an input with none makes the pipeline invalid.
    REQUIRE(inputs.size() == kWmoShadowVertexAttributes.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        INFO("shadow input " << inputs[i].name);
        CHECK(kWmoShadowVertexAttributes[i].location == inputs[i].location);
        CHECK(kWmoShadowVertexAttributes[i].componentCount ==
              inputs[i].componentCount);
    }
}

TEST_CASE("the shadow pass reads position and texcoord from the real places",
          "[wmovertex]") {
    // The two that are actually read. The bone attributes are aliased onto the
    // colour on purpose and are checked only for being in bounds above.
    CHECK(kWmoShadowVertexAttributes[0].offset == offsetof(WMOVertex, position));
    CHECK(kWmoShadowVertexAttributes[1].offset == offsetof(WMOVertex, texCoord));
}

TEST_CASE("the main attributes are in the order the struct declares them",
          "[wmovertex]") {
    // Ascending offsets, none overlapping. A layout that fails this is one
    // where two attributes name the same bytes, which is legal and is how a
    // normal ends up drawn as a colour.
    uint32_t previousEnd = 0;
    for (const VertexAttribute& attribute : kWmoVertexAttributes) {
        INFO("location " << attribute.location);
        CHECK(attribute.offset >= previousEnd);
        previousEnd = attribute.offset +
                      static_cast<uint32_t>(attribute.componentCount * sizeof(float));
    }
    CHECK(previousEnd == sizeof(WMOVertex));
}

TEST_CASE("the terrain attributes match what terrain.vert.glsl declares",
          "[terrainvertex]") {
    const auto inputs = readShaderInputs(shaderPath("terrain.vert.glsl"));
    if (inputs.empty()) {
        WARN("assets/shaders/terrain.vert.glsl not readable from here, skipping");
        return;
    }

    REQUIRE(inputs.size() == kTerrainVertexAttributes.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        INFO("shader input " << inputs[i].name);
        CHECK(kTerrainVertexAttributes[i].location == inputs[i].location);
        CHECK(kTerrainVertexAttributes[i].componentCount == inputs[i].componentCount);
    }
}

TEST_CASE("the terrain shadow attributes match the shared shadow shader",
          "[terrainvertex]") {
    const auto inputs = readShaderInputs(shaderPath("shadow.vert.glsl"));
    if (inputs.empty()) {
        WARN("assets/shaders/shadow.vert.glsl not readable from here, skipping");
        return;
    }

    REQUIRE(inputs.size() == kTerrainShadowVertexAttributes.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        INFO("shadow input " << inputs[i].name);
        CHECK(kTerrainShadowVertexAttributes[i].location == inputs[i].location);
        CHECK(kTerrainShadowVertexAttributes[i].componentCount ==
              inputs[i].componentCount);
    }
}

TEST_CASE("every terrain attribute sits inside the vertex", "[terrainvertex]") {
    // The trailing chunkIndex byte means the stride is a padded number rather
    // than the sum of the fields, so "in bounds" is the check that matters
    // and the old comment asserting 44 was not one.
    using wowee::pipeline::TerrainVertex;
    for (const VertexAttribute& attribute : kTerrainVertexAttributes) {
        INFO("location " << attribute.location);
        CHECK(attribute.offset + attribute.componentCount * sizeof(float) <=
              sizeof(TerrainVertex));
    }
    for (const VertexAttribute& attribute : kTerrainShadowVertexAttributes) {
        INFO("shadow location " << attribute.location);
        CHECK(attribute.offset + attribute.componentCount * sizeof(float) <=
              sizeof(TerrainVertex));
    }
}
