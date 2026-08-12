#include "cli_glb_inspect.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleValidateOrInfoGlb(int& i, int argc, char** argv) {
    // Shared handler: --validate-glb errors out on broken structure;
    // --info-glb prints the same metadata but exits 0 unless the
    // file is unreadable. Same parser, different verdict policy.
    bool isValidate = (std::strcmp(argv[i], "--validate-glb") == 0);
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr,
            "%s: cannot open %s\n",
            isValidate ? "validate-glb" : "info-glb", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    std::vector<std::string> errors;
    // 12-byte header: 'glTF' magic, version=2, total length.
    uint32_t magic = 0, version = 0, totalLen = 0;
    if (bytes.size() < 12) {
        errors.push_back("file too short for glTF header (need 12 bytes)");
    } else {
        std::memcpy(&magic,    &bytes[0], 4);
        std::memcpy(&version,  &bytes[4], 4);
        std::memcpy(&totalLen, &bytes[8], 4);
        if (magic != 0x46546C67) {
            errors.push_back("magic is not 'glTF' (0x46546C67)");
        }
        if (version != 2) {
            errors.push_back("version " + std::to_string(version) +
                             " not supported (only glTF 2.0)");
        }
        if (totalLen != bytes.size()) {
            errors.push_back("totalLength=" + std::to_string(totalLen) +
                             " != file size " + std::to_string(bytes.size()));
        }
    }
    // JSON chunk follows: 4-byte length, 4-byte type ('JSON'),
    // then payload. Then BIN chunk same shape.
    uint32_t jsonLen = 0, jsonType = 0;
    uint32_t binLen = 0, binType = 0;
    std::string jsonStr;
    std::vector<uint8_t> binData;
    if (errors.empty()) {
        if (bytes.size() < 20) {
            errors.push_back("missing JSON chunk header");
        } else {
            std::memcpy(&jsonLen, &bytes[12], 4);
            std::memcpy(&jsonType, &bytes[16], 4);
            if (jsonType != 0x4E4F534A) {
                errors.push_back("first chunk type is not 'JSON' (0x4E4F534A)");
            }
            if (20 + jsonLen > bytes.size()) {
                errors.push_back("JSON chunk extends past file end");
            } else {
                jsonStr.assign(bytes.begin() + 20,
                                bytes.begin() + 20 + jsonLen);
            }
        }
        size_t binOff = 20 + jsonLen;
        if (binOff + 8 <= bytes.size()) {
            std::memcpy(&binLen, &bytes[binOff], 4);
            std::memcpy(&binType, &bytes[binOff + 4], 4);
            if (binType != 0x004E4942) {
                errors.push_back("second chunk type is not 'BIN\\0' (0x004E4942)");
            }
            if (binOff + 8 + binLen > bytes.size()) {
                errors.push_back("BIN chunk extends past file end");
            } else {
                binData.assign(bytes.begin() + binOff + 8,
                                bytes.begin() + binOff + 8 + binLen);
            }
        }
        // BIN chunk is optional in spec; only flag missing if
        // accessors below reference a buffer.
    }
    // Parse JSON and validate structure.
    nlohmann::json gj;
    int meshCount = 0, primitiveCount = 0, accessorCount = 0,
        bufferViewCount = 0, bufferCount = 0;
    std::string assetVersion;
    if (errors.empty() && !jsonStr.empty()) {
        try {
            gj = nlohmann::json::parse(jsonStr);
            assetVersion = gj.value("/asset/version"_json_pointer, std::string{});
            if (assetVersion != "2.0") {
                errors.push_back("asset.version is '" + assetVersion +
                                 "', not '2.0'");
            }
            if (gj.contains("meshes") && gj["meshes"].is_array()) {
                meshCount = static_cast<int>(gj["meshes"].size());
                for (const auto& m : gj["meshes"]) {
                    if (m.contains("primitives") && m["primitives"].is_array()) {
                        primitiveCount += static_cast<int>(m["primitives"].size());
                    }
                }
            }
            if (gj.contains("accessors") && gj["accessors"].is_array()) {
                accessorCount = static_cast<int>(gj["accessors"].size());
                // Verify each accessor's bufferView exists.
                for (size_t a = 0; a < gj["accessors"].size(); ++a) {
                    const auto& acc = gj["accessors"][a];
                    if (acc.contains("bufferView")) {
                        int bv = acc["bufferView"];
                        if (!gj.contains("bufferViews") ||
                            bv >= static_cast<int>(gj["bufferViews"].size())) {
                            errors.push_back("accessor " + std::to_string(a) +
                                             " bufferView=" + std::to_string(bv) +
                                             " out of range");
                        }
                    }
                }
            }
            if (gj.contains("bufferViews") && gj["bufferViews"].is_array()) {
                bufferViewCount = static_cast<int>(gj["bufferViews"].size());
                for (size_t b = 0; b < gj["bufferViews"].size(); ++b) {
                    const auto& bv = gj["bufferViews"][b];
                    uint32_t bo = bv.value("byteOffset", 0u);
                    uint32_t bl = bv.value("byteLength", 0u);
                    uint64_t end = uint64_t(bo) + bl;
                    if (end > binLen) {
                        errors.push_back("bufferView " + std::to_string(b) +
                                         " range [" + std::to_string(bo) +
                                         ", " + std::to_string(end) +
                                         ") past BIN chunk length " +
                                         std::to_string(binLen));
                    }
                }
            }
            if (gj.contains("buffers") && gj["buffers"].is_array()) {
                bufferCount = static_cast<int>(gj["buffers"].size());
            }
        } catch (const std::exception& e) {
            errors.push_back(std::string("JSON parse error: ") + e.what());
        }
    }
    int errorCount = static_cast<int>(errors.size());
    if (jsonOut) {
        nlohmann::json j;
        j["glb"] = path;
        j["fileSize"] = bytes.size();
        j["version"] = version;
        j["assetVersion"] = assetVersion;
        j["totalLength"] = totalLen;
        j["jsonLength"] = jsonLen;
        j["binLength"] = binLen;
        j["meshes"] = meshCount;
        j["primitives"] = primitiveCount;
        j["accessors"] = accessorCount;
        j["bufferViews"] = bufferViewCount;
        j["buffers"] = bufferCount;
        j["errorCount"] = errorCount;
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return (isValidate && errorCount > 0) ? 1 : 0;
    }
    std::printf("GLB: %s\n", path.c_str());
    std::printf("  file bytes  : %zu\n", bytes.size());
    std::printf("  glTF version: %u (asset.version=%s)\n",
                version, assetVersion.empty() ? "?" : assetVersion.c_str());
    std::printf("  totalLength : %u\n", totalLen);
    std::printf("  JSON chunk  : %u bytes\n", jsonLen);
    std::printf("  BIN chunk   : %u bytes\n", binLen);
    std::printf("  meshes      : %d (%d primitives)\n",
                meshCount, primitiveCount);
    std::printf("  accessors   : %d  bufferViews: %d  buffers: %d\n",
                accessorCount, bufferViewCount, bufferCount);
    if (errors.empty()) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED - %d error(s):\n", errorCount);
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return isValidate ? 1 : 0;
}

int handleInfoGlbTree(int& i, int /*argc*/, char** argv) {
    // Pretty `tree`-style view of glTF structure. --info-glb gives
    // counts; this shows the actual scene→node→mesh→primitive
    // hierarchy with names. Useful when debugging 'why is this
    // imported model showing up empty in three.js?' (often
    // because the scene's nodes[] array references the wrong node).
    std::string path = argv[++i];
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr,
            "info-glb-tree: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    if (bytes.size() < 28) {
        std::fprintf(stderr, "info-glb-tree: file too short\n");
        return 1;
    }
    uint32_t magic, version;
    std::memcpy(&magic, &bytes[0], 4);
    std::memcpy(&version, &bytes[4], 4);
    if (magic != 0x46546C67 || version != 2) {
        std::fprintf(stderr, "info-glb-tree: not glTF 2.0\n");
        return 1;
    }
    uint32_t jsonLen;
    std::memcpy(&jsonLen, &bytes[12], 4);
    std::string jsonStr(bytes.begin() + 20, bytes.begin() + 20 + jsonLen);
    nlohmann::json gj;
    try { gj = nlohmann::json::parse(jsonStr); }
    catch (const std::exception& e) {
        std::fprintf(stderr, "info-glb-tree: JSON parse failed: %s\n", e.what());
        return 1;
    }
    // Tree drawing
    auto branch = [](bool last) { return last ? "└─ " : "├─ "; };
    auto cont = [](bool last) { return last ? "   " : "│  "; };
    std::printf("%s\n", path.c_str());
    // Asset section
    std::string genName = gj.value("/asset/version"_json_pointer, std::string{});
    std::string gen = gj.value("/asset/generator"_json_pointer, std::string{});
    std::printf("├─ asset (v%s, %s)\n",
                genName.c_str(),
                gen.empty() ? "no generator" : gen.c_str());
    // Buffers
    int nBuf = (gj.contains("buffers") && gj["buffers"].is_array())
                ? static_cast<int>(gj["buffers"].size()) : 0;
    std::printf("├─ buffers (%d)\n", nBuf);
    if (nBuf > 0) {
        for (int b = 0; b < nBuf; ++b) {
            bool last = (b == nBuf - 1);
            uint64_t bl = gj["buffers"][b].value("byteLength", 0u);
            std::printf("│  %s[%d] %llu bytes\n", branch(last), b,
                        static_cast<unsigned long long>(bl));
        }
    }
    // BufferViews
    int nBV = (gj.contains("bufferViews") && gj["bufferViews"].is_array())
               ? static_cast<int>(gj["bufferViews"].size()) : 0;
    std::printf("├─ bufferViews (%d)\n", nBV);
    for (int v = 0; v < nBV; ++v) {
        bool last = (v == nBV - 1);
        const auto& bv = gj["bufferViews"][v];
        uint32_t bo = bv.value("byteOffset", 0u);
        uint32_t bl = bv.value("byteLength", 0u);
        int target = bv.value("target", 0);
        std::printf("│  %s[%d] off=%u len=%u%s\n",
                    branch(last), v, bo, bl,
                    target == 34962 ? " (vertex)"
                  : target == 34963 ? " (index)"
                  : "");
    }
    // Accessors
    int nAcc = (gj.contains("accessors") && gj["accessors"].is_array())
                ? static_cast<int>(gj["accessors"].size()) : 0;
    std::printf("├─ accessors (%d)\n", nAcc);
    for (int a = 0; a < nAcc; ++a) {
        bool last = (a == nAcc - 1);
        const auto& acc = gj["accessors"][a];
        int ct = acc.value("componentType", 0);
        std::string type = acc.value("type", std::string{});
        uint32_t count = acc.value("count", 0u);
        int bv = acc.value("bufferView", -1);
        const char* ctName =
            ct == 5120 ? "i8" :
            ct == 5121 ? "u8" :
            ct == 5122 ? "i16" :
            ct == 5123 ? "u16" :
            ct == 5125 ? "u32" :
            ct == 5126 ? "f32" : "?";
        std::printf("│  %s[%d] %s %s ×%u (bv=%d)\n",
                    branch(last), a, ctName, type.c_str(), count, bv);
    }
    // Meshes (with primitives nested)
    int nMesh = (gj.contains("meshes") && gj["meshes"].is_array())
                 ? static_cast<int>(gj["meshes"].size()) : 0;
    std::printf("├─ meshes (%d)\n", nMesh);
    for (int m = 0; m < nMesh; ++m) {
        bool lastM = (m == nMesh - 1);
        const auto& mesh = gj["meshes"][m];
        std::string name = mesh.value("name", std::string{});
        int nPrim = (mesh.contains("primitives") && mesh["primitives"].is_array())
                     ? static_cast<int>(mesh["primitives"].size()) : 0;
        std::printf("│  %s[%d]%s%s (%d primitives)\n",
                    branch(lastM), m,
                    name.empty() ? "" : " ",
                    name.c_str(), nPrim);
        for (int p = 0; p < nPrim; ++p) {
            bool lastP = (p == nPrim - 1);
            const auto& prim = mesh["primitives"][p];
            int idxAcc = prim.value("indices", -1);
            int mode = prim.value("mode", 4);
            const char* modeName =
                mode == 0 ? "POINTS" :
                mode == 1 ? "LINES" :
                mode == 4 ? "TRIANGLES" : "?";
            std::printf("│  %s%s[%d] %s indices=acc#%d\n",
                        cont(lastM), branch(lastP), p, modeName, idxAcc);
        }
    }
    // Nodes (flat list - could be tree but glTF nodes are a graph)
    int nNode = (gj.contains("nodes") && gj["nodes"].is_array())
                 ? static_cast<int>(gj["nodes"].size()) : 0;
    std::printf("├─ nodes (%d)\n", nNode);
    for (int n = 0; n < nNode; ++n) {
        bool last = (n == nNode - 1);
        const auto& node = gj["nodes"][n];
        std::string name = node.value("name", std::string{});
        int meshIdx = node.value("mesh", -1);
        std::printf("│  %s[%d]%s%s%s\n",
                    branch(last), n,
                    name.empty() ? "" : " ",
                    name.c_str(),
                    meshIdx >= 0 ? (" -> mesh#" + std::to_string(meshIdx)).c_str() : "");
    }
    // Scenes (last branch)
    int nScene = (gj.contains("scenes") && gj["scenes"].is_array())
                  ? static_cast<int>(gj["scenes"].size()) : 0;
    std::printf("└─ scenes (%d, default=%d)\n",
                nScene, gj.value("scene", 0));
    for (int s = 0; s < nScene; ++s) {
        bool lastS = (s == nScene - 1);
        const auto& scene = gj["scenes"][s];
        int nodeRefs = (scene.contains("nodes") && scene["nodes"].is_array())
                        ? static_cast<int>(scene["nodes"].size()) : 0;
        std::printf("   %s[%d] nodes=[", branch(lastS), s);
        if (scene.contains("nodes") && scene["nodes"].is_array()) {
            for (size_t k = 0; k < scene["nodes"].size(); ++k) {
                std::printf("%s%d", k ? "," : "", scene["nodes"][k].get<int>());
            }
        }
        std::printf("] (%d nodes)\n", nodeRefs);
    }
    return 0;
}

int handleInfoGlbBytes(int& i, int argc, char** argv) {
    // Per-section + per-bufferView byte breakdown of a .glb. Useful
    // for understanding what's bloating a baked .glb (vertex attrs
    // vs indices, position vs uv vs normal data, mesh-level
    // payloads). Pairs with --info-glb (counts) and --info-glb-tree
    // (structure).
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr,
            "info-glb-bytes: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    if (bytes.size() < 28) {
        std::fprintf(stderr, "info-glb-bytes: file too short\n");
        return 1;
    }
    uint32_t magic, version;
    std::memcpy(&magic, &bytes[0], 4);
    std::memcpy(&version, &bytes[4], 4);
    if (magic != 0x46546C67 || version != 2) {
        std::fprintf(stderr, "info-glb-bytes: not glTF 2.0\n");
        return 1;
    }
    uint32_t jsonLen, binLen = 0;
    std::memcpy(&jsonLen, &bytes[12], 4);
    std::string jsonStr(bytes.begin() + 20,
                         bytes.begin() + 20 + jsonLen);
    size_t binOff = 20 + jsonLen;
    if (binOff + 8 <= bytes.size()) {
        std::memcpy(&binLen, &bytes[binOff], 4);
    }
    uint32_t headerBytes = 12;        // magic+version+totalLength
    uint32_t jsonHdrBytes = 8;        // jsonLen + jsonType
    uint32_t binHdrBytes = (binLen > 0) ? 8 : 0;
    nlohmann::json gj;
    try { gj = nlohmann::json::parse(jsonStr); }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "info-glb-bytes: JSON parse failed: %s\n", e.what());
        return 1;
    }
    // Per-bufferView size table.
    struct BV { int idx; uint32_t off, len; std::string label; };
    std::vector<BV> bufferViews;
    if (gj.contains("bufferViews") && gj["bufferViews"].is_array()) {
        for (size_t k = 0; k < gj["bufferViews"].size(); ++k) {
            const auto& bv = gj["bufferViews"][k];
            BV b;
            b.idx = static_cast<int>(k);
            b.off = bv.value("byteOffset", 0u);
            b.len = bv.value("byteLength", 0u);
            int target = bv.value("target", 0);
            b.label = (target == 34962) ? "vertex" :
                      (target == 34963) ? "index" : "other";
            bufferViews.push_back(b);
        }
    }
    // Bucket bufferViews by purpose using accessor types.
    // Walk accessors: each references a bufferView, with type
    // (VEC3/VEC2/SCALAR) hinting at content (position/uv/etc.)
    std::map<std::string, uint64_t> bytesByPurpose;
    if (gj.contains("accessors") && gj["accessors"].is_array() &&
        gj.contains("meshes") && gj["meshes"].is_array()) {
        std::set<int> seenAccessors;
        for (const auto& m : gj["meshes"]) {
            if (!m.contains("primitives") || !m["primitives"].is_array()) continue;
            for (const auto& p : m["primitives"]) {
                if (!p.contains("attributes")) continue;
                for (auto it = p["attributes"].begin();
                     it != p["attributes"].end(); ++it) {
                    int ai = it.value().get<int>();
                    if (seenAccessors.count(ai)) continue;
                    seenAccessors.insert(ai);
                    if (ai < 0 || ai >= static_cast<int>(gj["accessors"].size())) continue;
                    const auto& acc = gj["accessors"][ai];
                    int bv = acc.value("bufferView", -1);
                    if (bv < 0 || bv >= static_cast<int>(bufferViews.size())) continue;
                    std::string typeStr = acc.value("type", std::string{});
                    int comp = acc.value("componentType", 0);
                    uint32_t cnt = acc.value("count", 0u);
                    uint32_t byteStride =
                        typeStr == "VEC3" ? 12 :
                        typeStr == "VEC2" ? 8 :
                        typeStr == "VEC4" ? 16 :
                        typeStr == "SCALAR" ?
                            (comp == 5126 ? 4 : comp == 5125 ? 4 :
                             comp == 5123 ? 2 : comp == 5121 ? 1 : 4) : 4;
                    uint64_t b = uint64_t(cnt) * byteStride;
                    bytesByPurpose[it.key()] += b;
                }
                // Indices accessor.
                if (p.contains("indices")) {
                    int ai = p["indices"].get<int>();
                    if (seenAccessors.count(ai)) continue;
                    seenAccessors.insert(ai);
                    if (ai < 0 || ai >= static_cast<int>(gj["accessors"].size())) continue;
                    const auto& acc = gj["accessors"][ai];
                    uint32_t cnt = acc.value("count", 0u);
                    int comp = acc.value("componentType", 0);
                    uint32_t s = (comp == 5125 ? 4 : comp == 5123 ? 2 : 4);
                    bytesByPurpose["INDICES"] += uint64_t(cnt) * s;
                }
            }
        }
    }
    uint64_t totalBytes = bytes.size();
    if (jsonOut) {
        nlohmann::json j;
        j["glb"] = path;
        j["totalBytes"] = totalBytes;
        j["sections"] = {
            {"header", headerBytes},
            {"jsonHeader", jsonHdrBytes},
            {"json", jsonLen},
            {"binHeader", binHdrBytes},
            {"bin", binLen}
        };
        nlohmann::json bvArr = nlohmann::json::array();
        for (const auto& bv : bufferViews) {
            bvArr.push_back({{"index", bv.idx},
                              {"target", bv.label},
                              {"bytes", bv.len}});
        }
        j["bufferViews"] = bvArr;
        nlohmann::json byPurp = nlohmann::json::object();
        for (const auto& [p, b] : bytesByPurpose) byPurp[p] = b;
        j["byPurpose"] = byPurp;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("GLB bytes: %s\n", path.c_str());
    std::printf("  total: %llu bytes (%.2f MB)\n",
                static_cast<unsigned long long>(totalBytes),
                totalBytes / (1024.0 * 1024.0));
    std::printf("\n  Sections:\n");
    auto pct = [&](uint64_t v) {
        return totalBytes ? 100.0 * v / totalBytes : 0.0;
    };
    std::printf("    header     : %5u bytes  %5.2f%%\n", headerBytes, pct(headerBytes));
    std::printf("    JSON hdr   : %5u bytes  %5.2f%%\n", jsonHdrBytes, pct(jsonHdrBytes));
    std::printf("    JSON       : %5u bytes  %5.2f%%\n", jsonLen, pct(jsonLen));
    std::printf("    BIN hdr    : %5u bytes  %5.2f%%\n", binHdrBytes, pct(binHdrBytes));
    std::printf("    BIN        : %5u bytes  %5.2f%%\n", binLen, pct(binLen));
    if (!bufferViews.empty()) {
        std::printf("\n  BufferViews:\n");
        std::printf("    idx  target   bytes      MB    share-of-bin\n");
        for (const auto& bv : bufferViews) {
            double bvPct = binLen ? 100.0 * bv.len / binLen : 0.0;
            std::printf("    %3d  %-7s  %8u  %6.2f  %5.2f%%\n",
                        bv.idx, bv.label.c_str(), bv.len,
                        bv.len / (1024.0 * 1024.0), bvPct);
        }
    }
    if (!bytesByPurpose.empty()) {
        std::printf("\n  By attribute:\n");
        for (const auto& [p, b] : bytesByPurpose) {
            double bPct = binLen ? 100.0 * b / binLen : 0.0;
            std::printf("    %-12s %8llu bytes  (%.2f%% of BIN)\n",
                        p.c_str(),
                        static_cast<unsigned long long>(b), bPct);
        }
    }
    return 0;
}

int handleCheckGlbBounds(int& i, int argc, char** argv) {
    // Cross-checks every position accessor's claimed min/max
    // against the actual data in the BIN chunk. glTF viewers use
    // these for camera framing and frustum culling - stale
    // values (e.g. from a tool that edited geometry without
    // recomputing) cause models to vanish at certain angles or
    // get framed wrong on load.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr,
            "check-glb-bounds: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    // Parse glb structure (re-implements --validate-glb's parser
    // since we need access to the BIN chunk bytes here).
    if (bytes.size() < 28) {
        std::fprintf(stderr,
            "check-glb-bounds: file too short to be a .glb\n");
        return 1;
    }
    uint32_t magic, version;
    std::memcpy(&magic, &bytes[0], 4);
    std::memcpy(&version, &bytes[4], 4);
    if (magic != 0x46546C67 || version != 2) {
        std::fprintf(stderr,
            "check-glb-bounds: not a valid glTF 2.0 binary\n");
        return 1;
    }
    uint32_t jsonLen, jsonType;
    std::memcpy(&jsonLen, &bytes[12], 4);
    std::memcpy(&jsonType, &bytes[16], 4);
    std::string jsonStr(bytes.begin() + 20, bytes.begin() + 20 + jsonLen);
    size_t binOff = 20 + jsonLen;
    std::memcpy(&magic, &bytes[binOff + 4], 4);  // chunkType
    const uint8_t* binData = &bytes[binOff + 8];
    uint32_t binLen;
    std::memcpy(&binLen, &bytes[binOff], 4);
    (void)binLen;  // not range-checked here; --validate-glb does that
    nlohmann::json gj;
    try { gj = nlohmann::json::parse(jsonStr); }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "check-glb-bounds: JSON parse failed: %s\n", e.what());
        return 1;
    }
    std::vector<std::string> errors;
    int posAccessors = 0, mismatched = 0;
    // Walk all primitives, collect their POSITION accessor index,
    // dedupe (multiple primitives can share an accessor - only
    // recompute once per unique).
    std::set<int> posAccIndices;
    if (gj.contains("meshes") && gj["meshes"].is_array()) {
        for (const auto& m : gj["meshes"]) {
            if (!m.contains("primitives") || !m["primitives"].is_array()) continue;
            for (const auto& p : m["primitives"]) {
                if (p.contains("attributes") &&
                    p["attributes"].contains("POSITION")) {
                    posAccIndices.insert(p["attributes"]["POSITION"].get<int>());
                }
            }
        }
    }
    const auto& accessors = gj["accessors"];
    const auto& bufferViews = gj["bufferViews"];
    for (int ai : posAccIndices) {
        if (ai < 0 || ai >= static_cast<int>(accessors.size())) {
            errors.push_back("position accessor " + std::to_string(ai) +
                             " out of range");
            continue;
        }
        const auto& acc = accessors[ai];
        if (acc.value("type", std::string{}) != "VEC3" ||
            acc.value("componentType", 0) != 5126) {
            errors.push_back("accessor " + std::to_string(ai) +
                             " is not VEC3 FLOAT");
            continue;
        }
        posAccessors++;
        int bvIdx = acc.value("bufferView", -1);
        if (bvIdx < 0 || bvIdx >= static_cast<int>(bufferViews.size())) {
            errors.push_back("accessor " + std::to_string(ai) +
                             " bufferView " + std::to_string(bvIdx) +
                             " out of range");
            continue;
        }
        const auto& bv = bufferViews[bvIdx];
        uint32_t bvOff = bv.value("byteOffset", 0u);
        uint32_t accOff = acc.value("byteOffset", 0u);
        uint32_t count = acc.value("count", 0u);
        const uint8_t* p = binData + bvOff + accOff;
        glm::vec3 actualMin{1e30f}, actualMax{-1e30f};
        for (uint32_t v = 0; v < count; ++v) {
            glm::vec3 pos;
            std::memcpy(&pos.x, p + v * 12 + 0, 4);
            std::memcpy(&pos.y, p + v * 12 + 4, 4);
            std::memcpy(&pos.z, p + v * 12 + 8, 4);
            actualMin = glm::min(actualMin, pos);
            actualMax = glm::max(actualMax, pos);
        }
        // Compare against claimed min/max (within float epsilon).
        glm::vec3 claimedMin{0}, claimedMax{0};
        bool hasClaimed = (acc.contains("min") && acc.contains("max"));
        if (hasClaimed) {
            claimedMin.x = acc["min"][0]; claimedMin.y = acc["min"][1]; claimedMin.z = acc["min"][2];
            claimedMax.x = acc["max"][0]; claimedMax.y = acc["max"][1]; claimedMax.z = acc["max"][2];
            auto close = [](float a, float b) {
                return std::abs(a - b) < 1e-3f;
            };
            bool ok = close(claimedMin.x, actualMin.x) &&
                      close(claimedMin.y, actualMin.y) &&
                      close(claimedMin.z, actualMin.z) &&
                      close(claimedMax.x, actualMax.x) &&
                      close(claimedMax.y, actualMax.y) &&
                      close(claimedMax.z, actualMax.z);
            if (!ok) {
                mismatched++;
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "accessor %d bounds mismatch: claimed [%g,%g,%g]-[%g,%g,%g] vs actual [%g,%g,%g]-[%g,%g,%g]",
                    ai,
                    claimedMin.x, claimedMin.y, claimedMin.z,
                    claimedMax.x, claimedMax.y, claimedMax.z,
                    actualMin.x, actualMin.y, actualMin.z,
                    actualMax.x, actualMax.y, actualMax.z);
                errors.push_back(buf);
            }
        } else {
            // glTF spec requires position accessors to declare min/max.
            errors.push_back("accessor " + std::to_string(ai) +
                             " missing required min/max for POSITION attribute");
            mismatched++;
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["glb"] = path;
        j["positionAccessors"] = posAccessors;
        j["mismatched"] = mismatched;
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("GLB bounds: %s\n", path.c_str());
    std::printf("  position accessors checked : %d\n", posAccessors);
    std::printf("  mismatched                 : %d\n", mismatched);
    if (errors.empty()) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

}  // namespace

bool handleGlbInspect(int& i, int argc, char** argv, int& outRc) {
    if ((std::strcmp(argv[i], "--validate-glb") == 0 ||
         std::strcmp(argv[i], "--info-glb") == 0) && i + 1 < argc) {
        outRc = handleValidateOrInfoGlb(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-glb-tree") == 0 && i + 1 < argc) {
        outRc = handleInfoGlbTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-glb-bytes") == 0 && i + 1 < argc) {
        outRc = handleInfoGlbBytes(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--check-glb-bounds") == 0 && i + 1 < argc) {
        outRc = handleCheckGlbBounds(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
