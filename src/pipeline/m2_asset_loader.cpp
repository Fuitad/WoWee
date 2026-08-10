#include "pipeline/m2_asset_loader.hpp"

#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"

namespace wowee {
namespace pipeline {

bool loadM2WithSkin(AssetManager& assets, const std::string& m2Path, M2Model& outModel) {
    auto m2Data = assets.readFile(m2Path);
    if (m2Data.empty()) return false;

    outModel = M2Loader::load(m2Data);
    // Everything downstream identifies a model by its name, and a model that
    // does not carry one is identified by nothing.
    if (outModel.name.empty()) outModel.name = m2Path;

    if (outModel.version >= 264) {
        auto skinData = assets.readFile(skinPathForM2(m2Path));
        if (!skinData.empty()) M2Loader::loadSkin(skinData, outModel);
    }
    return outModel.isValid();
}

}  // namespace pipeline
}  // namespace wowee
