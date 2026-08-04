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
//     WOWEE_FRAMEXML_UI=candidates
//     WOWEE_FRAMEXML_UI=all
//
// "candidates" is the defaults plus every element the readiness report finds
// clean — every global its code calls answered, every event its frames want
// either sent or verified absent. Clean is not the same as seen working, and
// these are windows that open on an interaction, so a fault waits for the
// right NPC and then blocks it. That is why they are behind a word rather
// than in the defaults, and it is the batch to run when testing them.
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
    Bags,
    Spellbook,
    QuestLog,
    /// The quest giver, the gossip list and the mailbox. This client draws all
    /// three itself and they work; FrameXML's versions are drawn too unless
    /// they are named here, which is two of every window at every NPC.
    QuestGiver,
    Gossip,
    Mail,
    /// The rest of the windows this client draws itself. Each is named
    /// separately rather than lumped together so that handing one over later
    /// is a single word in the defaults, the way the character sheet was.
    Vendor,
    Loot,
    Bank,
    PartyFrames,
    Social,
    TradeSkill,
    ClassTrainer,
    AuctionHouse,
    GuildBank,
    Inspect,
    /// The buff and debuff bar. FrameXML's is already treated as in use — it
    /// is checked alongside the minimap cluster — but this client kept drawing
    /// its own beside it, so there were two.
    Buffs,
    /// The low-durability warning. Same story as the buffs: FrameXML's
    /// DurabilityFrame is checked as in use and this client drew its own
    /// warning beside it.
    Durability,
    /// The zone name that appears on crossing into one. Same story again:
    /// FrameXML's ZoneTextFrame listens for ZONE_CHANGED_NEW_AREA, which this
    /// client fires, so its large centred banner was raised beside this
    /// client's own smaller one and every zone crossing announced itself
    /// twice. The large one is what retail shows, so this is handed over
    /// rather than suppressed.
    ZoneText,
    /// Three more found by the unaccounted-frame sweep. Each has a working
    /// counterpart this client draws, and the first two are live duplicates:
    /// TRADE_SHOW and READY_CHECK are both fired, so FrameXML raised its
    /// window beside the client's own every time.
    ///
    /// RaidWarning is the exception and is named anyway. Its frames cannot
    /// appear today because CHAT_MSG_RAID_WARNING is never fired — but this
    /// client draws raid warnings from the chat history rather than from the
    /// event, so firing it later would put a second banner on screen with
    /// nothing to say why.
    Trade,
    ReadyCheck,
    RaidWarning,
    /// Windows this client draws that FrameXML also has. The last three cannot
    /// appear today because the events that would show them are not fired —
    /// but that is a fact about the client's current reach, not a decision,
    /// and it would stop being true the moment someone fired one.
    Achievements,
    BarberShop,
    Taxi,
    Stable,
    Book,
    /// The game menu and the options panels behind it, the help window, and
    /// the battleground scoreboard. Each has a working equivalent here, and
    /// each is reachable from the micro buttons on the bar this branch has
    /// taken over — so they open without anyone choosing them.
    GameMenu,
    Help,
    BattlegroundScore,
    /// Windows whose FrameXML version only became reachable once its API was
    /// finished. Each has a counterpart this client draws, so each has to be
    /// accounted for or it appears twice.
    Totems,
    Talents,
    UiErrors,
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

/// Every frame name the takeover system mentions anywhere, owned or not.
///
/// A name in here is a name somebody has considered. Anything FrameXML puts on
/// screen that is *not* in here is a part of the interface nobody has decided
/// about, which is how the zone banner came to be drawn beside this client's
/// own for months: it was not an element, so the element-level check had
/// nothing to iterate and never mentioned it.
std::vector<std::string> frameXmlAccountedFrames();

/// The frames worth looking at for elements not yet handed over.
///
/// Deciding whether the next element is ready means seeing whether FrameXML's
/// version of it is built, positioned and carrying data — and the check only
/// reports what is already owned, so readiness is exactly what cannot be seen.
/// These are reported alongside, marked as candidates.
std::vector<std::string> frameXmlCandidateFrames();

/// The frames to keep hidden because FrameXML draws them and this client has
/// not handed that element over.
///
/// Every FrameXML file is loaded, so every frame it declares exists and draws.
/// The takeover list only decides whether this client's own version is
/// suppressed alongside it — which for anything not yet handed over means two
/// of them on screen at once. Hiding them once after loading is not enough:
/// the interface shows them again on its own schedule.
std::vector<std::string> frameXmlSuppressedFrames();

/// The subset of those whose frames arrive with a load-on-demand addon.
///
/// They do not exist until something asks for that addon, so a report of names
/// that resolved to nothing has to leave them out — otherwise it names all of
/// them on every run and the one real typo is lost among them.
std::vector<std::string> frameXmlLazySuppressedFrames();

/// Whether a frame in the check list is one the interface builds only when it
/// has something to put in it.
///
/// A buff button does not exist until there is a buff: FrameXML creates them on
/// demand from AuraButton_Update. Reporting that as NOT BUILT alongside genuine
/// failures teaches the reader to skip the line, which costs the report the one
/// thing it is for. Named here rather than guessed from the name so that a real
/// BuffButton1 failure still reads as one.
bool frameXmlBuiltOnDemand(std::string_view frameName);

/// Note that the player has entered the world, and whether that has happened.
///
/// FrameXML does most of its arranging from PLAYER_ENTERING_WORLD: frames are
/// hidden, repositioned and filled with data that does not exist before then.
/// A diagnostic taken at load therefore describes a layout nobody ever sees,
/// which is worse than none — it looks like an answer. This is how the
/// diagnostics know to wait for the state being asked about.
/// Warn about any element that is neither handed over nor suppressed.
///
/// Such an element is drawn twice — once by this client and once by FrameXML —
/// and that is invisible from either list alone, because it is the gap between
/// them. Fifteen windows sat in that gap before anyone went looking.
void frameXmlReportUnaccountedElements();

void frameXmlNoteWorldEntry();
bool frameXmlWorldEntered();

/// Whether the interface has the cursor: a frame that takes the mouse is under
/// it, or one is holding a press.
///
/// The camera asks ImGui whether the interface wants the mouse, and FrameXML
/// draws into ImGui's background draw list — so ImGui has never heard of these
/// frames and answers no. Pressing a bag item therefore turned the camera as
/// well as pressing the item, and dragging one swung the view around.
void frameXmlNoteMouseOwned(bool owned);
bool frameXmlOwnsMouse();

/// The icon of the item the cursor is carrying, or empty for nothing.
///
/// Picking an item up in WoW takes it out of its slot and puts it on the
/// pointer, and that half is the client's job — FrameXML never draws it. Without
/// it a drag looked like nothing was happening at all, whether or not the move
/// went out. Set from the Lua bindings and read by the widget renderer, both on
/// the main thread.
void frameXmlSetCursorItem(const std::string& iconPath);
const std::string& frameXmlCursorItem();

/// Ask for the takeover check to be reported again on the next frame.
///
/// The automatic reports happen at fixed moments, and most of what goes wrong
/// is only visible in a state nobody can schedule: a target frame is only
/// wrong once something is targeted, a bag only once it is opened. Asking for
/// the report at the moment the interface looks wrong is the difference
/// between reading the state in question and guessing from an earlier one.
void frameXmlRequestCheck();

/// Consume a pending request, if there is one.
bool frameXmlTakeCheckRequest();

/// The same request, seen by the Lua side.
///
/// The widget report says whether a frame is shown; only the interface's own
/// API can say whether it should be. Two flags rather than one because the
/// renderer and the script engine each consume their own.
bool frameXmlTakeProbeRequest();

} // namespace wowee::ui
