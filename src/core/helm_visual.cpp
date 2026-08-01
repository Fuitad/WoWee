#include "core/helm_visual.hpp"

#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_layout.hpp"

#include <unordered_map>

namespace wowee {
namespace core {

namespace {

constexpr const char* kHeadDir = "Item\\ObjectComponents\\Head\\";

/// The two-letter race codes head models are suffixed with, plus M or F. Races
/// absent here have no per-race cut and use the base model.
std::string raceGenderSuffix(uint8_t raceId, uint8_t genderId) {
    static const std::unordered_map<uint8_t, std::string> kRaceCode = {
        {1, "Hu"}, {2, "Or"}, {3, "Dw"}, {4, "Ni"}, {5, "Sc"},
        {6, "Ta"}, {7, "Gn"}, {8, "Tr"}, {10, "Be"}, {11, "Dr"}
    };
    auto it = kRaceCode.find(raceId);
    if (it == kRaceCode.end()) return {};
    return "_" + it->second + (genderId == 0 ? "M" : "F");
}

std::string stripExtension(std::string name) {
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    return name;
}

} // namespace

HelmVisual resolveHelmVisual(pipeline::AssetManager& assets,
                             uint32_t itemDisplayInfoId,
                             uint8_t raceId,
                             uint8_t genderId) {
    HelmVisual out;
    if (itemDisplayInfoId == 0) return out;

    auto dbc = assets.loadDBC("ItemDisplayInfo.dbc");
    if (!dbc) return out;
    const int32_t row = dbc->findRecordById(itemDisplayInfoId);
    if (row < 0) return out;

    const auto* layout = pipeline::getActiveDBCLayout()
        ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
    const uint32_t modelField = layout ? (*layout)["LeftModel"] : 1u;
    const uint32_t textureField = layout ? (*layout)["LeftModelTexture"] : 3u;

    const std::string modelName =
        stripExtension(dbc->getString(static_cast<uint32_t>(row), modelField));
    if (modelName.empty()) return out;

    const std::string suffix = raceGenderSuffix(raceId, genderId);
    if (!suffix.empty()) out.racialModelPath = kHeadDir + modelName + suffix + ".m2";
    out.baseModelPath = kHeadDir + modelName + ".m2";

    const std::string textureName = dbc->getString(static_cast<uint32_t>(row), textureField);
    if (!textureName.empty()) {
        if (!suffix.empty()) {
            const std::string racial = kHeadDir + textureName + suffix + ".blp";
            if (assets.fileExists(racial)) out.texturePath = racial;
        }
        if (out.texturePath.empty()) out.texturePath = kHeadDir + textureName + ".blp";
    }
    return out;
}

} // namespace core
} // namespace wowee
