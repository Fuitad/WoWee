#pragma once

/**
 * m2_asset_loader.hpp — reading an M2 and its .skin out of the asset tree.
 *
 * Separate from m2_loader.hpp on purpose: that file parses bytes and knows
 * nothing about where they came from, which is what lets it be tested on its own
 * without an asset tree to point at.
 */

#include <string>

namespace wowee {
namespace pipeline {

class AssetManager;
struct M2Model;

/**
 * Read an M2 and the .skin beside it.
 *
 * Models from version 264 keep their submeshes in that separate file; earlier
 * ones carry them inside the M2 and it is not asked for. Returns false when the
 * M2 cannot be read or does not parse into a usable model.
 *
 * Four copies of this existed, one per thing that loads a weapon, a helm or a
 * portrait, and they disagreed about whether to name the model after its path —
 * which is what everything downstream identifies it by.
 */
bool loadM2WithSkin(AssetManager& assets, const std::string& m2Path, M2Model& outModel);

}  // namespace pipeline
}  // namespace wowee
