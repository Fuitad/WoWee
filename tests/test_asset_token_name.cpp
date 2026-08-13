// Which part of an asset path a token check is allowed to read.
//
// Model and texture names arrive as full asset paths. A token matched against
// the whole path is really matching the directory tree the artist filed the
// asset under, which has nothing to do with what the asset is.
//
// This was found once already, for models: every model in a
// PassiveDoodads/Lights directory matched the "light" lantern token and wall
// torches turned into floating glow sprites. The fix went into the model
// classifier and not into the texture colour-key hint beside it, which kept
// reading whole paths.
//
// The oracle is the shipped asset list. Of the 4929 textures whose path
// contains one of the colour-key tokens, 3338 contain it only in a directory
// name - 3174 of those being every character leg and boot component, because
// Item/TextureComponents/LegLowerTexture spells "glow". The black key discards
// every pixel darker than its threshold, so those textures were losing their
// dark areas: holes in dark boots, in Scholomance candelabras, in Hellfire
// Citadel statues, and in the logs of a campfire.
#include <catch_amalgamated.hpp>

#include <string>

#include "rendering/m2_model_classifier.hpp"

using wowee::rendering::assetNameHasToken;
using wowee::rendering::assetTokenName;

TEST_CASE("the token name is the file, not the path", "[asset-token]") {
    CHECK(assetTokenName("World\\Azeroth\\Elwynn\\Trees\\ElwynnTree01.m2") ==
          "elwynntree01");
    CHECK(assetTokenName("world/azeroth/elwynn/trees/elwynntree01.m2") ==
          "elwynntree01");
    // No directory at all, and no extension.
    CHECK(assetTokenName("ElwynnTree01") == "elwynntree01");
    CHECK(assetTokenName("") == "");
}

TEST_CASE("the extension is not part of the name", "[asset-token]") {
    // Rules that look at how a name ends would otherwise be reading ".m2".
    CHECK(assetTokenName("a\\b\\Tree.m2") == "tree");
    CHECK(assetTokenName("a\\b\\Tree.mdx") == "tree");
    CHECK(assetTokenName("a\\b\\Bark.blp") == "bark");
    // Anything else is left alone rather than guessed at.
    CHECK(assetTokenName("a\\b\\Notes.txt") == "notes.txt");
}

TEST_CASE("a directory cannot make an asset something it is not",
          "[asset-token]") {
    // The case that named this. Every leg and boot texture component sits
    // under a directory whose name contains "glow".
    CHECK_FALSE(assetNameHasToken(
        "Item\\TextureComponents\\LegLowerTexture\\Cloth_A_01Black_Boot_LL_F.blp",
        "glow"));

    // A zone directory is not a description of a doodad either.
    CHECK_FALSE(assetNameHasToken(
        "World\\Expansion01\\Doodads\\HellfirePeninsula\\Trees\\Tree01.blp",
        "fire"));
    CHECK_FALSE(assetNameHasToken(
        "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\Bonfire\\Wood08.blp",
        "fire"));
    CHECK_FALSE(assetNameHasToken(
        "World\\Lordaeron\\Scholomance\\PassiveDoodads\\Candles\\Candelabras.blp",
        "candle"));
    CHECK_FALSE(assetNameHasToken(
        "World\\Generic\\Dwarf\\Passive Doodads\\Braziers\\WallTrim01.blp",
        "brazier"));
    CHECK_FALSE(assetNameHasToken(
        "Interface\\WorldMap\\Hellfire\\FalconWatch1.blp", "fire"));
}

TEST_CASE("an asset named for what it is still matches", "[asset-token]") {
    // The other direction, which is what the token check is for.
    CHECK(assetNameHasToken(
        "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\Bonfire\\Fire01.blp",
        "fire"));
    CHECK(assetNameHasToken("Spells\\Fire_Glow_01.blp", "glow"));
    CHECK(assetNameHasToken("a\\b\\StormwindLampGlass.blp", "lamp"));
    CHECK(assetNameHasToken("a\\b\\Torch_Wall_01.m2", "torch"));

    // Case does not matter: paths arrive in whatever the artist typed.
    CHECK(assetNameHasToken("A\\B\\CANDLE_01.BLP", "candle"));
    CHECK(assetNameHasToken("a/b/Candle_01.blp", "candle"));
}

TEST_CASE("a token spanning the separator does not match", "[asset-token]") {
    // "fire" is not present in either component, only across the join. Reading
    // the file name makes that impossible, which is the point.
    CHECK_FALSE(assetNameHasToken("a\\elf\\iresomething.blp", "fire"));
    CHECK(assetNameHasToken("a\\elf\\firesomething.blp", "fire"));
}
