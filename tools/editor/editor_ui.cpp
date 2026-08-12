#include "editor_ui.hpp"
#include "cli_subprocess.hpp"
#include "editor_app.hpp"
#include "terrain_editor.hpp"
#include "texture_painter.hpp"
#include "object_placer.hpp"
#include "npc_spawner.hpp"
#include "npc_presets.hpp"
#include "quest_editor.hpp"
#include "pipeline/custom_zone_discovery.hpp"
#include "content_pack.hpp"
#include "sql_exporter.hpp"
#include "server_module_gen.hpp"
#include "wowee_terrain.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include <filesystem>
#include <random>
#include "asset_browser.hpp"
#include "transform_gizmo.hpp"
#include "terrain_biomes.hpp"
#include "rendering/terrain_renderer.hpp"
#include "rendering/camera.hpp"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>

namespace wowee {
namespace editor {

EditorUI::EditorUI() = default;

static bool matchesFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower.find(filter) != std::string::npos;
}

void EditorUI::render(EditorApp& app) {
    renderMenuBar(app);
    renderToolbar(app);
    if (showNewDialog_) renderNewTerrainDialog(app);
    if (showLoadDialog_) renderLoadDialog(app);
    if (showSaveDialog_) renderSaveDialog(app);

    switch (app.getMode()) {
        case EditorMode::Sculpt: renderBrushPanel(app); break;
        case EditorMode::Paint: renderTexturePaintPanel(app); break;
        case EditorMode::PlaceObject: renderObjectPanel(app); break;
        case EditorMode::Water: renderWaterPanel(app); break;
        case EditorMode::NPC: renderNpcPanel(app); break;
        case EditorMode::Quest: renderQuestPanel(app); break;
    }

    renderContextMenu(app);
    renderMinimap(app);
    renderPropertiesPanel(app);
    renderStatusBar(app);

    // Object/NPC name labels in viewport (screen-space text)
    if (app.hasTerrainLoaded() && app.getObjectPlacer().objectCount() > 0) {
        auto& cam0 = app.getEditorCamera().getCamera();
        auto vp0 = ImGui::GetMainViewport();
        glm::mat4 vp0m = cam0.getProjectionMatrix() * cam0.getViewMatrix();
        for (const auto& obj : app.getObjectPlacer().getObjects()) {
            if (!obj.selected) continue; // only show label for selected objects
            glm::vec4 clip = vp0m * glm::vec4(obj.position.x, obj.position.y, obj.position.z + 10.0f, 1.0f);
            if (clip.w <= 0.01f) continue;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            float sx = (ndc.x * 0.5f + 0.5f) * vp0->Size.x;
            float sy = (ndc.y * 0.5f + 0.5f) * vp0->Size.y;
            if (sx < 0 || sx > vp0->Size.x || sy < 0 || sy > vp0->Size.y) continue;
            std::string disp = obj.path;
            auto sl = disp.rfind('\\');
            if (sl != std::string::npos) disp = disp.substr(sl + 1);
            ImGui::GetForegroundDrawList()->AddText(ImVec2(sx - 40, sy - 10),
                IM_COL32(255, 220, 50, 220), disp.c_str());
        }
    }
    if (app.hasTerrainLoaded() && app.getNpcSpawner().spawnCount() > 0) {
        auto& cam = app.getEditorCamera().getCamera();
        auto vp2 = ImGui::GetMainViewport();
        glm::mat4 viewProj = cam.getProjectionMatrix() * cam.getViewMatrix();
        bool selectedOnly = app.getViewport().getNpcNameplatesSelectedOnly();
        int selIdx = app.getNpcSpawner().getSelectedIndex();
        const auto& spawns = app.getNpcSpawner().getSpawns();
        for (size_t i = 0; i < spawns.size(); ++i) {
            const auto& npc = spawns[i];
            // Toggle: when selectedOnly is on, skip every NPC except the
            // selected one. Was previously always-on, which got noisy on
            // populated zones.
            if (selectedOnly && static_cast<int>(i) != selIdx) continue;
            // Z offset: was 35 yards (way too high; nameplates floated in
            // the sky). Drop to ~3.5 yards which sits just above an
            // average creature's head and reads as attached.
            glm::vec4 clip = viewProj * glm::vec4(npc.position.x, npc.position.y,
                                                    npc.position.z + 3.5f, 1.0f);
            if (clip.w <= 0.01f) continue;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            float sx = (ndc.x * 0.5f + 0.5f) * vp2->Size.x;
            float sy = (ndc.y * 0.5f + 0.5f) * vp2->Size.y;
            if (sx < 0 || sx > vp2->Size.x || sy < 0 || sy > vp2->Size.y) continue;

            // Center the text on the projected position by measuring the
            // actual string width - the previous fixed -30 X offset
            // misaligned every name that wasn't ~6 chars long.
            ImVec2 textSz = ImGui::CalcTextSize(npc.name.c_str());
            ImVec4 col = npc.hostile ? ImVec4(1, 0.3f, 0.3f, 0.9f) : ImVec4(0.3f, 1, 0.3f, 0.9f);
            ImGui::GetForegroundDrawList()->AddText(
                ImVec2(sx - textSz.x * 0.5f, sy - textSz.y - 2),
                ImGui::ColorConvertFloat4ToU32(col),
                npc.name.c_str());
        }
    }

    // Toast notifications
    ImGuiViewport* tvp = ImGui::GetMainViewport();
    float toastY = tvp->Size.y - 60;
    for (const auto& t : app.getToasts()) {
        float alpha = std::min(1.0f, t.timer);
        ImGui::SetNextWindowPos(ImVec2(tvp->Size.x / 2 - 150, toastY));
        ImGui::SetNextWindowSize(ImVec2(300, 30));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.4f, 0.1f, 0.9f));
        char toastId[32]; std::snprintf(toastId, sizeof(toastId), "##toast%p", (void*)&t);
        ImGui::Begin(toastId, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("%s", t.msg.c_str());
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        toastY -= 35;
    }
}

void EditorUI::processActions(EditorApp& app) {
    if (newRequested_) {
        newRequested_ = false;
        app.createNewTerrain(newMapNameBuf_, newTileX_, newTileY_, newBaseHeight_,
                             static_cast<Biome>(newBiomeIdx_));
        if (generateAfterCreate_) {
            generateAfterCreate_ = false;
            app.generateCompleteZone();
        }
    }
    if (loadRequested_) {
        loadRequested_ = false;
        app.loadADT(loadMapNameBuf_, loadTileX_, loadTileY_);
    }
}

void EditorUI::setPathPoint(const glm::vec3& pos) {
    // Hard cap so a runaway click handler doesn't grow the polyline
    // unboundedly. 64 segments is more than enough for a tile-sized
    // river or road.
    constexpr size_t kMaxPathPoints = 64;
    if (pathPoints_.size() >= kMaxPathPoints) return;
    if (pathCapture_ == PathCapture::WaitingStart) {
        pathPoints_.clear();
        pathPoints_.push_back(pos);
        pathCapture_ = PathCapture::WaitingEnd;
    } else if (pathCapture_ == PathCapture::WaitingEnd) {
        pathPoints_.push_back(pos);
        // After the second point we stay in capture mode but switch to
        // WaitingMore so the user can either click "Apply" with the
        // 2-segment polyline or keep adding waypoints.
        pathCapture_ = PathCapture::WaitingMore;
    } else if (pathCapture_ == PathCapture::WaitingMore) {
        pathPoints_.push_back(pos);
    }
}

void EditorUI::renderMenuBar(EditorApp& app) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::BeginMenu("Project")) {
                static char projPathBuf[256] = "projects/MyExpansion/project.json";
                if (ImGui::MenuItem("New Project...")) {
                    app.getProject().name = "MyExpansion";
                    app.getProject().author = "Kelsi Davis";
                    app.getProject().zones.clear();
                    app.showToast("New project created");
                }
                if (ImGui::MenuItem("Save Project")) {
                    app.getProject().save(projPathBuf);
                    app.showToast("Project saved");
                }
                if (ImGui::MenuItem("Load Project")) {
                    if (app.getProject().load(projPathBuf))
                        app.showToast("Project loaded: " + app.getProject().name);
                    else
                        app.showToast("Failed to load project");
                }
                if (ImGui::MenuItem("Add Current Zone to Project") && app.hasTerrainLoaded()) {
                    ProjectZone pz;
                    pz.mapName = app.getLoadedMap();
                    pz.tileX = app.getLoadedTileX();
                    pz.tileY = app.getLoadedTileY();
                    app.getProject().zones.push_back(pz);
                    app.showToast("Zone added to project (" +
                                  std::to_string(app.getProject().zones.size()) + " zones)");
                }
                ImGui::Separator();
                ImGui::InputText("Path##proj", projPathBuf, sizeof(projPathBuf));
                if (!app.getProject().zones.empty() && ImGui::BeginMenu("Switch Zone")) {
                    for (int i = 0; i < static_cast<int>(app.getProject().zones.size()); i++) {
                        auto& z = app.getProject().zones[i];
                        char label[128];
                        std::snprintf(label, sizeof(label), "%s [%d,%d]",
                                      z.mapName.c_str(), z.tileX, z.tileY);
                        if (ImGui::MenuItem(label)) {
                            // Save current zone first, then load the selected one
                            app.quickSave();
                            app.loadADT(z.mapName, z.tileX, z.tileY);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Git (Collaboration)")) {
                    if (ImGui::MenuItem("Init Git Repo")) {
                        if (app.getProject().initGitRepo())
                            app.showToast("Git repo initialized");
                        else
                            app.showToast("Git init failed");
                    }
                    if (ImGui::MenuItem("Commit Changes")) {
                        app.quickSave();
                        if (app.getProject().gitCommit("Editor save"))
                            app.showToast("Changes committed");
                    }
                    if (ImGui::MenuItem("Push to Remote"))
                        app.getProject().gitPush() ? app.showToast("Pushed") : app.showToast("Push failed");
                    if (ImGui::MenuItem("Pull from Remote"))
                        app.getProject().gitPull() ? app.showToast("Pulled") : app.showToast("Pull failed");
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", app.getProject().gitStatus().c_str());
                    ImGui::EndMenu();
                }
                ImGui::Text("Project: %s (%zu zones)",
                            app.getProject().name.c_str(),
                            app.getProject().zones.size());
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New Terrain...", "Ctrl+N")) showNewDialog_ = true;
            if (ImGui::MenuItem("Load ADT...", "Ctrl+O")) showLoadDialog_ = true;
            if (ImGui::BeginMenu("Load Custom Zone")) {
                auto zones = pipeline::CustomZoneDiscovery::scan({"output", "custom_zones"});
                if (zones.empty()) {
                    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "No custom zones found");
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Export a zone first, or place .wcp in custom_zones/");
                } else {
                    for (const auto& z : zones) {
                        char label[128];
                        std::snprintf(label, sizeof(label), "%s%s",
                                      z.name.c_str(), z.hasQuests ? " [Q]" : "");
                        if (ImGui::MenuItem(label)) {
                            // Find first WOT file in this zone directory
                            for (auto& entry : std::filesystem::directory_iterator(z.directory)) {
                                if (entry.path().extension() == ".wot") {
                                    std::string base = entry.path().stem().string();
                                    // Parse tile coords from filename: MapName_X_Y
                                    auto lastU = base.rfind('_');
                                    auto prevU = base.rfind('_', lastU - 1);
                                    if (lastU != std::string::npos && prevU != std::string::npos) {
                                        int tx = 0, ty = 0;
                                        try {
                                            tx = std::stoi(base.substr(prevU + 1, lastU - prevU - 1));
                                            ty = std::stoi(base.substr(lastU + 1));
                                        } catch (...) { break; }
                                        app.createNewTerrain(z.name, tx, ty, 100.0f, Biome::Grassland);
                                        // Load the WOT/WHM data
                                        std::string wotBase = entry.path().parent_path().string() + "/" + base;
                                        pipeline::WoweeTerrainLoader::load(wotBase, *app.getTerrainEditor().getTerrain());
                                        app.showToast("Loaded custom zone: " + z.name);
                                    }
                                    break;
                                }
                            }
                        }
                        if (ImGui::IsItemHovered() && !z.description.empty())
                            ImGui::SetTooltip("%s\nBy: %s", z.description.c_str(), z.author.c_str());
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Recent Zones", !app.getRecentZones().empty())) {
                for (const auto& rz : app.getRecentZones()) {
                    char label[128];
                    std::snprintf(label, sizeof(label), "%s [%d, %d]",
                                  rz.mapName.c_str(), rz.tileX, rz.tileY);
                    if (ImGui::MenuItem(label))
                        app.loadADT(rz.mapName, rz.tileX, rz.tileY);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Content Packs (.wcp)")) {
                static char wcpImportPath[256] = "content.wcp";
                ImGui::InputText("Path##wcp", wcpImportPath, sizeof(wcpImportPath));
                if (ImGui::MenuItem("Import (unpack to custom_zones/)")) {
                    if (editor::ContentPacker::unpackZone(wcpImportPath, "custom_zones"))
                        app.showToast("Content pack imported");
                    else
                        app.showToast("Import failed - check path");
                }
                if (ImGui::MenuItem("Import & Load")) {
                    editor::ContentPackInfo info;
                    if (editor::ContentPacker::readInfo(wcpImportPath, info) &&
                        editor::ContentPacker::unpackZone(wcpImportPath, "custom_zones")) {
                        app.showToast("Imported: " + info.name);
                        auto zones = pipeline::CustomZoneDiscovery::scan({"custom_zones"});
                        for (const auto& z : zones) {
                            if (z.name == info.name && !z.tiles.empty()) {
                                app.loadADT(z.name, z.tiles[0].first, z.tiles[0].second);
                                break;
                            }
                        }
                    } else {
                        app.showToast("Import failed - check path");
                    }
                }
                if (ImGui::BeginMenu("Inspect Pack Info")) {
                    editor::ContentPackInfo info;
                    if (editor::ContentPacker::readInfo(wcpImportPath, info)) {
                        ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "%s v%s",
                            info.name.c_str(), info.version.c_str());
                        if (!info.author.empty())
                            ImGui::Text("By: %s", info.author.c_str());
                        if (!info.description.empty())
                            ImGui::TextWrapped("%s", info.description.c_str());
                        ImGui::Text("Map ID: %u, Files: %zu", info.mapId, info.files.size());
                        if (!info.files.empty()) {
                            ImGui::Separator();
                            int terrain = 0, models = 0, buildings = 0, textures = 0, data = 0;
                            uint64_t totalSize = 0;
                            for (const auto& fe : info.files) {
                                totalSize += fe.size;
                                if (fe.category == "terrain") terrain++;
                                else if (fe.category == "model") models++;
                                else if (fe.category == "building") buildings++;
                                else if (fe.category == "texture") textures++;
                                else if (fe.category == "data") data++;
                            }
                            ImGui::Text("Terrain: %d  Models: %d  Buildings: %d",
                                terrain, models, buildings);
                            ImGui::Text("Textures: %d  Data: %d  Total: %.1f KB",
                                textures, data, totalSize / 1024.0f);
                        }
                    } else {
                        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Cannot read pack");
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Import Heightmap", app.hasTerrainLoaded())) {
                static char hmPath[256] = "heightmap.png";
                static float hmScale = 200.0f;
                ImGui::InputText("File##hm", hmPath, sizeof(hmPath));
                ImGui::SliderFloat("Height Scale", &hmScale, 10.0f, 1000.0f);
                if (ImGui::MenuItem("Import Image (PNG/JPG/BMP/TGA)")) {
                    if (app.getTerrainEditor().importHeightmapImage(hmPath, hmScale))
                        app.showToast("Heightmap image imported (undoable)");
                    else
                        app.showToast("Failed - check image path and format");
                }
                if (ImGui::MenuItem("Import RAW (16/8-bit binary)")) {
                    if (app.getTerrainEditor().importHeightmap(hmPath, hmScale))
                        app.showToast("RAW heightmap imported (undoable)");
                    else
                        app.showToast("Failed - need 129x129 or 257x257 RAW");
                }
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                    "Supports any resolution PNG/JPG/BMP/TGA or RAW");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Generate Complete Zone", nullptr, false, app.hasTerrainLoaded()))
                app.generateCompleteZone();
            if (ImGui::BeginMenu("Random Populate", app.hasTerrainLoaded())) {
                static int rpCreatures = 20;
                static int rpObjects = 10;
                static int rpSeed = 42;
                ImGui::SliderInt("Creatures##rp", &rpCreatures, 0, 200);
                ImGui::SliderInt("Objects##rp", &rpObjects, 0, 200);
                ImGui::InputInt("Seed##rp", &rpSeed);
                if (rpSeed < 0) rpSeed = 0;
                if (ImGui::Button("Populate##rp", ImVec2(-1, 0))) {
                    app.randomPopulateZone(rpCreatures, rpObjects,
                                            static_cast<uint32_t>(rpSeed));
                }
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                    "Same seed → same population (reproducible)");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Snap All Spawns to Ground", nullptr, false,
                                  app.hasTerrainLoaded())) {
                app.snapAllSpawnsToGround();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Re-snap every creature + object's Z to actual terrain height.\n"
                    "Run after terrain edits to fix floating/buried spawns.");
            if (ImGui::MenuItem("Audit Spawns Against Terrain", nullptr, false,
                                  app.hasTerrainLoaded())) {
                int issues = app.auditSpawnsAgainstTerrain(5.0f);
                if (issues == 0)
                    app.showToast("Audit clean - every spawn within 5y of terrain");
                else
                    app.showToast(std::to_string(issues) +
                                   " spawn(s) more than 5y off terrain - try Snap All");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Count spawns whose Z is more than 5y off terrain.\n"
                    "Surfaces placement bugs without modifying anything.");
            if (ImGui::MenuItem("Clear All Objects/NPCs", nullptr, false, app.hasTerrainLoaded())) {
                if (app.getObjectPlacer().objectCount() > 0 || app.getNpcSpawner().spawnCount() > 0)
                    app.clearAllObjects();
                else
                    app.showToast("Nothing to clear");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quick Save", "Ctrl+S", false, app.hasTerrainLoaded()))
                app.quickSave();
            if (ImGui::MenuItem("Export Zone...", nullptr, false, app.hasTerrainLoaded()))
                showSaveDialog_ = true;
            if (ImGui::MenuItem("Export Open Format (.wot/.whm)", nullptr, false, app.hasTerrainLoaded()))
                app.exportOpenFormat("output");
            if (ImGui::MenuItem("Export Server SQL", nullptr, false,
                    app.getNpcSpawner().spawnCount() > 0 || app.getQuestEditor().questCount() > 0)) {
                std::string sqlPath = "output/" + app.getLoadedMap() + "/spawns.sql";
                editor::SQLExporter::exportAll(
                    app.getNpcSpawner().getSpawns(),
                    app.getQuestEditor().getQuests(),
                    sqlPath, app.getZoneManifest().mapId);
                app.showToast("SQL exported: " + sqlPath);
            }
            if (ImGui::MenuItem("Generate Server Module", nullptr, false, app.hasTerrainLoaded())) {
                editor::ServerModuleGenerator::generate(
                    app.getZoneManifest(),
                    app.getNpcSpawner().getSpawns(),
                    app.getQuestEditor().getQuests(),
                    "output");
                app.showToast("Server module: output/mod_wowee_" + app.getLoadedMap());
            }
            if (ImGui::MenuItem("Export Content Pack (.wcp)", "Ctrl+Shift+E", false, app.hasTerrainLoaded())) {
                std::string wcpPath = "output/" + app.getLoadedMap() + ".wcp";
                app.exportContentPack(wcpPath);
            }
            if (ImGui::BeginMenu("Validate Open Formats", app.hasTerrainLoaded())) {
                std::string zoneDir = "output/" + app.getLoadedMap();
                auto val = editor::ContentPacker::validateZone(zoneDir);
                int score = val.openFormatScore();
                ImVec4 scoreColor = score >= 5 ? ImVec4(0.3f, 1, 0.3f, 1) :
                                    score >= 3 ? ImVec4(1, 1, 0.3f, 1) :
                                                 ImVec4(1, 0.3f, 0.3f, 1);
                ImGui::TextColored(scoreColor, "Open Format Score: %d/7", score);
                ImGui::Separator();
                auto fmt = [](bool has, bool valid, const char* name, const char* desc) {
                    ImVec4 c = has ? (valid ? ImVec4(0.3f,1,0.3f,1) : ImVec4(1,0.7f,0.3f,1))
                                  : ImVec4(0.5f,0.5f,0.5f,1);
                    ImGui::TextColored(c, "%s %s - %s",
                        has ? (valid ? "[OK]" : "[!!]") : "[--]", name, desc);
                };
                fmt(val.hasWot, true, "WOT", "terrain metadata");
                fmt(val.hasWhm, val.whmValid, "WHM", "heightmap binary");
                fmt(val.hasZoneJson, true, "zone.json", "map definition");
                fmt(val.hasPng, true, "PNG", "textures");
                fmt(val.hasWom, val.womValid, "WOM", "models");
                fmt(val.hasWob, val.wobValid, "WOB", "buildings");
                fmt(val.hasWoc, val.wocValid, "WOC", "collision mesh");
                ImGui::Separator();
                fmt(val.hasCreatures, true, "creatures", "NPC spawns");
                fmt(val.hasQuests, true, "quests", "quest data");
                fmt(val.hasObjects, true, "objects", "placed objects");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Batch Convert Assets")) {
                static char batchDir[256] = "Data";
                ImGui::InputText("Data Directory", batchDir, sizeof(batchDir));
                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1),
                    "Recursively converts M2->WOM and WMO->WOB");
                if (ImGui::MenuItem("Convert All")) {
                    int n = app.batchConvertAssets(batchDir);
                    app.showToast("Converted " + std::to_string(n) + " assets to open format");
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add Adjacent Tile", app.hasTerrainLoaded())) {
                if (ImGui::MenuItem("North (+X)")) app.addAdjacentTile(1, 0);
                if (ImGui::MenuItem("South (-X)")) app.addAdjacentTile(-1, 0);
                if (ImGui::MenuItem("East (+Y)")) app.addAdjacentTile(0, 1);
                if (ImGui::MenuItem("West (-Y)")) app.addAdjacentTile(0, -1);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Export Heightmap", app.hasTerrainLoaded())) {
                static char expHmPath[256] = "output/heightmap.raw";
                static float expHmScale = 500.0f;
                ImGui::InputText("File##exphm", expHmPath, sizeof(expHmPath));
                ImGui::SliderFloat("Max Height##exphm", &expHmScale, 50.0f, 2000.0f);
                if (ImGui::MenuItem("Export 16-bit RAW (129x129)")) {
                    if (app.getTerrainEditor().exportHeightmap(expHmPath, expHmScale))
                        app.showToast("Heightmap exported");
                    else
                        app.showToast("Export failed");
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Export Zone Map", app.hasTerrainLoaded())) {
                static char mapPath[256] = "output/zone_map.png";
                static int mapRes = 512;
                ImGui::InputText("File##zonemap", mapPath, sizeof(mapPath));
                ImGui::SliderInt("Resolution", &mapRes, 128, 2048);
                if (ImGui::MenuItem("Export PNG")) {
                    if (editor::WoweeTerrain::exportZoneMap(
                            *app.getTerrainEditor().getTerrain(), mapPath, mapRes))
                        app.showToast("Zone map exported: " + std::string(mapPath));
                    else
                        app.showToast("Export failed");
                }
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                    "Top-down colored map with terrain, water, objects");
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) app.requestQuit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            auto& te = app.getTerrainEditor();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, te.history().canUndo())) te.undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, te.history().canRedo())) te.redo();
            ImGui::Separator();
            if (ImGui::BeginMenu("Auto-Save Settings")) {
                bool enabled = app.isAutoSaveEnabled();
                if (ImGui::Checkbox("Enabled", &enabled)) app.setAutoSaveEnabled(enabled);
                float interval = app.getAutoSaveInterval();
                if (ImGui::SliderFloat("Interval (sec)", &interval, 60.0f, 900.0f, "%.0fs"))
                    app.setAutoSaveInterval(interval);
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                    "Next in: %.0fs", app.getAutoSaveTimeRemaining());
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            bool wf = app.isWireframe();
            if (ImGui::MenuItem("Wireframe", "F3", &wf)) app.setWireframe(wf);
            bool fog = false;
            auto* tr = app.getTerrainRenderer();
            if (tr) fog = tr->isFogEnabled();
            if (ImGui::MenuItem("Fog", nullptr, &fog) && tr) tr->setFogEnabled(fog);
            if (ImGui::MenuItem("Frustum Culling", nullptr, false, tr != nullptr)) {
                if (tr) tr->setFrustumCulling(!true); // toggle would need state
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Camera")) app.resetCamera();
            if (ImGui::MenuItem("Center on Terrain", "Home")) app.centerOnTerrain();
            ImGui::Separator();
            if (ImGui::BeginMenu("Sky / Lighting")) {
                auto& vp = app.getViewport();
                float tod = vp.getTimeOfDay();
                if (ImGui::SliderFloat("Time of Day", &tod, 0.0f, 24.0f, "%.1fh"))
                    vp.setTimeOfDay(tod);
                ImGui::Separator();
                if (ImGui::MenuItem("Dawn (6:30)")) vp.setTimeOfDay(6.5f);
                if (ImGui::MenuItem("Noon (12:00)")) vp.setTimeOfDay(12.0f);
                if (ImGui::MenuItem("Dusk (18:00)")) vp.setTimeOfDay(18.0f);
                if (ImGui::MenuItem("Night (22:00)")) vp.setTimeOfDay(22.0f);
                ImGui::Separator();
                ImGui::ColorEdit3("Light", &vp.getLightColor().x, ImGuiColorEditFlags_Float);
                ImGui::ColorEdit3("Ambient", &vp.getAmbientColor().x, ImGuiColorEditFlags_Float);
                ImGui::ColorEdit3("Fog", &vp.getFogColor().x, ImGuiColorEditFlags_Float);
                ImGui::SliderFloat("Fog Near", &vp.getFogNear(), 100.0f, 10000.0f);
                ImGui::SliderFloat("Fog Far", &vp.getFogFar(), 500.0f, 20000.0f);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Bookmark", "F5")) app.saveBookmark("");
            auto& bmarks = app.getBookmarks();
            if (!bmarks.empty() && ImGui::BeginMenu("Load Bookmark")) {
                for (int i = 0; i < static_cast<int>(bmarks.size()); i++) {
                    if (ImGui::MenuItem(bmarks[i].name.c_str()))
                        app.loadBookmark(i);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Keyboard Shortcuts", "F1")) showHelp_ = !showHelp_;
            ImGui::Separator();
            if (ImGui::MenuItem("About Wowee Editor")) showAbout_ = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // About dialog (must be outside menu bar scope)
    if (showAbout_) {
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("About Wowee World Editor", &showAbout_)) {
            ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Wowee World Editor v1.0.0");
            ImGui::Text("by Kelsi Davis");
            ImGui::Separator();
            ImGui::Text("Standalone world editor for creating custom");
            ImGui::Text("WoW zones with novel open format exports.");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "Features:");
            ImGui::BulletText("6 editing modes (Sculpt/Paint/Objects/Water/NPCs/Quests)");
            ImGui::BulletText("30+ terrain tools with procedural generators");
            ImGui::BulletText("Quest chains with circular reference detection");
            ImGui::BulletText("631 creature presets across 8 categories");
            ImGui::BulletText("Full undo/redo for terrain + texture painting");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1), "Open Format Replacements (7/7):");
            ImGui::BulletText("ADT -> WOT/WHM (terrain + heightmap)");
            ImGui::BulletText("WDT -> zone.json (map definition)");
            ImGui::BulletText("BLP -> PNG (textures)");
            ImGui::BulletText("DBC -> JSON (data tables)");
            ImGui::BulletText("M2  -> WOM (models)");
            ImGui::BulletText("WMO -> WOB (buildings)");
            ImGui::BulletText("Col -> WOC (collision/walkability)");
            ImGui::Separator();
            ImGui::Text("Built with SDL2 / Vulkan / ImGui / nlohmann-json");
        }
        ImGui::End();
    }

    // Help overlay
    if (showHelp_) {
        ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Keyboard Shortcuts", &showHelp_)) {
            ImGui::Text("Navigation:");
            ImGui::BulletText("WASD - fly camera");
            ImGui::BulletText("Q/E - descend/ascend");
            ImGui::BulletText("Right-drag - look around");
            ImGui::BulletText("Scroll - adjust camera speed");
            ImGui::BulletText("Shift - sprint");
            ImGui::Separator();
            ImGui::Text("Editing:");
            ImGui::BulletText("Left-click - paint/place (depends on mode)");
            ImGui::BulletText("[ / ] - decrease / increase brush size");
            ImGui::BulletText("Ctrl+click - select object/NPC");
            ImGui::BulletText("Ctrl+S - quick save");
            ImGui::BulletText("Ctrl+Z - undo");
            ImGui::BulletText("Ctrl+Shift+Z / Ctrl+Y - redo");
            ImGui::BulletText("Delete - remove selected");
            ImGui::Separator();
            ImGui::Text("Object Transform:");
            ImGui::BulletText("G - move mode (then drag)");
            ImGui::BulletText("R - rotate mode (then drag)");
            ImGui::BulletText("T - scale mode (then drag)");
            ImGui::BulletText("X/Y/Z - constrain to axis");
            ImGui::BulletText("Escape - deselect / cancel");
            ImGui::BulletText("Right-click - context menu");
            ImGui::Separator();
            ImGui::Text("View:");
            ImGui::Text("Modes:");
            ImGui::BulletText("1 - Sculpt");
            ImGui::BulletText("2 - Paint");
            ImGui::BulletText("3 - Objects");
            ImGui::BulletText("4 - Water");
            ImGui::BulletText("5 - NPCs");
            ImGui::BulletText("6 - Quests");
            ImGui::Separator();
            ImGui::Text("Quick Actions:");
            ImGui::BulletText("Ctrl+N - new terrain");
            ImGui::BulletText("Ctrl+O - load map tile");
            ImGui::BulletText("Ctrl+A - select all objects");
            ImGui::BulletText("Ctrl+D - duplicate selected (objects + NPCs)");
            ImGui::BulletText("Ctrl+Wheel - rotate placement preview (Shift = fine)");
            ImGui::BulletText("Alt+Click - eyedropper (paint mode)");
            ImGui::BulletText("Ctrl+Shift+Click - add to selection");
            ImGui::BulletText("Middle-drag - orbit camera");
            ImGui::Separator();
            ImGui::Text("NPC Mode:");
            ImGui::BulletText("Click NPC marker - select (Shift+click forces placement)");
            ImGui::BulletText("W - add patrol waypoint at cursor (selected NPC must use Patrol behavior)");
            ImGui::Separator();
            ImGui::Text("View:");
            ImGui::BulletText("F1 - this help");
            ImGui::BulletText("F3 - wireframe toggle");
            ImGui::BulletText("F5 - save camera bookmark");
            ImGui::BulletText("Home - center on terrain");
            ImGui::BulletText("Scroll - zoom in/out");
            ImGui::BulletText("Shift+Scroll - adjust speed");
            ImGui::Separator();
            ImGui::Text("Object Tools:");
            ImGui::BulletText("Snap Ground - drop to terrain surface");
            ImGui::BulletText("Align Slope - rotate to terrain normal");
            ImGui::BulletText("Flatten Ground - level terrain around object");
            ImGui::BulletText("Scatter - mass-place with auto-align option");
            ImGui::BulletText("Select by Type - M2 models or WMO buildings");
            ImGui::Separator();
            ImGui::Text("Terrain Tools:");
            ImGui::BulletText("Generators: Hill/Mesa/Crater/Canyon/Island/Ridge/Dunes");
            ImGui::BulletText("River/Road: click start → click end → Apply");
            ImGui::BulletText("Stamp: Copy → Save/Load → Paste (cross-zone)");
            ImGui::BulletText("Mirror X/Y, Rotate 90, Scale/Offset heights");
            ImGui::BulletText("Auto-paint by height/slope, scatter patches");
            ImGui::Separator();
            ImGui::Text("Export:");
            ImGui::BulletText("Ctrl+S - quick save (all formats + collision)");
            ImGui::BulletText("Ctrl+Shift+E - export content pack (.wcp)");
            ImGui::BulletText("File → Batch Convert Assets (M2→WOM, WMO→WOB)");
            ImGui::BulletText("File → Generate Server Module (AzerothCore SQL)");
            ImGui::BulletText("Minimap: toggle slope overlay for collision preview");
        }
        ImGui::End();
    }
}

void EditorUI::renderToolbar(EditorApp& app) {
    ImGui::SetNextWindowPos(ImVec2(300, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 50), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        auto mode = app.getMode();

        auto modeButton = [&](const char* label, EditorMode m) {
            bool active = (mode == m);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }
            if (ImGui::Button(label, ImVec2(70, 28)))
                app.setMode(m);
            if (active)
                ImGui::PopStyleColor(2);
            ImGui::SameLine();
        };

        modeButton("Sculpt", EditorMode::Sculpt);
        modeButton("Paint", EditorMode::Paint);
        modeButton("Objects", EditorMode::PlaceObject);
        modeButton("Water", EditorMode::Water);
        modeButton("NPCs", EditorMode::NPC);
        modeButton("Quests", EditorMode::Quest);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorUI::renderNewTerrainDialog(EditorApp& /*app*/) {
    ImGui::SetNextWindowSize(ImVec2(400, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("New Terrain", &showNewDialog_)) {
        ImGui::InputText("Map Name", newMapNameBuf_, sizeof(newMapNameBuf_));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
            "Internal name (no spaces). Used for file paths.");
        ImGui::InputInt("Tile X", &newTileX_);
        ImGui::InputInt("Tile Y", &newTileY_);
        ImGui::SliderFloat("Base Height", &newBaseHeight_, 0.0f, 500.0f);
        newTileX_ = std::max(0, std::min(63, newTileX_));
        newTileY_ = std::max(0, std::min(63, newTileY_));

        ImGui::Separator();
        const char* biomeNames[] = {
            "Grassland", "Forest", "Jungle", "Desert", "Barrens",
            "Snow", "Swamp", "Rocky", "Beach", "Volcanic"
        };
        ImGui::Combo("Biome", &newBiomeIdx_, biomeNames, 10);
        const auto& bt = getBiomeTextures(static_cast<Biome>(newBiomeIdx_));
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "Base: %s", bt.base);
        ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.5f, 1.0f), "  + %s", bt.secondary);
        ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.5f, 1.0f), "  + %s", bt.accent);
        ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.5f, 1.0f), "  + %s", bt.detail);

        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(80, 0))) { newRequested_ = true; showNewDialog_ = false; }
        ImGui::SameLine();
        if (ImGui::Button("Create + Generate", ImVec2(130, 0))) {
            newRequested_ = true;
            generateAfterCreate_ = true;
            showNewDialog_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) showNewDialog_ = false;
    }
    ImGui::End();
}

void EditorUI::renderLoadDialog(EditorApp& app) {
    ImGui::SetNextWindowSize(ImVec2(400, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Map Tile", &showLoadDialog_)) {
        // Map browser
        auto& maps = app.getAssetBrowser().getMapNames();
        static char mapFilter[64] = "";
        ImGui::InputText("Search Maps", mapFilter, sizeof(mapFilter));
        std::string filter(mapFilter);
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        ImGui::BeginChild("MapList", ImVec2(0, 180), true);
        for (const auto& m : maps) {
            if (!filter.empty() && m.find(filter) == std::string::npos) continue;
            bool selected = (m == std::string(loadMapNameBuf_));
            if (ImGui::Selectable(m.c_str(), selected)) {
                std::strncpy(loadMapNameBuf_, m.c_str(), sizeof(loadMapNameBuf_) - 1);
                // Auto-find first valid tile for this map
                std::string ml = m;
                bool found = false;
                for (int x = 25; x < 45 && !found; x++)
                    for (int y = 25; y < 45 && !found; y++) {
                        std::string tp = "world\\maps\\" + ml + "\\" + ml + "_" +
                                         std::to_string(x) + "_" + std::to_string(y) + ".adt";
                        if (app.getAssetManager()->getManifest().hasEntry(tp))
                            { loadTileX_ = x; loadTileY_ = y; found = true; }
                    }
                if (!found)
                    for (int x = 0; x < 64 && !found; x++)
                        for (int y = 0; y < 64 && !found; y++) {
                            std::string tp = "world\\maps\\" + ml + "\\" + ml + "_" +
                                             std::to_string(x) + "_" + std::to_string(y) + ".adt";
                            if (app.getAssetManager()->getManifest().hasEntry(tp))
                                { loadTileX_ = x; loadTileY_ = y; found = true; }
                        }
            }
        }
        ImGui::EndChild();

        ImGui::Text("Selected: %s", loadMapNameBuf_);
        ImGui::InputInt("Tile X", &loadTileX_);
        ImGui::InputInt("Tile Y", &loadTileY_);
        loadTileX_ = std::max(0, std::min(63, loadTileX_));
        loadTileY_ = std::max(0, std::min(63, loadTileY_));

        // Check if the selected tile exists
        {
            std::string mapLower(loadMapNameBuf_);
            std::transform(mapLower.begin(), mapLower.end(), mapLower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            std::string testPath = "world\\maps\\" + mapLower + "\\" + mapLower + "_" +
                                   std::to_string(loadTileX_) + "_" + std::to_string(loadTileY_) + ".adt";
            bool exists = app.getAssetManager()->getManifest().hasEntry(testPath);
            if (exists)
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1), "Tile found");
            else
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1), "Tile not found");

            ImGui::SameLine();
            if (ImGui::SmallButton("Find Tile")) {
                bool found = false;
                // Scan center range first (open world maps)
                for (int x = 25; x < 45 && !found; x++) {
                    for (int y = 25; y < 45 && !found; y++) {
                        std::string tp = "world\\maps\\" + mapLower + "\\" + mapLower + "_" +
                                         std::to_string(x) + "_" + std::to_string(y) + ".adt";
                        if (app.getAssetManager()->getManifest().hasEntry(tp)) {
                            loadTileX_ = x; loadTileY_ = y; found = true;
                        }
                    }
                }
                // Full scan for dungeons/instances
                if (!found) {
                    for (int x = 0; x < 64 && !found; x++) {
                        for (int y = 0; y < 64 && !found; y++) {
                            std::string tp = "world\\maps\\" + mapLower + "\\" + mapLower + "_" +
                                             std::to_string(x) + "_" + std::to_string(y) + ".adt";
                            if (app.getAssetManager()->getManifest().hasEntry(tp)) {
                                loadTileX_ = x; loadTileY_ = y; found = true;
                            }
                        }
                    }
                }
                if (!found) {
                    // Check if it's a WMO-only instance
                    std::string wdtPath = "world\\maps\\" + mapLower + "\\" + mapLower + ".wdt";
                    if (app.getAssetManager()->getManifest().hasEntry(wdtPath))
                        app.showToast("WMO-only instance - click Load to open");
                    else
                        app.showToast("No ADT tiles found for this map");
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-find first available tile");

            // Count total tiles for this map
            ImGui::SameLine();
            if (ImGui::SmallButton("Count")) {
                int total = 0;
                for (int x = 0; x < 64; x++) {
                    for (int y = 0; y < 64; y++) {
                        std::string tp = "world\\maps\\" + mapLower + "\\" + mapLower + "_" +
                                         std::to_string(x) + "_" + std::to_string(y) + ".adt";
                        if (app.getAssetManager()->getManifest().hasEntry(tp)) total++;
                    }
                }
                app.showToast(mapLower + ": " + std::to_string(total) + " tiles");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Count total ADT tiles for this map");
        }

        ImGui::Spacing();
        if (ImGui::Button("Load", ImVec2(120, 0))) { loadRequested_ = true; showLoadDialog_ = false; }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) showLoadDialog_ = false;
    }
    ImGui::End();
}

void EditorUI::renderSaveDialog(EditorApp& app) {
    ImGui::SetNextWindowSize(ImVec2(500, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Export Zone", &showSaveDialog_)) {
        if (savePathBuf_[0] == '\0' && app.hasTerrainLoaded())
            std::snprintf(savePathBuf_, sizeof(savePathBuf_), "output");
        ImGui::InputText("Output Directory", savePathBuf_, sizeof(savePathBuf_));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
            "Exports: ADT+WDT, WOT+WHM, WOM, WOB, PNG, JSON");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
            "Output: %s/%s/", savePathBuf_, app.getLoadedMap().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Export All", ImVec2(140, 0))) {
            app.exportZone(savePathBuf_);
            showSaveDialog_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) showSaveDialog_ = false;
    }
    ImGui::End();
}

void EditorUI::renderBrushPanel(EditorApp& app) {
    ImGui::SetNextWindowPos(ImVec2(10, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sculpt")) {
        if (!app.hasTerrainLoaded()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Load or create terrain first");
            ImGui::End(); return;
        }
        // Show cursor info at top of panel
        auto& cursorBrush = app.getTerrainEditor().brush();
        auto cp = cursorBrush.getPosition();
        ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f),
            "Cursor: %.0f, %.0f  H: %.1f", cp.x, cp.y, cp.z);
        ImGui::Separator();
        auto& s = app.getTerrainEditor().brush().settings();
        const char* modes[] = {"Raise", "Lower", "Smooth", "Flatten", "Level", "Erode"};
        int idx = static_cast<int>(s.mode);
        if (ImGui::Combo("Mode", &idx, modes, 6)) s.mode = static_cast<BrushMode>(idx);
        if (ImGui::IsItemHovered()) {
            const char* tips[] = {
                "Raise: lift terrain up",
                "Lower: push terrain down",
                "Smooth: average neighbor heights",
                "Flatten: set to target height",
                "Level: same as flatten",
                "Erode: simulate water erosion downhill"
            };
            ImGui::SetTooltip("%s", tips[idx]);
        }
        ImGui::SliderFloat("Radius", &s.radius, 5.0f, 200.0f, "%.0f");
        if (ImGui::SmallButton("S##br")) { s.radius = 15.0f; } ImGui::SameLine();
        if (ImGui::SmallButton("M##br")) { s.radius = 50.0f; } ImGui::SameLine();
        if (ImGui::SmallButton("L##br")) { s.radius = 100.0f; } ImGui::SameLine();
        if (ImGui::SmallButton("XL##br")) { s.radius = 180.0f; }
        ImGui::SliderFloat("Strength", &s.strength, 0.5f, 50.0f, "%.1f");
        ImGui::SliderFloat("Falloff", &s.falloff, 0.0f, 1.0f, "%.2f");
        if (s.mode == BrushMode::Flatten || s.mode == BrushMode::Level) {
            ImGui::SliderFloat("Target Height", &s.flattenHeight, -500.0f, 1000.0f, "%.1f");
            ImGui::SameLine();
            auto& brush = app.getTerrainEditor().brush();
            if (ImGui::SmallButton("Pick") )
                s.flattenHeight = brush.getPosition().z;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Set target height from cursor position");
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Noise Generator")) {
            static float noiseFreq = 0.005f;
            static float noiseAmp = 20.0f;
            static int noiseOctaves = 4;
            static int noiseSeed = 42;
            ImGui::SliderFloat("Frequency", &noiseFreq, 0.001f, 0.05f, "%.4f");
            ImGui::SliderFloat("Amplitude", &noiseAmp, 1.0f, 200.0f, "%.0f");
            ImGui::SliderInt("Octaves", &noiseOctaves, 1, 8);
            ImGui::InputInt("Seed", &noiseSeed);
            ImGui::SameLine();
            if (ImGui::SmallButton("Rnd")) noiseSeed = static_cast<int>(std::rand());
            if (ImGui::SmallButton("<<")) noiseSeed = std::max(0, noiseSeed - 1);
            ImGui::SameLine();
            // Clamp ++ to INT_MAX-1 so signed overflow on the increment is impossible.
            if (ImGui::SmallButton(">>")) {
                noiseSeed = (noiseSeed < std::numeric_limits<int>::max() - 1)
                                ? noiseSeed + 1
                                : std::numeric_limits<int>::max();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Randomize All")) {
                noiseSeed = static_cast<int>(std::rand());
                app.getTerrainEditor().resetToFlat();
                app.getTerrainEditor().applyNoise(noiseFreq, noiseAmp, noiseOctaves,
                                                   static_cast<uint32_t>(noiseSeed));
                app.getTerrainEditor().smoothEntireTile(2);
                app.showToast("Randomized (seed " + std::to_string(noiseSeed) + ")");
            }
            static int noiseType = 0;
            ImGui::RadioButton("Value##nt", &noiseType, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Voronoi##nt", &noiseType, 1);

            if (noiseType == 1) {
                static int voronoiCells = 20;
                ImGui::SliderInt("Cells##vor", &voronoiCells, 5, 100);
                if (ImGui::Button("Apply Voronoi", ImVec2(-1, 0))) {
                    app.getTerrainEditor().applyVoronoiNoise(voronoiCells, noiseAmp,
                                                              static_cast<uint32_t>(noiseSeed));
                    app.showToast("Voronoi noise applied");
                }
            }

            if (noiseType == 0 && ImGui::Button("Apply Noise", ImVec2(140, 0))) {
                app.getTerrainEditor().applyNoise(noiseFreq, noiseAmp, noiseOctaves,
                                                   static_cast<uint32_t>(noiseSeed));
                app.showToast("Noise applied");
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset + Apply", ImVec2(120, 0))) {
                app.getTerrainEditor().resetToFlat();
                app.getTerrainEditor().applyNoise(noiseFreq, noiseAmp, noiseOctaves,
                                                   static_cast<uint32_t>(noiseSeed));
                app.showToast("Reset + noise applied");
            }
            static int smoothPasses = 2;
            ImGui::SliderInt("Smooth Passes", &smoothPasses, 1, 10);
            if (ImGui::Button("Smooth Entire Tile", ImVec2(-1, 0))) {
                app.getTerrainEditor().smoothEntireTile(smoothPasses);
                app.showToast("Tile smoothed");
            }
            ImGui::Separator();
            static float clampMin = 0.0f, clampMax = 500.0f;
            ImGui::DragFloatRange2("Clamp Range", &clampMin, &clampMax, 1.0f, -500.0f, 2000.0f);
            if (ImGui::Button("Clamp Heights", ImVec2(-1, 0))) {
                app.getTerrainEditor().clampHeights(clampMin, clampMax);
                app.showToast("Heights clamped");
            }
            ImGui::Separator();
            static float hScale = 1.5f;
            ImGui::SliderFloat("Height Scale", &hScale, 0.1f, 5.0f, "%.2f");
            if (ImGui::Button("Scale Heights", ImVec2(-1, 0))) {
                app.getTerrainEditor().scaleHeights(hScale);
                app.showToast("Heights scaled");
            }
            if (ImGui::Button("Reset to Flat", ImVec2(130, 0))) {
                app.getTerrainEditor().resetToFlat();
                app.showToast("Terrain reset to flat");
            }
            ImGui::SameLine();
            if (ImGui::Button("Invert", ImVec2(130, 0))) {
                app.getTerrainEditor().invertHeights();
                app.showToast("Heights inverted");
            }
            static float offsetAmt = 10.0f;
            ImGui::SliderFloat("Offset##ht", &offsetAmt, -100.0f, 100.0f, "%.0f");
            ImGui::SameLine();
            if (ImGui::SmallButton("Apply##off")) {
                app.getTerrainEditor().offsetHeights(offsetAmt);
                app.showToast("Heights offset");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Scale: exaggerate (>1) or flatten (<1) relief");
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("River / Road Carver")) {
            ImGui::RadioButton("River (carve down)", &pathMode_, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Road (flatten)", &pathMode_, 1);
            ImGui::SliderFloat("Width##path", &pathWidth_, 2.0f, 50.0f);
            if (pathMode_ == 0) ImGui::SliderFloat("Depth##path", &pathDepth_, 1.0f, 30.0f);

            // "Painting roads to/from models" - append the position of
            // the currently-selected object or NPC as a path waypoint.
            // Common workflow: place inn + town hall, select inn → click
            // append, select town hall → click append, click Apply Path.
            // The path then runs between them as a road.
            const auto* selObj = app.getObjectPlacer().getSelected();
            const auto* selNpc = app.getNpcSpawner().getSelected();
            const char* srcLabel = selObj ? "object" :
                                    selNpc ? "NPC" : nullptr;
            glm::vec3 srcPos(0);
            if (selObj) srcPos = selObj->position;
            else if (selNpc) srcPos = selNpc->position;
            if (srcLabel) {
                std::string btn = std::string("Append selected ") + srcLabel +
                                   " as path point";
                if (ImGui::Button(btn.c_str(), ImVec2(-1, 0))) {
                    // setPathPoint only acts when capture is in one of
                    // its waiting states. If we're idle, kick to
                    // WaitingStart for the first append; subsequent
                    // appends progress through Waiting* automatically.
                    if (pathCapture_ == PathCapture::None) {
                        pathCapture_ = pathPoints_.empty()
                                       ? PathCapture::WaitingStart
                                       : PathCapture::WaitingMore;
                    }
                    setPathPoint(srcPos);
                    app.showToast("Path point added at selected " +
                                   std::string(srcLabel));
                }
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                    "Select an object or NPC to append its position as a path point.");
            }

            if (pathCapture_ == PathCapture::None && pathPoints_.empty()) {
                if (ImGui::Button("Click Start Point", ImVec2(-1, 0))) {
                    pathCapture_ = PathCapture::WaitingStart;
                    app.showToast("Click terrain to set start point");
                }
            } else if (pathCapture_ == PathCapture::WaitingStart) {
                ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "Click terrain for START point...");
                if (ImGui::SmallButton("Cancel##path")) {
                    pathCapture_ = PathCapture::None;
                }
            } else if (pathCapture_ == PathCapture::WaitingEnd) {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
                                    "Start at (%.0f, %.0f) - click for next point",
                                    pathPoints_[0].x, pathPoints_[0].y);
                if (ImGui::SmallButton("Cancel##path")) {
                    clearPath();
                }
            } else if (pathCapture_ == PathCapture::WaitingMore || isPathReady()) {
                // Multi-point: show running count and let the user keep
                // clicking to add waypoints, or hit Apply with the current
                // polyline. The Apply branch iterates each segment.
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1),
                                    "%zu point(s) captured - click for more, or Apply",
                                    pathPoints_.size());
                if (ImGui::Button("Apply Path", ImVec2(-1, 0))) {
                    int segCount = 0;
                    for (size_t k = 0; k + 1 < pathPoints_.size(); ++k) {
                        const glm::vec3& a = pathPoints_[k];
                        const glm::vec3& b = pathPoints_[k + 1];
                        if (pathMode_ == 0) {
                            app.getTerrainEditor().carveRiver(a, b, pathWidth_, pathDepth_);
                            app.getTexturePainter().paintAlongPath(a, b,
                                pathWidth_ * 1.5f,
                                "Tileset\\Ashenvale\\AshenvaleSand.blp");
                            app.getTerrainEditor().fillWaterAlongPath(a, b,
                                pathWidth_, 0);
                        } else {
                            app.getTerrainEditor().flattenRoad(a, b, pathWidth_);
                            app.getTexturePainter().paintAlongPath(a, b,
                                pathWidth_,
                                "Tileset\\Elwynn\\ElwynnCobblestoneBase.blp");
                        }
                        segCount++;
                    }
                    if (pathMode_ == 0)
                        app.showToast("River applied across " +
                                       std::to_string(segCount) + " segment(s)");
                    else
                        app.showToast("Road applied across " +
                                       std::to_string(segCount) + " segment(s)");
                    clearPath();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset##path")) clearPath();
            } else if (!pathPoints_.empty()) {
                // Captured at least one point but the user dismissed the
                // capture mode without setting more - let them resume.
                if (ImGui::Button("Click Next Point", ImVec2(-1, 0)))
                    pathCapture_ = PathCapture::WaitingMore;
            }
        }

        if (ImGui::CollapsingHeader("Mirror / Rotate")) {
            if (ImGui::Button("Rotate 90 CW", ImVec2(-1, 0))) {
                app.getTerrainEditor().rotateTerrain90();
                app.showToast("Terrain rotated 90 degrees");
            }
            if (ImGui::Button("Mirror X (Left<>Right)", ImVec2(-1, 0))) {
                app.getTerrainEditor().mirrorX();
                app.showToast("Terrain mirrored X");
            }
            if (ImGui::Button("Mirror Y (Top<>Bottom)", ImVec2(-1, 0))) {
                app.getTerrainEditor().mirrorY();
                app.showToast("Terrain mirrored Y");
            }
        }

        if (ImGui::CollapsingHeader("Stamp / Clone")) {
            auto& brush2 = app.getTerrainEditor().brush();
            if (ImGui::Button("Copy Stamp", ImVec2(120, 0)) )
                app.getTerrainEditor().copyStamp(brush2.getPosition(), s.radius);
            ImGui::SameLine();
            if (ImGui::Button("Paste Stamp", ImVec2(120, 0))  &&
                app.getTerrainEditor().hasStamp())
                app.getTerrainEditor().pasteStamp(brush2.getPosition());
            if (app.getTerrainEditor().hasStamp()) {
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "Stamp ready");
                ImGui::SameLine();
                if (ImGui::SmallButton("Save##stamp"))
                    if (app.getTerrainEditor().saveStamp("output/stamps/terrain_stamp.json"))
                        app.showToast("Stamp saved");
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "No stamp copied");
            }
            if (ImGui::SmallButton("Load##stamp"))
                if (app.getTerrainEditor().loadStamp("output/stamps/terrain_stamp.json"))
                    app.showToast("Stamp loaded");
        }

        if (ImGui::CollapsingHeader("Detail Noise")) {
            static float detailAmp = 2.0f, detailFreq = 0.1f;
            static int detailSeed = 99;
            ImGui::SliderFloat("Amplitude##detail", &detailAmp, 0.5f, 10.0f);
            ImGui::SliderFloat("Frequency##detail", &detailFreq, 0.01f, 0.5f, "%.3f");
            ImGui::InputInt("Seed##detail", &detailSeed);
            if (ImGui::Button("Add Detail", ImVec2(-1, 0))) {
                app.getTerrainEditor().addDetailNoise(detailAmp, detailFreq,
                                                       static_cast<uint32_t>(detailSeed));
                app.showToast("Detail noise added");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Adds small-scale roughness to existing terrain");
        }

        if (ImGui::CollapsingHeader("Edge Ramp (Multi-tile)")) {
            static float rampTarget = 100.0f, rampWidth = 20.0f;
            ImGui::SliderFloat("Target Height##ramp", &rampTarget, 0.0f, 500.0f);
            ImGui::SliderFloat("Ramp Width##ramp", &rampWidth, 5.0f, 60.0f);
            if (ImGui::Button("Ramp Tile Edges", ImVec2(-1, 0))) {
                app.getTerrainEditor().rampEdges(rampTarget, rampWidth);
                app.showToast("Edges ramped");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Smoothly transitions tile borders to target height\nfor seamless multi-tile connections");
        }

        if (ImGui::CollapsingHeader("Thermal Erosion")) {
            static int erosionIters = 10;
            static float talusAngle = 40.0f;
            ImGui::SliderInt("Iterations##therm", &erosionIters, 1, 50);
            ImGui::SliderFloat("Talus Angle##therm", &talusAngle, 10.0f, 80.0f, "%.0f deg");
            if (ImGui::Button("Apply Thermal Erosion", ImVec2(-1, 0))) {
                app.getTerrainEditor().thermalErosion(erosionIters, talusAngle);
                app.showToast("Thermal erosion applied");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Material slides downhill. Lower angle = more erosion.");
        }

        if (ImGui::CollapsingHeader("Terrace / Steps")) {
            static int terraceSteps = 6;
            ImGui::SliderInt("Steps##terrace", &terraceSteps, 2, 20);
            if (ImGui::Button("Apply Terracing", ImVec2(-1, 0))) {
                app.getTerrainEditor().terraceHeights(terraceSteps);
                app.showToast("Terrain terraced");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Quantizes heights into flat shelves");
        }

        if (ImGui::CollapsingHeader("Dune Generator")) {
            static float duneWave = 30.0f, duneAmp = 8.0f, duneDir = 45.0f;
            static int duneSeed = 10;
            ImGui::SliderFloat("Wavelength##dune", &duneWave, 10.0f, 100.0f);
            ImGui::SliderFloat("Amplitude##dune", &duneAmp, 2.0f, 30.0f);
            ImGui::SliderFloat("Direction##dune", &duneDir, 0.0f, 360.0f, "%.0f deg");
            ImGui::InputInt("Seed##dune", &duneSeed);
            if (ImGui::Button("Create Dunes", ImVec2(-1, 0))) {
                app.getTerrainEditor().createDunes(duneWave, duneAmp, duneDir,
                                                    static_cast<uint32_t>(duneSeed));
                app.showToast("Dunes created");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Rolling sand dune pattern. Paint desert texture after.");
        }

        if (ImGui::CollapsingHeader("Canyon Generator")) {
            static float canyonWidth = 15.0f, canyonDepth = 20.0f;
            static int canyonSeed = 1;
            ImGui::SliderFloat("Width##canyon", &canyonWidth, 5.0f, 50.0f);
            ImGui::SliderFloat("Depth##canyon", &canyonDepth, 5.0f, 60.0f);
            ImGui::InputInt("Seed##canyon", &canyonSeed);
            ImGui::SameLine();
            if (ImGui::SmallButton("Rnd##cs")) canyonSeed = std::rand();
            if (ImGui::Button("Create Canyon", ImVec2(-1, 0))) {
                app.getTerrainEditor().createCanyon(canyonWidth, canyonDepth, static_cast<uint32_t>(canyonSeed));
                app.showToast("Canyon carved");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Winding canyon across tile. Fill with water for river.");
        }

        if (ImGui::CollapsingHeader("Island Generator")) {
            static float islandHeight = 30.0f, islandDrop = 20.0f;
            ImGui::SliderFloat("Center Height##island", &islandHeight, 5.0f, 100.0f);
            ImGui::SliderFloat("Edge Drop##island", &islandDrop, 5.0f, 50.0f);
            if (ImGui::Button("Create Island Shape", ImVec2(-1, 0))) {
                app.getTerrainEditor().createIsland(islandHeight, islandDrop);
                app.showToast("Island created");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Raised center dropping to edges. Add water around for ocean.");
        }

        if (ImGui::CollapsingHeader("Ridge / Mountain Range")) {
            static glm::vec3 ridgeStart{0}, ridgeEnd{0};
            static float ridgeWidth = 20.0f, ridgeHeight = 30.0f;
            static bool ridgeStartSet = false;
            ImGui::SliderFloat("Width##ridge", &ridgeWidth, 5.0f, 80.0f);
            ImGui::SliderFloat("Height##ridge", &ridgeHeight, 5.0f, 100.0f);
            auto& brushR = app.getTerrainEditor().brush();
            if (ImGui::Button("Set Start##ridge", ImVec2(120, 0)) ) {
                ridgeStart = brushR.getPosition();
                ridgeStartSet = true;
                app.showToast("Ridge start set");
            }
            ImGui::SameLine();
            if (ImGui::Button("Set End + Create##ridge", ImVec2(140, 0))  && ridgeStartSet) {
                ridgeEnd = brushR.getPosition();
                app.getTerrainEditor().createRidge(ridgeStart, ridgeEnd, ridgeWidth, ridgeHeight);
                app.showToast("Ridge created");
                ridgeStartSet = false;
            }
            if (ridgeStartSet)
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "Start set");
        }

        if (ImGui::CollapsingHeader("Hill Generator")) {
            static float hillRadius = 40.0f, hillHeight = 25.0f;
            ImGui::SliderFloat("Radius##hill", &hillRadius, 10.0f, 150.0f);
            ImGui::SliderFloat("Height##hill", &hillHeight, 5.0f, 100.0f);
            auto& brushH = app.getTerrainEditor().brush();
            if (ImGui::Button("Create Hill", ImVec2(120, 0)) ) {
                app.getTerrainEditor().createHill(brushH.getPosition(), hillRadius, hillHeight);
                app.showToast("Hill created");
            }
            ImGui::SameLine();
            if (ImGui::Button("Create Valley", ImVec2(120, 0)) ) {
                app.getTerrainEditor().createHill(brushH.getPosition(), hillRadius, -hillHeight);
                app.showToast("Valley created");
            }
        }

        if (ImGui::CollapsingHeader("Mesa / Plateau")) {
            static float mesaRadius = 40.0f, mesaHeight = 20.0f, mesaSteep = 0.3f;
            ImGui::SliderFloat("Radius##mesa", &mesaRadius, 10.0f, 150.0f);
            ImGui::SliderFloat("Height##mesa", &mesaHeight, 5.0f, 100.0f);
            ImGui::SliderFloat("Edge Steepness##mesa", &mesaSteep, 0.05f, 1.0f);
            auto& brush6 = app.getTerrainEditor().brush();
            if (ImGui::Button("Create Mesa at Cursor", ImVec2(-1, 0)) ) {
                app.getTerrainEditor().createMesa(brush6.getPosition(), mesaRadius, mesaHeight, mesaSteep);
                app.showToast("Mesa created");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Raised flat area with cliff edges");
        }

        if (ImGui::CollapsingHeader("Crater Generator")) {
            static float craterRadius = 30.0f, craterDepth = 10.0f, craterRim = 3.0f;
            ImGui::SliderFloat("Radius##crater", &craterRadius, 5.0f, 100.0f);
            ImGui::SliderFloat("Depth##crater", &craterDepth, 2.0f, 50.0f);
            ImGui::SliderFloat("Rim Height##crater", &craterRim, 0.0f, 15.0f);
            // Click-to-place: arm pendingCrater so the next left-click on
            // terrain spawns the crater there, not at the (stale) brush
            // position that was last set before the cursor moved onto the
            // button.
            auto& pc = app.pendingCrater();
            if (pc.active) {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1),
                    "ARMED - click on terrain to place");
                if (ImGui::Button("Cancel##crater", ImVec2(-1, 0))) {
                    pc.active = false;
                }
            } else {
                if (ImGui::Button("Click on terrain to place crater", ImVec2(-1, 0))) {
                    pc.active = true;
                    pc.radius = craterRadius;
                    pc.depth = craterDepth;
                    pc.rim = craterRim;
                    app.showToast("Click on terrain to place crater (Esc to cancel)");
                }
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Bowl with raised rim. Fill with water for a lake.");
        }

        if (ImGui::CollapsingHeader("Flatten Platform")) {
            auto& brush3 = app.getTerrainEditor().brush();
            if (ImGui::Button("Create Flat Platform at Cursor", ImVec2(-1, 0)) ) {
                // Flatten all vertices under brush to the cursor height
                auto& te = app.getTerrainEditor();
                float targetZ = brush3.getPosition().z;
                te.brush().settings().flattenHeight = targetZ;
                auto savedMode = te.brush().settings().mode;
                te.brush().settings().mode = BrushMode::Flatten;
                // Apply flatten for several frames worth
                for (int i = 0; i < 30; i++) te.applyBrush(0.1f);
                te.brush().settings().mode = savedMode;
                app.showToast("Platform created");
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Flattens area under brush to cursor height.\nGood for building sites, roads, camps.");
        }

        ImGui::Separator();
        ImGui::Text("Terrain Holes (cave entrances):");
        auto& brush = app.getTerrainEditor().brush();
        if (ImGui::Button("Punch Hole", ImVec2(120, 0)) )
            app.getTerrainEditor().punchHole(brush.getPosition(), s.radius);
        ImGui::SameLine();
        if (ImGui::Button("Fill Hole", ImVec2(120, 0)) )
            app.getTerrainEditor().fillHole(brush.getPosition(), s.radius);

        ImGui::Separator();
        auto& hist = app.getTerrainEditor().history();
        ImGui::Text("Undo: %zu  Redo: %zu", hist.undoCount(), hist.redoCount());
    }
    ImGui::End();
}

void EditorUI::renderTexturePaintPanel(EditorApp& app) {
    ImGui::SetNextWindowPos(ImVec2(10, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 550), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Texture Paint")) {
        if (!app.hasTerrainLoaded()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Load or create terrain first");
            ImGui::End(); return;
        }

        auto& s = app.getTerrainEditor().brush().settings();
        ImGui::SliderFloat("Radius", &s.radius, 5.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("Strength", &s.strength, 0.5f, 20.0f, "%.1f");
        ImGui::SliderFloat("Falloff", &s.falloff, 0.0f, 1.0f, "%.2f");

        ImGui::Separator();
        const char* paintModes[] = {"Paint", "Erase", "Replace Base"};
        int pm = static_cast<int>(paintMode_);
        if (ImGui::Combo("Paint Mode", &pm, paintModes, 3))
            paintMode_ = static_cast<PaintMode>(pm);

        if (ImGui::Button("Eyedropper (Alt+Click)")) {
            auto& brush = app.getTerrainEditor().brush();
            if (brush.isActive()) {
                std::string picked = app.getTexturePainter().pickTextureAt(brush.getPosition());
                if (!picked.empty()) {
                    app.getTexturePainter().setActiveTexture(picked);
                    app.showToast("Picked: " + picked.substr(picked.rfind('\\') + 1));
                }
            }
        }

        ImGui::Separator();

        // Directory filter
        auto& browser = app.getAssetBrowser();
        auto& dirs = browser.getTextureDirectories();
        if (ImGui::BeginCombo("Zone", texDirIdx_ < 0 ? "All" :
                dirs[texDirIdx_].c_str())) {
            if (ImGui::Selectable("All", texDirIdx_ < 0)) texDirIdx_ = -1;
            for (int i = 0; i < static_cast<int>(dirs.size()); i++) {
                // Show just the zone name part
                std::string label = dirs[i];
                auto slash = label.rfind('\\');
                if (slash != std::string::npos) label = label.substr(slash + 1);
                if (ImGui::Selectable(label.c_str(), i == texDirIdx_))
                    texDirIdx_ = i;
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Filter", texFilterBuf_, sizeof(texFilterBuf_));
        std::string filter(texFilterBuf_);
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        const auto& textures = browser.getTextures();
        ImGui::TextDisabled("Pool: %zu textures across %zu dirs",
                            textures.size(), dirs.size());
        float listHeight = ImGui::GetContentRegionAvail().y - 60;
        ImGui::BeginChild("TexList", ImVec2(0, listHeight), true);

        int shown = 0;
        for (const auto& tex : textures) {
            if (texDirIdx_ >= 0 && tex.directory != dirs[texDirIdx_]) continue;
            if (!matchesFilter(tex.wowPath, filter)) continue;
            if (++shown > 500) { ImGui::Text("... %zu more (refine filter)", textures.size()); break; }

            bool selected = (tex.wowPath == selectedTexture_);
            if (ImGui::Selectable(tex.displayName.c_str(), selected)) {
                selectedTexture_ = tex.wowPath;
                app.getTexturePainter().setActiveTexture(tex.wowPath);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tex.wowPath.c_str());
        }
        ImGui::EndChild();

        if (!selectedTexture_.empty())
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Active: %s",
                               selectedTexture_.c_str());

        // Auto-paint by height
        if (ImGui::CollapsingHeader("Quick Biome Paint")) {
            const char* biomeNames[] = {"Grassland","Forest","Desert","Snow","Swamp","Barrens"};
            static int quickBiome = 0;
            ImGui::Combo("Biome##qbp", &quickBiome, biomeNames, 6);
            if (ImGui::Button("Apply Full Biome", ImVec2(-1, 0))) {
                const char* bases[][2] = {
                    {"Tileset\\Elwynn\\ElwynnGrassBase.blp", "Tileset\\Elwynn\\ElwynnDirtBase.blp"},
                    {"Tileset\\Ashenvale\\AshenvaleGrass.blp", "Tileset\\Ashenvale\\AshenvaleDirt.blp"},
                    {"Tileset\\Tanaris\\TanarisSandBase01.blp", "Tileset\\Tanaris\\TanarisCrackedGround.blp"},
                    {"Tileset\\Expansion02\\Dragonblight\\DragonblightFreshSmoothSnowA.blp", "Tileset\\Winterspring Grove\\WinterspringDirt.blp"},
                    {"Tileset\\Wetlands\\WetlandsGrassDark01.blp", "Tileset\\Wetlands\\WetlandsDirtMoss01.blp"},
                    {"Tileset\\Barrens\\BarrensBaseDirt.blp", "Tileset\\Barrens\\BarrensBaseGrassGold.blp"}
                };
                // Set base texture for all chunks
                auto* t = app.getTerrainEditor().getTerrain();
                if (t) {
                    uint32_t baseId = 0;
                    for (uint32_t i = 0; i < t->textures.size(); i++)
                        if (t->textures[i] == bases[quickBiome][0]) { baseId = i; goto foundBase; }
                    t->textures.push_back(bases[quickBiome][0]);
                    baseId = static_cast<uint32_t>(t->textures.size() - 1);
                    foundBase:
                    for (int ci = 0; ci < 256; ci++)
                        if (!t->chunks[ci].layers.empty()) t->chunks[ci].layers[0].textureId = baseId;
                    // Scatter variation patches
                    app.getTexturePainter().scatterPatches(bases[quickBiome][1], 20, 15.0f, 40.0f, 42);
                    app.showToast("Biome applied: " + std::string(biomeNames[quickBiome]));
                }
            }
        }

        if (ImGui::CollapsingHeader("Scatter Patches")) {
            static int patchCount = 15;
            static float patchMinR = 10.0f, patchMaxR = 30.0f;
            static int patchSeed = 55;
            ImGui::SliderInt("Count##patches", &patchCount, 5, 50);
            ImGui::DragFloatRange2("Radius##patches", &patchMinR, &patchMaxR, 1.0f, 5.0f, 80.0f);
            ImGui::InputInt("Seed##patches", &patchSeed);
            if (ImGui::Button("Scatter Dirt Patches", ImVec2(-1, 0))) {
                app.getTexturePainter().scatterPatches(
                    "Tileset\\Elwynn\\ElwynnDirtBase.blp", patchCount, patchMinR, patchMaxR,
                    static_cast<uint32_t>(patchSeed));
                app.showToast("Patches scattered");
            }
            if (ImGui::Button("Scatter Rock Patches", ImVec2(-1, 0))) {
                app.getTexturePainter().scatterPatches(
                    "Tileset\\Barrens\\BarrensRock01.blp", patchCount, patchMinR, patchMaxR,
                    static_cast<uint32_t>(patchSeed + 1));
                app.showToast("Patches scattered");
            }
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "Random texture patches for variety");
        }

        if (ImGui::CollapsingHeader("Gradient Blend")) {
            static int gradDir = 0;
            ImGui::RadioButton("Horizontal##grad", &gradDir, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Vertical##grad", &gradDir, 1);
            if (ImGui::Button("Grass → Sand Gradient", ImVec2(-1, 0))) {
                app.getTexturePainter().gradientBlend(
                    "Tileset\\Elwynn\\ElwynnGrassBase.blp",
                    "Tileset\\Tanaris\\TanarisSandBase01.blp", gradDir == 0);
                app.showToast("Gradient applied");
            }
            if (ImGui::Button("Grass → Snow Gradient", ImVec2(-1, 0))) {
                app.getTexturePainter().gradientBlend(
                    "Tileset\\Elwynn\\ElwynnGrassBase.blp",
                    "Tileset\\Expansion02\\Dragonblight\\DragonblightFreshSmoothSnowA.blp", gradDir == 0);
                app.showToast("Gradient applied");
            }
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "Smooth texture transition across tile");
        }

        if (ImGui::CollapsingHeader("Auto-Paint by Slope")) {
            static float slopeThresh = 0.4f;
            ImGui::SliderFloat("Slope Threshold", &slopeThresh, 0.1f, 0.9f);
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "Paints rock on steep slopes");
            if (ImGui::Button("Apply Slope Paint", ImVec2(-1, 0))) {
                app.getTexturePainter().autoPaintBySlope(slopeThresh,
                    "Tileset\\Barrens\\BarrensRock01.blp");
                app.showToast("Slope paint applied");
            }
        }

        if (ImGui::CollapsingHeader("Auto-Paint by Height")) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Sets base texture per chunk based on average height");
            static float h1 = 90, h2 = 110, h3 = 140;
            ImGui::DragFloat("Sand/Dirt max##ap", &h1, 1.0f);
            ImGui::DragFloat("Grass max##ap", &h2, 1.0f);
            ImGui::DragFloat("Rock max##ap", &h3, 1.0f);
            ImGui::Text("Above %.0f: Snow", h3);
            static bool autoScatterObjects = false;
            ImGui::Checkbox("Also scatter objects by band", &autoScatterObjects);
            if (ImGui::Button("Apply Auto-Paint", ImVec2(-1, 0))) {
                std::vector<TexturePainter::HeightBand> bands = {
                    {h1, "Tileset\\Tanaris\\TanarisSandBase01.blp"},
                    {h2, "Tileset\\Elwynn\\ElwynnGrassBase.blp"},
                    {h3, "Tileset\\Barrens\\BarrensRock01.blp"},
                    {99999.0f, "Tileset\\Expansion02\\Dragonblight\\DragonblightFreshSmoothSnowA.blp"}
                };
                app.getTexturePainter().autoPaintByHeight(bands);
                // Force terrain refresh
                auto mesh = app.getTerrainEditor().regenerateMesh();
                // Mark all chunks dirty through a dummy edit
                app.showToast("Auto-painted by height");
            }
        }

        ImGui::Separator();

        // Show textures on chunk under cursor
        auto& brush = app.getTerrainEditor().brush();
        if (brush.isActive()) {
            if (auto* terrain = app.getTerrainEditor().getTerrain()) {
                glm::vec3 bp = brush.getPosition();
                float tileNW_X = (32.0f - static_cast<float>(terrain->coord.y)) * 533.33333f;
                float tileNW_Y = (32.0f - static_cast<float>(terrain->coord.x)) * 533.33333f;
                int cy = static_cast<int>((tileNW_X - bp.x) / (533.33333f / 16.0f));
                int cx = static_cast<int>((tileNW_Y - bp.y) / (533.33333f / 16.0f));
                cx = std::clamp(cx, 0, 15);
                cy = std::clamp(cy, 0, 15);
                auto& chunk = terrain->chunks[cy * 16 + cx];
                if (!chunk.layers.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Chunk [%d,%d] layers:", cx, cy);
                    for (size_t li = 0; li < chunk.layers.size(); li++) {
                        uint32_t tid = chunk.layers[li].textureId;
                        std::string tname = (tid < terrain->textures.size()) ? terrain->textures[tid] : "?";
                        auto sl = tname.rfind('\\');
                        if (sl != std::string::npos) tname = tname.substr(sl + 1);
                        ImGui::BulletText("%s%s", li == 0 ? "[base] " : "", tname.c_str());
                    }
                }
            }
        }

        // Recent textures
        auto& recent = app.getTexturePainter().getRecentTextures();
        if (!recent.empty()) {
            ImGui::Separator();
            ImGui::Text("Recent:");
            for (int i = 0; i < static_cast<int>(recent.size()) && i < 6; i++) {
                std::string disp = recent[i];
                auto sl = disp.rfind('\\');
                if (sl != std::string::npos) disp = disp.substr(sl + 1);
                if (ImGui::SmallButton(disp.c_str())) {
                    selectedTexture_ = recent[i];
                    app.getTexturePainter().setActiveTexture(recent[i]);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", recent[i].c_str());
                if (i < 5 && i + 1 < static_cast<int>(recent.size())) ImGui::SameLine();
            }
        }
    }
    ImGui::End();
}

void EditorUI::renderObjectPanel(EditorApp& app) {
    ImGui::SetNextWindowPos(ImVec2(10, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 550), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Object Placement")) {
        if (!app.hasTerrainLoaded()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Load or create terrain first");
            ImGui::End(); return;
        }

        auto& placer = app.getObjectPlacer();

        // Placement settings for new objects
        ImGui::Text("New Object Settings:");
        float rot = placer.getPlacementRotationY();
        if (ImGui::SliderFloat("Y Rotation", &rot, 0.0f, 360.0f, "%.0f deg"))
            placer.setPlacementRotationY(rot);
        ImGui::TextDisabled("Tip: Ctrl+Wheel rotates while placing (Shift = fine)");
        bool randRot = placer.getRandomRotation();
        if (ImGui::Checkbox("Random Rotation", &randRot))
            placer.setRandomRotation(randRot);
        ImGui::SameLine();
        bool snap = placer.getSnapToGround();
        if (ImGui::Checkbox("Snap Ground", &snap))
            placer.setSnapToGround(snap);
        float scale = placer.getPlacementScale();
        if (ImGui::DragFloat("Scale", &scale, 0.05f, 0.1f, 50.0f, "%.2f"))
            placer.setPlacementScale(scale);

        ImGui::Separator();
        ImGui::Checkbox("M2 Models", &showM2s_);
        ImGui::SameLine();
        ImGui::Checkbox("WMO Buildings", &showWMOs_);

        ImGui::InputText("Filter", objFilterBuf_, sizeof(objFilterBuf_));
        std::string filter(objFilterBuf_);
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        auto& browser = app.getAssetBrowser();
        // Show pool counts so the user knows the search base.
        ImGui::TextDisabled("Pool: %zu M2  %zu WMO",
                            browser.getM2Models().size(), browser.getWMOs().size());
        float listHeight = ImGui::GetContentRegionAvail().y - 100;
        ImGui::BeginChild("ObjList", ImVec2(0, listHeight), true);

        int shown = 0;
        if (showM2s_) {
            for (const auto& m2 : browser.getM2Models()) {
                if (!matchesFilter(m2.wowPath, filter)) continue;
                if (++shown > 500) { ImGui::Text("... refine filter"); break; }

                bool selected = (m2.wowPath == placer.getActivePath() &&
                                 placer.getActiveType() == PlaceableType::M2);
                if (ImGui::Selectable(m2.displayName.c_str(), selected))
                    placer.setActivePath(m2.wowPath, PlaceableType::M2);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", m2.wowPath.c_str());
            }
        }
        if (showWMOs_) {
            if (showM2s_ && shown > 0) ImGui::Separator();
            for (const auto& wmo : browser.getWMOs()) {
                if (!matchesFilter(wmo.wowPath, filter)) continue;
                if (++shown > 500) { ImGui::Text("... refine filter"); break; }

                bool selected = (wmo.wowPath == placer.getActivePath() &&
                                 placer.getActiveType() == PlaceableType::WMO);
                if (ImGui::Selectable(wmo.displayName.c_str(), selected))
                    placer.setActivePath(wmo.wowPath, PlaceableType::WMO);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", wmo.wowPath.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Placed: %zu objects (open format: WOM)", placer.objectCount());
        if (placer.objectCount() > 0 && ImGui::CollapsingHeader("Object List")) {
            ImGui::TextDisabled("Click to select, double-click to fly to");
            static char placedFilter[128] = "";
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##placedfilter", "Filter by name...", placedFilter, sizeof(placedFilter));
            std::string filterLower(placedFilter);
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            int shown = 0;
            ImGui::BeginChild("ObjPlacedList", ImVec2(0, 100), true);
            for (int i = 0; i < static_cast<int>(placer.objectCount()); i++) {
                auto& o = const_cast<std::vector<PlacedObject>&>(placer.getObjects())[i];
                std::string disp = o.path;
                auto sl = disp.rfind('\\');
                if (sl != std::string::npos) disp = disp.substr(sl + 1);
                if (!filterLower.empty()) {
                    std::string nameLower = disp;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (nameLower.find(filterLower) == std::string::npos) continue;
                }
                if (++shown > 200) { ImGui::Text("... refine filter"); break; }
                char lbl[128];
                std::snprintf(lbl, sizeof(lbl), "%s (%.0f,%.0f,%.0f)##obj%d",
                              disp.c_str(), o.position.x, o.position.y, o.position.z, i);
                if (ImGui::Selectable(lbl, o.selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    placer.clearSelection();
                    // Select by creating a ray through the object position
                    rendering::Ray r;
                    r.origin = o.position + glm::vec3(0, 0, 1);
                    r.direction = glm::vec3(0, 0, -1);
                    placer.selectAt(r, 1000.0f);
                    if (ImGui::IsMouseDoubleClicked(0))
                        app.flyToSelected();
                }
            }
            ImGui::EndChild();
        }
        if (auto* sel = placer.getSelected()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.9f, 0.3f, 1));
            if (placer.isMultiSelected())
                ImGui::Text("Selected: %zu objects", placer.selectionCount());
            else
                ImGui::Text("Selected: %s", sel->path.c_str());
            ImGui::PopStyleColor();

            bool changed = false;
            changed |= ImGui::DragFloat3("Position", &sel->position.x, 1.0f);
            changed |= ImGui::DragFloat3("Rotation", &sel->rotation.x, 1.0f, 0.0f, 360.0f, "%.1f deg");
            changed |= ImGui::DragFloat("Obj Scale", &sel->scale, 0.05f, 0.1f, 50.0f, "%.2f");

            if (changed) app.markObjectsDirty();

            ImGui::Text("Scale: %.2f  Rot: %.0f,%.0f,%.0f",
                        sel->scale, sel->rotation.x, sel->rotation.y, sel->rotation.z);
            if (ImGui::Button("Snap Ground", ImVec2(75, 0)))
                app.snapSelectedToGround();
            ImGui::SameLine();
            if (ImGui::Button("Align Slope", ImVec2(75, 0)))
                app.alignSelectedToTerrain();
            if (ImGui::Button("Flatten Ground", ImVec2(100, 0)))
                app.flattenAroundSelected();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Flatten terrain around object to its height");
            if (ImGui::Button("Fly To", ImVec2(55, 0)))
                app.flyToSelected();
            ImGui::SameLine();
            if (ImGui::Button("Delete", ImVec2(55, 0))) placer.deleteSelected();
            if (ImGui::Button("Duplicate", ImVec2(100, 0))) {
                std::string dupPath = sel->path;
                glm::vec3 dupPos = sel->position + glm::vec3(10.0f, 10.0f, 0.0f);
                glm::vec3 dupRot = sel->rotation;
                float dupScale = sel->scale;
                auto dupType = sel->type;
                placer.clearSelection();
                placer.setActivePath(dupPath, dupType);
                placer.setPlacementScale(dupScale);
                placer.setPlacementRotationY(dupRot.y);
                placer.placeObject(dupPos);
                app.markObjectsDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect", ImVec2(100, 0)))
                placer.clearSelection();
        }

        ImGui::Separator();
        // Object scatter
        if (ImGui::CollapsingHeader("Scatter Objects")) {
            static int objScatterCount = 8;
            static float objScatterRadius = 60.0f;
            static float objMinScale = 0.8f;
            static float objMaxScale = 1.5f;
            ImGui::SliderInt("Count##objsc", &objScatterCount, 1, 50);
            ImGui::SliderFloat("Radius##objsc", &objScatterRadius, 10.0f, 300.0f);
            ImGui::DragFloatRange2("Scale##objsc", &objMinScale, &objMaxScale, 0.05f, 0.1f, 10.0f);
            static bool scatterAlign = true;
            ImGui::Checkbox("Align to terrain", &scatterAlign);
            auto& brush = app.getTerrainEditor().brush();
            if (ImGui::Button("Scatter at Cursor##obj", ImVec2(-1, 0))) {
                if (brush.isActive() && !placer.getActivePath().empty()) {
                    size_t before = placer.objectCount();
                    placer.scatter(brush.getPosition(), objScatterRadius,
                                   objScatterCount, objMinScale, objMaxScale);
                    if (scatterAlign) {
                        for (size_t i = before; i < placer.objectCount(); i++) {
                            auto& obj = placer.getObjects()[i];
                            rendering::Ray ray;
                            ray.origin = obj.position + glm::vec3(0, 0, 500);
                            ray.direction = glm::vec3(0, 0, -1);
                            glm::vec3 hitPos;
                            if (app.getTerrainEditor().raycastTerrain(ray, hitPos)) {
                                obj.position.z = hitPos.z;
                                glm::vec3 n = app.getTerrainEditor().sampleTerrainNormal(obj.position);
                                obj.rotation.x = glm::degrees(std::asin(-n.x));
                                obj.rotation.z = glm::degrees(std::asin(n.y));
                            }
                        }
                    }
                    app.markObjectsDirty();
                }
            }
        }

        if (ImGui::CollapsingHeader("Auto-Populate Biome")) {
            static int popBiome = 0;
            static uint32_t popSeed = 42;
            const char* biomeNames[] = {"Grassland", "Forest", "Jungle", "Desert",
                "Barrens", "Snow", "Swamp", "Rocky", "Beach", "Volcanic"};
            ImGui::Combo("Biome##pop", &popBiome, biomeNames, 10);
            int seed = static_cast<int>(popSeed);
            if (ImGui::InputInt("Seed##pop", &seed)) popSeed = static_cast<uint32_t>(seed);

            auto veg = getBiomeVegetation(static_cast<Biome>(popBiome));
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "%zu asset types, density-based",
                veg.assets.size());

            if (ImGui::Button("Populate Zone", ImVec2(-1, 0)) && app.hasTerrainLoaded()) {
                auto* t = app.getTerrainEditor().getTerrain();
                float tileSize = 533.33333f;
                glm::vec3 origin(
                    (32.0f - t->coord.y) * tileSize,
                    (32.0f - t->coord.x) * tileSize, 0);
                int n = placer.populateBiome(veg, tileSize, origin, popSeed);
                app.markObjectsDirty();
                app.showToast("Populated " + std::string(biomeNames[popBiome]) +
                    ": " + std::to_string(n) + " objects");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Auto-place trees, rocks, bushes based on biome rules");
        }

        ImGui::Separator();
        // Bulk operations
        if (ImGui::CollapsingHeader("Bulk Operations")) {
            static float bulkRadius = 50.0f;
            ImGui::SliderFloat("Radius##bulk", &bulkRadius, 10.0f, 200.0f);
            auto& brush = app.getTerrainEditor().brush();
            if (ImGui::Button("Delete All in Radius", ImVec2(-1, 0)) ) {
                auto& objs = const_cast<std::vector<PlacedObject>&>(placer.getObjects());
                glm::vec3 center = brush.getPosition();
                objs.erase(std::remove_if(objs.begin(), objs.end(),
                    [&](const PlacedObject& o) {
                        return glm::length(glm::vec2(o.position.x - center.x,
                                                     o.position.y - center.y)) < bulkRadius;
                    }), objs.end());
                app.markObjectsDirty();
                app.showToast("Deleted objects in radius");
            }
            if (ImGui::Button("Snap All to Ground", ImVec2(-1, 0))) {
                for (auto& o : const_cast<std::vector<PlacedObject>&>(placer.getObjects())) {
                    rendering::Ray r;
                    r.origin = o.position + glm::vec3(0, 0, 500);
                    r.direction = glm::vec3(0, 0, -1);
                    glm::vec3 hit;
                    if (app.getTerrainEditor().raycastTerrain(r, hit))
                        o.position.z = hit.z;
                }
                app.markObjectsDirty();
                app.showToast("All objects snapped to ground");
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Left-click: place | Ctrl+click: select");
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "G: move | R: rotate | T: scale | Del: remove");
    }
    ImGui::End();
}

void EditorUI::renderNpcPanel(EditorApp& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - 400, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("NPC / Monsters")) {
        if (!app.hasTerrainLoaded()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Load terrain first");
            ImGui::End(); return;
        }

        auto& spawner = app.getNpcSpawner();
        auto& presets = app.getNpcPresets();
        auto& tmpl = spawner.getTemplate();

        bool showMarkers = app.getViewport().getShowNpcMarkers();
        if (ImGui::Checkbox("Show Position Markers", &showMarkers))
            app.getViewport().setShowNpcMarkers(showMarkers);
        bool selOnly = app.getViewport().getNpcNameplatesSelectedOnly();
        if (ImGui::Checkbox("Nameplate on selected only", &selOnly))
            app.getViewport().setNpcNameplatesSelectedOnly(selOnly);
        ImGui::Separator();

        // ---- Creature Browser ----
        if (ImGui::CollapsingHeader("Creature Browser", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Category filter
            static int catIdx = -1;
            if (ImGui::BeginCombo("Category", catIdx < 0 ? "All" :
                    NpcPresets::getCategoryName(static_cast<CreatureCategory>(catIdx)))) {
                if (ImGui::Selectable("All", catIdx < 0)) catIdx = -1;
                for (int i = 0; i < static_cast<int>(CreatureCategory::COUNT); i++) {
                    auto cat = static_cast<CreatureCategory>(i);
                    auto& list = presets.getByCategory(cat);
                    if (list.empty()) continue;
                    char label[64];
                    std::snprintf(label, sizeof(label), "%s (%zu)",
                                  NpcPresets::getCategoryName(cat), list.size());
                    if (ImGui::Selectable(label, catIdx == i)) catIdx = i;
                }
                ImGui::EndCombo();
            }

            static char npcFilter[128] = "";
            ImGui::InputText("Search##npc", npcFilter, sizeof(npcFilter));
            std::string filter(npcFilter);
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            ImGui::BeginChild("CreatureList", ImVec2(0, 150), true);
            const auto& list = (catIdx < 0) ? presets.getPresets()
                               : presets.getByCategory(static_cast<CreatureCategory>(catIdx));
            int shown = 0;
            for (const auto& p : list) {
                if (!filter.empty()) {
                    std::string lower = p.name;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (lower.find(filter) == std::string::npos) continue;
                }
                if (++shown > 200) { ImGui::Text("... refine search"); break; }

                bool selected = (tmpl.modelPath == p.modelPath);
                if (ImGui::Selectable(p.name.c_str(), selected)) {
                    tmpl.name = p.name;
                    tmpl.modelPath = p.modelPath;
                    tmpl.level = p.defaultLevel;
                    tmpl.health = p.defaultHealth;
                    tmpl.hostile = p.defaultHostile;
                    tmpl.minDamage = 3 + p.defaultLevel * 2;
                    tmpl.maxDamage = 5 + p.defaultLevel * 3;
                    tmpl.armor = p.defaultLevel * 10;
                    // Sensible AzerothCore FactionTemplate default - only set
                    // if the user hasn't already typed in a custom value.
                    if (tmpl.faction == 0) {
                        if (p.category == CreatureCategory::Critter) tmpl.faction = 250;
                        else if (p.defaultHostile) tmpl.faction = 14;  // Monster
                        else tmpl.faction = 35;                         // Friendly
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", p.modelPath.c_str());
            }
            ImGui::EndChild();

            if (!tmpl.modelPath.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "Selected: %s", tmpl.name.c_str());
            }
        }

        // ---- Stats & Behavior ----
        if (ImGui::CollapsingHeader("Stats & Behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char nameBuf[128] = "";
            if (nameBuf[0] == '\0') std::strncpy(nameBuf, tmpl.name.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText("Name##tmpl", nameBuf, sizeof(nameBuf)))
                tmpl.name = nameBuf;

            ImGui::DragFloat("Scale", &tmpl.scale, 0.05f, 0.1f, 50.0f, "%.2f");
            ImGui::SliderFloat("Facing", &tmpl.orientation, 0.0f, 360.0f, "%.0f deg");
            ImGui::SameLine();
            if (ImGui::SmallButton("Random##face")) {
                static std::mt19937 rng(7);
                std::uniform_real_distribution<float> d(0.0f, 360.0f);
                tmpl.orientation = d(rng);
            }
            ImGui::TextDisabled("Tip: Ctrl+Wheel rotates while placing (Shift = fine)");

            // Quick NPC linking for quests
            if (app.getQuestEditor().questCount() > 0) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1), "Quest link: set as giver/turn-in via Quest panel");
            }

            int lvl = tmpl.level;
            if (ImGui::SliderInt("Level", &lvl, 1, 83)) {
                tmpl.level = lvl;
                tmpl.health = 50 + lvl * 80;
                tmpl.minDamage = 3 + lvl * 2;
                tmpl.maxDamage = 5 + lvl * 3;
                tmpl.armor = lvl * 10;
            }

            int hp = tmpl.health;
            if (ImGui::InputInt("Health", &hp)) tmpl.health = std::max(1, hp);
            int mp = tmpl.mana;
            if (ImGui::InputInt("Mana", &mp)) tmpl.mana = std::max(0, mp);

            int dmin = tmpl.minDamage, dmax = tmpl.maxDamage;
            ImGui::InputInt("Min Dmg", &dmin); tmpl.minDamage = std::max(0, dmin);
            ImGui::InputInt("Max Dmg", &dmax); tmpl.maxDamage = std::max(0, dmax);
            int arm = tmpl.armor;
            if (ImGui::InputInt("Armor", &arm)) tmpl.armor = std::max(0, arm);

            // Faction template ID (FactionTemplate.dbc). Common AzerothCore values:
            // 7 = Stormwind, 35 = Friendly to all, 14 = Monster (hostile to all),
            // 16 = Beast (Wild), 250 = Critter, 71 = Theramore, etc.
            int fac = static_cast<int>(tmpl.faction);
            if (ImGui::InputInt("Faction", &fac))
                tmpl.faction = static_cast<uint32_t>(std::max(0, fac));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("FactionTemplate ID. 7=Stormwind 14=Monster 16=Beast 35=Friendly 250=Critter");

            const char* behaviors[] = {"Stationary", "Patrol", "Wander", "Scripted"};
            int bIdx = static_cast<int>(tmpl.behavior);
            if (ImGui::Combo("Behavior", &bIdx, behaviors, 4))
                tmpl.behavior = static_cast<CreatureBehavior>(bIdx);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Runtime AI mode (not editor preview).\n"
                    "Stationary: stay put.\n"
                    "Patrol: walk waypoints (W to add at cursor on selected NPC).\n"
                    "Wander: random walk within radius - common default.\n"
                    "Scripted: handled by server-side script.");

            if (tmpl.behavior == CreatureBehavior::Wander)
                ImGui::SliderFloat("Wander Dist", &tmpl.wanderRadius, 1.0f, 100.0f);
            ImGui::SliderFloat("Aggro Range", &tmpl.aggroRadius, 0.0f, 100.0f);

            float respSec = tmpl.respawnTimeMs / 1000.0f;
            if (ImGui::DragFloat("Respawn (s)", &respSec, 5.0f, 5.0f, 86400.0f, "%.0fs")) {
                if (respSec < 5.0f) respSec = 5.0f;
                tmpl.respawnTimeMs = static_cast<uint32_t>(respSec * 1000.0f);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Server respawn delay. AzerothCore default ~60-300s.");

            ImGui::Checkbox("Hostile", &tmpl.hostile);
            ImGui::SameLine(); ImGui::Checkbox("Questgiver", &tmpl.questgiver);
            ImGui::Checkbox("Vendor", &tmpl.vendor);
            ImGui::SameLine(); ImGui::Checkbox("Innkeeper", &tmpl.innkeeper);
            ImGui::Checkbox("Trainer", &tmpl.trainer);
            ImGui::SameLine(); ImGui::Checkbox("Banker", &tmpl.banker);
            ImGui::Checkbox("Auctioneer", &tmpl.auctioneer);
            ImGui::SameLine(); ImGui::Checkbox("Repair", &tmpl.repair);
            ImGui::SameLine(); ImGui::Checkbox("Flightmaster", &tmpl.flightmaster);

            // Update nameBuf when preset selection changes it
            if (tmpl.name.c_str() != std::string(nameBuf))
                std::strncpy(nameBuf, tmpl.name.c_str(), sizeof(nameBuf) - 1);
        }

        ImGui::Separator();

        // ---- Spawned list ----
        if (ImGui::CollapsingHeader("Spawned Creatures", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char spawnFilter[128] = "";
            // Count hostile / friendly / quest givers / vendors
            size_t hostile = 0, friendly = 0, qg = 0, vend = 0;
            for (const auto& s : spawner.getSpawns()) {
                if (s.hostile) hostile++; else friendly++;
                if (s.questgiver) qg++;
                if (s.vendor) vend++;
            }
            ImGui::Text("%zu placed", spawner.spawnCount());
            ImGui::TextDisabled("Hostile: %zu  Friendly: %zu  Q-givers: %zu  Vendors: %zu",
                                hostile, friendly, qg, vend);
            ImGui::TextDisabled("Double-click an entry to fly to it");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##spawnfilter", "Filter by name...", spawnFilter, sizeof(spawnFilter));
            std::string filterLower(spawnFilter);
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            int shown = 0;
            ImGui::BeginChild("SpawnList", ImVec2(0, 100), true);
            for (int i = 0; i < static_cast<int>(spawner.spawnCount()); i++) {
                auto& s = spawner.getSpawns()[i];
                if (!filterLower.empty()) {
                    std::string nameLower = s.name;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (nameLower.find(filterLower) == std::string::npos) continue;
                }
                if (++shown > 200) { ImGui::Text("... refine filter"); break; }
                bool sel = (i == spawner.getSelectedIndex());
                char label[128];
                std::snprintf(label, sizeof(label), "%s Lv%u (%.0f,%.0f,%.0f)",
                              s.name.c_str(), s.level,
                              s.position.x, s.position.y, s.position.z);
                if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                    spawner.selectAt(s.position, 10000.0f);
                    if (ImGui::IsMouseDoubleClicked(0))
                        app.flyToSelected();
                }
            }
            ImGui::EndChild();
        }

        // ---- Selected creature editor ----
        if (auto* sel = spawner.getSelected()) {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.9f, 0.3f, 1));
            ImGui::Text("Editing: %s", sel->name.c_str());
            ImGui::PopStyleColor();

            ImGui::DragFloat3("Pos##npc", &sel->position.x, 1.0f);
            ImGui::SliderFloat("Facing", &sel->orientation, 0.0f, 360.0f, "%.0f deg");
            ImGui::DragFloat("Scale##s", &sel->scale, 0.05f, 0.1f, 50.0f, "%.2f");
            int hp2 = sel->health; if (ImGui::InputInt("HP##s", &hp2)) sel->health = std::max(1, hp2);
            int lv2 = sel->level; if (ImGui::InputInt("Lv##s", &lv2)) sel->level = std::max(1, lv2);
            int mp2 = sel->mana; if (ImGui::InputInt("Mana##s", &mp2)) sel->mana = std::max(0, mp2);
            int dmin2 = sel->minDamage, dmax2 = sel->maxDamage;
            ImGui::InputInt("Min Dmg##s", &dmin2); sel->minDamage = std::max(0, dmin2);
            ImGui::InputInt("Max Dmg##s", &dmax2); sel->maxDamage = std::max(0, dmax2);
            int arm2 = sel->armor; if (ImGui::InputInt("Armor##s", &arm2)) sel->armor = std::max(0, arm2);
            float respSec2 = sel->respawnTimeMs / 1000.0f;
            if (ImGui::DragFloat("Respawn (s)##s", &respSec2, 5.0f, 5.0f, 86400.0f, "%.0fs")) {
                if (respSec2 < 5.0f) respSec2 = 5.0f;
                sel->respawnTimeMs = static_cast<uint32_t>(respSec2 * 1000.0f);
            }
            int dispId = static_cast<int>(sel->displayId);
            if (ImGui::InputInt("Display ID##s", &dispId))
                sel->displayId = static_cast<uint32_t>(std::max(0, dispId));
            if (sel->displayId == 0)
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1),
                    "Display ID 0 = invisible in game; set for SQL export");
            int facSel = static_cast<int>(sel->faction);
            if (ImGui::InputInt("Faction##s", &facSel))
                sel->faction = static_cast<uint32_t>(std::max(0, facSel));

            const char* beh2[] = {"Stationary", "Patrol", "Wander", "Scripted"};
            int bi2 = static_cast<int>(sel->behavior);
            if (ImGui::Combo("AI##s", &bi2, beh2, 4)) sel->behavior = static_cast<CreatureBehavior>(bi2);

            // NPC role flags - match the template editor's set so users can
            // toggle these on already-placed NPCs without re-creating them.
            ImGui::Checkbox("Hostile##s", &sel->hostile);
            ImGui::SameLine(); ImGui::Checkbox("Quest##s", &sel->questgiver);
            ImGui::SameLine(); ImGui::Checkbox("Vendor##s", &sel->vendor);
            ImGui::Checkbox("Inn##s", &sel->innkeeper);
            ImGui::SameLine(); ImGui::Checkbox("Train##s", &sel->trainer);
            ImGui::SameLine(); ImGui::Checkbox("Bank##s", &sel->banker);
            ImGui::Checkbox("Auc##s", &sel->auctioneer);
            ImGui::SameLine(); ImGui::Checkbox("Repair##s", &sel->repair);
            ImGui::SameLine(); ImGui::Checkbox("Flight##s", &sel->flightmaster);

            // Patrol path editing
            if (sel->behavior == CreatureBehavior::Patrol) {
                ImGui::Text("Patrol Points: %zu", sel->patrolPath.size());
                auto& brush = app.getTerrainEditor().brush();
                if (ImGui::Button("Add Point at Cursor##patrol", ImVec2(-1, 0))) {
                    if (brush.isActive()) {
                        PatrolPoint pp;
                        pp.position = brush.getPosition();
                        pp.waitTimeMs = 2000.0f;
                        sel->patrolPath.push_back(pp);
                    }
                }
                if (!sel->patrolPath.empty()) {
                    // Compute total loop distance so users can see how long the cycle is.
                    float totalDist = 0.0f;
                    glm::vec3 prev = sel->position;
                    for (const auto& wp : sel->patrolPath) {
                        totalDist += glm::length(wp.position - prev);
                        prev = wp.position;
                    }
                    if (sel->patrolPath.size() >= 2)
                        totalDist += glm::length(sel->position - prev);
                    ImGui::TextDisabled("Loop length: %.0f units", totalDist);

                    ImGui::BeginChild("PatrolList", ImVec2(0, 130), true);
                    for (int pi = 0; pi < static_cast<int>(sel->patrolPath.size()); pi++) {
                        auto& pp = sel->patrolPath[pi];
                        ImGui::PushID(pi);
                        ImGui::Text("P%d (%.0f,%.0f,%.0f)",
                                    pi, pp.position.x, pp.position.y, pp.position.z);
                        // Reorder + delete buttons in one row.
                        ImGui::SameLine();
                        if (pi > 0 && ImGui::SmallButton("up")) {
                            std::swap(sel->patrolPath[pi], sel->patrolPath[pi - 1]);
                        }
                        ImGui::SameLine();
                        if (pi + 1 < static_cast<int>(sel->patrolPath.size()) &&
                            ImGui::SmallButton("dn")) {
                            std::swap(sel->patrolPath[pi], sel->patrolPath[pi + 1]);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) {
                            sel->patrolPath.erase(sel->patrolPath.begin() + pi--);
                            ImGui::PopID();
                            continue;
                        }
                        ImGui::SetNextItemWidth(80);
                        float waitS = pp.waitTimeMs / 1000.0f;
                        if (ImGui::DragFloat("wait s", &waitS, 0.25f, 0.0f, 60.0f, "%.1fs"))
                            pp.waitTimeMs = std::max(0.0f, waitS) * 1000.0f;
                        // Insert-after at brush cursor - useful for slicing
                        // a long segment into two.
                        ImGui::SameLine();
                        auto& brush2 = app.getTerrainEditor().brush();
                        if (brush2.isActive() && ImGui::SmallButton("+after")) {
                            PatrolPoint np;
                            np.position = brush2.getPosition();
                            np.waitTimeMs = 2000.0f;
                            sel->patrolPath.insert(sel->patrolPath.begin() + pi + 1, np);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    if (ImGui::Button("Clear Path##patrol"))
                        sel->patrolPath.clear();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Fly To##npc", ImVec2(55, 0)))
                app.flyToSelected();
            ImGui::SameLine();
            if (ImGui::Button("Duplicate##npc", ImVec2(80, 0))) {
                CreatureSpawn copy = *sel;
                copy.position += glm::vec3(10, 10, 0);
                spawner.placeCreature(copy);
                app.markObjectsDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete##npc", ImVec2(80, 0))) spawner.removeCreature(spawner.getSelectedIndex());
            ImGui::SameLine();
            if (ImGui::Button("Desel.##npc", ImVec2(60, 0))) spawner.clearSelection();
        }

        ImGui::Separator();

        // Scatter tool
        if (ImGui::CollapsingHeader("Scatter Tool")) {
            static int scatterCount = 5;
            static float scatterRadius = 50.0f;
            static bool scatterSnap = true;
            ImGui::SliderInt("Count", &scatterCount, 1, 30);
            ImGui::SliderFloat("Radius##scatter", &scatterRadius, 10.0f, 200.0f);
            ImGui::Checkbox("Snap to Ground##scatter", &scatterSnap);
            auto& brush = app.getTerrainEditor().brush();
            if (ImGui::Button("Scatter at Cursor", ImVec2(-1, 0))) {
                if (brush.isActive() && !tmpl.modelPath.empty()) {
                    size_t before = spawner.spawnCount();
                    spawner.scatter(tmpl, brush.getPosition(), scatterRadius, scatterCount);
                    if (scatterSnap) {
                        auto& tEd = app.getTerrainEditor();
                        for (size_t i = before; i < spawner.spawnCount(); i++) {
                            auto& s = spawner.getSpawns()[i];
                            rendering::Ray ray;
                            ray.origin = s.position + glm::vec3(0, 0, 500);
                            ray.direction = glm::vec3(0, 0, -1);
                            glm::vec3 hit;
                            if (tEd.raycastTerrain(ray, hit)) s.position.z = hit.z;
                        }
                    }
                    app.markObjectsDirty();
                }
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "Places %d copies in %.0f radius", scatterCount, scatterRadius);
        }

        ImGui::Separator();
        static char npcPath[256] = "output/creatures.json";
        ImGui::InputText("File##npc", npcPath, sizeof(npcPath));
        if (ImGui::Button("Save NPCs", ImVec2(100, 0))) spawner.saveToFile(npcPath);
        ImGui::SameLine();
        if (ImGui::Button("Load NPCs", ImVec2(100, 0))) {
            spawner.loadFromFile(npcPath);
            app.markObjectsDirty();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Click terrain to place selected creature");
    }
    ImGui::End();
}

void EditorUI::renderQuestPanel(EditorApp& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - 400, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Quest Editor")) {
        if (!app.hasTerrainLoaded()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Load or create terrain first");
            ImGui::End(); return;
        }
        auto& qe = app.getQuestEditor();
        auto& tmpl = qe.getTemplate();

        if (ImGui::CollapsingHeader("New Quest", ImGuiTreeNodeFlags_DefaultOpen)) {
            char titleBuf[128] = {};
            std::strncpy(titleBuf, tmpl.title.c_str(), sizeof(titleBuf) - 1);
            if (ImGui::InputText("Title##q", titleBuf, sizeof(titleBuf)))
                tmpl.title = titleBuf;

            char descBuf[512] = {};
            std::strncpy(descBuf, tmpl.description.c_str(), sizeof(descBuf) - 1);
            if (ImGui::InputTextMultiline("Description##q", descBuf, sizeof(descBuf), ImVec2(-1, 60)))
                tmpl.description = descBuf;

            int lvl = tmpl.requiredLevel;
            if (ImGui::InputInt("Required Level", &lvl)) tmpl.requiredLevel = std::max(1, lvl);

            // Link to NPCs from spawn list
            auto& spawner = app.getNpcSpawner();
            if (ImGui::BeginCombo("Quest Giver##qg",
                    tmpl.questGiverNpcId > 0 ? std::to_string(tmpl.questGiverNpcId).c_str() : "None")) {
                if (ImGui::Selectable("None")) tmpl.questGiverNpcId = 0;
                for (const auto& s : spawner.getSpawns()) {
                    char lbl[64];
                    std::snprintf(lbl, sizeof(lbl), "%s (id %u)", s.name.c_str(), s.id);
                    if (ImGui::Selectable(lbl)) tmpl.questGiverNpcId = s.id;
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("Turn-in NPC##qt",
                    tmpl.turnInNpcId > 0 ? std::to_string(tmpl.turnInNpcId).c_str() : "None")) {
                if (ImGui::Selectable("None##ti")) tmpl.turnInNpcId = 0;
                for (const auto& s : spawner.getSpawns()) {
                    char lbl[64];
                    std::snprintf(lbl, sizeof(lbl), "%s (id %u)", s.name.c_str(), s.id);
                    if (ImGui::Selectable(lbl)) tmpl.turnInNpcId = s.id;
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();
            ImGui::Text("Objectives (max 4 Kill + 6 Collect):");
            // Limit to 10 total - matches the SQL exporter slot capacity
            // (RequiredNpcOrGo[1..4] + RequiredItemId[1..6]).
            if (tmpl.objectives.size() < 10 && ImGui::Button("Add Objective")) {
                QuestObjective obj;
                obj.description = "Kill 5 creatures";
                tmpl.objectives.push_back(obj);
            }
            for (int oi = 0; oi < static_cast<int>(tmpl.objectives.size()); oi++) {
                auto& obj = tmpl.objectives[oi];
                ImGui::PushID(oi);
                const char* types[] = {"Kill", "Collect", "Talk", "Explore", "Escort", "Use Object"};
                int ti = static_cast<int>(obj.type);
                ImGui::Combo("Type", &ti, types, 6);
                obj.type = static_cast<QuestObjectiveType>(ti);
                char objDesc[128] = {};
                std::strncpy(objDesc, obj.description.c_str(), sizeof(objDesc) - 1);
                if (ImGui::InputText("Desc", objDesc, sizeof(objDesc))) obj.description = objDesc;
                int cnt = obj.targetCount;
                if (ImGui::InputInt("Count", &cnt)) obj.targetCount = std::max(1, cnt);
                // Target ID input - for Kill objectives this is the creature
                // entry, for Collect it's the item ID. SQL export keys off it.
                char targetBuf[64] = {};
                std::strncpy(targetBuf, obj.targetName.c_str(), sizeof(targetBuf) - 1);
                if (ImGui::InputText("Target ID", targetBuf, sizeof(targetBuf)))
                    obj.targetName = targetBuf;
                // For Kill objectives, offer a dropdown of placed NPC entries.
                if (obj.type == QuestObjectiveType::KillCreature) {
                    if (ImGui::BeginCombo("Pick NPC", "(spawn list)")) {
                        for (size_t si = 0; si < spawner.spawnCount(); si++) {
                            const auto& s = spawner.getSpawns()[si];
                            char lbl[96];
                            std::snprintf(lbl, sizeof(lbl), "%s (id %u)", s.name.c_str(), s.id);
                            if (ImGui::Selectable(lbl)) obj.targetName = std::to_string(s.id);
                        }
                        ImGui::EndCombo();
                    }
                }
                if (ImGui::SmallButton("Remove")) tmpl.objectives.erase(tmpl.objectives.begin() + oi--);
                ImGui::PopID();
                ImGui::Separator();
            }

            ImGui::Text("Rewards:");
            int xp = tmpl.reward.xp;
            if (ImGui::InputInt("XP##qr", &xp)) tmpl.reward.xp = std::max(0, xp);
            int gold = tmpl.reward.gold;
            if (ImGui::InputInt("Gold##qr", &gold)) tmpl.reward.gold = std::max(0, gold);

            // Quest chain link
            ImGui::Text("Chain to next quest:");
            if (ImGui::BeginCombo("Next Quest##chain",
                    tmpl.nextQuestId > 0 ? std::to_string(tmpl.nextQuestId).c_str() : "None (end of chain)")) {
                if (ImGui::Selectable("None")) tmpl.nextQuestId = 0;
                for (int qi = 0; qi < static_cast<int>(qe.questCount()); qi++) {
                    auto* eq = qe.getQuest(qi);
                    char ql[128];
                    std::snprintf(ql, sizeof(ql), "[%u] %s", eq->id, eq->title.c_str());
                    if (ImGui::Selectable(ql)) tmpl.nextQuestId = eq->id;
                }
                ImGui::EndCombo();
            }

            char completeBuf[256] = {};
            std::strncpy(completeBuf, tmpl.completionText.c_str(), sizeof(completeBuf) - 1);
            if (ImGui::InputTextMultiline("Completion Text##q", completeBuf, sizeof(completeBuf), ImVec2(-1, 40)))
                tmpl.completionText = completeBuf;

            if (ImGui::Button("Create Quest", ImVec2(-1, 0))) {
                qe.addQuest(tmpl);
                app.showToast("Quest created: " + tmpl.title);
            }
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Quest List", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("%zu quests", qe.questCount());
            static int selectedQuest = -1;
            ImGui::BeginChild("QuestList", ImVec2(0, 150), true);
            for (int i = 0; i < static_cast<int>(qe.questCount()); i++) {
                auto* q = qe.getQuest(i);
                char lbl[128];
                std::snprintf(lbl, sizeof(lbl), "[%u] %s (Lv%u, %zu obj%s)",
                              q->id, q->title.c_str(), q->requiredLevel, q->objectives.size(),
                              q->nextQuestId ? " ->chain" : "");
                if (ImGui::Selectable(lbl, selectedQuest == i))
                    selectedQuest = i;
            }
            ImGui::EndChild();

            if (selectedQuest >= 0 && selectedQuest < static_cast<int>(qe.questCount())) {
                auto* sq = qe.getQuest(selectedQuest);
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Editing: [%u] %s", sq->id, sq->title.c_str());
                char etBuf[128] = {};
                std::strncpy(etBuf, sq->title.c_str(), sizeof(etBuf) - 1);
                if (ImGui::InputText("Title##edit", etBuf, sizeof(etBuf))) sq->title = etBuf;
                int elv = sq->requiredLevel;
                if (ImGui::InputInt("Level##edit", &elv)) sq->requiredLevel = std::max(1, elv);
                int exp = sq->reward.xp;
                if (ImGui::InputInt("XP##edit", &exp)) sq->reward.xp = std::max(0, exp);
                if (sq->nextQuestId > 0)
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1), "Chains to quest %u", sq->nextQuestId);
                if (ImGui::SmallButton("Delete##quest")) {
                    qe.removeQuest(selectedQuest);
                    selectedQuest = -1;
                }
            }

            // Chain validation
            std::vector<std::string> chainErrors;
            if (qe.questCount() > 0 && !qe.validateChains(chainErrors)) {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Chain issues:");
                for (const auto& e : chainErrors)
                    ImGui::BulletText("%s", e.c_str());
            }
        }

        ImGui::Separator();
        static char questPath[256] = "output/quests.json";
        ImGui::InputText("File##quest", questPath, sizeof(questPath));
        if (ImGui::Button("Save Quests", ImVec2(180, 0))) {
            qe.saveToFile(questPath);
            app.showToast("Quests saved");
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Quests", ImVec2(-1, 0))) {
            if (qe.loadFromFile(questPath))
                app.showToast("Loaded " + std::to_string(qe.questCount()) + " quests");
            else
                app.showToast("Failed to load quests");
        }
    }
    ImGui::End();
}

void EditorUI::renderWaterPanel(EditorApp& app) {
    ImGui::SetNextWindowPos(ImVec2(10, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 250), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Water")) {
        if (!app.hasTerrainLoaded()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Load or create terrain first");
            ImGui::End(); return;
        }

        auto& s = app.getTerrainEditor().brush().settings();
        ImGui::SliderFloat("Radius", &s.radius, 5.0f, 200.0f, "%.0f");

        float wh = app.getWaterHeight();
        if (ImGui::SliderFloat("Water Height", &wh, -200.0f, 500.0f, "%.1f"))
            app.setWaterHeight(wh);

        const char* types[] = {"Water", "Ocean", "Magma", "Slime"};
        int typeIdx = app.getWaterType();
        if (ImGui::Combo("Liquid Type", &typeIdx, types, 4))
            app.setWaterType(static_cast<uint16_t>(typeIdx));

        ImGui::Separator();
        if (ImGui::Button("Fill Entire Tile with Water", ImVec2(-1, 0))) {
            app.getTerrainEditor().fillWater(app.getWaterHeight(), app.getWaterType());
            app.showToast("Tile filled with water");
        }
        if (ImGui::Button("Remove Water Under Brush", ImVec2(-1, 0))) {
            auto& brush = app.getTerrainEditor().brush();
            if (brush.isActive()) {
                app.getTerrainEditor().removeWater(brush.getPosition(), s.radius);
            }
        }
        if (ImGui::Button("Smooth Beaches", ImVec2(-1, 0))) {
            app.getTerrainEditor().smoothBeaches(app.getWaterHeight(), 15.0f);
            app.showToast("Beaches smoothed");
        }
        if (ImGui::Button("Remove ALL Water", ImVec2(-1, 0))) {
            for (int ci = 0; ci < 256; ci++)
                app.getTerrainEditor().getTerrain()->waterData[ci].layers.clear();
            app.showToast("All water removed");
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Left-click to place water");
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Rendered as translucent overlay");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Left-click: place");
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Ctrl+click: select");
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Del: remove selected");
    }
    ImGui::End();
}

void EditorUI::renderContextMenu(EditorApp& app) {
    if (app.shouldOpenContextMenu()) {
        ImGui::OpenPopup("ObjectContextMenu");
        app.clearContextMenuFlag();
    }
    if (ImGui::BeginPopup("ObjectContextMenu")) {
        auto* objSel = app.getObjectPlacer().getSelected();
        auto* npcSel = app.getNpcSpawner().getSelected();
        if (!objSel && !npcSel) { ImGui::EndPopup(); return; }

        if (objSel) {
            std::string display = objSel->path;
            auto slash = display.rfind('\\');
            if (slash != std::string::npos) display = display.substr(slash + 1);
            ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "%s", display.c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "%s (NPC)", npcSel->name.c_str());
        }
        ImGui::Separator();

        if (objSel) {
            if (ImGui::MenuItem("Move (G)"))
                app.startGizmoMode(TransformMode::Move);
            if (ImGui::MenuItem("Rotate (R)"))
                app.startGizmoMode(TransformMode::Rotate);
            if (ImGui::MenuItem("Scale (T)"))
                app.startGizmoMode(TransformMode::Scale);
            ImGui::Separator();
            if (ImGui::MenuItem("Snap to Ground"))
                app.snapSelectedToGround();
            if (ImGui::MenuItem("Align to Slope"))
                app.alignSelectedToTerrain();
            if (ImGui::MenuItem("Fly To"))
                app.flyToSelected();
        }
        if (npcSel) {
            if (ImGui::MenuItem("Fly To"))
                app.flyToSelected();
            if (ImGui::MenuItem("Snap to Ground"))
                app.snapSelectedToGround();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                CreatureSpawn copy = *npcSel;
                copy.position += glm::vec3(10, 10, 0);
                app.getNpcSpawner().placeCreature(copy);
                app.markObjectsDirty();
            }
            if (ImGui::MenuItem("Face Camera")) {
                glm::vec3 cam = app.getEditorCamera().getCamera().getPosition();
                glm::vec3 to = cam - npcSel->position;
                npcSel->orientation = glm::degrees(std::atan2(to.y, to.x));
                if (npcSel->orientation < 0.0f) npcSel->orientation += 360.0f;
                app.markObjectsDirty();
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            if (objSel) { app.getObjectPlacer().deleteSelected(); }
            else { app.getNpcSpawner().removeCreature(app.getNpcSpawner().getSelectedIndex()); }
            app.markObjectsDirty();
        }
        if (ImGui::MenuItem("Select All", "Ctrl+A")) {
            app.getObjectPlacer().selectAll();
        }
        if (ImGui::MenuItem("Select All M2 Models")) {
            app.getObjectPlacer().selectByType(PlaceableType::M2);
            app.showToast("Selected " + std::to_string(app.getObjectPlacer().selectionCount()) + " M2 models");
        }
        if (ImGui::MenuItem("Select All WMO Buildings")) {
            app.getObjectPlacer().selectByType(PlaceableType::WMO);
            app.showToast("Selected " + std::to_string(app.getObjectPlacer().selectionCount()) + " WMO buildings");
        }
        if (ImGui::MenuItem("Deselect")) {
            app.getObjectPlacer().clearSelection();
            app.getNpcSpawner().clearSelection();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::renderMinimap(EditorApp& app) {
    if (!app.hasTerrainLoaded()) return;
    auto* terrain = app.getTerrainEditor().getTerrain();
    if (!terrain) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - 185, vp->Size.y - 210), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(175, 185), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    if (ImGui::Begin("Minimap", nullptr, ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float cellW = avail.x / 16.0f;
        float cellH = (avail.y - 14) / 16.0f;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Find height range
        float minH = 1e30f, maxH = -1e30f;
        for (int i = 0; i < 256; i++) {
            auto& c = terrain->chunks[i];
            if (!c.hasHeightMap()) continue;
            for (int v = 0; v < 145; v++) {
                float h = c.position[2] + c.heightMap.heights[v];
                minH = std::min(minH, h); maxH = std::max(maxH, h);
            }
        }
        float range = std::max(1.0f, maxH - minH);

        for (int cy = 0; cy < 16; cy++) {
            for (int cx = 0; cx < 16; cx++) {
                auto& c = terrain->chunks[cy * 16 + cx];
                float avgH = c.position[2];
                if (c.hasHeightMap()) {
                    float sum = 0;
                    for (int v = 0; v < 145; v++) sum += c.heightMap.heights[v];
                    avgH += sum / 145.0f;
                }
                float t = (avgH - minH) / range;

                // Color: low=blue, mid=green, high=brown/white
                float r, g, b;
                if (t < 0.3f) { r = 0.1f; g = 0.2f + t; b = 0.5f - t; }
                else if (t < 0.7f) { float tt = (t - 0.3f) / 0.4f; r = 0.1f + tt * 0.4f; g = 0.5f + tt * 0.2f; b = 0.1f; }
                else { float tt = (t - 0.7f) / 0.3f; r = 0.5f + tt * 0.3f; g = 0.7f - tt * 0.2f; b = 0.1f + tt * 0.5f; }

                ImVec2 p0(origin.x + cx * cellW, origin.y + cy * cellH);
                ImVec2 p1(p0.x + cellW - 1, p0.y + cellH - 1);
                dl->AddRectFilled(p0, p1, IM_COL32(
                    static_cast<int>(r*255), static_cast<int>(g*255),
                    static_cast<int>(b*255), 200));

                // Water indicator
                if (terrain->waterData[cy * 16 + cx].hasWater())
                    dl->AddRectFilled(p0, p1, IM_COL32(50, 100, 200, 100));
            }
        }

        // Draw objects (yellow=normal, white+ring=selected)
        float tileNW_X = (32.0f - static_cast<float>(terrain->coord.y)) * 533.33333f;
        float tileNW_Y = (32.0f - static_cast<float>(terrain->coord.x)) * 533.33333f;
        for (const auto& obj : app.getObjectPlacer().getObjects()) {
            float u = (tileNW_X - obj.position.x) / 533.33333f;
            float v = (tileNW_Y - obj.position.y) / 533.33333f;
            if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
                ImVec2 pt(origin.x + v * avail.x, origin.y + u * (16 * cellH));
                if (obj.selected) {
                    dl->AddCircleFilled(pt, 3.5f, IM_COL32(255, 255, 255, 230));
                    dl->AddCircle(pt, 5.0f, IM_COL32(255, 200, 50, 200), 0, 1.5f);
                } else {
                    dl->AddCircleFilled(pt, 2.0f, IM_COL32(255, 220, 50, 200));
                }
            }
        }
        // Draw NPCs as red dots
        for (const auto& npc : app.getNpcSpawner().getSpawns()) {
            float u = (tileNW_X - npc.position.x) / 533.33333f;
            float v = (tileNW_Y - npc.position.y) / 533.33333f;
            if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
                ImVec2 pt(origin.x + v * avail.x, origin.y + u * (16 * cellH));
                dl->AddCircleFilled(pt, 2.5f, npc.hostile ? IM_COL32(255, 60, 60, 200)
                                                           : IM_COL32(60, 200, 60, 200));
            }
        }

        ImGui::Dummy(ImVec2(avail.x, 16 * cellH));
        // Click minimap to move camera
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float mu = (mousePos.y - origin.y) / (16 * cellH);
            float mv = (mousePos.x - origin.x) / avail.x;
            if (mu >= 0 && mu <= 1 && mv >= 0 && mv <= 1) {
                float wx = tileNW_X - mu * 533.33333f;
                float wy = tileNW_Y - mv * 533.33333f;
                app.getEditorCamera().setPosition(glm::vec3(wx, wy,
                    app.getEditorCamera().getCamera().getPosition().z));
            }
        }

        // Camera indicator as white cross
        auto camPos = app.getEditorCamera().getCamera().getPosition();
        float camU = (tileNW_X - camPos.x) / 533.33333f;
        float camV = (tileNW_Y - camPos.y) / 533.33333f;
        if (camU >= 0 && camU <= 1 && camV >= 0 && camV <= 1) {
            ImVec2 cp(origin.x + camV * avail.x, origin.y + camU * (16 * cellH));
            dl->AddLine(ImVec2(cp.x - 3, cp.y), ImVec2(cp.x + 3, cp.y), IM_COL32(255,255,255,220), 2);
            dl->AddLine(ImVec2(cp.x, cp.y - 3), ImVec2(cp.x, cp.y + 3), IM_COL32(255,255,255,220), 2);
        }

        // Brush radius indicator on minimap
        if (app.getTerrainEditor().brush().isActive()) {
            auto bp = app.getTerrainEditor().brush().getPosition();
            float bu = (tileNW_X - bp.x) / 533.33333f;
            float bv = (tileNW_Y - bp.y) / 533.33333f;
            if (bu >= 0 && bu <= 1 && bv >= 0 && bv <= 1) {
                ImVec2 bc(origin.x + bv * avail.x, origin.y + bu * (16 * cellH));
                float bRadius = app.getTerrainEditor().brush().settings().radius / 533.33333f;
                float pixRadius = bRadius * avail.x;
                dl->AddCircle(bc, pixRadius, IM_COL32(255, 255, 100, 150), 16, 1.5f);
            }
        }

        // Slope/collision overlay (steep chunks highlighted red)
        static bool showSlopeOverlay = false;
        if (showSlopeOverlay) {
            float steepCos = std::cos(50.0f * 3.14159f / 180.0f);
            float unitSize = 533.33333f / 16.0f / 8.0f;
            for (int cy2 = 0; cy2 < 16; cy2++) {
                for (int cx2 = 0; cx2 < 16; cx2++) {
                    const auto& c = terrain->chunks[cy2 * 16 + cx2];
                    if (!c.hasHeightMap()) continue;
                    int steepVerts = 0;
                    for (int row = 0; row < 8; row++) {
                        for (int col2 = 0; col2 < 8; col2++) {
                            int i = row * 17 + col2;
                            float h00 = c.heightMap.heights[i];
                            float h10 = c.heightMap.heights[i + 1];
                            float h01 = c.heightMap.heights[i + 17];
                            float dzdx = (h10 - h00) / unitSize;
                            float dzdy = (h01 - h00) / unitSize;
                            float nz = 1.0f / std::sqrt(1.0f + dzdx*dzdx + dzdy*dzdy);
                            if (nz < steepCos) steepVerts++;
                        }
                    }
                    if (steepVerts > 0) {
                        float intensity = std::min(1.0f, steepVerts / 20.0f);
                        ImVec2 p0(origin.x + cx2 * cellW, origin.y + cy2 * cellH);
                        ImVec2 p1(p0.x + cellW - 1, p0.y + cellH - 1);
                        dl->AddRectFilled(p0, p1, IM_COL32(255, 30, 30,
                            static_cast<int>(intensity * 150)));
                    }
                }
            }
        }

        // Hole indicators (dark X marks)
        for (int cy2 = 0; cy2 < 16; cy2++) {
            for (int cx2 = 0; cx2 < 16; cx2++) {
                if (terrain->chunks[cy2 * 16 + cx2].holes) {
                    ImVec2 hp(origin.x + cx2 * cellW + cellW * 0.5f,
                              origin.y + cy2 * cellH + cellH * 0.5f);
                    dl->AddText(ImVec2(hp.x - 3, hp.y - 5), IM_COL32(0, 0, 0, 200), "H");
                }
            }
        }

        ImGui::Dummy(ImVec2(avail.x, 16 * cellH));
        // Legend
        ImDrawList* dl2 = ImGui::GetWindowDrawList();
        ImVec2 legPos = ImGui::GetCursorScreenPos();
        dl2->AddCircleFilled(ImVec2(legPos.x + 5, legPos.y + 5), 3, IM_COL32(255, 220, 50, 200));
        dl2->AddCircleFilled(ImVec2(legPos.x + 45, legPos.y + 5), 3, IM_COL32(255, 60, 60, 200));
        dl2->AddCircleFilled(ImVec2(legPos.x + 100, legPos.y + 5), 3, IM_COL32(60, 200, 60, 200));
        ImGui::Text("  Obj   Hostile  Friendly  +Cam  H=Hole");
        ImGui::Checkbox("Show Slopes (collision)", &showSlopeOverlay);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorUI::renderPropertiesPanel(EditorApp& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - 280, 90), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(270, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Info")) {
        auto* tr = app.getTerrainRenderer();
        if (tr && tr->getChunkCount() > 0) {
            static char renameBuf[128] = {};
            std::strncpy(renameBuf, app.getLoadedMap().c_str(), sizeof(renameBuf) - 1);
            ImGui::SetNextItemWidth(140);
            if (ImGui::InputText("##mapname", renameBuf, sizeof(renameBuf),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (app.setMapName(renameBuf))
                    app.showToast("Zone renamed: " + std::string(renameBuf));
                else
                    app.showToast("Invalid name (no slashes or special chars)");
            }
            ImGui::SameLine();
            ImGui::Text("[%d, %d]", app.getLoadedTileX(), app.getLoadedTileY());
            ImGui::Text("Chunks: %d  Tris: %d", tr->getChunkCount(), tr->getTriangleCount());
            ImGui::Text("Objects: %zu  NPCs: %zu  Q: %zu",
                        app.getObjectPlacer().objectCount(),
                        app.getNpcSpawner().spawnCount(),
                        app.getQuestEditor().questCount());

            // Zone metadata (mapId, displayName, flags)
            if (ImGui::CollapsingHeader("Zone Metadata")) {
                auto& m = app.getZoneManifest();
                int mid = static_cast<int>(m.mapId);
                if (ImGui::InputInt("Map ID", &mid))
                    m.mapId = static_cast<uint32_t>(std::clamp(mid, 0, 65535));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Custom zones: 9000-12000. Must be unique per server.");

                char dispBuf[128] = {};
                std::strncpy(dispBuf, m.displayName.c_str(), sizeof(dispBuf) - 1);
                if (ImGui::InputText("Display Name", dispBuf, sizeof(dispBuf)))
                    m.displayName = dispBuf;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Name shown in-game on the world map and loading screen");

                char descBuf[256] = {};
                std::strncpy(descBuf, m.description.c_str(), sizeof(descBuf) - 1);
                if (ImGui::InputTextMultiline("Description##zone", descBuf, sizeof(descBuf), ImVec2(-1, 40)))
                    m.description = descBuf;

                ImGui::Separator();
                ImGui::Text("Zone Flags:");
                ImGui::Checkbox("Allow Flying", &m.allowFlying);
                ImGui::SameLine();
                ImGui::Checkbox("PvP", &m.pvpEnabled);
                ImGui::Checkbox("Indoor", &m.isIndoor);
                ImGui::SameLine();
                ImGui::Checkbox("Sanctuary", &m.isSanctuary);
            }
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No terrain loaded");
        }
        if (auto* t = app.getTerrainEditor().getTerrain()) {
            ImGui::Text("Textures: %zu", t->textures.size());
            int waterChunks = 0;
            int holeChunks = 0;
            for (int i = 0; i < 256; i++) {
                if (t->waterData[i].hasWater()) waterChunks++;
                if (t->chunks[i].holes) holeChunks++;
            }
            if (waterChunks > 0) ImGui::Text("Water chunks: %d", waterChunks);
            if (holeChunks > 0) ImGui::Text("Hole chunks: %d", holeChunks);
        }

        // M2 render diagnostics
        if (auto* m2r = app.getM2Renderer()) {
            ImGui::Text("M2: %u models, %u instances, %u draws",
                        m2r->getModelCount(), m2r->getInstanceCount(),
                        m2r->getDrawCallCount());
        }

        ImGui::Separator();
        auto pos = app.getEditorCamera().getCamera().getPosition();
        ImGui::Text("Camera: %.0f, %.0f, %.0f", pos.x, pos.y, pos.z);
        ImGui::Text("Speed: %.0f (Shift+scroll)", app.getEditorCamera().getSpeed());

        // Cursor world position + chunk info
        auto& brush = app.getTerrainEditor().brush();
        if (brush.isActive()) {
            auto bp = brush.getPosition();
            ImGui::Text("Cursor: %.1f, %.1f, %.1f", bp.x, bp.y, bp.z);

            // Show active texture in paint mode
            if (app.getMode() == EditorMode::Paint) {
                auto& tex = app.getTexturePainter().getActiveTexture();
                if (!tex.empty()) {
                    auto lastSlash = tex.rfind('\\');
                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Texture: %s",
                        lastSlash != std::string::npos ? tex.c_str() + lastSlash + 1 : tex.c_str());
                }
            }
        }

        ImGui::Separator();
        // Undo/redo status
        auto& hist = app.getTerrainEditor().history();
        if (hist.canUndo() || hist.canRedo())
            ImGui::Text("Undo: %zu  Redo: %zu", hist.undoCount(), hist.redoCount());

        ImGui::Text("Quests: %zu", app.getQuestEditor().questCount());

        // Terrain height stats
        if (auto* t = app.getTerrainEditor().getTerrain()) {
            float minH = 1e30f, maxH = -1e30f, sumH = 0;
            int count = 0;
            for (int ci = 0; ci < 256; ci++) {
                if (!t->chunks[ci].hasHeightMap()) continue;
                for (int v = 0; v < 145; v++) {
                    float h = t->chunks[ci].position[2] + t->chunks[ci].heightMap.heights[v];
                    minH = std::min(minH, h); maxH = std::max(maxH, h);
                    sumH += h; count++;
                }
            }
            if (count > 0)
                ImGui::Text("Height: %.0f-%.0f (avg %.0f)", minH, maxH, sumH / count);
        }

        // Zone audio configuration
        if (app.hasTerrainLoaded() && ImGui::CollapsingHeader("Zone Audio")) {
            auto& manifest = app.getZoneManifest();
            static char musicBuf[256] = {};
            static char ambDayBuf[256] = {};
            static char ambNightBuf[256] = {};
            std::strncpy(musicBuf, manifest.musicTrack.c_str(), sizeof(musicBuf) - 1);
            std::strncpy(ambDayBuf, manifest.ambienceDay.c_str(), sizeof(ambDayBuf) - 1);
            std::strncpy(ambNightBuf, manifest.ambienceNight.c_str(), sizeof(ambNightBuf) - 1);

            // Lazy-loaded scan of available music files. Walks
            // <data>/Sound/Music recursively and grabs any .mp3/.wav/
            // .ogg. Cached after first build; "Refresh" rebuilds the
            // list. The dropdown then offers each scanned file as a
            // selectable preset alongside the manual text input below.
            static std::vector<std::string> musicScan;
            static std::vector<std::string> ambienceScan;
            static bool scanned = false;
            auto rescan = [&]() {
                musicScan.clear();
                ambienceScan.clear();
                namespace fs = std::filesystem;
                std::string dp = app.getDataPath();
                if (dp.empty()) dp = "Data";
                std::string musicRoot = dp + "/Sound/Music";
                std::string ambRoot   = dp + "/Sound/Ambience";
                auto walk = [](const std::string& root,
                                std::vector<std::string>& out) {
                    std::error_code ec;
                    if (!fs::exists(root)) return;
                    for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
                        if (!e.is_regular_file()) continue;
                        std::string ext = e.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        if (ext == ".mp3" || ext == ".wav" || ext == ".ogg") {
                            out.push_back(e.path().string());
                        }
                    }
                    std::sort(out.begin(), out.end());
                };
                walk(musicRoot, musicScan);
                walk(ambRoot, ambienceScan);
                scanned = true;
            };
            if (!scanned) rescan();
            if (ImGui::SmallButton("Refresh##audioScan")) rescan();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                "%zu music / %zu ambience files",
                musicScan.size(), ambienceScan.size());
            // Music dropdown.
            if (ImGui::BeginCombo("Music File##audio",
                                    manifest.musicTrack.empty()
                                    ? "(none)"
                                    : manifest.musicTrack.c_str())) {
                if (ImGui::Selectable("(none)", manifest.musicTrack.empty())) {
                    manifest.musicTrack.clear();
                    musicBuf[0] = 0;
                }
                for (const auto& p : musicScan) {
                    bool sel = (p == manifest.musicTrack);
                    if (ImGui::Selectable(p.c_str(), sel)) {
                        manifest.musicTrack = p;
                        std::strncpy(musicBuf, p.c_str(), sizeof(musicBuf) - 1);
                        musicBuf[sizeof(musicBuf) - 1] = 0;
                    }
                }
                ImGui::EndCombo();
            }
            // "Play preview" via OS default audio player. Avoids
            // pulling SDL_mixer into the editor binary just for the
            // preview workflow - the user already has a working audio
            // player and this hands the file off to it. macOS uses
            // 'open', most Linux desktops use 'xdg-open', Windows
            // uses 'start ""'.
            auto playFile = [](const std::string& path) {
                if (path.empty()) return;
                // Open file in OS default app without invoking a shell -
                // CodeQL flagged the previous std::system path as
                // command-injection because `path` is concatenated into
                // a shell command. Use platform-native exec instead.
                wowee::editor::cli::runChild(
#if defined(__APPLE__)
                    "/usr/bin/open",
#elif defined(_WIN32)
                    // `start` is a cmd.exe builtin, not an .exe - use the
                    // explorer fallback which works with CreateProcess.
                    "explorer.exe",
#else
                    "/usr/bin/xdg-open",
#endif
                    {path}, /*quiet=*/true);
            };
            if (!manifest.musicTrack.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Play##music")) playFile(manifest.musicTrack);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open in OS default audio player");
            }
            // Day-ambience dropdown.
            if (ImGui::BeginCombo("Ambience File##audio",
                                    manifest.ambienceDay.empty()
                                    ? "(none)"
                                    : manifest.ambienceDay.c_str())) {
                if (ImGui::Selectable("(none)", manifest.ambienceDay.empty())) {
                    manifest.ambienceDay.clear();
                    ambDayBuf[0] = 0;
                }
                for (const auto& p : ambienceScan) {
                    bool sel = (p == manifest.ambienceDay);
                    if (ImGui::Selectable(p.c_str(), sel)) {
                        manifest.ambienceDay = p;
                        std::strncpy(ambDayBuf, p.c_str(), sizeof(ambDayBuf) - 1);
                        ambDayBuf[sizeof(ambDayBuf) - 1] = 0;
                    }
                }
                ImGui::EndCombo();
            }
            if (!manifest.ambienceDay.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Play##ambDay")) playFile(manifest.ambienceDay);
            }

            if (ImGui::InputText("Music##audio", musicBuf, sizeof(musicBuf)))
                manifest.musicTrack = musicBuf;
            if (ImGui::InputText("Ambience (Day)##audio", ambDayBuf, sizeof(ambDayBuf)))
                manifest.ambienceDay = ambDayBuf;
            if (ImGui::InputText("Ambience (Night)##audio", ambNightBuf, sizeof(ambNightBuf)))
                manifest.ambienceNight = ambNightBuf;
            ImGui::SliderFloat("Music Vol", &manifest.musicVolume, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Ambience Vol", &manifest.ambienceVolume, 0.0f, 1.0f, "%.2f");

            if (ImGui::BeginCombo("Presets##audioPreset", "Select...")) {
                if (ImGui::Selectable("Elwynn Forest")) {
                    manifest.musicTrack = "Sound\\Music\\ZoneMusic\\Forest\\ForestDay.mp3";
                    manifest.ambienceDay = "Sound\\Ambience\\GlueScreenAmbience.wav";
                }
                if (ImGui::Selectable("Durotar")) {
                    manifest.musicTrack = "Sound\\Music\\ZoneMusic\\Barrens\\BarrensDay.mp3";
                    manifest.ambienceDay = "Sound\\Ambience\\GlueScreenAmbience.wav";
                }
                if (ImGui::Selectable("Darkshore")) {
                    manifest.musicTrack = "Sound\\Music\\ZoneMusic\\Darkshore\\DarkshoreDay.mp3";
                    manifest.ambienceDay = "Sound\\Ambience\\GlueScreenAmbience.wav";
                }
                if (ImGui::Selectable("Dungeon")) {
                    manifest.musicTrack = "Sound\\Music\\ZoneMusic\\Dungeon\\DungeonAmbience.mp3";
                    manifest.ambienceDay = "";
                }
                if (ImGui::Selectable("None (silent)")) {
                    manifest.musicTrack = "";
                    manifest.ambienceDay = "";
                    manifest.ambienceNight = "";
                }
                ImGui::EndCombo();
            }
        }

        if (app.getTerrainEditor().hasUnsavedChanges())
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "* Unsaved (Ctrl+S to save)");
        else
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "Saved (open format)");
    }
    ImGui::End();
}

void EditorUI::renderStatusBar(EditorApp& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = 24.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        const char* ms[] = {"Sculpt", "Paint", "Objects", "Water", "NPCs", "Quests"};
        const char* m = ms[static_cast<int>(app.getMode())];
        if (app.hasTerrainLoaded()) {
            bool dirty = app.getTerrainEditor().hasUnsavedChanges() ||
                         app.hasUnsavedNonTerrainChanges();
            ImGui::Text("[%s] %s [%d,%d] %s%s", m, app.getLoadedMap().c_str(),
                        app.getLoadedTileX(), app.getLoadedTileY(),
                        getBiomeName(app.getActiveBiome()),
                        dirty ? " *" : "");
            ImGui::SameLine(vp->Size.x * 0.35f);
            ImGui::Text("Obj:%zu NPC:%zu Q:%zu",
                        app.getObjectPlacer().objectCount(),
                        app.getNpcSpawner().spawnCount(),
                        app.getQuestEditor().questCount());
            ImGui::SameLine(vp->Size.x * 0.6f);
            auto& hist = app.getTerrainEditor().history();
            if (hist.undoCount() > 0 || hist.redoCount() > 0)
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                    "Undo:%zu Redo:%zu", hist.undoCount(), hist.redoCount());
        } else {
            ImGui::Text("[%s] Wowee World Editor", m);
        }
        // Camera (yellow) and cursor (cyan) world coords for navigation/debug.
        if (app.hasTerrainLoaded()) {
            glm::vec3 cp = app.getEditorCamera().getCamera().getPosition();
            const auto& brush = app.getTerrainEditor().brush();
            ImGui::SameLine(vp->Size.x - 460);
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.5f, 1.0f),
                "Cam:(%.0f,%.0f,%.0f)", cp.x, cp.y, cp.z);
            if (brush.isActive()) {
                glm::vec3 bp = brush.getPosition();
                ImGui::SameLine(vp->Size.x - 270);
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.85f, 1.0f),
                    "Cur:(%.0f,%.0f,%.0f)", bp.x, bp.y, bp.z);
            }
        }
        ImGui::SameLine(vp->Size.x - 120);
        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace editor
} // namespace wowee
