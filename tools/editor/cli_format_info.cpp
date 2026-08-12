#include "cli_format_info.hpp"

#include "pipeline/blp_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoPng(int& i, int argc, char** argv) {
    // Inspect a PNG sidecar - width, height, channels, bit depth.
    // Reads only the IHDR chunk (16 bytes after the 8-byte
    // signature) so it works on huge files instantly without
    // decoding pixels. Useful for verifying that the BLP→PNG
    // emitter produced the expected dimensions.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "info-png: cannot open %s\n", path.c_str());
        return 1;
    }
    uint8_t buf[24];
    in.read(reinterpret_cast<char*>(buf), 24);
    if (!in || in.gcount() < 24) {
        std::fprintf(stderr, "info-png: %s too short to be a PNG\n", path.c_str());
        return 1;
    }
    // Validate the 8-byte PNG signature: 89 50 4E 47 0D 0A 1A 0A
    static const uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47,
                                     0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(buf, kSig, 8) != 0) {
        std::fprintf(stderr, "info-png: %s missing PNG signature\n", path.c_str());
        return 1;
    }
    // IHDR chunk follows: 4-byte length, 4-byte type ('IHDR'),
    // then 13-byte payload (width:4, height:4, bitDepth:1,
    // colorType:1, compression:1, filter:1, interlace:1).
    // All multi-byte ints in PNG are big-endian.
    auto be32 = [](const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
    };
    uint32_t width  = be32(buf + 16);
    uint32_t height = be32(buf + 20);
    // Need bit depth + color type - read the next 5 bytes.
    uint8_t extra[5];
    in.read(reinterpret_cast<char*>(extra), 5);
    uint8_t bitDepth  = extra[0];
    uint8_t colorType = extra[1];
    // Channel count derives from color type (PNG spec table 11.1).
    int channels = 0;
    const char* colorName = "?";
    switch (colorType) {
        case 0: channels = 1; colorName = "grayscale"; break;
        case 2: channels = 3; colorName = "rgb"; break;
        case 3: channels = 1; colorName = "palette"; break;
        case 4: channels = 2; colorName = "grayscale+alpha"; break;
        case 6: channels = 4; colorName = "rgba"; break;
    }
    // File size for a quick sanity check - a 1024x1024 RGBA PNG
    // shouldn't be 12 bytes, that would mean truncation.
    std::error_code ec;
    uint64_t fsz = std::filesystem::file_size(path, ec);
    if (jsonOut) {
        nlohmann::json j;
        j["png"] = path;
        j["width"] = width;
        j["height"] = height;
        j["bitDepth"] = bitDepth;
        j["channels"] = channels;
        j["colorType"] = colorType;
        j["colorTypeName"] = colorName;
        j["fileSize"] = fsz;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("PNG: %s\n", path.c_str());
    std::printf("  size      : %u x %u\n", width, height);
    std::printf("  bit depth : %u\n", bitDepth);
    std::printf("  color     : %s (%d channel%s)\n",
                colorName, channels, channels == 1 ? "" : "s");
    std::printf("  file bytes: %llu\n", static_cast<unsigned long long>(fsz));
    return 0;
}

int handleInfoBlp(int& i, int argc, char** argv) {
    // Inspect a BLP texture: format/compression/mips/dimensions.
    // Loads the full image (which decompresses pixels) since we
    // also report channel count and decoded byte size - useful
    // for verifying the source before --convert-blp-png.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "info-blp: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    // Quick magic check before full decode - saves a confusing
    // 'invalid' from the loader when the user feeds a non-BLP.
    if (bytes.size() < 4 ||
        !(bytes[0] == 'B' && bytes[1] == 'L' && bytes[2] == 'P' &&
          (bytes[3] == '1' || bytes[3] == '2'))) {
        std::fprintf(stderr, "info-blp: %s is not a BLP1/BLP2 file\n",
                     path.c_str());
        return 1;
    }
    std::string magicVer = std::string(bytes.begin(), bytes.begin() + 4);
    auto img = wowee::pipeline::BLPLoader::load(bytes);
    if (!img.isValid()) {
        std::fprintf(stderr, "info-blp: failed to decode %s\n", path.c_str());
        return 1;
    }
    std::error_code ec;
    uint64_t fsz = std::filesystem::file_size(path, ec);
    const char* fmtName = wowee::pipeline::BLPLoader::getFormatName(img.format);
    const char* compName = wowee::pipeline::BLPLoader::getCompressionName(img.compression);
    if (jsonOut) {
        nlohmann::json j;
        j["blp"] = path;
        j["magic"] = magicVer;
        j["width"] = img.width;
        j["height"] = img.height;
        j["channels"] = img.channels;
        j["mipLevels"] = img.mipLevels;
        j["format"] = fmtName;
        j["compression"] = compName;
        j["decodedBytes"] = img.data.size();
        j["fileSize"] = fsz;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("BLP: %s (%s)\n", path.c_str(), magicVer.c_str());
    std::printf("  size       : %d x %d\n", img.width, img.height);
    std::printf("  channels   : %d\n", img.channels);
    std::printf("  format     : %s\n", fmtName);
    std::printf("  compression: %s\n", compName);
    std::printf("  mip levels : %d\n", img.mipLevels);
    std::printf("  file bytes : %llu\n", static_cast<unsigned long long>(fsz));
    std::printf("  decoded RGBA bytes: %zu\n", img.data.size());
    return 0;
}

int handleInfoM2(int& i, int argc, char** argv) {
    // Inspect a proprietary M2 model. Pairs with --info to inspect
    // the WOM equivalent, so users can see what was preserved/lost
    // by the M2 -> WOM conversion (e.g. M2 has particles + ribbons,
    // WOM doesn't yet).
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "info-m2: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    // Auto-merge matching <base>00.skin if present (WotLK+ models
    // store geometry there) so vertex/index counts match what
    // gets rendered.
    std::vector<uint8_t> skinBytes;
    {
        std::string skinPath = pipeline::skinPathForM2(path);
        std::ifstream sf(skinPath, std::ios::binary);
        if (sf) {
            skinBytes.assign((std::istreambuf_iterator<char>(sf)),
                              std::istreambuf_iterator<char>());
        }
    }
    auto m2 = wowee::pipeline::M2Loader::load(bytes);
    if (!skinBytes.empty()) {
        wowee::pipeline::M2Loader::loadSkin(skinBytes, m2);
    }
    if (!m2.isValid()) {
        std::fprintf(stderr, "info-m2: failed to parse %s\n", path.c_str());
        return 1;
    }
    std::error_code ec;
    uint64_t fsz = std::filesystem::file_size(path, ec);
    if (jsonOut) {
        nlohmann::json j;
        j["m2"] = path;
        j["name"] = m2.name;
        j["version"] = m2.version;
        j["fileSize"] = fsz;
        j["skinFound"] = !skinBytes.empty();
        j["vertices"] = m2.vertices.size();
        j["indices"] = m2.indices.size();
        j["triangles"] = m2.indices.size() / 3;
        j["bones"] = m2.bones.size();
        j["sequences"] = m2.sequences.size();
        j["batches"] = m2.batches.size();
        j["textures"] = m2.textures.size();
        j["materials"] = m2.materials.size();
        j["attachments"] = m2.attachments.size();
        j["particles"] = m2.particleEmitters.size();
        j["ribbons"] = m2.ribbonEmitters.size();
        j["collisionTris"] = m2.collisionIndices.size() / 3;
        j["boundRadius"] = m2.boundRadius;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("M2: %s\n", path.c_str());
    std::printf("  name        : %s\n", m2.name.c_str());
    std::printf("  version     : %u\n", m2.version);
    std::printf("  file bytes  : %llu\n", static_cast<unsigned long long>(fsz));
    std::printf("  skin file   : %s\n", skinBytes.empty() ? "not found" : "loaded");
    std::printf("  vertices    : %zu\n", m2.vertices.size());
    std::printf("  triangles   : %zu (%zu indices)\n",
                m2.indices.size() / 3, m2.indices.size());
    std::printf("  bones       : %zu\n", m2.bones.size());
    std::printf("  sequences   : %zu (animations)\n", m2.sequences.size());
    std::printf("  batches     : %zu\n", m2.batches.size());
    std::printf("  textures    : %zu\n", m2.textures.size());
    std::printf("  materials   : %zu\n", m2.materials.size());
    std::printf("  attachments : %zu\n", m2.attachments.size());
    std::printf("  particles   : %zu\n", m2.particleEmitters.size());
    std::printf("  ribbons     : %zu\n", m2.ribbonEmitters.size());
    std::printf("  collision   : %zu tris\n", m2.collisionIndices.size() / 3);
    std::printf("  boundRadius : %.2f\n", m2.boundRadius);
    return 0;
}

int handleInfoWmo(int& i, int argc, char** argv) {
    // Inspect a proprietary WMO building. Like --info-m2 this
    // pairs with --info-wob (the open WOB equivalent inspector)
    // so users can verify the conversion preserves group counts,
    // portal counts, and doodad references.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "info-wmo: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    auto wmo = wowee::pipeline::WMOLoader::load(bytes);
    // Try to locate group files (Foo_NNN.wmo) sitting next to the
    // root file and merge their geometry. Without this the
    // group/vertex counts would all be 0 since the root file only
    // has metadata.
    namespace fs = std::filesystem;
    std::string base = path;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wmo")
        base = base.substr(0, base.size() - 4);
    // Pre-allocate the groups array - loadGroup writes into
    // model.groups[gi] and bails if the slot doesn't exist.
    if (wmo.groups.size() < wmo.nGroups) wmo.groups.resize(wmo.nGroups);
    int groupsLoaded = 0;
    for (uint32_t gi = 0; gi < wmo.nGroups; ++gi) {
        // "_000.wmo" is 8 chars + NUL = 9 bytes; previous 8-byte
        // buffer was truncating to "_000.wm" and silently failing
        // every lookup.
        char buf[16];
        std::snprintf(buf, sizeof(buf), "_%03u.wmo", gi);
        std::string gp = base + buf;
        std::ifstream gf(gp, std::ios::binary);
        if (!gf) continue;
        std::vector<uint8_t> gd((std::istreambuf_iterator<char>(gf)),
                                 std::istreambuf_iterator<char>());
        if (wowee::pipeline::WMOLoader::loadGroup(gd, wmo, gi)) groupsLoaded++;
    }
    if (!wmo.isValid()) {
        std::fprintf(stderr, "info-wmo: failed to parse %s\n", path.c_str());
        return 1;
    }
    // Total vertex/index counts across loaded groups - this is the
    // useful number for sizing comparisons against WOB.
    size_t totalV = 0, totalI = 0;
    for (const auto& g : wmo.groups) {
        totalV += g.vertices.size();
        totalI += g.indices.size();
    }
    std::error_code ec;
    uint64_t fsz = fs::file_size(path, ec);
    if (jsonOut) {
        nlohmann::json j;
        j["wmo"] = path;
        j["version"] = wmo.version;
        j["fileSize"] = fsz;
        j["groups"] = wmo.nGroups;
        j["groupsLoaded"] = groupsLoaded;
        j["portals"] = wmo.nPortals;
        j["lights"] = wmo.nLights;
        j["doodadDefs"] = wmo.doodads.size();
        j["doodadSets"] = wmo.doodadSets.size();
        j["materials"] = wmo.materials.size();
        j["textures"] = wmo.textures.size();
        j["totalVerts"] = totalV;
        j["totalTris"] = totalI / 3;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMO: %s\n", path.c_str());
    std::printf("  version       : %u\n", wmo.version);
    std::printf("  file bytes    : %llu\n", static_cast<unsigned long long>(fsz));
    std::printf("  groups        : %u (%d loaded from group files)\n",
                wmo.nGroups, groupsLoaded);
    std::printf("  portals       : %u\n", wmo.nPortals);
    std::printf("  lights        : %u\n", wmo.nLights);
    std::printf("  doodad defs   : %zu (%zu sets)\n",
                wmo.doodads.size(), wmo.doodadSets.size());
    std::printf("  materials     : %zu\n", wmo.materials.size());
    std::printf("  textures      : %zu\n", wmo.textures.size());
    std::printf("  total verts   : %zu\n", totalV);
    std::printf("  total tris    : %zu\n", totalI / 3);
    return 0;
}

int handleInfoAdt(int& i, int argc, char** argv) {
    // Inspect a proprietary ADT terrain tile. Pairs with
    // --info-wot/--info-whm (open WOT/WHM equivalents) so users
    // can verify the conversion preserves chunk/doodad/wmo counts.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "info-adt: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    auto terrain = wowee::pipeline::ADTLoader::load(bytes);
    if (!terrain.isLoaded()) {
        std::fprintf(stderr, "info-adt: failed to parse %s\n", path.c_str());
        return 1;
    }
    // Walk chunks and tally height range + loaded count + water/holes.
    int loadedChunks = 0, holeChunks = 0, waterChunks = 0;
    float minH = 1e30f, maxH = -1e30f;
    for (size_t c = 0; c < 256; ++c) {
        const auto& chunk = terrain.chunks[c];
        if (!chunk.heightMap.isLoaded()) continue;
        loadedChunks++;
        if (chunk.holes != 0) holeChunks++;
        if (terrain.waterData[c].hasWater()) waterChunks++;
        for (float h : chunk.heightMap.heights) {
            if (std::isfinite(h)) {
                if (h < minH) minH = h;
                if (h > maxH) maxH = h;
            }
        }
    }
    std::error_code ec;
    uint64_t fsz = std::filesystem::file_size(path, ec);
    if (jsonOut) {
        nlohmann::json j;
        j["adt"] = path;
        j["version"] = terrain.version;
        j["fileSize"] = fsz;
        j["coord"] = {terrain.coord.x, terrain.coord.y};
        j["loadedChunks"] = loadedChunks;
        j["holeChunks"] = holeChunks;
        j["waterChunks"] = waterChunks;
        j["heightMin"] = (loadedChunks > 0) ? minH : 0.0f;
        j["heightMax"] = (loadedChunks > 0) ? maxH : 0.0f;
        j["textures"] = terrain.textures.size();
        j["doodadNames"] = terrain.doodadNames.size();
        j["wmoNames"] = terrain.wmoNames.size();
        j["doodadPlacements"] = terrain.doodadPlacements.size();
        j["wmoPlacements"] = terrain.wmoPlacements.size();
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("ADT: %s\n", path.c_str());
    std::printf("  version          : %u\n", terrain.version);
    std::printf("  file bytes       : %llu\n", static_cast<unsigned long long>(fsz));
    std::printf("  coord            : (%d, %d)\n", terrain.coord.x, terrain.coord.y);
    std::printf("  chunks loaded    : %d/256\n", loadedChunks);
    if (loadedChunks > 0) {
        std::printf("  height range     : [%.2f, %.2f]\n", minH, maxH);
    }
    std::printf("  hole chunks      : %d (with cave/gap masks)\n", holeChunks);
    std::printf("  water chunks     : %d\n", waterChunks);
    std::printf("  textures         : %zu\n", terrain.textures.size());
    std::printf("  doodad names     : %zu (%zu placements)\n",
                terrain.doodadNames.size(),
                terrain.doodadPlacements.size());
    std::printf("  wmo names        : %zu (%zu placements)\n",
                terrain.wmoNames.size(),
                terrain.wmoPlacements.size());
    return 0;
}

int handleInfoJsondbc(int& i, int argc, char** argv) {
    // Inspect a JSON DBC sidecar (the JSON output of asset_extract
    // --emit-json-dbc). Reports recordCount, fieldCount, source
    // filename, and format version - useful for verifying the
    // sidecar tracks the proprietary file's row count.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "info-jsondbc: cannot open %s\n", path.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "info-jsondbc: bad JSON in %s (%s)\n",
                     path.c_str(), e.what());
        return 1;
    }
    // The wowee JSON DBC schema (from open_format_emitter.cpp):
    // {format, source, recordCount, fieldCount, records:[[...], ...]}.
    // Tolerate missing fields rather than crashing - old sidecars
    // may predate a field addition.
    std::string format = doc.value("format", std::string{});
    std::string source = doc.value("source", std::string{});
    uint32_t recordCount = doc.value("recordCount", 0u);
    uint32_t fieldCount  = doc.value("fieldCount",  0u);
    uint32_t actualRecs = 0;
    if (doc.contains("records") && doc["records"].is_array()) {
        actualRecs = static_cast<uint32_t>(doc["records"].size());
    }
    bool countMismatch = (recordCount != actualRecs);
    if (jsonOut) {
        nlohmann::json j;
        j["jsondbc"] = path;
        j["format"] = format;
        j["source"] = source;
        j["recordCount"] = recordCount;
        j["fieldCount"] = fieldCount;
        j["actualRecords"] = actualRecs;
        j["countMismatch"] = countMismatch;
        std::printf("%s\n", j.dump(2).c_str());
        return countMismatch ? 1 : 0;
    }
    std::printf("JSON DBC: %s\n", path.c_str());
    std::printf("  format    : %s\n", format.empty() ? "?" : format.c_str());
    std::printf("  source    : %s\n", source.empty() ? "?" : source.c_str());
    std::printf("  records   : %u (header) / %u (actual)%s\n",
                recordCount, actualRecs,
                countMismatch ? " [MISMATCH]" : "");
    std::printf("  fields    : %u\n", fieldCount);
    return countMismatch ? 1 : 0;
}


}  // namespace

bool handleFormatInfo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-png") == 0 && i + 1 < argc) {
        outRc = handleInfoPng(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-blp") == 0 && i + 1 < argc) {
        outRc = handleInfoBlp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-m2") == 0 && i + 1 < argc) {
        outRc = handleInfoM2(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmo") == 0 && i + 1 < argc) {
        outRc = handleInfoWmo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-adt") == 0 && i + 1 < argc) {
        outRc = handleInfoAdt(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-jsondbc") == 0 && i + 1 < argc) {
        outRc = handleInfoJsondbc(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
