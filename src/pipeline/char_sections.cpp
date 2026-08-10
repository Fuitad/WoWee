#include "pipeline/char_sections.hpp"

#include "pipeline/dbc_loader.hpp"

namespace wowee {
namespace pipeline {

namespace {
// CharSections' BaseSection column: which part of a character a row describes.
constexpr uint32_t kSectionSkin = 0;
constexpr uint32_t kSectionFace = 1;
constexpr uint32_t kSectionHair = 3;
constexpr uint32_t kSectionUnderwear = 4;
}  // namespace

CharacterSectionTextures resolveCharacterSections(
    const DBCFile* charSections,
    const CharSectionsFields& f,
    const CharacterAppearance& who,
    bool (*keepUnderwear)(const std::string&, void*),
    void* keepUnderwearContext) {

    CharacterSectionTextures out;
    if (!charSections) return out;

    // The nearest usable face, kept in case the exact pair is absent.
    //
    // Character creation falls back to a synthetic 0..9 range whenever its own
    // scan of this table comes up empty, so a character can be created carrying
    // a face number that has no row at all — and would then have no face for
    // good, because the lookup simply did not match and said nothing.
    std::string altFaceLower, altFaceUpper;
    bool haveAltFace = false;

    bool foundSkin = false, foundUnderwear = false;

    for (uint32_t r = 0; r < charSections->getRecordCount(); r++) {
        if (charSections->getUInt32(r, f.raceId) != who.raceId) continue;
        if (charSections->getUInt32(r, f.sexId) != who.sexId) continue;

        const uint32_t section = charSections->getUInt32(r, f.baseSection);
        const uint32_t variation = charSections->getUInt32(r, f.variationIndex);
        const uint32_t colour = charSections->getUInt32(r, f.colorIndex);

        if (section == kSectionSkin && !foundSkin && colour == who.skinId) {
            std::string tex1 = charSections->getString(r, f.texture1);
            if (!tex1.empty()) {
                out.bodySkin = tex1;
                foundSkin = true;
            }
            out.skinExtra = charSections->getString(r, f.texture2);
        } else if (section == kSectionHair && !out.haveHair &&
                   variation == who.hairStyleId && colour == who.hairColorId) {
            out.hair = charSections->getString(r, f.texture1);
            out.haveHair = !out.hair.empty();
        } else if (section == kSectionFace && !out.exactFace &&
                   variation == who.faceId && colour == who.skinId) {
            out.faceLower = charSections->getString(r, f.texture1);
            out.faceUpper = charSections->getString(r, f.texture2);
            out.exactFace = true;
            out.haveFace = !out.faceLower.empty();
        } else if (section == kSectionFace && !out.exactFace && !haveAltFace &&
                   (variation == who.faceId || colour == who.skinId)) {
            // The same face in another skin colour, or failing that any face in
            // the right colour. Either beats a blank head.
            std::string tex1 = charSections->getString(r, f.texture1);
            if (!tex1.empty()) {
                altFaceLower = tex1;
                altFaceUpper = charSections->getString(r, f.texture2);
                haveAltFace = true;
            }
        } else if (section == kSectionUnderwear && !foundUnderwear &&
                   colour == who.skinId) {
            for (uint32_t col = f.texture1; col <= f.texture1 + 2; col++) {
                std::string tex = charSections->getString(r, col);
                if (tex.empty()) continue;
                if (keepUnderwear && !keepUnderwear(tex, keepUnderwearContext)) continue;
                out.underwear.push_back(tex);
            }
            foundUnderwear = !out.underwear.empty();
        }

        if (foundSkin && out.haveHair && out.exactFace && foundUnderwear) break;
    }

    if (!out.exactFace && haveAltFace) {
        out.faceLower = altFaceLower;
        out.faceUpper = altFaceUpper;
        out.haveFace = true;
    }
    return out;
}

}  // namespace pipeline
}  // namespace wowee
