#include "ui/framexml_takeover.hpp"

#include "core/logger.hpp"

#include <array>
#include <cstdlib>
#include <set>
#include <string>

namespace wowee::ui {

namespace {

struct Entry { UiElement element; std::string_view name; };

// One row per element, and the only place a name is written down.
constexpr std::array<Entry, 19> kElements{{
    {UiElement::PlayerFrame,  "playerframe"},
    {UiElement::TargetFrame,  "targetframe"},
    {UiElement::PetFrame,     "petframe"},
    {UiElement::FocusFrame,   "focusframe"},
    {UiElement::ActionBar,    "actionbar"},
    {UiElement::StanceBar,    "stancebar"},
    {UiElement::BagBar,       "bagbar"},
    {UiElement::MicroMenu,    "micromenu"},
    {UiElement::XpBar,        "xpbar"},
    {UiElement::RepBar,       "repbar"},
    {UiElement::CastBar,      "castbar"},
    {UiElement::Minimap,      "minimap"},
    {UiElement::Chat,         "chat"},
    {UiElement::QuestTracker, "questtracker"},
    {UiElement::WorldMap,     "worldmap"},
    {UiElement::CharacterFrame, "characterframe"},
    {UiElement::Bags,         "bags"},
    {UiElement::Spellbook,    "spellbook"},
    {UiElement::QuestLog,     "questlog"},
}};

/// Parsed once. An unknown name is reported rather than dropped: a typo would
/// otherwise read as a replacement that quietly did not happen.
const std::set<std::string>& requested() {
    static const std::set<std::string> names = [] {
        std::set<std::string> out;
        const char* raw = std::getenv("WOWEE_FRAMEXML_UI");

        // What this branch is working on, handed over without being asked.
        //
        // Every one of these has been seen drawing correctly: the player frame
        // with its art, portrait and bars; the minimap with the real map
        // inside its ring; the character sheet with the model in it; the
        // bottom bar. Requiring a flag to see them means every test run begins
        // by remembering the flag, and a run without it silently tests the old
        // interface instead. Naming any element in the environment replaces
        // this list rather than adding to it, so a single element can still be
        // looked at on its own.
        if (!raw || !*raw) {
            out = {"playerframe", "targetframe", "minimap",
                   "mainmenubar", "characterframe"};
            LOG_WARNING("FrameXML is drawing the branch defaults; "
                        "set WOWEE_FRAMEXML_UI to choose, or 'none' for this "
                        "client's own interface");
            return out;
        }

        std::string value(raw);
        size_t start = 0;
        while (start <= value.size()) {
            const size_t comma = value.find(',', start);
            std::string one = value.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            const size_t b = one.find_first_not_of(" \t");
            const size_t e = one.find_last_not_of(" \t");
            one = (b == std::string::npos) ? std::string() : one.substr(b, e - b + 1);
            if (!one.empty()) out.insert(one);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        if (out.count("all") == 0) {
            for (const std::string& name : out) {
                bool known = (name == "mainmenubar");
                for (const Entry& e : kElements) known |= (e.name == name);
                if (!known) LOG_WARNING("WOWEE_FRAMEXML_UI: no element called '", name, "'");
            }
        }
        if (!out.empty()) {
            std::string list;
            for (const std::string& n : out) { list += n; list += ' '; }
            LOG_WARNING("FrameXML is drawing these instead of the client: ", list);
        }
        return out;
    }();
    return names;
}

} // namespace

namespace {

/// Whether a name covers this element as part of something larger.
///
/// The client draws its action bar, bag bar, micro menu and the two thin bars
/// above them as separate pieces, because it built them separately. FrameXML
/// draws all of them as MainMenuBar: one frame, one strip of art, the griffins
/// at either end. Handing over "actionbar" alone therefore leaves the client's
/// bag bar and micro menu sitting on top of FrameXML's, in the same place, and
/// the result reads as one bar drawn twice rather than a replacement that half
/// worked.
bool coveredByGroup(const std::string& name, UiElement element) {
    if (name != "mainmenubar") return false;
    switch (element) {
        case UiElement::ActionBar:
        case UiElement::StanceBar:
        case UiElement::BagBar:
        case UiElement::MicroMenu:
        case UiElement::XpBar:
        case UiElement::RepBar:
            return true;
        default:
            return false;
    }
}

} // namespace

/// Whether FrameXML was loaded at all. Owning an element it did not build
/// would hide this client's version and put nothing in its place.
static bool frameXmlLoaded() {
    static const bool on = [] {
        const char* v = std::getenv("WOWEE_LOAD_FRAMEXML");
        return v ? (std::string(v) != "0") : true;
    }();
    return on;
}

bool frameXmlOwns(UiElement element) {
    // Nothing is owned if FrameXML was not loaded: hiding this client's own
    // version of something and putting nothing in its place is worse than
    // either interface on its own.
    if (!frameXmlLoaded()) return false;

    const auto& names = requested();
    if (names.empty() || names.count("none")) return false;
    if (names.count("all")) return true;
    if (names.count(std::string(uiElementName(element)))) return true;
    for (const std::string& n : names) {
        if (coveredByGroup(n, element)) return true;
    }
    return false;
}

std::vector<std::string> frameXmlCheckFrames() {
    // One row per element: what has to exist for it to have arrived. Chosen as
    // the frame itself, the art that frames it, and the parts that carry live
    // data — which between them separate "never built" from "built and empty"
    // from "built and misplaced".
    struct Check { UiElement element; const char* frames; };
    static const Check kChecks[] = {
        {UiElement::PlayerFrame,  "PlayerFrame PlayerFrameTexture PlayerPortrait "
                                  "PlayerFrameHealthBar PlayerFrameManaBar PlayerName"},
        {UiElement::TargetFrame,  "TargetFrame TargetFrameTextureFrame TargetFramePortrait "
                                  "TargetFrameHealthBar TargetFrameManaBar "
                                  "TargetFrameTextureFrameName TargetFrameNameBackground"},
        {UiElement::PetFrame,     "PetFrame PetFrameHealthBar PetFrameManaBar"},
        {UiElement::Minimap,      "Minimap MinimapBorder MinimapZoomIn MinimapZoneText"},
        {UiElement::ActionBar,    "MainMenuBar MainMenuBarArtFrame MainMenuBarLeftEndCap "
                                  "MainMenuBarRightEndCap ActionButton1 ActionButton12"},
        {UiElement::BagBar,       "MainMenuBarBackpackButton CharacterBag0Slot"},
        {UiElement::MicroMenu,    "CharacterMicroButton MainMenuBarPerformanceBar"},
        // MainMenuExpBar is the bar itself; ExhaustionTick is the rested
        // marker that rides on it. Checked against the XML rather than
        // guessed — a name invented here reports NOT BUILT forever and reads
        // as a fault in the interface rather than in this list.
        {UiElement::XpBar,        "MainMenuExpBar ExhaustionTick"},
        // Both hang off the minimap cluster, so if either is in the wrong
        // place the cluster's own rect is the first thing to look at.
        {UiElement::Minimap,      "MinimapCluster BuffFrame BuffButton1 "
                                  "DurabilityFrame"},
        {UiElement::RepBar,       "ReputationWatchBar ReputationWatchStatusBar"},
        {UiElement::StanceBar,    "ShapeshiftBarFrame ShapeshiftButton1"},
        {UiElement::CastBar,      "CastingBarFrame CastingBarFrameBorder CastingBarFrameText"},
        {UiElement::Chat,         "ChatFrame1 ChatFrame1EditBox GeneralDockManager"},
        {UiElement::QuestTracker, "WatchFrame WatchFrameTitle"},
        {UiElement::FocusFrame,   "FocusFrame FocusFrameHealthBar"},
        {UiElement::WorldMap,     "WorldMapFrame WorldMapDetailFrame WorldMapButton "
                                  "WorldMapZoneMinimapDropDown"},
        {UiElement::CharacterFrame, "CharacterFrame PaperDollFrame CharacterModelFrame "
                                    "CharacterNameText CharacterHeadSlot "
                                    "CharacterResistanceFrame CharacterAttributesFrame "
                                    "MagicResFrame1 CharacterMainHandSlot"},
        {UiElement::Bags,         "ContainerFrame1 ContainerFrame1Item1 "
                                  "ContainerFrame1Name"},
        {UiElement::Spellbook,    "SpellBookFrame SpellButton1 SpellBookSkillLineTab1"},
        {UiElement::QuestLog,     "QuestLogFrame QuestLogListScrollFrame "
                                  "QuestLogDetailScrollFrame"},
    };

    std::vector<std::string> out;
    for (const Check& c : kChecks) {
        if (!frameXmlOwns(c.element)) continue;
        std::string all(c.frames);
        size_t at = 0;
        while (at < all.size()) {
            const size_t sp = all.find(' ', at);
            const std::string one = all.substr(
                at, sp == std::string::npos ? std::string::npos : sp - at);
            if (!one.empty()) out.push_back(one);
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
    }
    return out;
}

std::string_view uiElementName(UiElement element) {
    for (const Entry& e : kElements) {
        if (e.element == element) return e.name;
    }
    return "";
}

} // namespace wowee::ui
