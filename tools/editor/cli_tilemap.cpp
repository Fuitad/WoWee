#include "cli_tilemap.hpp"

#include "zone_manifest.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoTilemap(int& i, int argc, char** argv) {
    // Visualize the WoW 64x64 ADT grid showing which tiles are
    // claimed by which zones across a project. Useful for
    // spotting tile-coord collisions before two zones try to
    // ship overlapping content, and for getting a 'where am I
    // working?' overview of a multi-zone project.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-tilemap: %s is not a directory\n", projectDir.c_str());
        return 1;
    }
    // Map (tx, ty) -> vector<zone names> so collision overlaps
    // are visible. Walk every zone in the project.
    std::map<std::pair<int,int>, std::vector<std::string>> claims;
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        wowee::editor::ZoneManifest zm;
        if (!zm.load((entry.path() / "zone.json").string())) continue;
        std::string zname = zm.mapName.empty()
            ? entry.path().filename().string() : zm.mapName;
        zones.push_back(zname);
        for (const auto& [tx, ty] : zm.tiles) {
            if (tx >= 0 && tx < 64 && ty >= 0 && ty < 64) {
                claims[{tx, ty}].push_back(zname);
            }
        }
    }
    // Per-zone label glyph: first letter of the zone name,
    // uppercased so different zones get distinct chars in the
    // grid. Multi-letter overlap collapses to '*'.
    std::map<std::string, char> zoneGlyph;
    char nextGlyph = 'A';
    for (const auto& z : zones) {
        if (zoneGlyph.count(z)) continue;
        if (!z.empty() && std::isalpha(static_cast<unsigned char>(z[0]))) {
            zoneGlyph[z] = static_cast<char>(std::toupper(static_cast<unsigned char>(z[0])));
        } else {
            zoneGlyph[z] = nextGlyph++;
            if (nextGlyph > 'Z') nextGlyph = 'a';
        }
    }
    int collisions = 0;
    for (const auto& [coord, owners] : claims) {
        if (owners.size() > 1) collisions++;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["projectDir"] = projectDir;
        j["zoneCount"] = zones.size();
        j["claimedTiles"] = claims.size();
        j["collisions"] = collisions;
        nlohmann::json claimsJson = nlohmann::json::array();
        for (const auto& [coord, owners] : claims) {
            claimsJson.push_back({{"x", coord.first},
                                   {"y", coord.second},
                                   {"zones", owners}});
        }
        j["claims"] = claimsJson;
        std::printf("%s\n", j.dump(2).c_str());
        return collisions == 0 ? 0 : 1;
    }
    std::printf("Tilemap: %s\n", projectDir.c_str());
    std::printf("  zones      : %zu\n", zones.size());
    std::printf("  tiles used : %zu\n", claims.size());
    std::printf("  collisions : %d (multiple zones claiming same tile)\n",
                collisions);
    std::printf("  legend     :");
    for (const auto& [name, glyph] : zoneGlyph) {
        std::printf(" %c=%s", glyph, name.c_str());
    }
    std::printf("\n\n");
    // Render 64x64 grid. Print column header in groups of 10
    // for readability.
    std::printf("       ");
    for (int x = 0; x < 64; ++x) {
        std::printf("%c", (x % 10 == 0) ? '0' + (x / 10) : ' ');
    }
    std::printf("\n");
    std::printf("       ");
    for (int x = 0; x < 64; ++x) std::printf("%d", x % 10);
    std::printf("\n");
    for (int y = 0; y < 64; ++y) {
        // Skip rows that have no tiles claimed - keeps the
        // output bounded for projects in one corner of the map.
        bool rowHasContent = false;
        for (int x = 0; x < 64 && !rowHasContent; ++x) {
            if (claims.count({x, y})) rowHasContent = true;
        }
        if (!rowHasContent) continue;
        std::printf("  y=%2d ", y);
        for (int x = 0; x < 64; ++x) {
            auto it = claims.find({x, y});
            if (it == claims.end()) {
                std::printf(".");
            } else if (it->second.size() > 1) {
                std::printf("*");  // collision
            } else {
                std::printf("%c", zoneGlyph[it->second[0]]);
            }
        }
        std::printf("\n");
    }
    if (collisions > 0) {
        std::printf("\n  COLLISIONS:\n");
        for (const auto& [coord, owners] : claims) {
            if (owners.size() < 2) continue;
            std::printf("    (%d, %d) claimed by:", coord.first, coord.second);
            for (const auto& o : owners) std::printf(" %s", o.c_str());
            std::printf("\n");
        }
    }
    return collisions == 0 ? 0 : 1;
}


}  // namespace

bool handleTilemap(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-tilemap") == 0 && i + 1 < argc) {
        outRc = handleInfoTilemap(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
