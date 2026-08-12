#include "object_placer.hpp"
#include "cli_catalog_paths.hpp"
#include "terrain_biomes.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <random>

namespace wowee {
namespace editor {

void ObjectPlacer::setActivePath(const std::string& path, PlaceableType type) {
    activePath_ = path;
    activeType_ = type;
}

uint32_t ObjectPlacer::nextUniqueId() {
    return uniqueIdCounter_++;
}

void ObjectPlacer::placeObject(const glm::vec3& position) {
    if (activePath_.empty()) return;

    PlacedObject obj;
    obj.type = activeType_;
    obj.path = activePath_;
    obj.nameId = 0;
    obj.uniqueId = nextUniqueId();
    obj.position = position;
    float rotY = placementRotY_;
    if (randomRotation_) {
        static std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(0.0f, 360.0f);
        rotY = dist(rng);
    }
    obj.rotation = glm::vec3(0.0f, rotY, 0.0f);
    obj.scale = placementScale_;
    obj.selected = false;

    objects_.push_back(obj);
    undoStack_.push_back(static_cast<int>(objects_.size() - 1));
    if (undoStack_.size() > 50) undoStack_.erase(undoStack_.begin());
    LOG_INFO("Placed ", (activeType_ == PlaceableType::M2 ? "M2" : "WMO"),
             ": ", activePath_, " at (", position.x, ",", position.y, ",", position.z, ")");
}

int ObjectPlacer::selectAt(const rendering::Ray& ray, float maxDist) {
    clearSelection();
    // Reject NaN ray - without this every disc < 0 short-circuit returns
    // false and we'd 'hit' every object with garbage t values.
    if (!std::isfinite(ray.origin.x) || !std::isfinite(ray.origin.y) ||
        !std::isfinite(ray.origin.z) || !std::isfinite(ray.direction.x) ||
        !std::isfinite(ray.direction.y) || !std::isfinite(ray.direction.z) ||
        !std::isfinite(maxDist)) {
        return -1;
    }

    float bestDist = maxDist;
    int bestIdx = -1;

    for (int i = 0; i < static_cast<int>(objects_.size()); i++) {
        // Skip objects with NaN position/scale - would feed NaN into the
        // sphere test (NaN comparisons short-circuit to false → "hit").
        if (!std::isfinite(objects_[i].position.x) ||
            !std::isfinite(objects_[i].position.y) ||
            !std::isfinite(objects_[i].position.z) ||
            !std::isfinite(objects_[i].scale)) continue;
        // Simple sphere test (radius based on scale)
        float radius = 5.0f * objects_[i].scale;
        glm::vec3 oc = ray.origin - objects_[i].position;
        float b = glm::dot(oc, ray.direction);
        float c = glm::dot(oc, oc) - radius * radius;
        float disc = b * b - c;
        if (disc < 0) continue;

        float t = -b - std::sqrt(disc);
        if (t < 0) t = -b + std::sqrt(disc);
        if (t > 0 && t < bestDist) {
            bestDist = t;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0) {
        selectedIdx_ = bestIdx;
        objects_[bestIdx].selected = true;
    }
    return bestIdx;
}

void ObjectPlacer::addToSelection(int idx) {
    if (idx < 0 || idx >= static_cast<int>(objects_.size())) return;
    for (int si : selectedIndices_) { if (si == idx) return; }
    selectedIndices_.push_back(idx);
    objects_[idx].selected = true;
    selectedIdx_ = idx;
}

void ObjectPlacer::toggleSelection(int idx) {
    if (idx < 0 || idx >= static_cast<int>(objects_.size())) return;
    auto it = std::find(selectedIndices_.begin(), selectedIndices_.end(), idx);
    if (it != selectedIndices_.end()) {
        objects_[idx].selected = false;
        selectedIndices_.erase(it);
        selectedIdx_ = selectedIndices_.empty() ? -1 : selectedIndices_.back();
    } else {
        addToSelection(idx);
    }
}

void ObjectPlacer::clearSelection() {
    for (int idx : selectedIndices_) {
        if (idx >= 0 && idx < static_cast<int>(objects_.size()))
            objects_[idx].selected = false;
    }
    selectedIndices_.clear();
    if (selectedIdx_ >= 0 && selectedIdx_ < static_cast<int>(objects_.size()))
        objects_[selectedIdx_].selected = false;
    selectedIdx_ = -1;
}

PlacedObject* ObjectPlacer::getSelected() {
    if (selectedIdx_ < 0 || selectedIdx_ >= static_cast<int>(objects_.size())) return nullptr;
    return &objects_[selectedIdx_];
}

void ObjectPlacer::selectAll() {
    clearSelection();
    for (int i = 0; i < static_cast<int>(objects_.size()); i++) {
        objects_[i].selected = true;
        selectedIndices_.push_back(i);
    }
    if (!objects_.empty()) selectedIdx_ = 0;
}

void ObjectPlacer::selectByType(PlaceableType type) {
    clearSelection();
    for (int i = 0; i < static_cast<int>(objects_.size()); i++) {
        if (objects_[i].type == type) {
            objects_[i].selected = true;
            selectedIndices_.push_back(i);
        }
    }
    if (!selectedIndices_.empty()) selectedIdx_ = selectedIndices_[0];
}

void ObjectPlacer::moveSelected(const glm::vec3& delta) {
    // NaN delta would poison every selected position permanently -
    // the renderer would then produce NaN model matrices.
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        !std::isfinite(delta.z)) return;
    if (selectedIndices_.size() > 1) {
        for (int idx : selectedIndices_) objects_[idx].position += delta;
    } else if (auto* obj = getSelected()) {
        obj->position += delta;
    }
}

void ObjectPlacer::rotateSelected(const glm::vec3& deltaDeg) {
    if (!std::isfinite(deltaDeg.x) || !std::isfinite(deltaDeg.y) ||
        !std::isfinite(deltaDeg.z)) return;
    if (selectedIndices_.size() > 1) {
        for (int idx : selectedIndices_) objects_[idx].rotation += deltaDeg;
    } else if (auto* obj = getSelected()) {
        obj->rotation += deltaDeg;
    }
}

void ObjectPlacer::scaleSelected(float delta) {
    if (!std::isfinite(delta)) return;
    if (selectedIndices_.size() > 1) {
        for (int idx : selectedIndices_)
            objects_[idx].scale = std::max(0.1f, objects_[idx].scale + delta);
    } else if (auto* obj = getSelected()) {
        obj->scale = std::max(0.1f, obj->scale + delta);
    }
}

void ObjectPlacer::deleteSelected() {
    if (!selectedIndices_.empty()) {
        std::sort(selectedIndices_.begin(), selectedIndices_.end(), std::greater<int>());
        for (int idx : selectedIndices_) {
            if (idx >= 0 && idx < static_cast<int>(objects_.size()))
                objects_.erase(objects_.begin() + idx);
        }
        selectedIndices_.clear();
        selectedIdx_ = -1;
    } else if (selectedIdx_ >= 0 && selectedIdx_ < static_cast<int>(objects_.size())) {
        objects_.erase(objects_.begin() + selectedIdx_);
        selectedIdx_ = -1;
    }
}

void ObjectPlacer::scatter(const glm::vec3& center, float radius, int count,
                            float minScale, float maxScale) {
    if (activePath_.empty()) return;
    // Defensive bounds - UI sliders cap these, but the function is also
    // callable programmatically. count > 100k would freeze the editor;
    // minScale >= maxScale violates uniform_real_distribution preconditions.
    if (count <= 0 || count > 100'000) return;
    if (!std::isfinite(radius) || radius < 0.0f) return;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(center.z)) return;
    if (!std::isfinite(minScale) || !std::isfinite(maxScale)) return;
    if (minScale <= 0.0f) minScale = 0.01f;
    if (maxScale < minScale) maxScale = minScale + 0.01f;

    std::mt19937 rng(static_cast<uint32_t>(center.x * 100 + center.y * 37 + objects_.size()));
    std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> distDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> distRot(0.0f, 360.0f);
    std::uniform_real_distribution<float> distScale(minScale, maxScale);

    for (int i = 0; i < count; i++) {
        float angle = distAngle(rng);
        float dist = std::sqrt(distDist(rng)) * radius;
        glm::vec3 pos = center + glm::vec3(std::cos(angle) * dist, std::sin(angle) * dist, 0.0f);

        PlacedObject obj;
        obj.type = activeType_;
        obj.path = activePath_;
        obj.nameId = 0;
        obj.uniqueId = nextUniqueId();
        obj.position = pos;
        obj.rotation = glm::vec3(0.0f, distRot(rng), 0.0f);
        obj.scale = distScale(rng);
        obj.selected = false;
        objects_.push_back(obj);
    }
    LOG_INFO("Scattered ", count, " objects in radius ", radius);
}

int ObjectPlacer::populateBiome(const BiomeVegetation& vegetation,
                                float tileSize, const glm::vec3& tileOrigin,
                                uint32_t seed) {
    int placed = 0;
    if (!std::isfinite(tileSize) || tileSize <= 0.0f) return 0;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distPos(0.0f, 1.0f);
    std::uniform_real_distribution<float> distRot(0.0f, 360.0f);

    for (const auto& asset : vegetation.assets) {
        // Calculate object count from density (per 100x100 area)
        float areaFactor = (tileSize * tileSize) / 10000.0f;
        int count = static_cast<int>(asset.density * areaFactor);
        // Cap per-asset count - a runaway density value would freeze the
        // editor and exceed sensible vertex/draw limits for the tile.
        if (count > 50'000) count = 50'000;
        if (count <= 0) continue;

        // Ensure the scale distribution preconditions hold (a < b, both
        // positive). Asset definitions are normally edited in code but we
        // also load them from JSON in some setups.
        float minS = std::isfinite(asset.minScale) ? asset.minScale : 1.0f;
        float maxS = std::isfinite(asset.maxScale) ? asset.maxScale : minS + 0.01f;
        if (minS <= 0.0f) minS = 0.01f;
        if (maxS < minS) maxS = minS + 0.01f;
        std::uniform_real_distribution<float> distScale(minS, maxS);

        for (int i = 0; i < count; i++) {
            float u = distPos(rng);
            float v = distPos(rng);
            glm::vec3 pos = tileOrigin + glm::vec3(
                -u * tileSize, -v * tileSize, 0.0f);

            PlacedObject obj;
            obj.type = PlaceableType::M2;
            obj.path = asset.path;
            obj.nameId = 0;
            obj.uniqueId = nextUniqueId();
            obj.position = pos;
            obj.rotation = glm::vec3(0.0f, distRot(rng), 0.0f);
            obj.scale = distScale(rng);
            obj.selected = false;
            objects_.push_back(obj);
            placed++;
        }
    }

    LOG_INFO("Biome populated: ", vegetation.name, " - ", placed, " objects placed");
    return placed;
}

void ObjectPlacer::undoLastPlace() {
    if (undoStack_.empty()) return;
    int idx = undoStack_.back();
    undoStack_.pop_back();
    if (idx >= 0 && idx < static_cast<int>(objects_.size())) {
        if (selectedIdx_ == idx) selectedIdx_ = -1;
        else if (selectedIdx_ > idx) selectedIdx_--;
        objects_.erase(objects_.begin() + idx);
        // Adjust remaining undo indices
        for (auto& i : undoStack_) { if (i > idx) i--; }
    }
}

bool ObjectPlacer::saveToFile(const std::string& path) const {
    cli::ensureParentDirectory(path);

    // nlohmann::json throws on NaN/inf serialization. Scrub on the way
    // out so a bad in-memory transform can't kill the whole save.
    auto san = [](float x) { return std::isfinite(x) ? x : 0.0f; };
    auto sanScale = [](float x) { return (std::isfinite(x) && x > 0.0f) ? x : 1.0f; };
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& o : objects_) {
        arr.push_back({
            {"type", static_cast<int>(o.type)},
            {"path", o.path},
            {"pos", {san(o.position.x), san(o.position.y), san(o.position.z)}},
            {"rot", {san(o.rotation.x), san(o.rotation.y), san(o.rotation.z)}},
            {"scale", sanScale(o.scale)},
            {"uniqueId", o.uniqueId}
        });
    }

    std::ofstream f(path);
    if (!f) return false;
    f << arr.dump(2) << "\n";
    LOG_INFO("Objects saved: ", path, " (", objects_.size(), " objects)");
    return true;
}

bool ObjectPlacer::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    try {
        auto arr = nlohmann::json::parse(f);
        if (!arr.is_array()) return false;

        objects_.clear();
        undoStack_.clear();
        selectedIdx_ = -1;
        selectedIndices_.clear();
        uniqueIdCounter_ = 1;

        // Cap object count - a stale autosave or biome-populate runaway
        // could produce 100k+ entries that bloat the renderer instance
        // SSBO and drag the editor framerate to single digits.
        constexpr size_t kMaxObjects = 100'000;

        for (const auto& jo : arr) {
            if (objects_.size() >= kMaxObjects) {
                LOG_WARNING("Object cap reached (", kMaxObjects,
                            ") - remaining entries dropped");
                break;
            }
            PlacedObject obj;
            obj.type = static_cast<PlaceableType>(jo.value("type", 0));
            obj.path = jo.value("path", "");
            obj.scale = jo.value("scale", 1.0f);
            // Guard against corrupted/partial-write JSON: clamp invalid scale.
            if (!std::isfinite(obj.scale) || obj.scale <= 0.0001f) obj.scale = 1.0f;

            if (jo.contains("pos") && jo["pos"].is_array() && jo["pos"].size() >= 3) {
                obj.position = glm::vec3(jo["pos"][0].get<float>(),
                                         jo["pos"][1].get<float>(),
                                         jo["pos"][2].get<float>());
                if (!std::isfinite(obj.position.x) || !std::isfinite(obj.position.y) ||
                    !std::isfinite(obj.position.z)) {
                    obj.position = glm::vec3(0.0f);
                }
            }
            if (jo.contains("rot") && jo["rot"].is_array() && jo["rot"].size() >= 3) {
                obj.rotation = glm::vec3(jo["rot"][0].get<float>(),
                                         jo["rot"][1].get<float>(),
                                         jo["rot"][2].get<float>());
                if (!std::isfinite(obj.rotation.x) || !std::isfinite(obj.rotation.y) ||
                    !std::isfinite(obj.rotation.z)) {
                    obj.rotation = glm::vec3(0.0f);
                }
            }

            if (!obj.path.empty()) {
                // Preserve original uniqueId from JSON if present so ADT round-trip
                // is stable. Bump uniqueIdCounter_ past any loaded value to avoid
                // collisions with future placements.
                if (jo.contains("uniqueId")) {
                    obj.uniqueId = jo["uniqueId"].get<uint32_t>();
                    if (obj.uniqueId >= uniqueIdCounter_)
                        uniqueIdCounter_ = obj.uniqueId + 1;
                } else {
                    obj.uniqueId = nextUniqueId();
                }
                objects_.push_back(obj);
            }
        }

        // Restore active path from last loaded object for seamless placement
        if (!objects_.empty()) {
            activePath_ = objects_.back().path;
            activeType_ = objects_.back().type;
        }

        LOG_INFO("Objects loaded: ", path, " (", objects_.size(), " objects)");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse objects file: ", e.what());
        return false;
    }
}

void ObjectPlacer::syncToTerrain() {
    if (!terrain_) return;

    terrain_->doodadNames.clear();
    terrain_->doodadPlacements.clear();
    terrain_->wmoNames.clear();
    terrain_->wmoPlacements.clear();

    // Build name lists and placements
    std::vector<std::string> m2Names, wmoNames;

    for (const auto& obj : objects_) {
        if (obj.type == PlaceableType::M2) {
            // Find or add name
            uint32_t nameId = 0;
            for (uint32_t i = 0; i < m2Names.size(); i++) {
                if (m2Names[i] == obj.path) { nameId = i; goto foundM2; }
            }
            nameId = static_cast<uint32_t>(m2Names.size());
            m2Names.push_back(obj.path);
            foundM2:

            // Editor stores positions in render/world coords; ADT files use
            // ADT-space placement coords. Convert back so re-loading the saved
            // ADT yields identical world positions.
            // Inverse of the load-time transform:
            //   load: obj.rot = (-dp.rot[2], -dp.rot[0], dp.rot[1] + 180)
            //   save: dp.rot  = (-obj.rot.y, obj.rot.z - 180, -obj.rot.x)
            glm::vec3 adtPos = core::coords::worldToAdt(obj.position);
            pipeline::ADTTerrain::DoodadPlacement dp{};
            dp.nameId = nameId;
            dp.uniqueId = obj.uniqueId;
            dp.position[0] = adtPos.x;
            dp.position[1] = adtPos.y;
            dp.position[2] = adtPos.z;
            dp.rotation[0] = -obj.rotation.y;
            dp.rotation[1] = obj.rotation.z - 180.0f;
            dp.rotation[2] = -obj.rotation.x;
            dp.scale = static_cast<uint16_t>(obj.scale * 1024.0f);
            dp.flags = 0;
            terrain_->doodadPlacements.push_back(dp);

        } else {
            uint32_t nameId = 0;
            for (uint32_t i = 0; i < wmoNames.size(); i++) {
                if (wmoNames[i] == obj.path) { nameId = i; goto foundWMO; }
            }
            nameId = static_cast<uint32_t>(wmoNames.size());
            wmoNames.push_back(obj.path);
            foundWMO:

            glm::vec3 adtPos = core::coords::worldToAdt(obj.position);
            pipeline::ADTTerrain::WMOPlacement wp{};
            wp.nameId = nameId;
            wp.uniqueId = obj.uniqueId;
            wp.position[0] = adtPos.x;
            wp.position[1] = adtPos.y;
            wp.position[2] = adtPos.z;
            wp.rotation[0] = -obj.rotation.y;
            wp.rotation[1] = obj.rotation.z - 180.0f;
            wp.rotation[2] = -obj.rotation.x;
            wp.flags = 0;
            wp.doodadSet = 0;
            wp.nameSet = 0;
            // MODF scale is fixed-point u16 (1024 = 1.0); cap to u16 max.
            float s1024 = obj.scale * 1024.0f;
            wp.scale = static_cast<uint16_t>(std::clamp(s1024, 0.0f, 65535.0f));
            terrain_->wmoPlacements.push_back(wp);
        }
    }

    terrain_->doodadNames = std::move(m2Names);
    terrain_->wmoNames = std::move(wmoNames);
}

} // namespace editor
} // namespace wowee
