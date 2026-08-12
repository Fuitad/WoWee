// Which WMO materials are drawn as glass.
//
// The glass path picks a material up when its texture path contains "window"
// or "glass". Most of those should be glass. Some are walls with window
// openings painted or cut into them, and the ruined guard towers of Tirisfal
// are the case that showed it: their whole upper section came out looking
// glazed.
//
// The oracle for the tower is the shipped WMO data. ruinedhumanguardtower01
// material 3 is MOMT flags 0x0, blend mode 0, texture TOWER_WINDOW_01.BLP, and
// that BLP2 header reports alphaDepth 0 - a stone wall with no transparency
// anywhere in it.
//
// The same data says why this is a named list rather than a rule. Neither the
// authored flags nor the alpha channel separates the two groups: Stormwind's
// windows carry no flag and Booty Bay's lit ones have no alpha, while
// Uldaman's stained glass has neither. Either test alone drops around 400
// materials that look right today.
#include <catch_amalgamated.hpp>

#include "rendering/wmo_material_class.hpp"

using wowee::rendering::wmoMaterialIsGlass;
using wowee::rendering::wmoTextureIsPaintedWall;
using wowee::rendering::wmoTextureNamedGlass;
using wowee::rendering::WMO_MAT_SIDN;
using wowee::rendering::WMO_MAT_UNCULLED;
using wowee::rendering::WMO_MAT_UNLIT;
using wowee::rendering::WMO_MAT_WINDOW;

TEST_CASE("the ruined guard tower is stone", "[wmo-material]") {
    // The reported case. Seven materials across the ruined and damaged tower
    // models use this texture.
    CHECK_FALSE(wmoMaterialIsGlass(
        0x00000000,
        "DUNGEONS\\TEXTURES\\BRIAN\\RUINEDTOWER\\TOWER_WINDOW_01.BLP"));

    // Its neighbours in the same model were never affected: they are named for
    // what they are, so nothing about them changes.
    CHECK_FALSE(wmoMaterialIsGlass(
        0, "DUNGEONS\\TEXTURES\\BRIAN\\RUINEDTOWER\\TOWER_BROKEN.BLP"));
    CHECK_FALSE(wmoMaterialIsGlass(
        0, "DUNGEONS\\TEXTURES\\BRIAN\\RUINEDTOWER\\TOWER_EXWALL_01.BLP"));
}

TEST_CASE("the excluded texture is matched by name, not by path",
          "[wmo-material]") {
    // MOTX stores whatever the artist typed. Both separators, any case, and
    // the file name alone all have to find it.
    CHECK(wmoTextureIsPaintedWall("TOWER_WINDOW_01.BLP"));
    CHECK(wmoTextureIsPaintedWall("tower_window_01.blp"));
    CHECK(wmoTextureIsPaintedWall("a\\b\\TOWER_WINDOW_01.BLP"));
    CHECK(wmoTextureIsPaintedWall("a/b/tower_window_01.blp"));

    // And only that texture: a name that merely starts the same is a
    // different file.
    CHECK_FALSE(wmoTextureIsPaintedWall("tower_window_02.blp"));
    CHECK_FALSE(wmoTextureIsPaintedWall("my_tower_window_01.blp"));
    CHECK_FALSE(wmoTextureIsPaintedWall(""));
}

TEST_CASE("every other window texture is still glass", "[wmo-material]") {
    // The constraint on this change: what worked keeps working. These are the
    // most repeated window textures in the shipped WMOs, none of which the
    // list names.
    const char* stillGlass[] = {
        "DUNGEONS\\TEXTURES\\WINDOWS\\MM_STRMWND_WINDOWS_01.BLP",
        "DUNGEONS\\TEXTURES\\BLOODELF\\JLO_BLOODELF_WALLWINDOW_01.BLP",
        "DUNGEONS\\TEXTURES\\WYRMREST\\WR_ROUND_WINDOW.BLP",
        "DUNGEONS\\TEXTURES\\WYRMREST\\WR_WINDOW.BLP",
        "DUNGEONS\\TEXTURES\\DRAENEI\\JLO_DRAENEI_WINDOW_02.BLP",
        "DUNGEONS\\TEXTURES\\DALARAN\\EB_DALARAN_WINDOW3.BLP",
        "DUNGEONS\\TEXTURES\\ULDAMAN\\CE_ULDR_STAINEDGLASS01.BLP",
        "DUNGEONS\\TEXTURES\\BOOTYBAY\\AM_BBAY_WINDOWS.BLP",
        "DUNGEONS\\TEXTURES\\ICECROWN\\JLO_ICEC_WINDOW.BLP",
    };
    for (const char* tex : stillGlass) {
        INFO(tex);
        CHECK(wmoMaterialIsGlass(0x00000000, tex));
    }
}

TEST_CASE("a material the artist marked a window is glass whatever its name",
          "[wmo-material]") {
    // Five materials in the shipped WMOs carry F_WINDOW and a texture name the
    // filter would miss, so the flag is worth reading.
    CHECK(wmoMaterialIsGlass(WMO_MAT_WINDOW,
                             "DUNGEONS\\TEXTURES\\INN\\MM_INN_COMBO_03.BLP"));
    CHECK(wmoMaterialIsGlass(WMO_MAT_WINDOW,
                             "DUNGEONS\\TEXTURES\\SHDWFNG\\BM_SHDWFNG_BIGBEAM_01.BLP"));
    CHECK(wmoMaterialIsGlass(WMO_MAT_WINDOW | WMO_MAT_UNLIT, "anything.blp"));

    // The list still wins: a texture named as stone is stone even if some
    // model marks the material.
    CHECK_FALSE(wmoMaterialIsGlass(WMO_MAT_WINDOW, "tower_window_01.blp"));
}

TEST_CASE("a texture named for neither is not glass", "[wmo-material]") {
    CHECK_FALSE(wmoMaterialIsGlass(0, "DUNGEONS\\TEXTURES\\STONE_01.BLP"));
    CHECK_FALSE(wmoMaterialIsGlass(WMO_MAT_SIDN, "LAMPPOST_01.BLP"));
    CHECK_FALSE(wmoMaterialIsGlass(WMO_MAT_UNLIT | WMO_MAT_UNCULLED, ""));
}

TEST_CASE("the name test reads the whole path, in any case",
          "[wmo-material]") {
    CHECK(wmoTextureNamedGlass("A\\B\\TOWER_WINDOW_01.BLP"));
    CHECK(wmoTextureNamedGlass("a\\b\\tower_window_01.blp"));
    CHECK(wmoTextureNamedGlass("StormwindLampGlass.blp"));
    // A directory names it as readily as the file, which is how the filter has
    // always worked and part of why it over-reaches.
    CHECK(wmoTextureNamedGlass("DUNGEONS\\WINDOWS\\wall.blp"));
    CHECK(wmoTextureNamedGlass("GLASSY_LOOKING_STONE.BLP"));

    CHECK_FALSE(wmoTextureNamedGlass("DUNGEONS\\TEXTURES\\STONE_01.BLP"));
    CHECK_FALSE(wmoTextureNamedGlass("WIND_BANNER.BLP"));
    CHECK_FALSE(wmoTextureNamedGlass(""));
}
