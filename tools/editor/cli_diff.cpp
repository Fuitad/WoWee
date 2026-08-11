#include "cli_diff.hpp"
#include "cli_catalog_paths.hpp"

#include "pipeline/wowee_model.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "content_pack.hpp"
#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleDiffWcp(int& i, int argc, char** argv) {
    // Print which files differ between two WCP archives. Useful
    // when verifying that an authoring tweak only changed what
    // it claimed to change, or when comparing pack-WCP output
    // across editor versions for regression detection.
    std::string aPath = argv[++i];
    std::string bPath = argv[++i];
    // Optional --json after both paths for machine-readable output.
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::ContentPackInfo aInfo, bInfo;
    if (!wowee::editor::ContentPacker::readInfo(aPath, aInfo) ||
        !wowee::editor::ContentPacker::readInfo(bPath, bInfo)) {
        std::fprintf(stderr, "Failed to read WCP info\n");
        return 1;
    }
    std::unordered_map<std::string, uint64_t> aFiles, bFiles;
    for (const auto& f : aInfo.files) aFiles[f.path] = f.size;
    for (const auto& f : bInfo.files) bFiles[f.path] = f.size;

    int onlyA = 0, onlyB = 0, sizeChanged = 0, identical = 0;
    std::vector<std::string> onlyAList, onlyBList, changedList;
    // For JSON we want size-change rows as structured records, not
    // pre-formatted strings — collect both forms in one pass.
    struct ChangedRow { std::string path; uint64_t aSize, bSize; };
    std::vector<ChangedRow> changedRows;
    for (const auto& [p, sz] : aFiles) {
        auto it = bFiles.find(p);
        if (it == bFiles.end()) { onlyA++; onlyAList.push_back(p); }
        else if (it->second != sz) {
            sizeChanged++;
            changedList.push_back(p + " (" + std::to_string(sz) + " -> " +
                                  std::to_string(it->second) + ")");
            changedRows.push_back({p, sz, it->second});
        } else identical++;
    }
    for (const auto& [p, sz] : bFiles) {
        if (aFiles.find(p) == aFiles.end()) { onlyB++; onlyBList.push_back(p); }
    }
    std::sort(onlyAList.begin(), onlyAList.end());
    std::sort(onlyBList.begin(), onlyBList.end());
    std::sort(changedList.begin(), changedList.end());
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aPath;
        j["b"] = bPath;
        j["identical"] = identical;
        j["changed"] = sizeChanged;
        j["onlyA"] = onlyA;
        j["onlyB"] = onlyB;
        std::sort(changedRows.begin(), changedRows.end(),
                  [](const auto& x, const auto& y) { return x.path < y.path; });
        nlohmann::json changedArr = nlohmann::json::array();
        for (const auto& c : changedRows) {
            changedArr.push_back({{"path", c.path},
                                   {"aSize", c.aSize},
                                   {"bSize", c.bSize}});
        }
        j["changedFiles"] = changedArr;
        j["onlyAFiles"] = onlyAList;
        j["onlyBFiles"] = onlyBList;
        std::printf("%s\n", j.dump(2).c_str());
        return (onlyA + onlyB + sizeChanged) == 0 ? 0 : 1;
    }
    std::printf("Diff: %s vs %s\n", aPath.c_str(), bPath.c_str());
    std::printf("  identical : %d\n", identical);
    std::printf("  changed   : %d\n", sizeChanged);
    std::printf("  only in A : %d\n", onlyA);
    std::printf("  only in B : %d\n", onlyB);
    for (const auto& s : changedList) std::printf("  ~  %s\n", s.c_str());
    for (const auto& s : onlyAList)   std::printf("  -  %s\n", s.c_str());
    for (const auto& s : onlyBList)   std::printf("  +  %s\n", s.c_str());
    return (onlyA + onlyB + sizeChanged) == 0 ? 0 : 1;
}

int handleDiffZone(int& i, int argc, char** argv) {
    // Compare two unpacked zone directories: zone.json fields,
    // creature names, object paths, quest titles. Useful when a
    // designer wants to see what changed between an upstream
    // template (--copy-zone source) and their customized variant,
    // or to verify a refactor only touched what it claimed to.
    std::string aDir = argv[++i];
    std::string bDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    for (const auto& d : {aDir, bDir}) {
        if (!fs::exists(d + "/zone.json")) {
            std::fprintf(stderr,
                "diff-zone: %s has no zone.json — not a zone dir\n",
                d.c_str());
            return 1;
        }
    }
    wowee::editor::ZoneManifest aZ, bZ;
    aZ.load(aDir + "/zone.json");
    bZ.load(bDir + "/zone.json");
    // Helper: load a sub-file if present, returning empty container
    // when missing — both sides may legitimately omit a content
    // file (e.g. a quest-free zone) without that being a diff per se.
    auto loadCreatures = [](const std::string& dir) {
        std::vector<std::string> names;
        wowee::editor::NpcSpawner sp;
        if (sp.loadFromFile(dir + "/creatures.json")) {
            for (const auto& s : sp.getSpawns()) names.push_back(s.name);
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    auto loadObjectPaths = [](const std::string& dir) {
        std::vector<std::string> paths;
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile(dir + "/objects.json")) {
            for (const auto& o : op.getObjects()) paths.push_back(o.path);
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    };
    auto loadQuestTitles = [](const std::string& dir) {
        std::vector<std::string> titles;
        wowee::editor::QuestEditor qe;
        if (qe.loadFromFile(dir + "/quests.json")) {
            for (const auto& q : qe.getQuests()) titles.push_back(q.title);
        }
        std::sort(titles.begin(), titles.end());
        return titles;
    };
    auto aCreatures = loadCreatures(aDir);
    auto bCreatures = loadCreatures(bDir);
    auto aObjects = loadObjectPaths(aDir);
    auto bObjects = loadObjectPaths(bDir);
    auto aQuests = loadQuestTitles(aDir);
    auto bQuests = loadQuestTitles(bDir);
    // Set diff: returns (onlyA, onlyB) where each is a sorted list.
    auto setDiff = [](const std::vector<std::string>& a,
                      const std::vector<std::string>& b) {
        std::vector<std::string> onlyA, onlyB;
        std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                            std::back_inserter(onlyA));
        std::set_difference(b.begin(), b.end(), a.begin(), a.end(),
                            std::back_inserter(onlyB));
        return std::pair{onlyA, onlyB};
    };
    auto [creatOnlyA, creatOnlyB] = setDiff(aCreatures, bCreatures);
    auto [objOnlyA, objOnlyB] = setDiff(aObjects, bObjects);
    auto [questOnlyA, questOnlyB] = setDiff(aQuests, bQuests);
    // Manifest field diffs.
    std::vector<std::string> manifestDiffs;
    auto cmp = [&](const char* field, const std::string& a,
                   const std::string& b) {
        if (a != b) {
            manifestDiffs.push_back(std::string(field) + ": '" +
                                    a + "' -> '" + b + "'");
        }
    };
    cmp("mapName",      aZ.mapName,      bZ.mapName);
    cmp("displayName",  aZ.displayName,  bZ.displayName);
    cmp("biome",        aZ.biome,        bZ.biome);
    cmp("musicTrack",   aZ.musicTrack,   bZ.musicTrack);
    if (aZ.mapId != bZ.mapId) {
        manifestDiffs.push_back("mapId: " + std::to_string(aZ.mapId) +
                                " -> " + std::to_string(bZ.mapId));
    }
    if (aZ.tiles.size() != bZ.tiles.size()) {
        manifestDiffs.push_back("tile count: " + std::to_string(aZ.tiles.size()) +
                                " -> " + std::to_string(bZ.tiles.size()));
    }
    int diffs = manifestDiffs.size() +
                creatOnlyA.size() + creatOnlyB.size() +
                objOnlyA.size() + objOnlyB.size() +
                questOnlyA.size() + questOnlyB.size();
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aDir;
        j["b"] = bDir;
        j["identical"] = (diffs == 0);
        j["manifestDiffs"] = manifestDiffs;
        j["creatures"] = {{"a", aCreatures.size()},
                           {"b", bCreatures.size()},
                           {"onlyA", creatOnlyA},
                           {"onlyB", creatOnlyB}};
        j["objects"] = {{"a", aObjects.size()},
                         {"b", bObjects.size()},
                         {"onlyA", objOnlyA},
                         {"onlyB", objOnlyB}};
        j["quests"] = {{"a", aQuests.size()},
                        {"b", bQuests.size()},
                        {"onlyA", questOnlyA},
                        {"onlyB", questOnlyB}};
        j["totalDiffs"] = diffs;
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    std::printf("Diff: %s vs %s\n", aDir.c_str(), bDir.c_str());
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    std::printf("  manifest  : %zu field diff(s)\n", manifestDiffs.size());
    for (const auto& d : manifestDiffs) std::printf("    ~ %s\n", d.c_str());
    std::printf("  creatures : %zu vs %zu\n",
                aCreatures.size(), bCreatures.size());
    for (const auto& s : creatOnlyA) std::printf("    - %s\n", s.c_str());
    for (const auto& s : creatOnlyB) std::printf("    + %s\n", s.c_str());
    std::printf("  objects   : %zu vs %zu\n",
                aObjects.size(), bObjects.size());
    for (const auto& s : objOnlyA) std::printf("    - %s\n", s.c_str());
    for (const auto& s : objOnlyB) std::printf("    + %s\n", s.c_str());
    std::printf("  quests    : %zu vs %zu\n",
                aQuests.size(), bQuests.size());
    for (const auto& s : questOnlyA) std::printf("    - %s\n", s.c_str());
    for (const auto& s : questOnlyB) std::printf("    + %s\n", s.c_str());
    return 1;
}

int handleDiffGlb(int& i, int argc, char** argv) {
    // Structural compare of two .glb files. Useful for confirming
    // that an alternate export path produces equivalent output
    // (e.g. --bake-zone-glb vs concatenated --export-whm-glbs)
    // or that a re-export of the same source is byte-equivalent.
    // Compares structure (mesh/primitive/accessor counts +
    // chunk sizes), NOT byte-level — JSON key ordering can vary.
    std::string aPath = argv[++i];
    std::string bPath = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    // Reuse the parser from --info-glb. Inline here since it's
    // small and the alternative is a 3-way handler refactor.
    auto loadGlb = [](const std::string& path,
                      uint32_t& outJsonLen, uint32_t& outBinLen,
                      std::string& outJsonStr) -> bool {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
        if (bytes.size() < 20) return false;
        uint32_t magic, version, totalLen;
        std::memcpy(&magic,    &bytes[0], 4);
        std::memcpy(&version,  &bytes[4], 4);
        std::memcpy(&totalLen, &bytes[8], 4);
        if (magic != 0x46546C67 || version != 2) return false;
        std::memcpy(&outJsonLen, &bytes[12], 4);
        if (20 + outJsonLen > bytes.size()) return false;
        outJsonStr.assign(bytes.begin() + 20,
                           bytes.begin() + 20 + outJsonLen);
        size_t binOff = 20 + outJsonLen;
        if (binOff + 8 <= bytes.size()) {
            std::memcpy(&outBinLen, &bytes[binOff], 4);
        } else {
            outBinLen = 0;
        }
        return true;
    };
    uint32_t aJsonLen = 0, aBinLen = 0;
    uint32_t bJsonLen = 0, bBinLen = 0;
    std::string aJsonStr, bJsonStr;
    if (!loadGlb(aPath, aJsonLen, aBinLen, aJsonStr)) {
        std::fprintf(stderr, "diff-glb: failed to read %s\n", aPath.c_str());
        return 1;
    }
    if (!loadGlb(bPath, bJsonLen, bBinLen, bJsonStr)) {
        std::fprintf(stderr, "diff-glb: failed to read %s\n", bPath.c_str());
        return 1;
    }
    // Pull structural counts from JSON. Skip if parse fails on
    // either side — diff is meaningless then.
    auto countOf = [](const nlohmann::json& j, const char* key) {
        if (j.contains(key) && j[key].is_array()) {
            return static_cast<int>(j[key].size());
        }
        return 0;
    };
    int aMesh = 0, aPrim = 0, aAcc = 0, aBV = 0, aBuf = 0;
    int bMesh = 0, bPrim = 0, bAcc = 0, bBV = 0, bBuf = 0;
    try {
        auto aj = nlohmann::json::parse(aJsonStr);
        auto bj = nlohmann::json::parse(bJsonStr);
        aMesh = countOf(aj, "meshes");
        bMesh = countOf(bj, "meshes");
        if (aj.contains("meshes") && aj["meshes"].is_array()) {
            for (const auto& m : aj["meshes"]) {
                if (m.contains("primitives") && m["primitives"].is_array()) {
                    aPrim += static_cast<int>(m["primitives"].size());
                }
            }
        }
        if (bj.contains("meshes") && bj["meshes"].is_array()) {
            for (const auto& m : bj["meshes"]) {
                if (m.contains("primitives") && m["primitives"].is_array()) {
                    bPrim += static_cast<int>(m["primitives"].size());
                }
            }
        }
        aAcc = countOf(aj, "accessors"); bAcc = countOf(bj, "accessors");
        aBV  = countOf(aj, "bufferViews"); bBV  = countOf(bj, "bufferViews");
        aBuf = countOf(aj, "buffers");   bBuf = countOf(bj, "buffers");
    } catch (const std::exception&) {
        std::fprintf(stderr, "diff-glb: JSON parse failed on one side\n");
        return 1;
    }
    int diffs = (aMesh != bMesh) + (aPrim != bPrim) + (aAcc != bAcc) +
                (aBV != bBV) + (aBuf != bBuf) +
                (aBinLen != bBinLen);
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aPath; j["b"] = bPath;
        j["meshes"]      = {{"a", aMesh},  {"b", bMesh}};
        j["primitives"]  = {{"a", aPrim},  {"b", bPrim}};
        j["accessors"]   = {{"a", aAcc},   {"b", bAcc}};
        j["bufferViews"] = {{"a", aBV},    {"b", bBV}};
        j["buffers"]     = {{"a", aBuf},   {"b", bBuf}};
        j["binBytes"]    = {{"a", aBinLen},{"b", bBinLen}};
        j["jsonBytes"]   = {{"a", aJsonLen},{"b", bJsonLen}};
        j["totalDiffs"]  = diffs;
        j["identical"]   = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    auto cmp = [](const char* name, int a, int b) {
        std::printf("  %-12s: %6d %6d  %s\n", name, a, b,
                    a == b ? "" : "DIFF");
    };
    std::printf("Diff: %s vs %s\n", aPath.c_str(), bPath.c_str());
    std::printf("                       a      b\n");
    cmp("meshes",      aMesh, bMesh);
    cmp("primitives",  aPrim, bPrim);
    cmp("accessors",   aAcc,  bAcc);
    cmp("bufferViews", aBV,   bBV);
    cmp("buffers",     aBuf,  bBuf);
    cmp("BIN bytes",   static_cast<int>(aBinLen),
                        static_cast<int>(bBinLen));
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    return 1;
}

int handleDiffWom(int& i, int argc, char** argv) {
    // Structural compare of two WOM models. Useful for verifying
    // that a --migrate-wom or round-trip through OBJ/glTF/STL
    // preserved the right counts. Compares sizes only — point-
    // wise vertex compare would be O(n²) and brittle to minor
    // float diffs from format conversions.
    std::string aBase = argv[++i];
    std::string bBase = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    for (auto* base : {&aBase, &bBase}) {
        if (base->size() >= 4 &&
            base->substr(base->size() - 4) == ".wom") {
            *base = base->substr(0, base->size() - 4);
        }
    }
    for (const auto& base : {aBase, bBase}) {
        if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
            return reportMissing("diff-wom", "WOM", base, ".wom");
        }
    }
    auto a = wowee::pipeline::WoweeModelLoader::load(aBase);
    auto b = wowee::pipeline::WoweeModelLoader::load(bBase);
    // Each row is (label, a-value, b-value) so the table renders
    // straight.
    struct Row {
        const char* label;
        long long av, bv;
    };
    std::vector<Row> rows = {
        {"version",       a.version,                 b.version},
        {"vertices",  (long long)a.vertices.size(),  (long long)b.vertices.size()},
        {"indices",   (long long)a.indices.size(),   (long long)b.indices.size()},
        {"triangles", (long long)(a.indices.size()/3),(long long)(b.indices.size()/3)},
        {"textures",  (long long)a.texturePaths.size(),(long long)b.texturePaths.size()},
        {"bones",     (long long)a.bones.size(),     (long long)b.bones.size()},
        {"animations",(long long)a.animations.size(),(long long)b.animations.size()},
        {"batches",   (long long)a.batches.size(),   (long long)b.batches.size()},
    };
    // Bounds compare with float epsilon since round-trips through
    // text formats can perturb the last bit. 0.01-unit slop is
    // generous (positions are typically in yards, ~1m).
    auto closeBounds = [](const glm::vec3& x, const glm::vec3& y) {
        return std::abs(x.x - y.x) < 0.01f &&
               std::abs(x.y - y.y) < 0.01f &&
               std::abs(x.z - y.z) < 0.01f;
    };
    bool boundsMatch = closeBounds(a.boundMin, b.boundMin) &&
                        closeBounds(a.boundMax, b.boundMax) &&
                        std::abs(a.boundRadius - b.boundRadius) < 0.01f;
    int diffs = 0;
    for (const auto& r : rows) if (r.av != r.bv) diffs++;
    if (!boundsMatch) diffs++;
    bool nameMatch = (a.name == b.name);
    if (!nameMatch) diffs++;
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aBase + ".wom";
        j["b"] = bBase + ".wom";
        for (const auto& r : rows) {
            j[r.label] = {{"a", r.av}, {"b", r.bv}};
        }
        j["name"] = {{"a", a.name}, {"b", b.name}};
        j["boundsMatch"] = boundsMatch;
        j["totalDiffs"] = diffs;
        j["identical"] = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    std::printf("Diff: %s.wom vs %s.wom\n", aBase.c_str(), bBase.c_str());
    std::printf("                       a              b\n");
    for (const auto& r : rows) {
        std::printf("  %-12s: %12lld %12lld  %s\n",
                    r.label, r.av, r.bv,
                    r.av == r.bv ? "" : "DIFF");
    }
    std::printf("  %-12s: %-13s %-13s  %s\n",
                "name",
                a.name.substr(0, 13).c_str(),
                b.name.substr(0, 13).c_str(),
                nameMatch ? "" : "DIFF");
    std::printf("  %-12s: %s\n", "bounds",
                boundsMatch ? "match" : "DIFF");
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    return 1;
}

int handleDiffWob(int& i, int argc, char** argv) {
    // Companion to --diff-wom for buildings. Same shape: count-
    // based compare so round-trips through OBJ/glTF can be
    // validated without false positives from float perturbation.
    std::string aBase = argv[++i];
    std::string bBase = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    for (auto* base : {&aBase, &bBase}) {
        if (base->size() >= 4 &&
            base->substr(base->size() - 4) == ".wob") {
            *base = base->substr(0, base->size() - 4);
        }
    }
    for (const auto& base : {aBase, bBase}) {
        if (!wowee::pipeline::WoweeBuildingLoader::exists(base)) {
            return reportMissing("diff-wob", "WOB", base, ".wob");
        }
    }
    auto a = wowee::pipeline::WoweeBuildingLoader::load(aBase);
    auto b = wowee::pipeline::WoweeBuildingLoader::load(bBase);
    // Aggregate vertex+index counts across all groups for the
    // headline 'totalVerts/totalTris' metric (matches what
    // --info-wob reports).
    auto sumGroupVerts = [](const auto& bld) {
        size_t s = 0;
        for (const auto& g : bld.groups) s += g.vertices.size();
        return s;
    };
    auto sumGroupIdx = [](const auto& bld) {
        size_t s = 0;
        for (const auto& g : bld.groups) s += g.indices.size();
        return s;
    };
    struct Row {
        const char* label;
        long long av, bv;
    };
    // WoweeBuilding doesn't have a top-level textures vector or
    // doodadSets — materials and textures are per-group, doodad
    // sets are flattened. Aggregate the per-group counts.
    long long aMats = 0, bMats = 0;
    long long aGroupTex = 0, bGroupTex = 0;
    for (const auto& g : a.groups) {
        aMats += static_cast<long long>(g.materials.size());
        aGroupTex += static_cast<long long>(g.texturePaths.size());
    }
    for (const auto& g : b.groups) {
        bMats += static_cast<long long>(g.materials.size());
        bGroupTex += static_cast<long long>(g.texturePaths.size());
    }
    std::vector<Row> rows = {
        {"groups",      (long long)a.groups.size(),   (long long)b.groups.size()},
        {"portals",     (long long)a.portals.size(),  (long long)b.portals.size()},
        {"doodads",     (long long)a.doodads.size(),  (long long)b.doodads.size()},
        {"materials",   aMats, bMats},
        {"groupTex",    aGroupTex, bGroupTex},
        {"totalVerts",  (long long)sumGroupVerts(a),  (long long)sumGroupVerts(b)},
        {"totalIdx",    (long long)sumGroupIdx(a),    (long long)sumGroupIdx(b)},
    };
    int diffs = 0;
    for (const auto& r : rows) if (r.av != r.bv) diffs++;
    bool nameMatch = (a.name == b.name);
    if (!nameMatch) diffs++;
    bool radMatch = (std::abs(a.boundRadius - b.boundRadius) < 0.01f);
    if (!radMatch) diffs++;
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aBase + ".wob";
        j["b"] = bBase + ".wob";
        for (const auto& r : rows) {
            j[r.label] = {{"a", r.av}, {"b", r.bv}};
        }
        j["name"] = {{"a", a.name}, {"b", b.name}};
        j["boundRadiusMatch"] = radMatch;
        j["totalDiffs"] = diffs;
        j["identical"] = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    std::printf("Diff: %s.wob vs %s.wob\n", aBase.c_str(), bBase.c_str());
    std::printf("                       a              b\n");
    for (const auto& r : rows) {
        std::printf("  %-12s: %12lld %12lld  %s\n",
                    r.label, r.av, r.bv,
                    r.av == r.bv ? "" : "DIFF");
    }
    std::printf("  %-12s: %-13s %-13s  %s\n",
                "name", a.name.substr(0, 13).c_str(),
                b.name.substr(0, 13).c_str(),
                nameMatch ? "" : "DIFF");
    std::printf("  %-12s: %s\n", "boundRadius",
                radMatch ? "match" : "DIFF");
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    return 1;
}

int handleDiffWhm(int& i, int argc, char** argv) {
    // Terrain diff. Catches the common "did my edit actually
    // change anything?" question for heightmap tweaks. Compares
    // chunk presence + height range + placement counts; not
    // pointwise height compare since float perturbation from
    // round-trips would false-flag.
    std::string aBase = argv[++i];
    std::string bBase = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    for (auto* base : {&aBase, &bBase}) {
        for (const char* ext : {".wot", ".whm"}) {
            if (base->size() >= 4 && base->substr(base->size() - 4) == ext) {
                *base = base->substr(0, base->size() - 4);
                break;
            }
        }
    }
    for (const auto& base : {aBase, bBase}) {
        if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) {
            std::fprintf(stderr,
                "diff-whm: WHM/WOT not found: %s.{whm,wot}\n", base.c_str());
            return 1;
        }
    }
    wowee::pipeline::ADTTerrain a, b;
    wowee::pipeline::WoweeTerrainLoader::load(aBase, a);
    wowee::pipeline::WoweeTerrainLoader::load(bBase, b);
    // Per-side height range walk — same as --info-whm.
    auto stats = [](const wowee::pipeline::ADTTerrain& t) {
        struct S { int loaded; float minH, maxH; } s{0, 1e30f, -1e30f};
        for (const auto& c : t.chunks) {
            if (!c.heightMap.isLoaded()) continue;
            s.loaded++;
            for (float h : c.heightMap.heights) {
                if (std::isfinite(h)) {
                    s.minH = std::min(s.minH, h);
                    s.maxH = std::max(s.maxH, h);
                }
            }
        }
        if (s.loaded == 0) { s.minH = 0; s.maxH = 0; }
        return s;
    };
    auto sa = stats(a);
    auto sb = stats(b);
    struct Row { const char* label; long long av, bv; };
    std::vector<Row> rows = {
        {"loadedChunks",  sa.loaded,                          sb.loaded},
        {"doodadPlace",   (long long)a.doodadPlacements.size(),(long long)b.doodadPlacements.size()},
        {"wmoPlace",      (long long)a.wmoPlacements.size(),  (long long)b.wmoPlacements.size()},
        {"textures",      (long long)a.textures.size(),       (long long)b.textures.size()},
        {"doodadNames",   (long long)a.doodadNames.size(),    (long long)b.doodadNames.size()},
        {"wmoNames",      (long long)a.wmoNames.size(),       (long long)b.wmoNames.size()},
    };
    int diffs = 0;
    for (const auto& r : rows) if (r.av != r.bv) diffs++;
    // Tile coords + height range comparison (epsilon for floats).
    bool tileMatch = (a.coord.x == b.coord.x && a.coord.y == b.coord.y);
    if (!tileMatch) diffs++;
    bool heightMatch = (std::abs(sa.minH - sb.minH) < 0.01f &&
                         std::abs(sa.maxH - sb.maxH) < 0.01f);
    if (!heightMatch) diffs++;
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aBase; j["b"] = bBase;
        for (const auto& r : rows) {
            j[r.label] = {{"a", r.av}, {"b", r.bv}};
        }
        j["tile"] = {{"a", {a.coord.x, a.coord.y}},
                      {"b", {b.coord.x, b.coord.y}}};
        j["heightRange"] = {{"a", {sa.minH, sa.maxH}},
                             {"b", {sb.minH, sb.maxH}}};
        j["totalDiffs"] = diffs;
        j["identical"] = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    std::printf("Diff: %s vs %s\n", aBase.c_str(), bBase.c_str());
    std::printf("                       a              b\n");
    std::printf("  %-13s: (%4d,%4d)    (%4d,%4d)  %s\n",
                "tile", a.coord.x, a.coord.y, b.coord.x, b.coord.y,
                tileMatch ? "" : "DIFF");
    for (const auto& r : rows) {
        std::printf("  %-13s: %12lld %12lld  %s\n",
                    r.label, r.av, r.bv,
                    r.av == r.bv ? "" : "DIFF");
    }
    std::printf("  %-13s: [%.2f,%.2f]  [%.2f,%.2f]  %s\n",
                "heightRange", sa.minH, sa.maxH, sb.minH, sb.maxH,
                heightMatch ? "" : "DIFF");
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    return 1;
}

int handleDiffWoc(int& i, int argc, char** argv) {
    // Collision-mesh diff. Confirms a --regen-collision pass
    // actually changed something (or didn't, when the heightmap
    // tweak was below the slope threshold).
    std::string aPath = argv[++i];
    std::string bPath = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    for (const auto& p : {aPath, bPath}) {
        if (!std::filesystem::exists(p)) {
            std::fprintf(stderr, "diff-woc: WOC not found: %s\n", p.c_str());
            return 1;
        }
    }
    auto a = wowee::pipeline::WoweeCollisionBuilder::load(aPath);
    auto b = wowee::pipeline::WoweeCollisionBuilder::load(bPath);
    struct Row { const char* label; long long av, bv; };
    std::vector<Row> rows = {
        {"triangles", (long long)a.triangles.size(), (long long)b.triangles.size()},
        {"walkable",  (long long)a.walkableCount(),   (long long)b.walkableCount()},
        {"steep",     (long long)a.steepCount(),      (long long)b.steepCount()},
    };
    int diffs = 0;
    for (const auto& r : rows) if (r.av != r.bv) diffs++;
    bool tileMatch = (a.tileX == b.tileX && a.tileY == b.tileY);
    if (!tileMatch) diffs++;
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aPath; j["b"] = bPath;
        for (const auto& r : rows) {
            j[r.label] = {{"a", r.av}, {"b", r.bv}};
        }
        j["tile"] = {{"a", {a.tileX, a.tileY}},
                      {"b", {b.tileX, b.tileY}}};
        j["totalDiffs"] = diffs;
        j["identical"] = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    std::printf("Diff: %s vs %s\n", aPath.c_str(), bPath.c_str());
    std::printf("                       a              b\n");
    std::printf("  %-12s: (%4u,%4u)    (%4u,%4u)  %s\n",
                "tile", a.tileX, a.tileY, b.tileX, b.tileY,
                tileMatch ? "" : "DIFF");
    for (const auto& r : rows) {
        std::printf("  %-12s: %12lld %12lld  %s\n",
                    r.label, r.av, r.bv,
                    r.av == r.bv ? "" : "DIFF");
    }
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    return 1;
}

int handleDiffJsondbc(int& i, int argc, char** argv) {
    // JSON DBC structural diff. Catches schema regressions when
    // a sidecar is regenerated by a different tool version (or
    // a different DBC layout produces a different field count).
    std::string aPath = argv[++i];
    std::string bPath = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    auto loadDoc = [](const std::string& p, nlohmann::json& doc) {
        std::ifstream in(p);
        if (!in) return false;
        try { in >> doc; } catch (...) { return false; }
        return true;
    };
    nlohmann::json a, b;
    if (!loadDoc(aPath, a)) {
        std::fprintf(stderr, "diff-jsondbc: failed to read %s\n", aPath.c_str());
        return 1;
    }
    if (!loadDoc(bPath, b)) {
        std::fprintf(stderr, "diff-jsondbc: failed to read %s\n", bPath.c_str());
        return 1;
    }
    // Pull comparable fields with safe defaults.
    std::string aFmt = a.value("format", std::string{});
    std::string bFmt = b.value("format", std::string{});
    std::string aSrc = a.value("source", std::string{});
    std::string bSrc = b.value("source", std::string{});
    uint32_t aRC = a.value("recordCount", 0u);
    uint32_t bRC = b.value("recordCount", 0u);
    uint32_t aFC = a.value("fieldCount", 0u);
    uint32_t bFC = b.value("fieldCount", 0u);
    uint32_t aActual = (a.contains("records") && a["records"].is_array())
        ? static_cast<uint32_t>(a["records"].size()) : 0u;
    uint32_t bActual = (b.contains("records") && b["records"].is_array())
        ? static_cast<uint32_t>(b["records"].size()) : 0u;
    int diffs = 0;
    if (aFmt != bFmt) diffs++;
    if (aSrc != bSrc) diffs++;
    if (aRC != bRC) diffs++;
    if (aFC != bFC) diffs++;
    if (aActual != bActual) diffs++;
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aPath; j["b"] = bPath;
        j["format"]      = {{"a", aFmt},     {"b", bFmt}};
        j["source"]      = {{"a", aSrc},     {"b", bSrc}};
        j["recordCount"] = {{"a", aRC},      {"b", bRC}};
        j["fieldCount"]  = {{"a", aFC},      {"b", bFC}};
        j["actualRecs"]  = {{"a", aActual},  {"b", bActual}};
        j["totalDiffs"] = diffs;
        j["identical"]  = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    auto cmpStr = [](const char* lbl, const std::string& a, const std::string& b) {
        std::printf("  %-12s: %-20s %-20s  %s\n", lbl,
                    a.empty() ? "(unset)" : a.c_str(),
                    b.empty() ? "(unset)" : b.c_str(),
                    a == b ? "" : "DIFF");
    };
    auto cmpNum = [](const char* lbl, uint32_t a, uint32_t b) {
        std::printf("  %-12s: %20u %20u  %s\n", lbl, a, b,
                    a == b ? "" : "DIFF");
    };
    std::printf("Diff: %s vs %s\n", aPath.c_str(), bPath.c_str());
    cmpStr("format",      aFmt, bFmt);
    cmpStr("source",      aSrc, bSrc);
    cmpNum("recordCount", aRC, bRC);
    cmpNum("fieldCount",  aFC, bFC);
    cmpNum("actualRecs",  aActual, bActual);
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    return 1;
}

int handleDiffExtract(int& i, int argc, char** argv) {
    // Compare two extracted asset directories. Useful for diffing
    // a fresh asset_extract run against a previous baseline (did
    // the new MPQ add files? did any get dropped?), or comparing
    // what each WoW expansion contributes.
    std::string aDir = argv[++i];
    std::string bDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    for (const auto& d : {aDir, bDir}) {
        if (!fs::exists(d) || !fs::is_directory(d)) {
            std::fprintf(stderr,
                "diff-extract: %s is not a directory\n", d.c_str());
            return 1;
        }
    }
    // Tally per-extension counts + bytes for each side.
    struct Stats { int count = 0; uint64_t bytes = 0; };
    auto walk = [](const std::string& dir) {
        std::map<std::string, Stats> m;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext.empty()) ext = "(no-ext)";
            auto& s = m[ext];
            s.count++;
            s.bytes += e.file_size(ec);
        }
        return m;
    };
    auto a = walk(aDir);
    auto b = walk(bDir);
    // Union of all extensions.
    std::set<std::string> allExts;
    for (const auto& [e, _] : a) allExts.insert(e);
    for (const auto& [e, _] : b) allExts.insert(e);
    int diffs = 0;
    for (const auto& e : allExts) {
        int aC = a.count(e) ? a[e].count : 0;
        int bC = b.count(e) ? b[e].count : 0;
        if (aC != bC) diffs++;
    }
    int aTotalFiles = 0, bTotalFiles = 0;
    uint64_t aTotalBytes = 0, bTotalBytes = 0;
    for (const auto& [_, s] : a) { aTotalFiles += s.count; aTotalBytes += s.bytes; }
    for (const auto& [_, s] : b) { bTotalFiles += s.count; bTotalBytes += s.bytes; }
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aDir; j["b"] = bDir;
        j["totalFiles"] = {{"a", aTotalFiles}, {"b", bTotalFiles}};
        j["totalBytes"] = {{"a", aTotalBytes}, {"b", bTotalBytes}};
        nlohmann::json byExt = nlohmann::json::array();
        for (const auto& e : allExts) {
            int aC = a.count(e) ? a[e].count : 0;
            int bC = b.count(e) ? b[e].count : 0;
            uint64_t aB = a.count(e) ? a[e].bytes : 0;
            uint64_t bB = b.count(e) ? b[e].bytes : 0;
            byExt.push_back({{"ext", e},
                              {"a", {{"count", aC}, {"bytes", aB}}},
                              {"b", {{"count", bC}, {"bytes", bB}}}});
        }
        j["byExtension"] = byExt;
        j["totalDiffs"] = diffs;
        j["identical"] = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    std::printf("Diff: %s vs %s\n", aDir.c_str(), bDir.c_str());
    std::printf("  totals: %d files / %.1f MB    vs    %d files / %.1f MB\n",
                aTotalFiles, aTotalBytes / (1024.0 * 1024.0),
                bTotalFiles, bTotalBytes / (1024.0 * 1024.0));
    std::printf("\n  Per-extension (count then bytes):\n");
    std::printf("  %-12s   a count   b count    a bytes      b bytes  status\n", "ext");
    for (const auto& e : allExts) {
        int aC = a.count(e) ? a[e].count : 0;
        int bC = b.count(e) ? b[e].count : 0;
        uint64_t aB = a.count(e) ? a[e].bytes : 0;
        uint64_t bB = b.count(e) ? b[e].bytes : 0;
        const char* status = (aC == bC) ? ""
                          : (aC == 0)   ? "+B"
                          : (bC == 0)   ? "-A"
                          : "DIFF";
        std::printf("  %-12s %9d %9d  %10llu %12llu  %s\n",
                    e.c_str(), aC, bC,
                    static_cast<unsigned long long>(aB),
                    static_cast<unsigned long long>(bB),
                    status);
    }
    if (diffs == 0) {
        std::printf("\n  IDENTICAL (per-extension counts match)\n");
        return 0;
    }
    std::printf("\n  %d extension(s) differ\n", diffs);
    return 1;
}

int handleDiffChecksum(int& i, int argc, char** argv) {
    // Compare two SHA256SUMS files (from --export-zone-checksum).
    // Reports which files are added / removed / changed between
    // two zone snapshots — much faster than walking the
    // filesystem to recompute hashes of unchanged content.
    std::string aPath = argv[++i];
    std::string bPath = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    // Parse standard sha256sum format: "<64-hex>  <path>"
    auto load = [](const std::string& p,
                    std::map<std::string, std::string>& out) {
        std::ifstream in(p);
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 66) continue;
            std::string hash = line.substr(0, 64);
            // Two spaces, then path.
            size_t sep = line.find("  ", 64);
            if (sep == std::string::npos) continue;
            std::string path = line.substr(sep + 2);
            out[path] = hash;
        }
        return true;
    };
    std::map<std::string, std::string> a, b;
    if (!load(aPath, a)) {
        std::fprintf(stderr,
            "diff-checksum: failed to read %s\n", aPath.c_str());
        return 1;
    }
    if (!load(bPath, b)) {
        std::fprintf(stderr,
            "diff-checksum: failed to read %s\n", bPath.c_str());
        return 1;
    }
    std::vector<std::string> added, removed, changed;
    for (const auto& [path, hash] : a) {
        auto it = b.find(path);
        if (it == b.end()) removed.push_back(path);
        else if (it->second != hash) changed.push_back(path);
    }
    for (const auto& [path, hash] : b) {
        if (a.count(path) == 0) added.push_back(path);
    }
    int diffs = added.size() + removed.size() + changed.size();
    if (jsonOut) {
        nlohmann::json j;
        j["a"] = aPath; j["b"] = bPath;
        j["added"] = added;
        j["removed"] = removed;
        j["changed"] = changed;
        j["totalDiffs"] = diffs;
        j["identical"] = (diffs == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return diffs == 0 ? 0 : 1;
    }
    std::printf("Diff: %s vs %s\n", aPath.c_str(), bPath.c_str());
    std::printf("  added   : %zu\n", added.size());
    std::printf("  removed : %zu\n", removed.size());
    std::printf("  changed : %zu\n", changed.size());
    for (const auto& p : added)   std::printf("  +  %s\n", p.c_str());
    for (const auto& p : removed) std::printf("  -  %s\n", p.c_str());
    for (const auto& p : changed) std::printf("  ~  %s\n", p.c_str());
    if (diffs == 0) {
        std::printf("  IDENTICAL\n");
        return 0;
    }
    return 1;
}


}  // namespace

bool handleDiff(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--diff-wcp") == 0 && i + 2 < argc) {
        outRc = handleDiffWcp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-zone") == 0 && i + 2 < argc) {
        outRc = handleDiffZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-glb") == 0 && i + 2 < argc) {
        outRc = handleDiffGlb(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-wom") == 0 && i + 2 < argc) {
        outRc = handleDiffWom(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-wob") == 0 && i + 2 < argc) {
        outRc = handleDiffWob(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-whm") == 0 && i + 2 < argc) {
        outRc = handleDiffWhm(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-woc") == 0 && i + 2 < argc) {
        outRc = handleDiffWoc(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-jsondbc") == 0 && i + 2 < argc) {
        outRc = handleDiffJsondbc(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-extract") == 0 && i + 2 < argc) {
        outRc = handleDiffExtract(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-checksum") == 0 && i + 2 < argc) {
        outRc = handleDiffChecksum(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
