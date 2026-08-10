#include "pipeline/item_textures.hpp"

#include <algorithm>
#include <cctype>

#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/dbc_layout.hpp"

namespace wowee {
namespace pipeline {

namespace {

// The folder names, in ItemDisplayInfo's own column order. Region 0 is the
// upper arm and region 7 the foot; the numbering is used as an index into a
// character's composite atlas, so the order is not free.
constexpr const char* kComponentDirs[kItemTextureRegionCount] = {
    "ArmUpperTexture",
    "ArmLowerTexture",
    "HandTexture",
    "TorsoUpperTexture",
    "TorsoLowerTexture",
    "LegUpperTexture",
    "LegLowerTexture",
    "FootTexture",
};

bool hasBlpExtension(const std::string& name) {
    if (name.size() < 4) return false;
    std::string tail = name.substr(name.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".blp";
}

}  // namespace

const char* itemComponentDir(int region) {
    if (region < 0 || region >= kItemTextureRegionCount) return "";
    return kComponentDirs[region];
}

std::string resolveItemRegionTexture(AssetManager& assets, int region,
                                     const std::string& texName, bool isFemale) {
    if (texName.empty()) return {};
    const char* dir = itemComponentDir(region);
    if (dir[0] == '\0') return {};

    const std::string base = std::string("Item\\TextureComponents\\") + dir + "\\" + texName;
    const std::string gendered = base + (isFemale ? "_F.blp" : "_M.blp");
    if (assets.fileExists(gendered)) return gendered;
    const std::string unisex = base + "_U.blp";
    if (assets.fileExists(unisex)) return unisex;
    const std::string plain = base + ".blp";
    if (assets.fileExists(plain)) return plain;
    return {};
}

ItemDisplayArt readItemDisplayArt(const DBCFile& itemDisplayInfo, uint32_t recordIndex) {
    const auto* layout = getActiveDBCLayout()
        ? getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
    const uint32_t modelLeft   = layout ? (*layout)["LeftModel"]         : 1u;
    const uint32_t modelRight  = layout ? (*layout)["RightModel"]        : 2u;
    const uint32_t texLeft     = layout ? (*layout)["LeftModelTexture"]  : 3u;
    const uint32_t texRight    = layout ? (*layout)["RightModelTexture"] : 4u;

    ItemDisplayArt art;
    art.modelFile   = itemDisplayInfo.getString(recordIndex, modelLeft);
    art.textureName = itemDisplayInfo.getString(recordIndex, texLeft);
    if (art.modelFile.empty()) {
        art.modelFile   = itemDisplayInfo.getString(recordIndex, modelRight);
        art.textureName = itemDisplayInfo.getString(recordIndex, texRight);
    }
    if (art.modelFile.empty()) return art;

    // The tables name .mdx, the format these models were in before they were
    // converted. Every caller renamed it, and one of them forgot the case where
    // the name has no extension at all.
    const size_t dot = art.modelFile.rfind('.');
    if (dot != std::string::npos) art.modelFile.resize(dot);
    art.modelFile += ".m2";
    return art;
}

std::vector<std::string> capeTextureCandidates(const std::string& rawName, bool isFemale) {
    std::vector<std::string> candidates;
    if (rawName.empty()) return candidates;

    std::string name = rawName;
    std::replace(name.begin(), name.end(), '/', '\\');
    const bool hasDir = name.find('\\') != std::string::npos;
    const bool hasExt = hasBlpExtension(name);

    if (hasDir) {
        candidates.push_back(hasExt ? name : name + ".blp");
        return candidates;
    }

    const std::string baseObj = "Item\\ObjectComponents\\Cape\\" + name;
    const std::string baseTex = "Item\\TextureComponents\\Cape\\" + name;
    if (hasExt) {
        candidates.push_back(baseObj);
        candidates.push_back(baseTex);
    } else {
        candidates.push_back(baseObj + ".blp");
        candidates.push_back(baseTex + ".blp");
    }
    const char* suffix = isFemale ? "_F.blp" : "_M.blp";
    candidates.push_back(baseObj + suffix);
    candidates.push_back(baseObj + "_U.blp");
    candidates.push_back(baseTex + suffix);
    candidates.push_back(baseTex + "_U.blp");
    return candidates;
}

}  // namespace pipeline
}  // namespace wowee
