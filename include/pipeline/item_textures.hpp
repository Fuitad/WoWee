#pragma once

/**
 * item_textures.hpp — where the art for a worn item lives.
 *
 * Two conventions, both of them written out at every call site until now:
 *
 * The eight body regions an item paints onto a character each have their own
 * folder under Item\TextureComponents, and the file in it carries a suffix for
 * who is wearing it — _M, _F, or _U for art that serves both. Which of the three
 * exists is not recorded anywhere; it has to be asked of the filesystem, in that
 * order, and a caller that asks in a different order gets a man's arm on a
 * woman.
 *
 * A cape is filed under either ObjectComponents or TextureComponents, with or
 * without the suffix, and sometimes the DBC already names the folder. Six
 * candidates, in an order that matters, spelled out in three places.
 *
 * The region walk appeared six times and the cape list three, in the local
 * player's composition, in every other player's, in the NPC path twice, in the
 * portrait, and in the HUD. They agreed, which is the only reason nothing was
 * visibly wrong — but the ordering is a rule, and a rule kept in six places is
 * kept by luck.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

class AssetManager;
class DBCFile;

/// The eight texture regions of ItemDisplayInfo, in the order its columns run.
constexpr int kItemTextureRegionCount = 8;

/// The folder under Item\TextureComponents for one region. Out-of-range answers
/// an empty string rather than reading past the table.
const char* itemComponentDir(int region);

/// The path to one region's art for this wearer, or empty if none of the three
/// spellings is on disk.
///
/// Asks for the gendered file first, then the unisex one, then the bare name.
/// `texName` is the value out of ItemDisplayInfo, without a folder or extension.
std::string resolveItemRegionTexture(AssetManager& assets, int region,
                                     const std::string& texName, bool isFemale);

/// The model and texture an item display names.
///
/// ItemDisplayInfo carries two pairs, left and right. The left one is the whole
/// item and the right one is often just a hilt, so the left is asked for first
/// and the right taken only when there is no left — a rule three of the five
/// readers had and two did not, which is a weapon that renders for an NPC and
/// not for the player holding the same one.
///
/// `modelFile` comes back with the extension normalised to .m2: the tables name
/// .mdx, which is the format the models were in before they were converted, and
/// every caller renamed it itself.
struct ItemDisplayArt {
    std::string modelFile;
    std::string textureName;   ///< no folder and no extension, as the table has it
};

ItemDisplayArt readItemDisplayArt(const DBCFile& itemDisplayInfo, uint32_t recordIndex);

/// Every path a cape's texture might be at, in the order to try them.
///
/// A name that already carries a folder is taken as given. Otherwise both
/// component folders are tried, unsuffixed first — which is what the shipped art
/// mostly uses — and then with the wearer's suffix and the unisex one.
std::vector<std::string> capeTextureCandidates(const std::string& rawName, bool isFemale);

}  // namespace pipeline
}  // namespace wowee
