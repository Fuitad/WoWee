// What the renderer does with a batch that is a glow card.
//
// A lamp's halo, a brazier's flame, an elf building's window bloom: a small
// quad, bright in the middle and black at the edges. Drawn as a mesh it reads
// as a card, so a billboarded sprite stands in for it.
//
// Three decisions go into that and the renderer made all three twice, once per
// pass, three hundred and fifty lines apart, in expressions identical down to
// the constants. A batch is meant to be skipped by exactly one pass and drawn
// by the other, so the copies drifting apart raises nothing: it draws the card
// twice, or never.
//
// The cases below are the real ones the renderer has to get right, and are
// written against what the models actually carry rather than against the
// expressions that were there.
#include <catch_amalgamated.hpp>

#include "rendering/m2_glow_card.hpp"

using wowee::rendering::M2GlowCardBatch;
using wowee::rendering::m2GlowSpriteReplacesMesh;
using wowee::rendering::m2IsSmallCardLikeBatch;
using wowee::rendering::m2WantsGlowSprite;

namespace {

/// A lamp's own glow: small, hinted, additive.
M2GlowCardBatch lanternGlow() {
    M2GlowCardBatch b;
    b.glowSize = 0.8f;
    b.blendMode = 3;
    b.lanternGlowHint = true;
    b.unlit = true;
    b.modelIsLanternLike = true;
    return b;
}

}  // namespace

TEST_CASE("a lamp's glow becomes a sprite and loses its mesh", "[m2-glow]") {
    const auto b = lanternGlow();
    CHECK(m2IsSmallCardLikeBatch(b));
    CHECK(m2WantsGlowSprite(b));
    CHECK(m2GlowSpriteReplacesMesh(b));
}

TEST_CASE("a fixture keeps its mesh when the card is too big to stand in for",
          "[m2-glow]") {
    // The lamp post is the thing you see. Replacing a large card with a sprite
    // leaves a post with no lamp on it.
    auto b = lanternGlow();
    b.glowSize = 12.0f;
    CHECK_FALSE(m2IsSmallCardLikeBatch(b));
    CHECK_FALSE(m2WantsGlowSprite(b));
}

TEST_CASE("a lantern's glow is allowed to be larger than a plain card",
          "[m2-glow]") {
    // The fixture is bigger than the light it gives off, so the hint buys more
    // room: 1.35 without it, 6.0 with.
    M2GlowCardBatch plain;
    plain.glowSize = 3.0f;
    CHECK_FALSE(m2IsSmallCardLikeBatch(plain));

    plain.lanternGlowHint = true;
    CHECK(m2IsSmallCardLikeBatch(plain));

    plain.glowSize = 6.5f;
    CHECK_FALSE(m2IsSmallCardLikeBatch(plain));
}

TEST_CASE("a batch asked to keep its mesh keeps it", "[m2-glow]") {
    // preserveGlowMesh is how a lantern-family model says the sprite is an
    // addition rather than a replacement.
    auto b = lanternGlow();
    b.preserveGlowMesh = true;
    CHECK(m2WantsGlowSprite(b));
    CHECK_FALSE(m2GlowSpriteReplacesMesh(b));
}

TEST_CASE("a spell effect is never turned into a glow sprite", "[m2-glow]") {
    // Spell visuals are authored as cards on purpose and animate; a static
    // sprite is not a stand-in for one.
    auto b = lanternGlow();
    b.modelIsSpellEffect = true;
    CHECK_FALSE(m2WantsGlowSprite(b));
}

TEST_CASE("a kobold's candle keeps its flame", "[m2-glow]") {
    // The one model whose colour-keyed card must survive as a mesh.
    auto b = lanternGlow();
    b.colorKeyBlack = true;
    b.modelIsKoboldFlame = true;
    CHECK_FALSE(m2WantsGlowSprite(b));
}

TEST_CASE("an elf building's bloom is a sprite without any fixture",
          "[m2-glow]") {
    // isElvenLike stands in for the fixture test, so the hint is not required.
    M2GlowCardBatch b;
    b.glowSize = 1.0f;
    b.blendMode = 4;
    b.modelIsElvenLike = true;
    CHECK(m2WantsGlowSprite(b));
    // And with no fixture flags, the mesh goes.
    CHECK(m2GlowSpriteReplacesMesh(b));
}

TEST_CASE("a batch that is not a card at all is left alone", "[m2-glow]") {
    // An opaque, lit, unhinted batch on an ordinary model: the common case,
    // and none of this should touch it.
    M2GlowCardBatch b;
    b.glowSize = 0.5f;
    b.blendMode = 0;
    CHECK_FALSE(m2WantsGlowSprite(b));
    CHECK_FALSE(m2GlowSpriteReplacesMesh(b));
}

TEST_CASE("the Orgrimmar bonfire's glow is too large for a sprite",
          "[m2-glow]") {
    // Blend mode 4, colour keyed by its texture's name, unlit, on a model
    // classified as a brazier. It is a whole bonfire rather than a lamp, so
    // the sprite path does not claim it and the mesh is what draws.
    M2GlowCardBatch b;
    b.glowSize = 9.0f;
    b.blendMode = 4;
    b.unlit = true;
    b.colorKeyBlack = true;
    b.lanternGlowHint = true;
    b.modelIsBrazierOrFire = true;
    CHECK_FALSE(m2IsSmallCardLikeBatch(b));
    CHECK_FALSE(m2WantsGlowSprite(b));
}
