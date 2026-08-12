#pragma once

/**
 * obj_parse.hpp - reading a Wavefront OBJ, once.
 *
 * Two importers parsed OBJ: --import-obj, which builds a .wom, and
 * --import-wob-obj, which builds a .wob with one vertex pool per group. The
 * parsing was the same in both down to the strtol walk over "3/4/5"; only what
 * they built from it differed.
 *
 * The parts worth writing down once are the ones a reader would not guess and
 * a second copy gets wrong quietly:
 *
 *   * face indices are 1-based, so index 1 is the first element
 *   * a negative index counts back from the end of the pool AS IT STANDS AT
 *     THAT LINE, not from its final size, so the same file cannot be resolved
 *     in a second pass
 *   * a corner may be "v", "v/vt", "v//vn" or "v/vt/vn", and the missing
 *     fields are absent rather than zero
 *   * a face may have more than three corners, and is fan-triangulated
 *
 * A wrong index here does not fail. It reads a neighbouring vertex, and the
 * model imports with a few triangles pointing somewhere else.
 *
 * What this does NOT do is build vertices. The two callers pool them
 * differently - one buffer against one per group - and that is the part that
 * was genuinely different.
 */

#include <cstdlib>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace wowee {
namespace editor {
namespace cli {

/// One corner of a face, resolved to 0-based offsets into the pools.
///
/// `texcoord` and `normal` are -1 when the corner did not name one. `position`
/// is always valid: a face with a corner that resolved outside the pool is
/// dropped by the parser rather than reaching a caller.
struct ObjCorner {
    int position = -1;
    int texcoord = -1;
    int normal = -1;
};

/// A face, as a run of corners and the group it was declared under.
struct ObjFace {
    size_t firstCorner = 0;
    size_t cornerCount = 0;
    /// Which `g` block this face fell in, counted by occurrence rather than by
    /// name: two `g` lines with the same name are two groups, which is what
    /// the importer did and what an exporter that numbers its groups expects.
    size_t group = 0;
};

struct ObjDocument {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;
    std::vector<ObjCorner> corners;  ///< flat, indexed by ObjFace
    std::vector<ObjFace> faces;
    /// One per `g` block, in order. Index 0 is an unnamed implicit group when
    /// the file declares faces before any `g`, because the two callers spell
    /// that default differently.
    std::vector<std::string> groupNames;
    std::string objectName;               ///< the first `o` line
    std::vector<std::string> comments;    ///< `#` lines, verbatim
    size_t malformedFaces = 0;  ///< fewer than three corners, or one unresolved
    size_t ngonFaces = 0;       ///< faces with more than three corners
};

/// Split one face corner token - "3", "3/4", "3//5", "3/4/5" - into its three
/// raw OBJ indices. A field the token does not carry stays 0, which is not a
/// valid OBJ index and is how "absent" is spelled.
inline void parseObjCorner(const std::string& token, int& v, int& t, int& n) {
    v = t = n = 0;
    const char* p = token.c_str();
    char* endp = nullptr;
    v = static_cast<int>(std::strtol(p, &endp, 10));
    if (*endp != '/') return;
    ++endp;
    if (*endp != '/') t = static_cast<int>(std::strtol(endp, &endp, 10));
    if (*endp != '/') return;
    ++endp;
    n = static_cast<int>(std::strtol(endp, &endp, 10));
}

/// Turn one raw OBJ index into a 0-based offset.
///
/// Positive indices are 1-based. A negative index is relative to the end of
/// the pool at this point in the file, so -1 is the element most recently
/// declared. `poolSize` therefore has to be the size now, not at the end.
inline int objIndexToOffset(int index, size_t poolSize) {
    if (index < 0) return static_cast<int>(poolSize) + index;
    return index - 1;
}

/// Read an OBJ into pools and faces.
///
/// Lines this does not recognise - mtllib, usemtl, s - are skipped, as they
/// were by both importers. Comments are kept because one caller reads its own
/// annotations back out of them.
inline ObjDocument parseObj(std::istream& in) {
    ObjDocument doc;
    size_t activeGroup = 0;
    bool anyGroupDeclared = false;
    std::string line;

    while (std::getline(in, line)) {
        // Strip CR for CRLF files, and trailing blanks, before anything reads
        // the last field on the line.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) continue;
        if (line[0] == '#') {
            doc.comments.push_back(line);
            continue;
        }

        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            glm::vec3 p; ss >> p.x >> p.y >> p.z;
            doc.positions.push_back(p);
        } else if (tag == "vt") {
            glm::vec2 t; ss >> t.x >> t.y;
            doc.texcoords.push_back(t);
        } else if (tag == "vn") {
            glm::vec3 n; ss >> n.x >> n.y >> n.z;
            doc.normals.push_back(n);
        } else if (tag == "o") {
            if (doc.objectName.empty()) ss >> doc.objectName;
        } else if (tag == "g") {
            std::string name;
            ss >> name;
            doc.groupNames.push_back(name);
            activeGroup = doc.groupNames.size() - 1;
            anyGroupDeclared = true;
        } else if (tag == "f") {
            std::vector<std::string> tokens;
            std::string token;
            while (ss >> token) tokens.push_back(token);
            if (tokens.size() < 3) {
                ++doc.malformedFaces;
                continue;
            }
            std::vector<ObjCorner> resolved;
            resolved.reserve(tokens.size());
            bool ok = true;
            for (const std::string& tok : tokens) {
                int v = 0, t = 0, n = 0;
                parseObjCorner(tok, v, t, n);
                ObjCorner corner;
                corner.position = objIndexToOffset(v, doc.positions.size());
                corner.texcoord =
                    (t == 0) ? -1 : objIndexToOffset(t, doc.texcoords.size());
                corner.normal =
                    (n == 0) ? -1 : objIndexToOffset(n, doc.normals.size());
                if (corner.position < 0 ||
                    corner.position >= static_cast<int>(doc.positions.size())) {
                    ok = false;
                    break;
                }
                // A texcoord or normal index outside its pool is dropped
                // rather than failing the face: the position is what a
                // triangle needs, and both importers already fell back to a
                // zero UV and a +Z normal for a corner that named neither.
                if (corner.texcoord < 0 ||
                    corner.texcoord >= static_cast<int>(doc.texcoords.size())) {
                    corner.texcoord = -1;
                }
                if (corner.normal < 0 ||
                    corner.normal >= static_cast<int>(doc.normals.size())) {
                    corner.normal = -1;
                }
                resolved.push_back(corner);
            }
            if (!ok) {
                ++doc.malformedFaces;
                continue;
            }
            if (!anyGroupDeclared) {
                doc.groupNames.emplace_back();  // the caller names it
                anyGroupDeclared = true;
            }
            if (resolved.size() > 3) ++doc.ngonFaces;
            ObjFace face;
            face.firstCorner = doc.corners.size();
            face.cornerCount = resolved.size();
            face.group = activeGroup;
            doc.corners.insert(doc.corners.end(), resolved.begin(),
                               resolved.end());
            doc.faces.push_back(face);
        }
    }
    return doc;
}

} // namespace cli
} // namespace editor
} // namespace wowee
