#include "core/local_time.hpp"
#include "server_module_gen.hpp"
#include "sql_exporter.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>

namespace wowee {
namespace editor {

bool ServerModuleGenerator::generate(const ZoneManifest& manifest,
                                      const std::vector<CreatureSpawn>& creatures,
                                      const std::vector<Quest>& quests,
                                      const std::string& outputDir) {
    namespace fs = std::filesystem;
    // Sanitize mapName for filesystem and conf-key use. The original may
    // contain spaces, slashes, or punctuation (we let the user pick); we
    // need a strict identifier here for paths and conf keys.
    std::string slug;
    slug.reserve(manifest.mapName.size());
    for (char c : manifest.mapName) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            slug += c;
        } else if (c == ' ' || c == '/' || c == '\\') {
            slug += '_';
        }
    }
    if (slug.empty()) slug = "zone";
    std::string dir = outputDir + "/mod_wowee_" + slug;
    fs::create_directories(dir + "/sql");
    fs::create_directories(dir + "/conf");

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timeBuf[32];
    const std::tm tm = wowee::core::localTime(time);
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm);

    Config cfg;
    cfg.mapId = manifest.mapId;
    // SQL VALUES use cfg.mapName via SQLExporter::escape - keep raw display
    // text. Conf keys (Wowee.<key>.*) must be the slug, so use it there.
    cfg.mapName = manifest.mapName;
    cfg.displayName = manifest.displayName.empty() ? manifest.mapName : manifest.displayName;

    // 1. Generate map registration SQL
    {
        std::ofstream f(dir + "/sql/01_map.sql");
        f << "-- Wowee Custom Zone: " << cfg.displayName << "\n";
        f << "-- Generated: " << timeBuf << "\n";
        f << "-- Map ID: " << cfg.mapId << "\n\n";

        // Escape user-provided strings - a zone name like "King's Land"
        // would otherwise produce invalid SQL.
        const std::string mapName = SQLExporter::escape(cfg.mapName);
        const std::string displayName = SQLExporter::escape(cfg.displayName);

        f << "-- Register custom map\n";
        f << "INSERT INTO `map_dbc` (`ID`, `MapName`, `MapType`, `MapDescription`) VALUES ("
          << cfg.mapId << ", '" << mapName << "', 0, '"
          << displayName << "') ON DUPLICATE KEY UPDATE `MapName`='"
          << mapName << "';\n\n";

        f << "-- Register zone area\n";
        f << "INSERT INTO `area_table_dbc` (`ID`, `MapID`, `AreaName`, `ExploreFlag`) VALUES ("
          << cfg.zoneId << ", " << cfg.mapId << ", '"
          << displayName << "', 1) ON DUPLICATE KEY UPDATE `AreaName`='"
          << displayName << "';\n";
    }

    // 2. Generate creature + quest SQL
    if (!creatures.empty() || !quests.empty()) {
        SQLExporter::exportAll(creatures, quests,
                               dir + "/sql/02_spawns.sql",
                               cfg.mapId, cfg.startCreatureEntry);
    }

    // 3. Generate teleport command SQL
    if (!manifest.tiles.empty()) {
        std::ofstream f(dir + "/sql/03_teleport.sql");
        f << "-- Teleport location for .tele command\n";
        float tileSize = 533.33333f;
        float x = (32.0f - manifest.tiles[0].second) * tileSize;
        float y = (32.0f - manifest.tiles[0].first) * tileSize;
        const std::string teleName = SQLExporter::escape(manifest.mapName);
        f << "INSERT INTO `game_tele` (`name`, `position_x`, `position_y`, "
          << "`position_z`, `orientation`, `map`) VALUES ('"
          << teleName << "', " << x << ", " << y << ", "
          << manifest.baseHeight + 10.0f << ", 0, " << cfg.mapId
          << ") ON DUPLICATE KEY UPDATE `position_x`=" << x << ";\n";
        f << "\n-- Usage: .tele " << manifest.mapName << "\n";
    }

    // 4. Generate zone flags SQL
    {
        std::ofstream f(dir + "/sql/04_zone_flags.sql");
        f << "-- Zone gameplay flags\n";
        if (manifest.isSanctuary) {
            f << "-- Sanctuary zone (no PvP)\n";
            f << "UPDATE `area_table_dbc` SET `Flags` = `Flags` | 0x800 WHERE `ID` = "
              << cfg.zoneId << ";\n";
        }
        if (manifest.pvpEnabled) {
            f << "-- PvP zone\n";
            f << "UPDATE `area_table_dbc` SET `Flags` = `Flags` | 0x40 WHERE `ID` = "
              << cfg.zoneId << ";\n";
        }
    }

    // 5. Generate worldserver.conf snippet
    {
        std::ofstream f(dir + "/conf/mod_wowee.conf.dist");
        f << "#\n# Wowee Custom Zone: " << cfg.displayName << "\n";
        f << "# Add this to your worldserver.conf\n#\n\n";
        f << "# Enable custom zone " << cfg.displayName << "\n";
        f << "Wowee." << slug << ".Enabled = 1\n";
        f << "Wowee." << slug << ".MapId = " << cfg.mapId << "\n";
        f << "Wowee." << slug << ".ZoneId = " << cfg.zoneId << "\n\n";
        f << "# Zone settings\n";
        f << "Wowee." << slug << ".AllowFlying = "
          << (manifest.allowFlying ? 1 : 0) << "\n";
        f << "Wowee." << slug << ".PvP = "
          << (manifest.pvpEnabled ? 1 : 0) << "\n";
    }

    // 6. Generate server admin README
    {
        std::ofstream f(dir + "/README.md");
        f << "# " << cfg.displayName << " - Custom Zone for AzerothCore\n\n";
        f << "Generated by Wowee World Editor v1.0.0\n\n";
        f << "## Installation\n\n";
        f << "1. Copy SQL files to your AzerothCore `sql/custom/` directory\n";
        f << "2. Execute in order: `01_map.sql`, `02_spawns.sql`, `03_teleport.sql`, `04_zone_flags.sql`\n";
        f << "3. Copy `conf/mod_wowee.conf.dist` settings to `worldserver.conf`\n";
        f << "4. Restart worldserver\n\n";
        f << "## Details\n\n";
        f << "- **Map ID**: " << cfg.mapId << "\n";
        f << "- **Zone**: " << cfg.displayName << "\n";
        f << "- **Creatures**: " << creatures.size() << "\n";
        f << "- **Quests**: " << quests.size() << "\n";
        f << "- **Tiles**: " << manifest.tiles.size() << "\n";
        if (manifest.allowFlying) f << "- **Flying**: Enabled\n";
        if (manifest.pvpEnabled) f << "- **PvP**: Enabled\n";
        if (manifest.isSanctuary) f << "- **Sanctuary**: Yes\n";
        f << "\n## Teleport\n\n";
        f << "```\n.tele " << manifest.mapName << "\n```\n";
    }

    // 7. Generate manifest JSON
    {
        nlohmann::json j;
        j["format"] = "wowee-server-module-1.0";
        j["zone"] = cfg.displayName;
        j["mapId"] = cfg.mapId;
        j["creatures"] = creatures.size();
        j["quests"] = quests.size();
        j["tiles"] = manifest.tiles.size();
        j["generated"] = timeBuf;
        std::ofstream f(dir + "/module.json");
        f << j.dump(2) << "\n";
    }

    LOG_INFO("Server module generated: ", dir, " (",
             creatures.size(), " creatures, ", quests.size(), " quests)");
    return true;
}

} // namespace editor
} // namespace wowee
