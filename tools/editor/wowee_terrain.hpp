#pragma once

#include "pipeline/adt_loader.hpp"
#include <string>

namespace wowee {
namespace editor {

// Wowee Open Terrain Format (.wot)
// JSON + binary heightmap - no Blizzard formats, fully open
class WoweeTerrain {
public:
    // Export terrain to open format: .wot (JSON metadata) + .whm (binary heightmap)
    static bool exportOpen(const pipeline::ADTTerrain& terrain,
                           const std::string& basePath, int tileX, int tileY);

    // Export normal map as PNG (129x129 RGB)
    static bool exportNormalMap(const pipeline::ADTTerrain& terrain,
                                const std::string& path);

    // Export alpha maps as individual 64x64 grayscale PNGs per chunk layer
    static int exportAlphaMaps(const pipeline::ADTTerrain& terrain,
                                const std::string& outputDir);

    // Export heightmap as grayscale PNG preview (129x129)
    static bool exportHeightmapPreview(const pipeline::ADTTerrain& terrain,
                                        const std::string& path);

    // Export water mask as PNG (white=water, black=land)
    static bool exportWaterMask(const pipeline::ADTTerrain& terrain,
                                 const std::string& path);

    // Export hole mask as PNG (white=hole, black=solid)
    static bool exportHoleMask(const pipeline::ADTTerrain& terrain,
                                const std::string& path);

    // Export zone overview map as colored PNG (terrain + water + objects)
    static bool exportZoneMap(const pipeline::ADTTerrain& terrain,
                               const std::string& path, int resolution = 512);

    // Import terrain from open format back to ADTTerrain
    static bool importOpen(const std::string& basePath, pipeline::ADTTerrain& terrain);
};

} // namespace editor
} // namespace wowee
