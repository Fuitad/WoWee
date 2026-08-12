#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

struct ZoneManifest {
    std::string mapName;
    std::string displayName;
    uint32_t mapId = 9000; // Custom map IDs start high to avoid conflicts
    std::vector<std::pair<int, int>> tiles; // (tileX, tileY) pairs
    std::string biome;
    float baseHeight = 100.0f;
    bool hasCreatures = false;
    std::string description;

    // Zone gameplay flags
    bool allowFlying = false;
    bool pvpEnabled = false;
    bool isIndoor = false;
    bool isSanctuary = false;

    // Audio configuration
    std::string musicTrack;         // Background music file path
    std::string ambienceDay;        // Daytime ambient sound
    std::string ambienceNight;      // Nighttime ambient sound
    float musicVolume = 0.7f;
    float ambienceVolume = 0.5f;

    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

/// The zone directories of a project, in a stable order.
///
/// What makes a directory a zone is that it holds a zone.json - twenty-six
/// handlers said so for themselves, in twenty-two files, and each also
/// remembered to sort the result so its report came out the same way twice.
/// A change to what counts as a zone would have been twenty-six edits, and a
/// handler that missed the sort would list zones in whatever order the
/// filesystem happened to hand them over, which differs between machines.
///
/// An unreadable or absent project directory answers empty rather than
/// throwing: every caller checks the directory itself first and reports its
/// own message.
std::vector<std::string> projectZoneDirs(const std::string& projectDir);

} // namespace editor
} // namespace wowee
