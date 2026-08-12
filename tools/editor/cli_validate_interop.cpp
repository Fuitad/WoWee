#include "cli_validate_interop.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleValidateStl(int& i, int argc, char** argv) {
    // Structural validator for ASCII STL - pairs with --export-stl
    // and --import-stl (and --bake-zone-stl). Catches truncation,
    // missing solid framing, mismatched facet/vertex counts, and
    // non-finite vertex coords that would crash a slicer's mesh
    // analyzer.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr,
            "validate-stl: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<std::string> errors;
    std::string solidName;
    int facetCount = 0, vertCount = 0, nonFinite = 0;
    int facetsOpen = 0;  // facet-without-endfacet leak detector
    bool sawSolid = false, sawEndsolid = false;
    int currentFacetVerts = 0;
    std::string line;
    int lineNum = 0;
    while (std::getline(in, line)) {
        lineNum++;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok == "solid") {
            if (sawSolid) {
                errors.push_back("line " + std::to_string(lineNum) +
                                 ": multiple 'solid' headers");
            }
            sawSolid = true;
            ss >> solidName;
        } else if (tok == "facet") {
            facetCount++;
            facetsOpen++;
            currentFacetVerts = 0;
            std::string nrmTok;
            ss >> nrmTok;
            if (nrmTok != "normal") {
                errors.push_back("line " + std::to_string(lineNum) +
                                 ": 'facet' missing 'normal' subtoken");
            } else {
                float nx, ny, nz;
                if (!(ss >> nx >> ny >> nz)) {
                    errors.push_back("line " + std::to_string(lineNum) +
                                     ": 'facet normal' missing 3 floats");
                } else if (!std::isfinite(nx) || !std::isfinite(ny) ||
                            !std::isfinite(nz)) {
                    errors.push_back("line " + std::to_string(lineNum) +
                                     ": non-finite facet normal");
                    nonFinite++;
                }
            }
        } else if (tok == "vertex") {
            vertCount++;
            currentFacetVerts++;
            float x, y, z;
            if (!(ss >> x >> y >> z)) {
                errors.push_back("line " + std::to_string(lineNum) +
                                 ": 'vertex' missing 3 floats");
            } else if (!std::isfinite(x) || !std::isfinite(y) ||
                        !std::isfinite(z)) {
                nonFinite++;
                if (errors.size() < 30) {
                    errors.push_back("line " + std::to_string(lineNum) +
                                     ": non-finite vertex coord");
                }
            }
        } else if (tok == "endfacet") {
            facetsOpen--;
            if (currentFacetVerts != 3) {
                errors.push_back("line " + std::to_string(lineNum) +
                                 ": facet has " +
                                 std::to_string(currentFacetVerts) +
                                 " vertices, expected exactly 3");
            }
        } else if (tok == "endsolid") {
            sawEndsolid = true;
        }
        // outer loop / endloop are required by spec but ignored
        // here; their absence doesn't break parsing as long as
        // the vertex count per facet is correct.
    }
    if (!sawSolid) errors.push_back("missing 'solid' header");
    if (!sawEndsolid) errors.push_back("missing 'endsolid' footer");
    if (facetsOpen != 0) {
        errors.push_back(std::to_string(facetsOpen) +
                         " unclosed 'facet' (missing 'endfacet')");
    }
    if (vertCount != facetCount * 3) {
        errors.push_back("vertex count " + std::to_string(vertCount) +
                         " != 3 * facet count " +
                         std::to_string(facetCount));
    }
    if (jsonOut) {
        nlohmann::json j;
        j["stl"] = path;
        j["solidName"] = solidName;
        j["facetCount"] = facetCount;
        j["vertexCount"] = vertCount;
        j["nonFiniteCount"] = nonFinite;
        j["errorCount"] = errors.size();
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("STL: %s\n", path.c_str());
    std::printf("  solid name : %s\n",
                solidName.empty() ? "(unset)" : solidName.c_str());
    std::printf("  facets     : %d\n", facetCount);
    std::printf("  vertices   : %d\n", vertCount);
    if (nonFinite > 0) {
        std::printf("  non-finite : %d\n", nonFinite);
    }
    if (errors.empty()) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleValidatePng(int& i, int argc, char** argv) {
    // Full PNG structural validator - beyond --info-png's
    // header-only sniff. Walks every chunk, verifies CRC,
    // ensures IHDR/IDAT/IEND are present and ordered correctly.
    // Catches the kind of corruption (truncation mid-IDAT,
    // bit-flip in CRC) that browsers/decoders silently skip.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr,
            "validate-png: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    std::vector<std::string> errors;
    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    static const uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47,
                                     0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSig, 8) != 0) {
        errors.push_back("missing PNG signature");
    }
    // CRC32 table per PNG spec (matches the standard polynomial
    // 0xEDB88320; building once via constexpr-eligible logic).
    uint32_t crcTable[256];
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crcTable[n] = c;
    }
    auto crc32 = [&](const uint8_t* data, size_t len) {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t k = 0; k < len; ++k) {
            c = crcTable[(c ^ data[k]) & 0xFF] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    };
    auto be32 = [](const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
    };
    int chunkCount = 0;
    int badCrcs = 0;
    bool sawIHDR = false, sawIDAT = false, sawIEND = false;
    bool ihdrFirst = false;
    std::string firstChunkType;
    uint32_t width = 0, height = 0;
    uint8_t bitDepth = 0, colorType = 0;
    // Walk chunks: each is length(4) + type(4) + data(length) + crc(4).
    size_t off = 8;
    while (errors.empty() && off + 12 <= bytes.size()) {
        uint32_t len = be32(&bytes[off]);
        if (off + 8 + len + 4 > bytes.size()) {
            errors.push_back("chunk at offset " + std::to_string(off) +
                             " extends past file end");
            break;
        }
        std::string type(reinterpret_cast<const char*>(&bytes[off + 4]), 4);
        if (chunkCount == 0) {
            firstChunkType = type;
            ihdrFirst = (type == "IHDR");
        }
        chunkCount++;
        if (type == "IHDR") {
            sawIHDR = true;
            if (len >= 13) {
                width = be32(&bytes[off + 8]);
                height = be32(&bytes[off + 12]);
                bitDepth = bytes[off + 16];
                colorType = bytes[off + 17];
            }
        } else if (type == "IDAT") {
            sawIDAT = true;
        } else if (type == "IEND") {
            sawIEND = true;
        }
        // Verify CRC (computed over type + data, not length).
        uint32_t storedCrc = be32(&bytes[off + 8 + len]);
        uint32_t actualCrc = crc32(&bytes[off + 4], 4 + len);
        if (storedCrc != actualCrc) {
            badCrcs++;
            if (errors.size() < 10) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                    "chunk '%s' at offset %zu: CRC mismatch (stored=0x%08X actual=0x%08X)",
                    type.c_str(), off, storedCrc, actualCrc);
                errors.push_back(buf);
            }
        }
        off += 8 + len + 4;
    }
    if (!ihdrFirst) {
        errors.push_back("first chunk is '" + firstChunkType +
                          "', expected 'IHDR'");
    }
    if (!sawIHDR) errors.push_back("missing required IHDR chunk");
    if (!sawIDAT) errors.push_back("missing required IDAT chunk");
    if (!sawIEND) errors.push_back("missing required IEND chunk");
    if (off < bytes.size()) {
        errors.push_back(std::to_string(bytes.size() - off) +
                          " trailing bytes after IEND chunk");
    }
    if (jsonOut) {
        nlohmann::json j;
        j["png"] = path;
        j["width"] = width;
        j["height"] = height;
        j["bitDepth"] = bitDepth;
        j["colorType"] = colorType;
        j["chunkCount"] = chunkCount;
        j["badCrcs"] = badCrcs;
        j["fileSize"] = bytes.size();
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("PNG: %s\n", path.c_str());
    std::printf("  size       : %u x %u\n", width, height);
    std::printf("  bit depth  : %u (color type %u)\n", bitDepth, colorType);
    std::printf("  chunks     : %d (%d CRC mismatches)\n",
                chunkCount, badCrcs);
    std::printf("  file bytes : %zu\n", bytes.size());
    if (errors.empty()) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleValidateBlp(int& i, int argc, char** argv) {
    // BLP structural validator. --info-blp shows header fields
    // (full decode); this checks structural invariants without
    // decoding pixels - useful for spot-checking thousands of
    // BLPs in an extract dir without paying the DXT decompress
    // cost on each.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr,
            "validate-blp: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    std::vector<std::string> errors;
    uint32_t width = 0, height = 0;
    std::string magic;
    int validMips = 0;
    // BLP1 and BLP2 share magic 'BLP1' / 'BLP2' at byte 0; both
    // have 16 mipOffset slots + 16 mipSize slots after the
    // initial header (offsets vary by version).
    if (bytes.size() < 8) {
        errors.push_back("file too short to be a BLP");
    } else {
        magic.assign(bytes.begin(), bytes.begin() + 4);
        if (magic != "BLP1" && magic != "BLP2") {
            errors.push_back("magic is '" + magic + "', expected 'BLP1' or 'BLP2'");
        }
    }
    // BLP1 layout (post-magic):
    //   compression(4) + alphaBits(4) + width(4) + height(4) +
    //   extra(4) + hasMips(4) + mipOffsets[16](64) + mipSizes[16](64) +
    //   palette[256](1024)  [palette only present if compression==1]
    // BLP2 layout (post-magic):
    //   version(4) + compression(1) + alphaDepth(1) +
    //   alphaEncoding(1) + hasMips(1) + width(4) + height(4) +
    //   mipOffsets[16](64) + mipSizes[16](64) + palette[256](1024)
    uint32_t mipOffPos = 0, mipSzPos = 0;
    if (errors.empty()) {
        auto le32 = [&](size_t off) {
            uint32_t v = 0;
            if (off + 4 <= bytes.size()) std::memcpy(&v, &bytes[off], 4);
            return v;
        };
        if (magic == "BLP1") {
            width  = le32(4 + 8);   // skip magic + comp + alphaBits
            height = le32(4 + 12);
            mipOffPos = 4 + 24;     // after extra + hasMips
            mipSzPos  = 4 + 24 + 64;
        } else {
            width  = le32(4 + 8);   // BLP2: skip magic + version + 4 bytes
            height = le32(4 + 12);
            mipOffPos = 4 + 16;
            mipSzPos  = 4 + 16 + 64;
        }
        if (width == 0 || height == 0) {
            errors.push_back("zero width or height in header");
        }
        if (width > 8192 || height > 8192) {
            errors.push_back("dimensions " + std::to_string(width) +
                             "x" + std::to_string(height) +
                             " exceed 8192 (rejected by texture exporter)");
        }
        // Walk the mipOffset/mipSize tables and verify each
        // mip's data range is within the file. Stops at the
        // first zero offset (BLP convention for unused slots).
        if (mipSzPos + 64 <= bytes.size()) {
            for (int m = 0; m < 16; ++m) {
                uint32_t off = le32(mipOffPos + m * 4);
                uint32_t sz  = le32(mipSzPos  + m * 4);
                if (off == 0 && sz == 0) break;  // unused slot
                if (off == 0 || sz == 0) {
                    errors.push_back("mip " + std::to_string(m) +
                                     " has off=0 but size=" +
                                     std::to_string(sz) + " (or vice versa)");
                    continue;
                }
                if (uint64_t(off) + sz > bytes.size()) {
                    errors.push_back("mip " + std::to_string(m) +
                                     " range [" + std::to_string(off) +
                                     ", " + std::to_string(off + sz) +
                                     ") past file end " +
                                     std::to_string(bytes.size()));
                } else {
                    validMips++;
                }
            }
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["blp"] = path;
        j["magic"] = magic;
        j["width"] = width;
        j["height"] = height;
        j["validMips"] = validMips;
        j["fileSize"] = bytes.size();
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("BLP: %s\n", path.c_str());
    std::printf("  magic      : %s\n", magic.empty() ? "(none)" : magic.c_str());
    std::printf("  size       : %u x %u\n", width, height);
    std::printf("  valid mips : %d\n", validMips);
    std::printf("  file bytes : %zu\n", bytes.size());
    if (errors.empty()) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleValidateJsondbc(int& i, int argc, char** argv) {
    // Strict schema validator for JSON DBC sidecars. --info-jsondbc
    // checks that header recordCount matches the actual records[]
    // length; this goes deeper:
    //   - format tag is the wowee 1.0 string
    //   - source field present (so re-import knows which DBC slot)
    //   - recordCount + fieldCount are non-negative integers
    //   - records is an array
    //   - each record is an array exactly fieldCount long
    //   - each cell is string|number|bool|null (no objects/arrays)
    // Catches the kind of corruption that load() might silently
    // tolerate (missing fields default to 0/empty), letting the
    // editor's runtime DBC loader downstream-fail in confusing
    // ways.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr,
            "validate-jsondbc: cannot open %s\n", path.c_str());
        return 1;
    }
    nlohmann::json doc;
    std::vector<std::string> errors;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        errors.push_back(std::string("JSON parse error: ") + e.what());
    }
    std::string format, source;
    uint32_t recordCount = 0, fieldCount = 0;
    uint32_t actualRecs = 0;
    int badRowWidths = 0, badCellTypes = 0;
    if (errors.empty()) {
        if (!doc.is_object()) {
            errors.push_back("top-level value is not a JSON object");
        } else {
            if (!doc.contains("format")) {
                errors.push_back("missing 'format' field");
            } else if (!doc["format"].is_string()) {
                errors.push_back("'format' field is not a string");
            } else {
                format = doc["format"].get<std::string>();
                if (format != "wowee-dbc-json-1.0") {
                    errors.push_back("'format' is '" + format +
                                     "', expected 'wowee-dbc-json-1.0'");
                }
            }
            if (!doc.contains("source")) {
                errors.push_back("missing 'source' field (re-import needs it)");
            } else {
                source = doc.value("source", std::string{});
            }
            if (!doc.contains("recordCount") ||
                !doc["recordCount"].is_number_integer()) {
                errors.push_back("'recordCount' missing or not an integer");
            } else {
                recordCount = doc["recordCount"].get<uint32_t>();
            }
            if (!doc.contains("fieldCount") ||
                !doc["fieldCount"].is_number_integer()) {
                errors.push_back("'fieldCount' missing or not an integer");
            } else {
                fieldCount = doc["fieldCount"].get<uint32_t>();
            }
            if (!doc.contains("records") || !doc["records"].is_array()) {
                errors.push_back("'records' missing or not an array");
            } else {
                const auto& records = doc["records"];
                actualRecs = static_cast<uint32_t>(records.size());
                if (actualRecs != recordCount) {
                    errors.push_back("recordCount " + std::to_string(recordCount) +
                                     " != actual records " +
                                     std::to_string(actualRecs));
                }
                for (size_t r = 0; r < records.size(); ++r) {
                    const auto& row = records[r];
                    if (!row.is_array()) {
                        errors.push_back("record[" + std::to_string(r) +
                                         "] is not an array");
                        continue;
                    }
                    if (row.size() != fieldCount) {
                        badRowWidths++;
                        if (badRowWidths <= 3) {
                            errors.push_back("record[" + std::to_string(r) +
                                             "] has " + std::to_string(row.size()) +
                                             " cells, expected " +
                                             std::to_string(fieldCount));
                        }
                    }
                    for (size_t c = 0; c < row.size(); ++c) {
                        const auto& cell = row[c];
                        bool ok = cell.is_string() || cell.is_number() ||
                                  cell.is_boolean() || cell.is_null();
                        if (!ok) {
                            badCellTypes++;
                            if (badCellTypes <= 3) {
                                errors.push_back("record[" + std::to_string(r) +
                                                 "][" + std::to_string(c) +
                                                 "] has invalid type (objects/arrays not allowed)");
                            }
                        }
                    }
                }
                if (badRowWidths > 3) {
                    errors.push_back("... and " + std::to_string(badRowWidths - 3) +
                                     " more rows with wrong cell count");
                }
                if (badCellTypes > 3) {
                    errors.push_back("... and " + std::to_string(badCellTypes - 3) +
                                     " more cells with invalid types");
                }
            }
        }
    }
    int errorCount = static_cast<int>(errors.size());
    if (jsonOut) {
        nlohmann::json j;
        j["jsondbc"] = path;
        j["format"] = format;
        j["source"] = source;
        j["recordCount"] = recordCount;
        j["fieldCount"] = fieldCount;
        j["actualRecords"] = actualRecs;
        j["errorCount"] = errorCount;
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("JSON DBC: %s\n", path.c_str());
    std::printf("  format    : %s\n", format.empty() ? "?" : format.c_str());
    std::printf("  source    : %s\n", source.empty() ? "?" : source.c_str());
    std::printf("  records   : %u (header) / %u (actual)\n",
                recordCount, actualRecs);
    std::printf("  fields    : %u\n", fieldCount);
    if (errors.empty()) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED - %d error(s):\n", errorCount);
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

}  // namespace

bool handleValidateInterop(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--validate-stl") == 0 && i + 1 < argc) {
        outRc = handleValidateStl(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-png") == 0 && i + 1 < argc) {
        outRc = handleValidatePng(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-blp") == 0 && i + 1 < argc) {
        outRc = handleValidateBlp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-jsondbc") == 0 && i + 1 < argc) {
        outRc = handleValidateJsondbc(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
