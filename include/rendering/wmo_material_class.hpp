#pragma once

/// Deciding which WMO materials are drawn as glass.
///
/// The glass path picks a material up when its texture path contains "window"
/// or "glass", and gives it a Fresnel term, a blue reflection tint and a
/// specular highlight. Across the 2392 root WMOs the client ships, 143
/// distinct textures match that name, on 835 materials.
///
/// Most of them should be glass and look right. Some are not glass at all:
/// they are walls with window openings painted or cut into them, and the
/// ruined guard towers of Tirisfal are the case that showed it. Their
/// TOWER_WINDOW_01 is a plain stone wall - MOMT flags 0x0, blend mode 0, and a
/// BLP with no alpha channel at all - so the whole upper tower came out
/// looking glazed.
///
/// WHY THIS IS A LIST AND NOT A RULE
///
/// There is no property of the data that separates the two. Measured over all
/// 143 textures:
///
///   * the authored flags do not. F_WINDOW (0x20) and F_SIDN (0x10) are set on
///     the Booty Bay and Blood Elf windows and not on Stormwind's, and the
///     ruined tower carries neither - but neither does Uldaman's stained
///     glass.
///   * the alpha channel does not. MM_STRMWND_WINDOWS_01 has one and
///     AM_BBAY_WINDOWS, which is a lit window, does not.
///
/// Either test alone drops around 400 materials that currently look correct.
/// So the default stays what it was, and what is known not to be glass is
/// named here one texture at a time.
///
/// ADDING TO THE LIST
///
/// A texture belongs here when the surface is stone or wood with window
/// shapes on it rather than a pane. Add the file name, lower case, and say
/// where it was seen.

#include <cstdint>
#include <string>

namespace wowee::rendering {

/// MOMT material flags, as far as the glass path cares.
enum WmoMaterialFlags : uint32_t {
    WMO_MAT_UNLIT    = 0x01,
    WMO_MAT_UNFOGGED = 0x02,
    WMO_MAT_UNCULLED = 0x04,
    WMO_MAT_EXTLIGHT = 0x08,
    WMO_MAT_SIDN     = 0x10,  ///< night glow: windows and lamps
    WMO_MAT_WINDOW   = 0x20,  ///< marked a window by the artist
};

/// True when the texture path names a window or glass surface.
inline bool wmoTextureNamedGlass(const std::string& texturePath) {
    std::string lower = texturePath;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    return lower.find("window") != std::string::npos ||
           lower.find("glass") != std::string::npos;
}

/// Textures that carry a window in their name and are not glass.
///
/// Matched on the file name, so a path in any case and with either separator
/// finds them.
inline bool wmoTextureIsPaintedWall(const std::string& texturePath) {
    static const char* kPaintedWalls[] = {
        // The ruined guard towers of Tirisfal and Elwynn, and the damaged one.
        // MOMT flags 0x0, blend 0, and the BLP has no alpha channel: the whole
        // texture is stone, and the windows in it are holes.
        "tower_window_01.blp",
    };
    std::string lower = texturePath;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == '\\') c = '/';
    }
    const size_t slash = lower.find_last_of('/');
    const std::string file =
        slash == std::string::npos ? lower : lower.substr(slash + 1);
    for (const char* wall : kPaintedWalls) {
        if (file == wall) return true;
    }
    return false;
}

/// Should this material be drawn as glass?
///
/// The name decides, as it did before, minus the textures named above. A
/// material the artist marked F_WINDOW is glass whatever it is called: five
/// materials in the shipped WMOs carry that flag and a texture name the test
/// above would miss.
inline bool wmoMaterialIsGlass(uint32_t materialFlags,
                               const std::string& texturePath) {
    if (wmoTextureIsPaintedWall(texturePath)) return false;
    if ((materialFlags & WMO_MAT_WINDOW) != 0) return true;
    return wmoTextureNamedGlass(texturePath);
}

}  // namespace wowee::rendering
