#include "cli_add.hpp"

#include "object_placer.hpp"
#include "npc_spawner.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleAddObject(int& i, int argc, char** argv) {
    // Append a single object placement to a zone's objects.json.
    // Args: <zoneDir> <m2|wmo> <gamePath> <x> <y> <z> [scale]
    std::string zoneDir = argv[++i];
    std::string typeStr = argv[++i];
    std::string gamePath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr, "add-object: zone '%s' does not exist\n",
                     zoneDir.c_str());
        return 1;
    }
    wowee::editor::PlaceableType ptype;
    if (typeStr == "m2") ptype = wowee::editor::PlaceableType::M2;
    else if (typeStr == "wmo") ptype = wowee::editor::PlaceableType::WMO;
    else {
        std::fprintf(stderr, "add-object: type must be 'm2' or 'wmo'\n");
        return 1;
    }
    glm::vec3 pos;
    try {
        pos.x = std::stof(argv[++i]);
        pos.y = std::stof(argv[++i]);
        pos.z = std::stof(argv[++i]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "add-object: bad coordinate (%s)\n", e.what());
        return 1;
    }
    wowee::editor::ObjectPlacer placer;
    std::string path = zoneDir + "/objects.json";
    if (fs::exists(path)) placer.loadFromFile(path);
    placer.setActivePath(gamePath, ptype);
    placer.placeObject(pos);
    // Optional scale after coordinates.
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try {
            float scale = std::stof(argv[++i]);
            if (std::isfinite(scale) && scale > 0.0f) {
                // Set scale on the just-placed object (last in list).
                placer.getObjects().back().scale = scale;
            }
        } catch (...) {}
    }
    if (!placer.saveToFile(path)) {
        std::fprintf(stderr, "add-object: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Added %s '%s' to %s (now %zu total)\n",
                typeStr.c_str(), gamePath.c_str(), path.c_str(),
                placer.getObjects().size());
    return 0;
}

int handleAddCreature(int& i, int argc, char** argv) {
    // Append a single creature spawn to a zone's creatures.json.
    // Args: <zoneDir> <name> <x> <y> <z> [displayId] [level]
    // Useful for batch-populating zones via shell script without
    // launching the GUI placement tool.
    std::string zoneDir = argv[++i];
    std::string name = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr, "add-creature: zone '%s' does not exist\n",
                     zoneDir.c_str());
        return 1;
    }
    wowee::editor::CreatureSpawn s;
    s.name = name;
    try {
        s.position.x = std::stof(argv[++i]);
        s.position.y = std::stof(argv[++i]);
        s.position.z = std::stof(argv[++i]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "add-creature: bad coordinate (%s)\n", e.what());
        return 1;
    }
    // Optional displayId (positional, after coordinates).
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try {
            s.displayId = static_cast<uint32_t>(std::stoul(argv[++i]));
        } catch (...) { /* leave 0 → SQL exporter substitutes 11707 */ }
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try {
            s.level = static_cast<uint32_t>(std::stoul(argv[++i]));
        } catch (...) { /* leave default 1 */ }
    }
    // Load existing spawns (if any), append, save.
    wowee::editor::NpcSpawner spawner;
    std::string path = zoneDir + "/creatures.json";
    if (fs::exists(path)) spawner.loadFromFile(path);
    spawner.placeCreature(s);
    if (!spawner.saveToFile(path)) {
        std::fprintf(stderr, "add-creature: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Added creature '%s' to %s (now %zu total)\n",
                name.c_str(), path.c_str(), spawner.spawnCount());
    return 0;
}

int handleAddItem(int& i, int argc, char** argv) {
    // Append one item entry to <zoneDir>/items.json. Inline
    // JSON without a dedicated editor class - items.json is
    // a simple {"items": [...]} array of records, and the
    // schema is small enough that we don't need NpcSpawner-
    // style infrastructure yet.
    //
    // Schema per item:
    //   id (uint32) - Item.dbc primary key (auto-increments
    //                 from 1 if omitted)
    //   name (string)
    //   quality (uint8) - 0..6 (poor..artifact, default 1)
    //   displayId (uint32) - ItemDisplayInfo index (default 0)
    //   itemLevel (uint32) - default 1
    //   stackable (uint32) - max stack size (default 1)
    std::string zoneDir = argv[++i];
    std::string name = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr,
            "add-item: zone '%s' does not exist\n", zoneDir.c_str());
        return 1;
    }
    uint32_t id = 0, displayId = 0, itemLevel = 1;
    uint32_t quality = 1;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { id = static_cast<uint32_t>(std::stoul(argv[++i])); }
        catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { quality = static_cast<uint32_t>(std::stoul(argv[++i])); }
        catch (...) {}
        if (quality > 6) quality = 1;
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { displayId = static_cast<uint32_t>(std::stoul(argv[++i])); }
        catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { itemLevel = static_cast<uint32_t>(std::stoul(argv[++i])); }
        catch (...) {}
    }
    std::string path = zoneDir + "/items.json";
    nlohmann::json doc = nlohmann::json::object({{"items",
                                                  nlohmann::json::array()}});
    if (fs::exists(path)) {
        std::ifstream in(path);
        try { in >> doc; } catch (...) {
            std::fprintf(stderr,
                "add-item: %s exists but is not valid JSON\n",
                path.c_str());
            return 1;
        }
        if (!doc.contains("items") || !doc["items"].is_array()) {
            doc["items"] = nlohmann::json::array();
        }
    }
    // Auto-assign id if user passed 0 / nothing - pick the
    // smallest unused positive integer so the items.json
    // numbering stays contiguous.
    if (id == 0) {
        std::set<uint32_t> used;
        for (const auto& it : doc["items"]) {
            if (it.contains("id") && it["id"].is_number_unsigned()) {
                used.insert(it["id"].get<uint32_t>());
            }
        }
        id = 1;
        while (used.count(id)) ++id;
    }
    // Reject duplicate id so the user notices a collision.
    for (const auto& it : doc["items"]) {
        if (it.contains("id") && it["id"].is_number_unsigned() &&
            it["id"].get<uint32_t>() == id) {
            std::fprintf(stderr,
                "add-item: id %u already in use in %s\n",
                id, path.c_str());
            return 1;
        }
    }
    nlohmann::json item = {
        {"id", id},
        {"name", name},
        {"quality", quality},
        {"displayId", displayId},
        {"itemLevel", itemLevel},
        {"stackable", 1},
    };
    doc["items"].push_back(item);
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr,
            "add-item: failed to write %s\n", path.c_str());
        return 1;
    }
    out << doc.dump(2);
    out.close();
    static const char* qualityNames[] = {
        "poor", "common", "uncommon", "rare", "epic",
        "legendary", "artifact"
    };
    std::printf("Added item '%s' (id=%u, quality=%s, ilvl=%u) to %s (now %zu total)\n",
                name.c_str(), id,
                qualityNames[quality], itemLevel,
                path.c_str(), doc["items"].size());
    return 0;
}

}  // namespace

bool handleAdd(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--add-object") == 0 && i + 5 < argc) {
        outRc = handleAddObject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--add-creature") == 0 && i + 4 < argc) {
        outRc = handleAddCreature(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--add-item") == 0 && i + 2 < argc) {
        outRc = handleAddItem(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
