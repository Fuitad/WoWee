#include "cli_world_map.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_world_map.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

std::string stripWomxExt(std::string base) {
    stripExt(base, ".womx");
    return base;
}


void printGenSummary(const wowee::pipeline::WoweeWorldMap& m,
                     const std::string& base) {
    std::printf("Wrote %s.womx\n", base.c_str());
    std::printf("  name        : %s\n", m.name.c_str());
    std::printf("  worldType   : %u (%s)\n", m.worldType,
                wowee::pipeline::WoweeWorldMap::worldTypeName(m.worldType));
    std::printf("  gridSize    : %ux%u tiles\n", m.gridSize, m.gridSize);
    std::printf("  tilesPresent: %u / %u\n",
                m.countTiles(),
                static_cast<uint32_t>(m.gridSize) * m.gridSize);
}

int handleGenContinent(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string mapName = "Continent";
    if (i + 1 < argc && argv[i + 1][0] != '-') mapName = argv[++i];
    base = stripWomxExt(base);
    auto m = wowee::pipeline::WoweeWorldMapLoader::makeContinent(mapName);
    if (!saveOrError<wowee::pipeline::WoweeWorldMapLoader>(m, base, "gen-world-map", ".womx")) return 1;
    printGenSummary(m, base);
    return 0;
}

int handleGenInstance(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string mapName = "Instance";
    if (i + 1 < argc && argv[i + 1][0] != '-') mapName = argv[++i];
    base = stripWomxExt(base);
    auto m = wowee::pipeline::WoweeWorldMapLoader::makeInstance(mapName);
    if (!saveOrError<wowee::pipeline::WoweeWorldMapLoader>(m, base, "gen-world-map-instance", ".womx")) return 1;
    printGenSummary(m, base);
    return 0;
}

int handleGenArena(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string mapName = "Arena";
    if (i + 1 < argc && argv[i + 1][0] != '-') mapName = argv[++i];
    base = stripWomxExt(base);
    auto m = wowee::pipeline::WoweeWorldMapLoader::makeArena(mapName);
    if (!saveOrError<wowee::pipeline::WoweeWorldMapLoader>(m, base, "gen-world-map-arena", ".womx")) return 1;
    printGenSummary(m, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWomxExt(base);
    if (!wowee::pipeline::WoweeWorldMapLoader::exists(base)) {
        return reportMissing("WOMX", base, ".womx");
    }
    auto m = wowee::pipeline::WoweeWorldMapLoader::load(base);
    uint32_t total = static_cast<uint32_t>(m.gridSize) * m.gridSize;
    uint32_t present = m.countTiles();
    if (jsonOut) {
        nlohmann::json j;
        j["womx"] = base + ".womx";
        j["name"] = m.name;
        j["worldType"] = m.worldType;
        j["worldTypeName"] =
            wowee::pipeline::WoweeWorldMap::worldTypeName(m.worldType);
        j["gridSize"] = m.gridSize;
        j["totalTiles"] = total;
        j["tilesPresent"] = present;
        j["defaultLightId"] = m.defaultLightId;
        j["defaultWeatherId"] = m.defaultWeatherId;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOMX: %s.womx\n", base.c_str());
    std::printf("  name             : %s\n", m.name.c_str());
    std::printf("  worldType        : %u (%s)\n", m.worldType,
                wowee::pipeline::WoweeWorldMap::worldTypeName(m.worldType));
    std::printf("  gridSize         : %ux%u (%u total tiles)\n",
                m.gridSize, m.gridSize, total);
    std::printf("  tilesPresent     : %u (%.1f%%)\n",
                present,
                total ? (100.0f * present / total) : 0.0f);
    std::printf("  defaultLightId   : %u\n", m.defaultLightId);
    std::printf("  defaultWeatherId : %u\n", m.defaultWeatherId);
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Export a .womx to a human-editable JSON sidecar. Tiles
    // are represented as one '1'/'0' string per row (dense)
    // because a full 64x64 continent has 4096 tiles — sparse
    // [[x,y]] arrays would be 4× larger and harder to spot
    // missing-row patterns visually. The dense string form is
    // easy to hand-edit ('1' = tile present, '0' = no tile).
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    base = stripWomxExt(base);
    if (outPath.empty()) outPath = base + ".womx.json";
    if (!wowee::pipeline::WoweeWorldMapLoader::exists(base)) {
        return reportMissing("export-womx-json", "WOMX", base, ".womx");
    }
    auto m = wowee::pipeline::WoweeWorldMapLoader::load(base);
    nlohmann::json j;
    j["name"] = m.name;
    j["worldType"] = m.worldType;
    j["worldTypeName"] =
        wowee::pipeline::WoweeWorldMap::worldTypeName(m.worldType);
    j["gridSize"] = m.gridSize;
    j["defaultLightId"] = m.defaultLightId;
    j["defaultWeatherId"] = m.defaultWeatherId;
    nlohmann::json rows = nlohmann::json::array();
    for (uint32_t y = 0; y < m.gridSize; ++y) {
        std::string row;
        row.reserve(m.gridSize);
        for (uint32_t x = 0; x < m.gridSize; ++x)
            row.push_back(m.hasTile(x, y) ? '1' : '0');
        rows.push_back(row);
    }
    j["tiles"] = rows;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-womx-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.womx\n", base.c_str());
    std::printf("  grid   : %ux%u\n", m.gridSize, m.gridSize);
    std::printf("  tiles  : %u present\n", m.countTiles());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    // Round-trip pair for --export-womx-json. Accepts the
    // same dense rows-of-strings layout. Tolerates missing
    // optional fields by using the WoweeWorldMap defaults.
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (i + 1 < argc && argv[i + 1][0] != '-') outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".womx.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = stripWomxExt(outBase);
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-womx-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-womx-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto typeFromName = [](const std::string& s) -> uint8_t {
        if (s == "continent")    return wowee::pipeline::WoweeWorldMap::Continent;
        if (s == "instance")     return wowee::pipeline::WoweeWorldMap::Instance;
        if (s == "battleground") return wowee::pipeline::WoweeWorldMap::Battleground;
        if (s == "arena")        return wowee::pipeline::WoweeWorldMap::Arena;
        return wowee::pipeline::WoweeWorldMap::Continent;
    };
    wowee::pipeline::WoweeWorldMap m;
    m.name = j.value("name", std::string{});
    if (j.contains("worldType") && j["worldType"].is_number_integer()) {
        m.worldType = static_cast<uint8_t>(j["worldType"].get<int>());
    } else if (j.contains("worldTypeName") && j["worldTypeName"].is_string()) {
        m.worldType = typeFromName(j["worldTypeName"].get<std::string>());
    }
    m.gridSize = static_cast<uint8_t>(j.value("gridSize", 64));
    m.defaultLightId = j.value("defaultLightId", 0u);
    m.defaultWeatherId = j.value("defaultWeatherId", 0u);
    if (m.gridSize == 0 || m.gridSize > 128) {
        std::fprintf(stderr,
            "import-womx-json: gridSize %u out of range 1..128\n",
            m.gridSize);
        return 1;
    }
    size_t bytes =
        (static_cast<size_t>(m.gridSize) * m.gridSize + 7) / 8;
    m.tileBitmap.assign(bytes, 0);
    if (j.contains("tiles") && j["tiles"].is_array()) {
        const auto& rows = j["tiles"];
        for (uint32_t y = 0; y < m.gridSize && y < rows.size(); ++y) {
            if (!rows[y].is_string()) continue;
            const std::string& row = rows[y].get_ref<const std::string&>();
            for (uint32_t x = 0;
                 x < m.gridSize && x < row.size(); ++x) {
                if (row[x] == '1') m.setTile(x, y, true);
            }
        }
    }
    if (!wowee::pipeline::WoweeWorldMapLoader::save(m, outBase)) {
        std::fprintf(stderr,
            "import-womx-json: failed to save %s.womx\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.womx\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  grid   : %ux%u\n", m.gridSize, m.gridSize);
    std::printf("  tiles  : %u present\n", m.countTiles());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWomxExt(base);
    if (!wowee::pipeline::WoweeWorldMapLoader::exists(base)) {
        return reportMissing("validate-womx", "WOMX", base, ".womx");
    }
    auto m = wowee::pipeline::WoweeWorldMapLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (m.gridSize == 0 || m.gridSize > 128) {
        errors.push_back("gridSize " + std::to_string(m.gridSize) +
                         " not in range 1..128");
    }
    if (m.worldType > wowee::pipeline::WoweeWorldMap::Arena) {
        errors.push_back("worldType " + std::to_string(m.worldType) +
                         " not in known range 0..3");
    }
    size_t expectedBytes =
        (static_cast<size_t>(m.gridSize) * m.gridSize + 7) / 8;
    if (m.tileBitmap.size() != expectedBytes) {
        errors.push_back("tileBitmap size " +
                         std::to_string(m.tileBitmap.size()) +
                         " != expected " + std::to_string(expectedBytes) +
                         " for grid " + std::to_string(m.gridSize) + "x" +
                         std::to_string(m.gridSize));
    }
    if (m.countTiles() == 0) {
        warnings.push_back("no tiles present in bitmap (empty world)");
    }
    bool ok = errors.empty();
    if (jsonOut) {
        nlohmann::json j;
        j["womx"] = base + ".womx";
        j["ok"] = ok;
        j["errors"] = errors;
        j["warnings"] = warnings;
        std::printf("%s\n", j.dump(2).c_str());
        return ok ? 0 : 1;
    }
    std::printf("validate-womx: %s.womx\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %ux%u grid, %u/%u tiles present\n",
                    m.gridSize, m.gridSize,
                    m.countTiles(),
                    static_cast<uint32_t>(m.gridSize) * m.gridSize);
        return 0;
    }
    if (!warnings.empty()) {
        std::printf("  warnings (%zu):\n", warnings.size());
        for (const auto& w : warnings)
            std::printf("    - %s\n", w.c_str());
    }
    if (!errors.empty()) {
        std::printf("  ERRORS (%zu):\n", errors.size());
        for (const auto& e : errors)
            std::printf("    - %s\n", e.c_str());
    }
    return ok ? 0 : 1;
}

} // namespace

bool handleWorldMap(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-world-map") == 0 && i + 1 < argc) {
        outRc = handleGenContinent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-world-map-instance") == 0 && i + 1 < argc) {
        outRc = handleGenInstance(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-world-map-arena") == 0 && i + 1 < argc) {
        outRc = handleGenArena(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-womx") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-womx") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-womx-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-womx-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
