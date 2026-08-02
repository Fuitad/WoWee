#pragma once

// Which parts of the interface FrameXML has taken over from this client.
//
// The two interfaces draw the same things — a player frame, an action bar, a
// minimap — and while the original is being replaced they would otherwise both
// be on screen at once. This says which of the client's own elements to leave
// out, one name at a time, so a replacement can be tried and backed out without
// touching the code that draws either.
//
// Set through the environment:
//
//     WOWEE_FRAMEXML_UI=playerframe,targetframe
//     WOWEE_FRAMEXML_UI=mainmenubar
//     WOWEE_FRAMEXML_UI=all
//
// "mainmenubar" is one name for the whole bottom of the screen — the action
// bar, the stance bar, the bags, the micro menu and the two thin bars above
// them — because FrameXML draws all of them as a single frame and handing over
// any one of them on its own leaves the rest sitting on top of it.
//
// Names are matched exactly and unknown ones are reported at startup rather
// than ignored, because a misspelling would otherwise look like a replacement
// that silently did not happen.

#include <string>
#include <string_view>
#include <vector>

namespace wowee::ui {

/// The client's own elements that FrameXML has an equivalent for.
enum class UiElement {
    PlayerFrame,
    TargetFrame,
    PetFrame,
    FocusFrame,
    ActionBar,
    StanceBar,
    BagBar,
    MicroMenu,
    XpBar,
    RepBar,
    CastBar,
    Minimap,
    Chat,
    QuestTracker,
    WorldMap,
    CharacterFrame,
};

/// True when FrameXML is drawing this instead, so the client should not.
bool frameXmlOwns(UiElement element);

/// The name an element is switched on by, for diagnostics.
std::string_view uiElementName(UiElement element);

/// The frames worth checking for every element currently handed over.
///
/// Replacing one part of the interface at a time only works if there is an
/// answer to "did it arrive". A screenshot is the honest test but not always
/// available, and the failures so far have all been legible from the tree
/// alone: a frame that was never created, one that is hidden, one laid out to
/// nothing. These are the frames each element stands or falls on, so the check
/// can be one block of log rather than an inspection.
std::vector<std::string> frameXmlCheckFrames();

} // namespace wowee::ui
