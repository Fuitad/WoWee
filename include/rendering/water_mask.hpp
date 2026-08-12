#pragma once

/// Which of a water chunk's sixty-four sub-tiles a surface draws.
///
/// Here rather than inline in the renderer because this rule has been got wrong
/// repeatedly and every failure looks like scenery: water outside a pond's
/// banks, a gap in open sea, or - the one that named this file - an ocean drawn
/// across a dry chunk, roofing the Caverns of Time. None of them raises, fails a
/// test or writes a line of log, so the rule needs somewhere it can be stated
/// once and checked without a device.
///
/// The mask is the canonical chunk-wide 8x8 form both MH2O and MCLQ are
/// normalised into by the ADT loader: bit index = row * 8 + col, LSB first,
/// where row is the MH2O y axis and col the x. See the water/terrain axis notes
/// - every read of it is LSB-only, and the old mirror-OR and neighbour dilation
/// were compensation for bugs that are fixed.

#include <cstdint>
#include <vector>

namespace wowee::rendering {

/// The sub-tiles of one chunk that should be drawn, as an 8x8 bitmask packed
/// into a uint64_t with the same row*8+col, LSB-first ordering.
///
/// `isOcean` is a whole-chunk statement, not a per-tile one: an ocean layer's
/// own sub-rect is frequently sparse, and honouring it leaves holes in open sea.
/// A chunk that declares ocean therefore fills completely.
///
/// **A chunk that declares no liquid never reaches here.** It contributes no
/// layer, so it gets no bits. The bug this replaced seeded every sub-tile of the
/// whole ADT before the chunks were consulted, which gave water to chunks that
/// had none - invisible against land above sea level, and a ceiling of ocean
/// over anything below it.
inline uint64_t chunkWaterMask(const std::vector<uint8_t>& layerMask,
                               int x, int y, int width, int height,
                               bool isOcean) {
    if (isOcean) return ~uint64_t{0};

    uint64_t out = 0;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const int tile = (y + row) * 8 + (x + col);
            if (tile < 0 || tile >= 64) continue;
            // An absent or short mask means the sub-rect is solid, which is how
            // a layer with no exists bitmap is defined.
            bool render = true;
            const int byteIdx = tile / 8;
            if (!layerMask.empty() && byteIdx < static_cast<int>(layerMask.size()))
                render = (layerMask[byteIdx] & (1u << (tile % 8))) != 0;
            if (render) out |= uint64_t{1} << tile;
        }
    }
    return out;
}

}  // namespace wowee::rendering
