#include "rendering/m2_model_classifier.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace wowee {
namespace rendering {

namespace {

// Returns true if `lower` contains `token` as a substring.
// Caller must provide an already-lowercased string.
inline bool has(const std::string& lower, std::string_view token) noexcept {
    return lower.find(token) != std::string::npos;
}

// Returns true if any token in the compile-time array is a substring of `lower`.
template <std::size_t N>
bool hasAny(const std::string& lower,
            const std::array<std::string_view, N>& tokens) noexcept {
    for (auto tok : tokens)
        if (lower.find(tok) != std::string::npos) return true;
    return false;
}

} // namespace

M2ClassificationResult classifyM2Model(
    const std::string& name,
    const glm::vec3&   boundsMin,
    const glm::vec3&   boundsMax,
    std::size_t        vertexCount,
    std::size_t        emitterCount)
{
    // Lowercased full name for explicitly path-based checks (ground detail).
    std::string fullPath = name;
    std::transform(fullPath.begin(), fullPath.end(), fullPath.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Token checks run on the basename only: model names are often full asset
    // paths, and directory tokens poison them (e.g. every model under
    // PASSIVEDOODADS\LIGHTS\ matched the "light" lantern token, turning wall
    // torches into floating glow sprites). Filenames carry the real semantics.
    std::string n = fullPath;
    const size_t lastSep = n.find_last_of("\\/");
    if (lastSep != std::string::npos) n = n.substr(lastSep + 1);

    M2ClassificationResult r;

    // ---------------------------------------------------------------
    // Geometry metrics
    // ---------------------------------------------------------------
    const glm::vec3 dims = boundsMax - boundsMin;
    const float horiz    = std::max(dims.x, dims.y);
    const float vert     = std::max(0.0f, dims.z);
    const bool lowWide   = (horiz > 1.4f && vert > 0.2f && vert < horiz * 0.70f);
    const bool lowPlat   = (horiz > 1.8f && vert > 0.2f && vert < 1.8f);

    // ---------------------------------------------------------------
    // Simple single-token flags
    // ---------------------------------------------------------------
    r.isInvisibleTrap = has(n, "invisibletrap");
    r.isGroundDetail  = has(fullPath, "\\nodxt\\detail\\") || has(fullPath, "\\detail\\");
    r.isSmoke         = has(n, "smoke");
    r.isLavaModel     = has(n, "forgelava") || has(n, "lavapot") || has(n, "lavaflow")
                    || has(n, "lavapool");

    r.isInstancePortal  = has(n, "instanceportal") || has(n, "instancenewportal")
                        || has(n, "portalfx")       || has(n, "spellportal");

    r.isWaterVegetation = has(n, "cattail") || has(n, "reed")     || has(n, "bulrush")
                        || has(n, "seaweed") || has(n, "kelp")    || has(n, "lilypad")
                        || has(n, "waterlily");

    r.isWaterfall       = has(n, "waterfall");

    r.isElvenLike   = has(n, "elf")     || has(n, "elven") || has(n, "quel");
    // Directional volumetric light effects (lighthouse beam, light rays/shafts) match
    // the broad "light" token but are NOT point lanterns. They are animated additive
    // meshes — e.g. the Stormwind lighthouse beam rotates via a looped single-bone
    // animation. Classifying them as lanterns routed their batch through the glow-card
    // path, which skips the mesh and draws a static billboard instead, freezing the
    // sweep. Exclude beams/rays/shafts so their animated mesh renders normally.
    const bool volumetricLightBeam =
        has(n, "beam")     || has(n, "lighthouse") || has(n, "lightray") ||
        has(n, "lightshaft") || has(n, "godray")   || has(n, "sunray");
    r.isLanternLike = (has(n, "lantern") || has(n, "lamp")  || has(n, "light") ||
                       has(n, "sconce")  || has(n, "candle") ||
                       has(n, "candelabra") || has(n, "chandelier"))
                      && !volumetricLightBeam;
    r.isKoboldFlame = has(n, "kobold")
                    && (has(n, "candle") || has(n, "torch") || has(n, "mine"));

    // Fire / brazier / torch model detection (for ambient emitter + rendering)
    const bool fireName    = has(n, "fire") || has(n, "campfire") || has(n, "bonfire");
    const bool brazierName = has(n, "brazier") || has(n, "cauldronfire");
    const bool forgeName   = has(n, "forge") && !has(n, "forgelava");
    const bool torchName   = has(n, "torch") && !r.isKoboldFlame;
    r.isBrazierOrFire = fireName || brazierName;
    // TaurenLampPost is the small ground-level path fire used around Camp
    // Narache, despite its misleading model name. Its decorative halo must
    // follow the lowest flame emitter rather than the mesh/card center.
    r.isGroundFire    = (fireName && !brazierName) || has(n, "taurenlamppost");
    r.isTorch         = torchName;
    r.isForge         = forgeName;

    // ---------------------------------------------------------------
    // Collision: shape categories (mirrors original logic ordering)
    // ---------------------------------------------------------------
    const bool isPlanter      = has(n, "planter");
    const bool likelyCurb     = isPlanter || has(n, "curb")  || has(n, "base")
                                           || has(n, "ring")  || has(n, "well");
    const bool knownSwPlanter = has(n, "stormwindplanter")
                              || has(n, "stormwindwindowplanter");
    const bool bridgeName     = has(n, "bridge") || has(n, "plank") || has(n, "walkway");
    const bool statueName     = has(n, "statue") || has(n, "monument") || has(n, "sculpture");
    const bool sittable       = has(n, "chair")  || has(n, "bench") || has(n, "stool")
                                                 || has(n, "seat")  || has(n, "throne");
    const bool smallSolid     = (statueName && !sittable)
                              || has(n, "crate") || has(n, "box")
                              || has(n, "chest") || has(n, "barrel")
                              || has(n, "anvil") || has(n, "mailbox")
                              || has(n, "cauldron") || has(n, "cannon")
                              || has(n, "wagon") || has(n, "cart")
                              || has(n, "table") || has(n, "desk");
    const bool chestName      = has(n, "chest");

    r.collisionSteppedFountain    = has(n, "fountain");
    r.collisionSteppedLowPlatform = !r.collisionSteppedFountain
                                  && (knownSwPlanter || bridgeName
                                      || (likelyCurb && (lowPlat || lowWide)));
    r.collisionBridge             = bridgeName;
    r.collisionPlanter            = isPlanter;
    r.collisionStatue             = statueName;

    const bool narrowVertName  = has(n, "lamp")    || has(n, "lantern")
                               || has(n, "post")    || has(n, "pole");
    const bool narrowVertShape = (horiz > 0.12f && horiz < 2.0f
                               && vert  > 2.2f  && vert  > horiz * 1.8f);
    r.collisionNarrowVerticalProp = !r.collisionSteppedFountain
                                  && !r.collisionSteppedLowPlatform
                                  && (narrowVertName || narrowVertShape);

    // ---------------------------------------------------------------
    // Foliage token table (sorted alphabetically)
    // ---------------------------------------------------------------
    static constexpr auto kFoliageTokens = std::to_array<std::string_view>({
        "algae",      "bamboo",     "banana",     "barley",     "bean",
        "bracken",    "bramble",    "branch",     "briar",      "brush",
        "bush",
        "cactus",     "canopy",     "carrot",     "cattail",    "clover",
        "clump",      "coconut",    "coral",      "corn",       "crop",
        "dead-grass", "dead_grass", "deadgrass",
        "dry-grass",  "dry_grass",  "drygrass",
        "fern",       "fernleaf",   "fireflies",  "firefly",    "fireflys",
        "flower",     "frond",      "fungus",     "gourd",      "grapes",
        "grass",
        "hay",        "hedge",      "herb",       "hops",       "ivy",
        "kelp",       "leaf",       "leaves",     "lettuce",    "lichen",
        "lily",
        "melon",      "moss",       "mushroom",   "nettle",
        "okra",       "onion",
        "palm",       "pepper",     "pinecone",   "potato",     "pumpkin",
        "reed",       "root",
        "sapling",    "seaweed",    "seedling",   "shrub",      "sprout",
        "squash",     "stalk",      "thorn",      "thistle",    "toadstool",
        "tomato",     "turnip",
        "underbrush", "vine",       "watermelon", "weed",       "wheat",
    });

    // "plant" is foliage unless "planter" is also present (planters are solid curbs).
    const bool foliagePlant = has(n, "plant") && !isPlanter;
    const bool foliageName  = foliagePlant || hasAny(n, kFoliageTokens);
    const bool treeLike     = has(n, "tree");
    const bool hardTreePart = has(n, "trunk") || has(n, "stump") || has(n, "log");

    // Trees wide/tall enough to have a visible trunk → solid cylinder collision.
    const bool treeWithTrunk = treeLike && !hardTreePart && !foliageName
                             && horiz > 6.0f && vert > 4.0f;
    const bool softTree      = treeLike && !hardTreePart && !treeWithTrunk;

    r.collisionTreeTrunk = treeWithTrunk;

    const bool genericSolid     = (horiz > 0.6f && horiz < 6.0f
                                && vert  > 0.30f && vert  < 4.0f
                                && vert  > horiz * 0.16f) || statueName;
    const bool curbLikeName     = has(n, "curb")   || has(n, "planter")
                                || has(n, "ring")   || has(n, "well")  || has(n, "base");
    const bool lowPlatLikeShape = lowWide || lowPlat;

    r.collisionSmallSolidProp = !r.collisionSteppedFountain
                              && !r.collisionSteppedLowPlatform
                              && !r.collisionNarrowVerticalProp
                              && !r.collisionTreeTrunk
                              && !curbLikeName
                              && !lowPlatLikeShape
                              && (smallSolid
                                  || (genericSolid && !foliageName && !softTree));

    const bool carpetOrRug    = has(n, "carpet") || has(n, "rug");
    const bool forceSolidCurb = r.collisionSteppedLowPlatform || knownSwPlanter
                              || likelyCurb || r.collisionPlanter;
    r.collisionNoBlock        = (foliageName || softTree || carpetOrRug) && !forceSolidCurb;
    // Ground-clutter detail cards are always non-blocking.
    if (r.isGroundDetail) r.collisionNoBlock = true;
    // Small doodads that aren't explicitly solid should not block movement.
    // In WoW, only named solid objects (crates, barrels, anvils, etc.) and
    // large structural doodads have collision — small decorative models are
    // always walkthrough regardless of their name.
    if (!r.collisionNoBlock && !smallSolid && !forceSolidCurb
        && !r.collisionSteppedFountain && !r.collisionTreeTrunk
        && !r.collisionNarrowVerticalProp && !r.collisionStatue
        && horiz < 2.0f && vert < 2.0f) {
        r.collisionNoBlock = true;
    }

    // ---------------------------------------------------------------
    // Ambient creatures: fireflies, dragonflies, moths, butterflies
    // ---------------------------------------------------------------
    static constexpr auto kAmbientTokens = std::to_array<std::string_view>({
        "butterfly", "dragonflies", "dragonfly",
        "fireflies", "firefly",     "fireflys", "moth",
    });
    const bool ambientCreature = hasAny(n, kAmbientTokens);

    // ---------------------------------------------------------------
    // Sky birds / bats: animated flying doodads that look frozen beyond bone range
    // ---------------------------------------------------------------
    static constexpr auto kSkyBirdTokens = std::to_array<std::string_view>({
        "albatross", "carrionbird", "crane", "crow",
        "eagle",     "gull",        "hawk",  "osprey",
        "owl",       "parrot",      "pelican",
        "raven",     "seagull",     "vulture",
    });
    r.isSkyBird = hasAny(n, kSkyBirdTokens) || has(n, "\\bird")
                || has(n, "\\bat\\") || has(n, "\\bat.");
    // These meshes are visible from far beyond the normal skeletal LOD radius,
    // and their sweep is entirely bone-driven. Freezing distant bone matrices
    // therefore freezes the whole lighthouse beam in its bind pose.
    r.isLightBeam = has(n, "lighthousebeam") || has(n, "lightbeam")
                 || has(n, "lightray");
    r.isTransportDoodad = has(n, "transportship_sails")
                       || has(n, "icebreaker_paddlewheel");

    // ---------------------------------------------------------------
    // Animation / foliage rendering flags
    // ---------------------------------------------------------------
    const bool foliageOrTree = foliageName || treeLike;
    r.isFoliageLike    = foliageOrTree && !ambientCreature;
    r.disableAnimation = r.isFoliageLike || chestName;
    r.shadowWindFoliage = r.isFoliageLike;
    r.isFireflyEffect   = ambientCreature;

    // Small foliage: foliage-like models with a small bounding box.
    // Used to skip rendering during taxi/flight for performance.
    r.isSmallFoliage = r.isFoliageLike && !treeLike
                     && horiz < 3.0f && vert < 2.0f;

    // ---------------------------------------------------------------
    // Spell effects (named tokens + particle-dominated geometry heuristic)
    // ---------------------------------------------------------------
    static constexpr auto kEffectTokens = std::to_array<std::string_view>({
        "bubbles",        "dustcloud",        "hazardlight",
        "instancenewportal", "instanceportal",
        "lavabubble",     "lavasplash",        "lavasteam",         "levelup",
        "lightshaft",     "mageportal",        "particleemitter",
        "smokepuff",      "sparkle",           "spotlight",
        "volumetriclight", "wisps",            "worldtreeportal",
    });
    // A bare "steam" substring collides with solid models — the Steam Tonk /
    // Steam Tank vehicles, Steamwheedle doodads — turning them additive/unlit
    // (glowing translucent). The low-poly gate below alone is not enough: the
    // TBC/Turtle SteamTonk overlay models are tiny proxies that slip under the
    // vertex threshold, so also exclude vehicle names outright — a "tonk"/"tank"
    // is never a steam VFX (real ones are steam/steamgeyser/lavasteam/etc.).
    const bool steamVehicle = has(n, "tonk") || has(n, "tank");
    const bool steamVfx = has(n, "steam") && !steamVehicle
                        && emitterCount >= 1 && vertexCount <= 200;
    r.isSpellEffect = hasAny(n, kEffectTokens) || steamVfx
                    || (emitterCount >= 3 && vertexCount <= 200);
    // Instance portals are spell effects too.
    if (r.isInstancePortal) r.isSpellEffect = true;

    // ---------------------------------------------------------------
    // Ambient emitter type (for sound system integration)
    // ---------------------------------------------------------------
    if (r.isBrazierOrFire) {
        const bool isSmallFire = has(n, "small") || has(n, "campfire");
        r.ambientEmitterType = isSmallFire ? AmbientEmitterType::FireplaceSmall
                                           : AmbientEmitterType::FireplaceLarge;
    } else if (r.isTorch) {
        r.ambientEmitterType = AmbientEmitterType::Torch;
    } else if (forgeName) {
        r.ambientEmitterType = AmbientEmitterType::Forge;
    } else if (r.collisionSteppedFountain) {
        r.ambientEmitterType = AmbientEmitterType::Fountain;
    } else if (r.isWaterfall) {
        r.ambientEmitterType = AmbientEmitterType::Waterfall;
    }

    return r;
}

// ---------------------------------------------------------------------------
// classifyBatchTexture
// ---------------------------------------------------------------------------

M2BatchTexClassification classifyBatchTexture(const std::string& lowerTexKey)
{
    M2BatchTexClassification r;

    // Exact paths for well-known lantern / lamp glow-card textures.
    static constexpr auto kExactGlowTextures = std::to_array<std::string_view>({
        "world\\azeroth\\karazahn\\passivedoodads\\bonfire\\flamelicksmallblue.blp",
        "world\\expansion06\\doodads\\nightelf\\7ne_druid_streetlamp01_light.blp",
        "world\\generic\\human\\passive doodads\\stormwind\\t_vfx_glow01_64.blp",
        "world\\generic\\nightelf\\passive doodads\\lamps\\glowblue32.blp",
        "world\\generic\\nightelf\\passive doodads\\magicalimplements\\glow.blp",
    });
    for (auto s : kExactGlowTextures)
        if (lowerTexKey == s) { r.exactLanternGlowTex = true; break; }

    static constexpr auto kGlowTokens = std::to_array<std::string_view>({
        "flare", "glow", "halo", "light",
    });
    static constexpr auto kFlameTokens = std::to_array<std::string_view>({
        "ember", "fire", "flame", "flamelick",
    });
    static constexpr auto kGlowCardTokens = std::to_array<std::string_view>({
        "flamelick", "genericglow", "glow", "glowball",
        "lensflare", "lightbeam",   "t_vfx",
    });
    static constexpr auto kLikelyFlameTokens = std::to_array<std::string_view>({
        "fire", "flame", "torch",
    });
    static constexpr auto kLanternFamilyTokens = std::to_array<std::string_view>({
        "elf", "lamp", "lantern", "quel", "silvermoon", "thalas",
    });
    static constexpr auto kCoolTintTokens = std::to_array<std::string_view>({
        "arcane", "blue", "nightelf",
    });
    static constexpr auto kRedTintTokens = std::to_array<std::string_view>({
        "red", "ruby", "scarlet",
    });

    r.hasGlowToken     = hasAny(lowerTexKey, kGlowTokens);
    r.hasFlameToken    = hasAny(lowerTexKey, kFlameTokens);
    r.hasGlowCardToken = hasAny(lowerTexKey, kGlowCardTokens);
    if (r.exactLanternGlowTex) r.hasGlowCardToken = true;
    r.likelyFlame      = hasAny(lowerTexKey, kLikelyFlameTokens);
    r.lanternFamily    = hasAny(lowerTexKey, kLanternFamilyTokens);
    // Stormwind street lamps use an opaque unlit glass texture rather than a
    // named glow card or particle emitter. Preserve that glass mesh and layer
    // a soft halo over it in the renderer.
    r.softGlowSurface  = lowerTexKey ==
        "dungeons\\textures\\doodads\\stormwindlampglass.blp";
    r.glowTint         = hasAny(lowerTexKey, kCoolTintTokens) ? 1
                       : hasAny(lowerTexKey, kRedTintTokens)  ? 2
                       : 0;

    return r;
}

// ---------------------------------------------------------------------------
// classifyAmbientEmitter — lightweight name-only emitter type detection
// ---------------------------------------------------------------------------

AmbientEmitterType classifyAmbientEmitter(const std::string& lowerName)
{
    const bool fireName    = has(lowerName, "fire") || has(lowerName, "campfire")
                           || has(lowerName, "bonfire");
    const bool brazierName = has(lowerName, "brazier") || has(lowerName, "cauldronfire");
    const bool forgeName   = has(lowerName, "forge") && !has(lowerName, "forgelava");

    if (fireName || brazierName) {
        const bool isSmall = has(lowerName, "small") || has(lowerName, "campfire");
        return isSmall ? AmbientEmitterType::FireplaceSmall
                       : AmbientEmitterType::FireplaceLarge;
    }
    if (has(lowerName, "torch"))     return AmbientEmitterType::Torch;
    if (forgeName)                   return AmbientEmitterType::Forge;
    if (has(lowerName, "fountain"))  return AmbientEmitterType::Fountain;
    if (has(lowerName, "waterfall")) return AmbientEmitterType::Waterfall;
    return AmbientEmitterType::None;
}

} // namespace rendering
} // namespace wowee
