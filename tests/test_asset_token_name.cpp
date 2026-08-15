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
using wowee::rendering::assetNameHasWordToken;
using wowee::rendering::assetNameLooksLikeFlame;

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

// ---------------------------------------------------------------------------
// A token that is only the tail of a longer word.
//
// Outland's sky is Environment\Stars\HellFireSkyNebula01.blp, and "fire" is in
// "hellfire". Matched as a substring, every layer of the Hellfire sky was read
// as a flame texture and given the black colour key - which discards each
// fragment darker than a threshold, and most of a nebula is. Turning the
// camera moves each fragment's sampled luminance across that threshold, so
// pixels dropped in and out: the sky flickered whenever the view moved and
// held still when it did not, in one zone, on the blended layers only.
//
// Reading the file name rather than the path does not help here. The name
// itself contains the token; it is the word boundary that is missing.
TEST_CASE("a token is not matched as the tail of a longer word",
          "[asset-token]") {
    CHECK_FALSE(assetNameHasWordToken("environment\\stars\\hellfireskynebula01.blp", "fire"));
    CHECK_FALSE(assetNameHasWordToken("world\\hellfirecitadel\\door.blp", "fire"));

    // Still matched where the token starts a word, or follows a separator or a
    // digit. A rule that fixed the sky by refusing everything would be worse
    // than the fault.
    CHECK(assetNameHasWordToken("spells\\firebeam.blp", "fire"));
    CHECK(assetNameHasWordToken("doodads\\stone_fire01.blp", "fire"));
    CHECK(assetNameHasWordToken("fire.blp", "fire"));
    CHECK(assetNameHasWordToken("a\\b\\2fire.blp", "fire"));
}

TEST_CASE("the flame list keeps every token both renderers had",
          "[asset-token]") {
    // Eleven in the M2 renderer and four in the character renderer, merged.
    // Losing one silently turns a colour key off for a whole family of
    // textures, which shows up as a black square around a flame rather than as
    // anything that raises.
    for (const char* named : {"candle.blp", "flame01.blp", "fire.blp",
                              "torch02.blp", "lamp.blp", "lantern.blp",
                              "glow.blp", "flare.blp", "brazier.blp",
                              "campfire.blp", "bonfire.blp"}) {
        INFO(named);
        CHECK(assetNameLooksLikeFlame(std::string("doodads\\") + named));
    }

    // And the sky is not one of them, whichever renderer asks.
    CHECK_FALSE(assetNameLooksLikeFlame("environment\\stars\\hellfireskynebula01.blp"));
    CHECK_FALSE(assetNameLooksLikeFlame("environment\\stars\\hellfireskyclouds01.blp"));

    // The character-component exclusion that used to be written by hand is not
    // needed: the directory is no longer part of the question.
    CHECK_FALSE(assetNameLooksLikeFlame(
        "item\\texturecomponents\\leglowertexture\\leather_a_01brown_pant_ll.blp"));
}

// ---------------------------------------------------------------------------
// The model-name half of the same fault.
//
// classifyM2Model asked has(name, "fire"), so HellfireSkyBox classified as a
// brazier - and the renderer gives an additive batch of a brazier a lamp
// flicker keyed on the instance position. A sky dome's position is the
// camera's, rewritten every frame, and lampFlicker quantises its seed to a
// one-unit grid: every cell the camera crossed re-rolled the phase. Outland's
// sky strobed whenever the camera moved or turned, on its additive layers
// alone, and held still when the camera did.
//
// The file already had this shape once, for "forge" inside "Ironforge".
TEST_CASE("a sky is not a brazier because its name ends in fire",
          "[asset-token][classifier]") {
    const glm::vec3 lo(-181.0f, -181.0f, -110.0f);
    const glm::vec3 hi(181.0f, 181.0f, 145.0f);
    const auto sky = wowee::rendering::classifyM2Model(
        "Environment\\Stars\\HellfireSkyBox.mdx", lo, hi, 1761, 0);
    CHECK_FALSE(sky.isBrazierOrFire);

    // Something actually named for a fire still is one.
    const glm::vec3 small(-1.0f, -1.0f, 0.0f);
    const glm::vec3 big(1.0f, 1.0f, 2.0f);
    CHECK(wowee::rendering::classifyM2Model(
        "World\\Doodads\\Campfire01.m2", small, big, 200, 1).isBrazierOrFire);
    CHECK(wowee::rendering::classifyM2Model(
        "World\\Doodads\\Fire_Large.m2", small, big, 200, 1).isBrazierOrFire);
}
