// Reading a Wavefront OBJ.
//
// Two importers parsed it: --import-obj, which builds a .wom, and
// --import-wob-obj, which builds a .wob with one vertex pool per group. The
// parsing was identical in both down to the strtol walk over "3/4/5"; only
// what they built afterwards differed.
//
// Nothing here fails loudly. An index resolved one off reads a neighbouring
// vertex and the model imports with some triangles pointing somewhere else,
// which looks like a modelling mistake rather than an importer one.
//
// The oracle is the OBJ format rather than the old code. The rules being
// pinned are the format's, and the two that a second copy gets wrong are the
// 1-based indexing and the negative index, which counts back from the pool as
// it stands at that line rather than from its final size.
#include <catch_amalgamated.hpp>

#include <sstream>
#include <string>

#include "obj_parse.hpp"

using wowee::editor::cli::ObjDocument;
using wowee::editor::cli::objIndexToOffset;
using wowee::editor::cli::parseObj;
using wowee::editor::cli::parseObjCorner;

namespace {

ObjDocument parse(const std::string& text) {
    std::istringstream in(text);
    return parseObj(in);
}

// The corners of face `f`, so a test reads like the file it parsed.
std::vector<wowee::editor::cli::ObjCorner> cornersOf(const ObjDocument& doc,
                                                     size_t f) {
    const auto& face = doc.faces.at(f);
    return {doc.corners.begin() + static_cast<long>(face.firstCorner),
            doc.corners.begin() +
                static_cast<long>(face.firstCorner + face.cornerCount)};
}

}  // namespace

TEST_CASE("a face index is one-based", "[obj]") {
    // The single most consequential rule in the format, and the one a
    // reimplementation drops: "f 1 2 3" is the first three positions.
    const auto doc = parse(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    REQUIRE(doc.faces.size() == 1);
    const auto corners = cornersOf(doc, 0);
    REQUIRE(corners.size() == 3);
    CHECK(corners[0].position == 0);
    CHECK(corners[1].position == 1);
    CHECK(corners[2].position == 2);
    CHECK(doc.malformedFaces == 0);
}

TEST_CASE("a negative index counts back from the pool as it stands", "[obj]") {
    // Blender writes these. -1 is the vertex declared most recently, so the
    // same token means a different vertex further down the file, and a parser
    // that resolves negatives after reading everything gets every one wrong.
    const auto doc = parse(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f -1 -2 -3\n"
        "v 9 9 9\n"
        "f -1 -2 -3\n");
    REQUIRE(doc.faces.size() == 2);

    const auto first = cornersOf(doc, 0);
    CHECK(first[0].position == 2);
    CHECK(first[1].position == 1);
    CHECK(first[2].position == 0);

    // Same three tokens, one vertex later: every corner has moved.
    const auto second = cornersOf(doc, 1);
    CHECK(second[0].position == 3);
    CHECK(second[1].position == 2);
    CHECK(second[2].position == 1);
}

TEST_CASE("the four corner spellings each name what they name", "[obj]") {
    int v = 0, t = 0, n = 0;

    parseObjCorner("7", v, t, n);
    CHECK(v == 7); CHECK(t == 0); CHECK(n == 0);

    parseObjCorner("7/8", v, t, n);
    CHECK(v == 7); CHECK(t == 8); CHECK(n == 0);

    // The two-slash form skips the texcoord. Reading it as a texcoord is the
    // classic OBJ bug and puts the normal's index into the UV pool.
    parseObjCorner("7//9", v, t, n);
    CHECK(v == 7); CHECK(t == 0); CHECK(n == 9);

    parseObjCorner("7/8/9", v, t, n);
    CHECK(v == 7); CHECK(t == 8); CHECK(n == 9);

    parseObjCorner("-1/-2/-3", v, t, n);
    CHECK(v == -1); CHECK(t == -2); CHECK(n == -3);
}

TEST_CASE("zero is not an index, it is the absence of one", "[obj]") {
    // OBJ has no index 0, so a field left out reads as 0 and has to stay
    // distinguishable from a real index. Treating it as one would resolve to
    // -1 and read off the front of the pool.
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0.5 0.5\n"
        "f 1 2 3\n");
    const auto corners = cornersOf(doc, 0);
    for (const auto& corner : corners) {
        CHECK(corner.texcoord == -1);
        CHECK(corner.normal == -1);
    }
}

TEST_CASE("each pool is indexed independently", "[obj]") {
    // Positions, UVs and normals are three separate arrays. One shared counter
    // works for an exporter's own output, where they happen to line up, and
    // silently breaks on a file from anywhere else.
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
        "vt 0.0 0.0\nvt 1.0 0.0\n"
        "vn 0 0 1\n"
        "f 4/2/1 3/1/1 2/2/1\n");
    const auto corners = cornersOf(doc, 0);
    REQUIRE(corners.size() == 3);
    CHECK(corners[0].position == 3);
    CHECK(corners[0].texcoord == 1);
    CHECK(corners[0].normal == 0);
    CHECK(corners[1].position == 2);
    CHECK(corners[1].texcoord == 0);
    CHECK(corners[2].position == 1);
    CHECK(corners[2].texcoord == 1);
}

TEST_CASE("a face with more than three corners is kept whole", "[obj]") {
    // The parser does not triangulate: the callers fan the corners themselves,
    // and a quad has to arrive as four corners for that to be possible.
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 2 0\n"
        "f 1 2 3 4\n"
        "f 1 2 3 4 5\n");
    REQUIRE(doc.faces.size() == 2);
    CHECK(doc.faces[0].cornerCount == 4);
    CHECK(doc.faces[1].cornerCount == 5);
    CHECK(doc.ngonFaces == 2);
}

TEST_CASE("a face that cannot be resolved is dropped and counted", "[obj]") {
    // An index past the end of the pool, and a face with too few corners.
    // Both used to be reported to the user as a count, which is worth keeping:
    // a file where every face is malformed imports as an empty model.
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "f 1 2 9\n"      // 9 is past the end
        "f 1 2\n"        // two corners is not a face
        "f 0 1 2\n"      // 0 is not an index
        "f 1 2 3\n");
    CHECK(doc.faces.size() == 1);
    CHECK(doc.malformedFaces == 3);
}

TEST_CASE("a dropped face contributes no corners at all", "[obj]") {
    // Both importers used to resolve a face corner by corner and build a
    // vertex for each as they went, so a face that failed on its last corner
    // left the earlier ones behind as vertices no triangle referenced. That
    // was not free: the .wom importer computes the model's bounds over every
    // vertex, and one malformed face naming a distant position stretched the
    // bounding box to reach it. The renderer culls by that box.
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "v 900 900 900\nv 901 901 901\n"
        "f 1 2 3\n"
        "f 4 5 99\n");
    REQUIRE(doc.faces.size() == 1);
    CHECK(doc.malformedFaces == 1);
    // Three corners, from the one face that resolved. The two far positions
    // are still in the pool, because the pool is what the file declared, and
    // no corner refers to them.
    CHECK(doc.corners.size() == 3);
    CHECK(doc.positions.size() == 5);
}

TEST_CASE("a texcoord past the end drops to none rather than dropping the face",
          "[obj]") {
    // The position is what a triangle needs. Both importers already fell back
    // to a zero UV for a corner that named none, so a UV index that is out of
    // range takes the same path rather than losing the geometry.
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0.5 0.5\n"
        "f 1/9 2/1 3/9\n");
    REQUIRE(doc.faces.size() == 1);
    const auto corners = cornersOf(doc, 0);
    CHECK(corners[0].texcoord == -1);
    CHECK(corners[1].texcoord == 0);
    CHECK(corners[2].texcoord == -1);
    CHECK(doc.malformedFaces == 0);
}

TEST_CASE("two g lines with the same name are two groups", "[obj]") {
    // Groups are counted by occurrence, not by name. The .wob importer pools
    // vertices per group and clears its dedupe table at each `g`, so merging
    // two same-named blocks would merge two vertex pools.
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "g wall\n"
        "f 1 2 3\n"
        "g wall\n"
        "f 1 2 3\n");
    REQUIRE(doc.faces.size() == 2);
    CHECK(doc.groupNames.size() == 2);
    CHECK(doc.faces[0].group == 0);
    CHECK(doc.faces[1].group == 1);
}

TEST_CASE("faces before any g line still belong to a group", "[obj]") {
    // A .wom OBJ has no groups at all, and its faces still have to land
    // somewhere for the .wob importer to build from the same document. The
    // name is left empty: --import-wob-obj calls that group "imported" and
    // nothing else names it at all, so the parser does not choose.
    const auto doc = parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    REQUIRE(doc.faces.size() == 1);
    CHECK(doc.faces[0].group == 0);
    REQUIRE(doc.groupNames.size() == 1);
    CHECK(doc.groupNames[0].empty());
}

TEST_CASE("a g line with no name is still a group", "[obj]") {
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "g\n"
        "f 1 2 3\n");
    REQUIRE(doc.groupNames.size() == 1);
    CHECK(doc.groupNames[0].empty());
    CHECK(doc.faces[0].group == 0);
}

TEST_CASE("the object name is the first o line", "[obj]") {
    const auto doc = parse("o tree\nv 0 0 0\no ignored\n");
    CHECK(doc.objectName == "tree");
}

TEST_CASE("comments are kept, because one importer reads its own", "[obj]") {
    // --export-wob-obj writes doodad placements as comments and
    // --import-wob-obj reads them back, so dropping them at the parse loses
    // every prop in the building.
    const auto doc = parse(
        "# generated\n"
        "# doodad tree.wom pos 1,2,3 rot 0,0,0 scale 1\n"
        "v 0 0 0\n");
    REQUIRE(doc.comments.size() == 2);
    CHECK(doc.comments[1] == "# doodad tree.wom pos 1,2,3 rot 0,0,0 scale 1");
}

TEST_CASE("a CRLF file parses as a LF one", "[obj]") {
    // The carriage return would otherwise end up inside the last field on
    // every line, which breaks the last index of every face.
    const auto doc = parse(
        "v 0 0 0\r\nv 1 0 0\r\nv 0 1 0\r\n"
        "f 1 2 3\r\n");
    REQUIRE(doc.faces.size() == 1);
    CHECK(cornersOf(doc, 0)[2].position == 2);
    CHECK(doc.malformedFaces == 0);
}

TEST_CASE("trailing blanks do not become an empty corner", "[obj]") {
    const auto doc = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "f 1 2 3   \n");
    REQUIRE(doc.faces.size() == 1);
    CHECK(doc.faces[0].cornerCount == 3);
}

TEST_CASE("lines the format defines and this does not use are skipped",
          "[obj]") {
    const auto doc = parse(
        "mtllib scene.mtl\n"
        "usemtl stone\n"
        "s off\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "f 1 2 3\n");
    CHECK(doc.positions.size() == 3);
    CHECK(doc.faces.size() == 1);
    CHECK(doc.malformedFaces == 0);
}

TEST_CASE("an empty file is an empty document, not a failure", "[obj]") {
    const auto doc = parse("");
    CHECK(doc.positions.empty());
    CHECK(doc.faces.empty());
    CHECK(doc.malformedFaces == 0);
}

TEST_CASE("the offset rule stands on its own", "[obj]") {
    CHECK(objIndexToOffset(1, 3) == 0);
    CHECK(objIndexToOffset(3, 3) == 2);
    CHECK(objIndexToOffset(-1, 3) == 2);
    CHECK(objIndexToOffset(-3, 3) == 0);
    // Out of range in both directions, for the caller to reject.
    CHECK(objIndexToOffset(4, 3) == 3);
    CHECK(objIndexToOffset(-4, 3) == -1);
}
