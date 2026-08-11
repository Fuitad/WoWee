#include "cli_catalog_paths.hpp"
#include "cli_convert_single.hpp"

#include "pipeline/asset_manager.hpp"
#include "pipeline/wowee_model.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/wmo_loader.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/blp_loader.hpp"
#include "stb_image_write.h"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleConvertM2(int& i, int /*argc*/, char** argv,
                    std::string& dataPath) {
    std::string m2Path = argv[++i];
    std::printf("Converting M2-WOM: %s\n", m2Path.c_str());
    if (dataPath.empty()) dataPath = "Data";
    wowee::pipeline::AssetManager am;
    if (!am.initialize(dataPath)) {
        std::fprintf(stderr, "FAILED: cannot initialize asset manager\n");
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::fromM2(m2Path, &am);
    if (!wom.isValid()) {
        std::fprintf(stderr, "FAILED: %s\n", m2Path.c_str());
        am.shutdown();
        return 1;
    }
    std::string outPath = m2Path;
    auto dot = outPath.rfind('.');
    if (dot != std::string::npos) outPath = outPath.substr(0, dot);
    wowee::pipeline::WoweeModelLoader::save(wom, "output/models/" + outPath);
    std::printf("OK: output/models/%s.wom (v%u, %zu verts, %zu bones, %zu batches)\n",
        outPath.c_str(), wom.version, wom.vertices.size(),
        wom.bones.size(), wom.batches.size());
    am.shutdown();
    return 0;
}

int handleConvertWmo(int& i, int /*argc*/, char** argv,
                     std::string& dataPath) {
    std::string wmoPath = argv[++i];
    std::printf("Converting WMO-WOB: %s\n", wmoPath.c_str());
    if (dataPath.empty()) dataPath = "Data";
    wowee::pipeline::AssetManager am;
    if (!am.initialize(dataPath)) {
        std::fprintf(stderr, "FAILED: cannot initialize asset manager\n");
        return 1;
    }
    auto wmoData = am.readFile(wmoPath);
    if (wmoData.empty()) {
        std::fprintf(stderr, "FAILED: file not found: %s\n", wmoPath.c_str());
        am.shutdown();
        return 1;
    }
    auto wmoModel = wowee::pipeline::WMOLoader::load(wmoData);
    if (wmoModel.nGroups > 0) {
        std::string wmoBase = wmoPath;
        if (wmoBase.size() > 4) wmoBase = wmoBase.substr(0, wmoBase.size() - 4);
        for (uint32_t gi = 0; gi < wmoModel.nGroups; gi++) {
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "_%03u.wmo", gi);
            auto gd = am.readFile(wmoBase + suffix);
            if (!gd.empty()) wowee::pipeline::WMOLoader::loadGroup(gd, wmoModel, gi);
        }
    }
    auto wob = wowee::pipeline::WoweeBuildingLoader::fromWMO(wmoModel, wmoPath);
    if (!wob.isValid()) {
        std::fprintf(stderr, "FAILED: %s\n", wmoPath.c_str());
        am.shutdown();
        return 1;
    }
    std::string outPath = wmoPath;
    auto dot = outPath.rfind('.');
    if (dot != std::string::npos) outPath = outPath.substr(0, dot);
    wowee::pipeline::WoweeBuildingLoader::save(wob, "output/buildings/" + outPath);
    std::printf("OK: output/buildings/%s.wob (%zu groups)\n",
        outPath.c_str(), wob.groups.size());
    am.shutdown();
    return 0;
}

int handleConvertDbcJson(int& i, int argc, char** argv) {
    // Standalone DBC -> JSON sidecar conversion. Mirrors what
    // asset_extract --emit-open does for one file at a time, so
    // designers don't have to re-run a full extraction just to
    // refresh one DBC sidecar.
    std::string dbcPath = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    if (outPath.empty()) {
        outPath = dbcPath;
        if (outPath.size() >= 4 &&
            outPath.substr(outPath.size() - 4) == ".dbc") {
            outPath = outPath.substr(0, outPath.size() - 4);
        }
        outPath += ".json";
    }
    std::ifstream in(dbcPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "convert-dbc-json: cannot open %s\n", dbcPath.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    wowee::pipeline::DBCFile dbc;
    if (!dbc.load(bytes)) {
        std::fprintf(stderr, "convert-dbc-json: failed to parse %s\n", dbcPath.c_str());
        return 1;
    }
    // Same JSON schema asset_extract emits, so the editor's runtime
    // overlay loader picks the file up without changes.
    nlohmann::json j;
    j["format"] = "wowee-dbc-json-1.0";
    j["source"] = std::filesystem::path(dbcPath).filename().string();
    j["recordCount"] = dbc.getRecordCount();
    j["fieldCount"] = dbc.getFieldCount();
    nlohmann::json records = nlohmann::json::array();
    for (uint32_t r = 0; r < dbc.getRecordCount(); ++r) {
        nlohmann::json row = nlohmann::json::array();
        for (uint32_t f = 0; f < dbc.getFieldCount(); ++f) {
            // Same heuristic as open_format_emitter::emitJsonFromDbc:
            // prefer string > float > uint32 based on what the
            // bytes plausibly are. Round-trips through loadJSON.
            uint32_t val = dbc.getUInt32(r, f);
            std::string s = dbc.getString(r, f);
            if (!s.empty() && s[0] != '\0' && s.size() < 200) {
                row.push_back(s);
            } else {
                float fv = dbc.getFloat(r, f);
                if (val != 0 && fv != 0.0f && fv > -1e10f && fv < 1e10f &&
                    static_cast<uint32_t>(fv) != val) {
                    row.push_back(fv);
                } else {
                    row.push_back(val);
                }
            }
        }
        records.push_back(std::move(row));
    }
    j["records"] = std::move(records);
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr, "convert-dbc-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    std::printf("Converted %s -> %s\n", dbcPath.c_str(), outPath.c_str());
    std::printf("  %u records x %u fields\n",
                dbc.getRecordCount(), dbc.getFieldCount());
    return 0;
}

int handleConvertJsonDbc(int& i, int argc, char** argv) {
    // Reverse direction — JSON sidecar back to binary DBC. Useful
    // for shipping edited content to private servers (AzerothCore /
    // TrinityCore) which only consume binary DBC. The output is
    // byte-compatible with the original Blizzard format.
    std::string jsonPath = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    if (outPath.empty()) {
        outPath = jsonPath;
        if (outPath.size() >= 5 &&
            outPath.substr(outPath.size() - 5) == ".json") {
            outPath = outPath.substr(0, outPath.size() - 5);
        }
        outPath += ".dbc";
    }
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr, "convert-json-dbc: cannot open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json doc;
    try { in >> doc; }
    catch (const std::exception& e) {
        std::fprintf(stderr, "convert-json-dbc: bad JSON in %s (%s)\n",
                     jsonPath.c_str(), e.what());
        return 1;
    }
    uint32_t fieldCount = doc.value("fieldCount", 0u);
    if (!doc.contains("records") || !doc["records"].is_array()) {
        std::fprintf(stderr, "convert-json-dbc: missing 'records' array in %s\n",
                     jsonPath.c_str());
        return 1;
    }
    const auto& records = doc["records"];
    uint32_t recordCount = static_cast<uint32_t>(records.size());
    if (fieldCount == 0 && recordCount > 0 && records[0].is_array()) {
        // Tolerate JSON files that drop fieldCount — derive from row.
        fieldCount = static_cast<uint32_t>(records[0].size());
    }
    if (fieldCount == 0) {
        std::fprintf(stderr,
            "convert-json-dbc: cannot determine fieldCount in %s\n",
            jsonPath.c_str());
        return 1;
    }
    uint32_t recordSize = fieldCount * 4;
    // Build records + string block. Strings are deduped: identical
    // strings reuse the same offset in the block. The first byte
    // of the block is always '\0' so offset=0 means empty string,
    // matching Blizzard's convention.
    std::vector<uint8_t> recordBytes(
        static_cast<size_t>(recordCount) * static_cast<size_t>(recordSize), 0);
    std::vector<uint8_t> stringBlock;
    stringBlock.push_back(0);  // leading NUL — empty-string offset
    std::unordered_map<std::string, uint32_t> stringOffsets;
    stringOffsets[""] = 0;
    auto internString = [&](const std::string& s) -> uint32_t {
        if (s.empty()) return 0;
        auto it = stringOffsets.find(s);
        if (it != stringOffsets.end()) return it->second;
        uint32_t off = static_cast<uint32_t>(stringBlock.size());
        for (char c : s) stringBlock.push_back(static_cast<uint8_t>(c));
        stringBlock.push_back(0);
        stringOffsets[s] = off;
        return off;
    };
    int convertErrors = 0;
    for (uint32_t r = 0; r < recordCount; ++r) {
        const auto& row = records[r];
        if (!row.is_array() || row.size() != fieldCount) {
            convertErrors++;
            continue;
        }
        uint8_t* dst = recordBytes.data() + r * recordSize;
        for (uint32_t f = 0; f < fieldCount; ++f) {
            uint32_t val = 0;
            const auto& cell = row[f];
            if (cell.is_string()) {
                val = internString(cell.get<std::string>());
            } else if (cell.is_number_float()) {
                float fv = cell.get<float>();
                std::memcpy(&val, &fv, 4);
            } else if (cell.is_number_unsigned()) {
                val = cell.get<uint32_t>();
            } else if (cell.is_number_integer()) {
                // Negative ints reinterpret as uint32 (DBC has no
                // separate signed type; the consumer interprets).
                int32_t sv = cell.get<int32_t>();
                std::memcpy(&val, &sv, 4);
            } else if (cell.is_boolean()) {
                val = cell.get<bool>() ? 1u : 0u;
            } else if (cell.is_null()) {
                val = 0;
            } else {
                convertErrors++;
            }
            // Little-endian write — DBC is always LE per Blizzard
            // format spec, regardless of host architecture.
            dst[f * 4 + 0] =  val        & 0xFF;
            dst[f * 4 + 1] = (val >>  8) & 0xFF;
            dst[f * 4 + 2] = (val >> 16) & 0xFF;
            dst[f * 4 + 3] = (val >> 24) & 0xFF;
        }
    }
    // Header: WDBC magic + 4 uint32s (recordCount, fieldCount,
    // recordSize, stringBlockSize).
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "convert-json-dbc: cannot write %s\n", outPath.c_str());
        return 1;
    }
    uint32_t header[5] = {
        0x43424457u,                       // 'WDBC' little-endian
        recordCount, fieldCount, recordSize,
        static_cast<uint32_t>(stringBlock.size())
    };
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    out.write(reinterpret_cast<const char*>(recordBytes.data()),
              recordBytes.size());
    out.write(reinterpret_cast<const char*>(stringBlock.data()),
              stringBlock.size());
    out.close();
    std::printf("Converted %s -> %s\n", jsonPath.c_str(), outPath.c_str());
    std::printf("  %u records x %u fields, %zu-byte string block\n",
                recordCount, fieldCount, stringBlock.size());
    if (convertErrors > 0) {
        std::printf("  warning: %d cell(s) had unrecognized types\n", convertErrors);
    }
    return 0;
}

int handleConvertBlpPng(int& i, int argc, char** argv) {
    // Standalone BLP -> PNG conversion. Same code path as
    // asset_extract --emit-open's per-file walker, but for one
    // texture without re-running a full extraction.
    std::string blpPath = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    if (outPath.empty()) {
        outPath = blpPath;
        if (outPath.size() >= 4 &&
            outPath.substr(outPath.size() - 4) == ".blp") {
            outPath = outPath.substr(0, outPath.size() - 4);
        }
        outPath += ".png";
    }
    std::ifstream in(blpPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "convert-blp-png: cannot open %s\n", blpPath.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    auto img = wowee::pipeline::BLPLoader::load(bytes);
    if (!img.isValid()) {
        std::fprintf(stderr, "convert-blp-png: failed to decode %s\n",
                     blpPath.c_str());
        return 1;
    }
    // Same dimension/buffer-size guards as the asset_extract
    // emitter so we never feed stbi_write_png an invalid buffer.
    const size_t expected = static_cast<size_t>(img.width) * img.height * 4;
    if (img.width <= 0 || img.height <= 0 ||
        img.width > 8192 || img.height > 8192 ||
        img.data.size() < expected) {
        std::fprintf(stderr, "convert-blp-png: invalid dimensions or data (%dx%d, %zu bytes)\n",
                     img.width, img.height, img.data.size());
        return 1;
    }
    // Ensure the output directory exists.
    //
    // The comment that used to be here said create_directories with an empty
    // path is a no-op, so a png written into the working directory needed no
    // special case. It is not a no-op: it throws filesystem_error, "Invalid
    // argument", and nothing here catches it — so a bare output filename
    // terminated the process. That belief is why twenty-five mesh generators
    // did the same thing.
    ensureParentDirectory(outPath);
    int rc = stbi_write_png(outPath.c_str(),
                             img.width, img.height, 4,
                             img.data.data(), img.width * 4);
    if (!rc) {
        std::fprintf(stderr, "convert-blp-png: stbi_write_png failed for %s\n",
                     outPath.c_str());
        return 1;
    }
    std::printf("Converted %s -> %s\n", blpPath.c_str(), outPath.c_str());
    std::printf("  %dx%d, %zu bytes (RGBA8)\n",
                img.width, img.height, img.data.size());
    return 0;
}

}  // namespace

bool handleConvertSingle(int& i, int argc, char** argv,
                         std::string& dataPath, int& outRc) {
    if (std::strcmp(argv[i], "--convert-m2") == 0 && i + 1 < argc) {
        outRc = handleConvertM2(i, argc, argv, dataPath); return true;
    }
    if (std::strcmp(argv[i], "--convert-wmo") == 0 && i + 1 < argc) {
        outRc = handleConvertWmo(i, argc, argv, dataPath); return true;
    }
    if (std::strcmp(argv[i], "--convert-dbc-json") == 0 && i + 1 < argc) {
        outRc = handleConvertDbcJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--convert-json-dbc") == 0 && i + 1 < argc) {
        outRc = handleConvertJsonDbc(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--convert-blp-png") == 0 && i + 1 < argc) {
        outRc = handleConvertBlpPng(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
