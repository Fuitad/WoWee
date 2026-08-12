#include "editor_project.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace wowee {
namespace editor {

bool EditorProject::save(const std::string& path) const {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());

    nlohmann::json j;
    j["format"] = "wowee-project-1.0";
    j["name"] = name;
    j["author"] = author;
    j["description"] = description;
    j["version"] = version;
    j["startMapId"] = startMapId;

    nlohmann::json zarr = nlohmann::json::array();
    for (const auto& z : zones) {
        zarr.push_back({{"mapName", z.mapName}, {"tileX", z.tileX},
                        {"tileY", z.tileY}, {"biome", z.biome},
                        {"description", z.description}});
    }
    j["zones"] = zarr;

    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2) << "\n";

    LOG_INFO("Project saved: ", path, " (", zones.size(), " zones)");
    return true;
}

bool EditorProject::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    try {
        nlohmann::json j = nlohmann::json::parse(f);

        name = j.value("name", "");
        author = j.value("author", "");
        description = j.value("description", "");
        version = j.value("version", "1.0.0");
        startMapId = j.value("startMapId", 9000u);
        projectDir = std::filesystem::path(path).parent_path().string();

        // Cap mapId to a reasonable range. mapId 0 is reserved for Eastern
        // Kingdoms; >65535 wraps DBC fields. Custom zones live in 9000+.
        if (startMapId == 0 || startMapId > 65535) startMapId = 9000;

        zones.clear();
        if (j.contains("zones") && j["zones"].is_array()) {
            // Cap project zones - same defense pattern as the other
            // editor JSON loaders. WoW only supports 65535 maps total
            // and the editor loads one tile at a time, so 1024 zones
            // per project is plenty.
            constexpr size_t kMaxZones = 1024;
            for (const auto& jz : j["zones"]) {
                if (zones.size() >= kMaxZones) {
                    LOG_WARNING("Project zone cap reached (", kMaxZones,
                                ") - remaining entries dropped");
                    break;
                }
                ProjectZone z;
                z.mapName = jz.value("mapName", "");
                z.tileX = jz.value("tileX", 32);
                z.tileY = jz.value("tileY", 32);
                z.biome = jz.value("biome", "");
                z.description = jz.value("description", "");
                // Tile coords valid 0..63 in WoW. Out-of-range values would
                // produce garbage ADT filenames or off-map terrain placement.
                if (z.tileX < 0 || z.tileX > 63) z.tileX = 32;
                if (z.tileY < 0 || z.tileY > 63) z.tileY = 32;
                if (!z.mapName.empty()) zones.push_back(z);
            }
        }

        LOG_INFO("Project loaded: ", path, " (", name, ", ", zones.size(), " zones)");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load project: ", e.what());
        return false;
    }
}

bool EditorProject::initGitRepo() const {
    if (projectDir.empty()) return false;
    int ret = std::system(("cd \"" + projectDir + "\" && git init && git add -A && "
                           "git commit -m \"Initial project commit\"").c_str());
    return ret == 0;
}

bool EditorProject::gitCommit(const std::string& message) const {
    if (projectDir.empty()) return false;
    // Sanitize commit message to prevent shell injection
    std::string safe;
    for (char c : message) {
        if (c == '\'' || c == '\\') safe += '\\';
        safe += c;
    }
    int ret = std::system(("cd \"" + projectDir + "\" && git add -A && "
                           "git commit -m '" + safe + "'").c_str());
    return ret == 0;
}

bool EditorProject::gitPush() const {
    if (projectDir.empty()) return false;
    return std::system(("cd \"" + projectDir + "\" && git push").c_str()) == 0;
}

bool EditorProject::gitPull() const {
    if (projectDir.empty()) return false;
    return std::system(("cd \"" + projectDir + "\" && git pull").c_str()) == 0;
}

std::string EditorProject::gitStatus() const {
    if (projectDir.empty()) return "No project directory";
    std::string cmd = "cd \"" + projectDir + "\" && git status --short 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "git not available";
    char buf[256];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    return result.empty() ? "Clean (no changes)" : result;
}

std::string EditorProject::getZoneOutputDir(int zoneIdx) const {
    if (zoneIdx < 0 || zoneIdx >= static_cast<int>(zones.size())) return "";
    return projectDir + "/" + zones[zoneIdx].mapName;
}

} // namespace editor
} // namespace wowee
