#include "dbc_exporter.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace wowee {
namespace editor {

bool DBCExporter::exportAsJson(pipeline::AssetManager* am,
                                const std::string& dbcName,
                                const std::string& outputPath) {
    auto dbc = am->loadDBC(dbcName);
    if (!dbc || !dbc->isLoaded()) return false;

    namespace fs = std::filesystem;
    fs::create_directories(fs::path(outputPath).parent_path());

    nlohmann::json j;
    j["format"] = "wowee-dbc-json-1.0";
    j["source"] = dbcName;
    j["recordCount"] = dbc->getRecordCount();
    j["fieldCount"] = dbc->getFieldCount();

    nlohmann::json records = nlohmann::json::array();
    for (uint32_t i = 0; i < dbc->getRecordCount(); i++) {
        nlohmann::json row = nlohmann::json::array();
        for (uint32_t field = 0; field < dbc->getFieldCount(); field++) {
            uint32_t val = dbc->getUInt32(i, field);
            std::string strVal = dbc->getString(i, field);
            if (!strVal.empty() && strVal[0] != '\0' && strVal.size() < 200) {
                row.push_back(strVal);
            } else {
                float fval = dbc->getFloat(i, field);
                if (val != 0 && fval != 0.0f && fval > -1e10f && fval < 1e10f &&
                    static_cast<uint32_t>(fval) != val) {
                    row.push_back(fval);
                } else {
                    row.push_back(val);
                }
            }
        }
        records.push_back(row);
    }
    j["records"] = records;

    std::ofstream f(outputPath);
    if (!f) return false;
    f << j.dump(2) << "\n";

    LOG_INFO("DBC exported as JSON: ", dbcName, " → ", outputPath,
             " (", dbc->getRecordCount(), " records)");
    return true;
}

int DBCExporter::exportZoneDBCs(pipeline::AssetManager* am,
                                  const std::string& outputDir) {
    const char* zoneDBCs[] = {
        "AreaTable.dbc",
        "Map.dbc",
        "Light.dbc",
        "LightParams.dbc",
        "ZoneMusic.dbc",
        "SoundAmbience.dbc",
        "GroundEffectTexture.dbc",
        "GroundEffectDoodad.dbc",
        "LiquidType.dbc",
        // Creature/NPC display data so spawned creatures can be looked up
        // without the original game DBCs.
        "CreatureDisplayInfo.dbc",
        "CreatureModelData.dbc",
        "CreatureType.dbc",
        "CreatureFamily.dbc",
        "FactionTemplate.dbc",
        "Faction.dbc",
        // Item display data - buildings/decorations may reference these
        "ItemDisplayInfo.dbc"
    };

    int exported = 0;
    for (const char* name : zoneDBCs) {
        std::string baseName(name);
        auto dot = baseName.rfind('.');
        if (dot != std::string::npos) baseName = baseName.substr(0, dot);

        std::string outPath = outputDir + "/" + baseName + ".json";
        if (exportAsJson(am, name, outPath))
            exported++;
    }

    LOG_INFO("Exported ", exported, " zone-relevant DBCs as JSON to ", outputDir);
    return exported;
}

} // namespace editor
} // namespace wowee
