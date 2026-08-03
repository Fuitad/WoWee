#include "ui/framexml_takeover.hpp"

#include "core/logger.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <set>
#include <string>

namespace wowee::ui {

namespace {

struct Entry { UiElement element; std::string_view name; };

// One row per element, and the only place a name is written down.
constexpr std::array<Entry, 45> kElements{{
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
    {UiElement::QuestGiver,   "questgiver"},
    {UiElement::Gossip,       "gossip"},
    {UiElement::Mail,         "mail"},
    {UiElement::Vendor,       "vendor"},
    {UiElement::Loot,         "loot"},
    {UiElement::Bank,         "bank"},
    {UiElement::PartyFrames,  "partyframes"},
    {UiElement::Social,       "social"},
    {UiElement::TradeSkill,   "tradeskill"},
    {UiElement::ClassTrainer, "classtrainer"},
    {UiElement::AuctionHouse, "auctionhouse"},
    {UiElement::GuildBank,    "guildbank"},
    {UiElement::Inspect,      "inspect"},
    {UiElement::Buffs,        "buffs"},
    {UiElement::Durability,   "durability"},
    {UiElement::Achievements, "achievements"},
    {UiElement::BarberShop,   "barbershop"},
    {UiElement::Taxi,         "taxi"},
    {UiElement::Stable,       "stable"},
    {UiElement::Book,         "book"},
    {UiElement::GameMenu,          "gamemenu"},
    {UiElement::Help,              "help"},
    {UiElement::BattlegroundScore, "bgscore"},
    {UiElement::Totems,       "totems"},
    {UiElement::Talents,      "talents"},
    {UiElement::UiErrors,     "uierrors"},
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
                   "mainmenubar", "characterframe", "bags", "castbar",
                   "spellbook", "petframe", "focusframe", "buffs", "durability"};
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
                // "none" and "all" are answers, not element names — "none" is
                // how a run says to use this client's own interface throughout,
                // and reporting it as a misspelling reads as though the flag
                // was ignored.
                bool known = (name == "mainmenubar" || name == "none" ||
                              name == "all");
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

namespace {
/// Written from the packet thread and read from the render thread.
std::atomic<bool> gWorldEntered{false};
std::atomic<bool> gCheckRequested{false};
std::atomic<bool> gMouseOwned{false};
} // namespace

namespace {
/// Main thread only: the Lua bindings that set it and the renderer that reads
/// it both run there.
std::string gCursorItem;
} // namespace

void frameXmlSetCursorItem(const std::string& iconPath) { gCursorItem = iconPath; }
const std::string& frameXmlCursorItem() { return gCursorItem; }

void frameXmlNoteMouseOwned(bool owned) {
    gMouseOwned.store(owned, std::memory_order_relaxed);
}
bool frameXmlOwnsMouse() { return gMouseOwned.load(std::memory_order_relaxed); }

void frameXmlNoteWorldEntry() { gWorldEntered.store(true, std::memory_order_relaxed); }
bool frameXmlWorldEntered() { return gWorldEntered.load(std::memory_order_relaxed); }

namespace { std::atomic<bool> gProbeRequested{false}; }

void frameXmlRequestCheck() {
    gCheckRequested.store(true, std::memory_order_relaxed);
    gProbeRequested.store(true, std::memory_order_relaxed);
}
bool frameXmlTakeCheckRequest() {
    return gCheckRequested.exchange(false, std::memory_order_relaxed);
}
bool frameXmlTakeProbeRequest() {
    return gProbeRequested.exchange(false, std::memory_order_relaxed);
}

bool frameXmlBuiltOnDemand(std::string_view frameName) {
    // Aura buttons, and nothing else so far. The buff frame holds up to 32 of
    // them and creates each the first time an aura needs it, so on a character
    // with no buffs none of them exist and that is the correct state.
    return frameName.rfind("BuffButton", 0) == 0 ||
           frameName.rfind("DebuffButton", 0) == 0;
}

std::vector<std::string> frameXmlCandidateFrames() {
    // The elements this branch has not taken over yet, in the order they are
    // likely to go next. Named individually rather than derived from the check
    // list so that adding a candidate is a deliberate act.
    struct Candidate { UiElement element; const char* frames; };
    // Empty for now: every element with a FrameXML equivalent worth the swap has
    // been handed over. What is left is drawn by this client on purpose — chat,
    // the quest log, the quest tracker — so there is nothing to report as ready.
    // Adding the next one back is a matter of listing it here.
    static const std::vector<Candidate> kCandidates = {};

    std::vector<std::string> out;
    for (const Candidate& c : kCandidates) {
        if (frameXmlOwns(c.element)) continue;   // already in use, checked above
        std::string all(c.frames);
        size_t at = 0;
        while (at < all.size()) {
            const size_t sp = all.find(' ', at);
            std::string one = all.substr(
                at, sp == std::string::npos ? std::string::npos : sp - at);
            if (!one.empty()) out.push_back(std::move(one));
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
    }
    return out;
}

namespace {
struct Suppress {
    UiElement element;
    const char* frames;
    /// True when these frames arrive with a load-on-demand addon and so do not
    /// exist until something asks for it. Suppression still works — the pass
    /// looks each name up every frame — but the "nothing is named this" report
    /// must stay quiet about them, or it fires for all of them every run and
    /// stops being worth reading.
    bool lazy = false;
};
const Suppress kSuppress[] = {
        // The tabs are parented to UIParent rather than to the frame they
        // belong to, so hiding the chat windows leaves a row of tabs behind.
        // The combat log is ChatFrame2 with its own strip of buttons.
        {UiElement::Chat,     "ChatFrame1 ChatFrame2 ChatFrame3 ChatFrame4 ChatFrame5 "
                              "ChatFrame6 ChatFrame7 "
                              "ChatFrame1Tab ChatFrame2Tab ChatFrame3Tab ChatFrame4Tab "
                              "ChatFrame5Tab ChatFrame6Tab ChatFrame7Tab "
                              "GeneralDockManager GeneralDockManagerOverflowButton "
                              "ChatFrameMenuButton FriendsMicroButton "
                              "CombatLogQuickButtonFrame_Custom", true},
        {UiElement::QuestLog, "QuestLogFrame QuestLogDetailFrame"},
        // Talking to an NPC opened two of everything: this client's gossip and
        // quest windows, which work, and FrameXML's, which cannot — the calls
        // behind them are among the names the missing-API report lists every
        // run. The greeting and detail panels are children of QuestFrame and
        // go with it.
        {UiElement::QuestGiver, "QuestFrame"},
        {UiElement::Gossip,     "GossipFrame"},
        {UiElement::Mail,       "MailFrame OpenMailFrame"},
        // Every one of these has a working window in this client and a
        // FrameXML twin that shows on the same event. The client fires
        // MERCHANT_SHOW, LOOT_OPENED, BANKFRAME_OPENED, PARTY_MEMBERS_CHANGED
        // and FRIENDLIST_UPDATE, so all of them were appearing in pairs.
        {UiElement::Vendor,      "MerchantFrame"},
        {UiElement::Loot,        "LootFrame"},
        {UiElement::Bank,        "BankFrame"},
        {UiElement::PartyFrames, "PartyMemberFrame1 PartyMemberFrame2 "
                                 "PartyMemberFrame3 PartyMemberFrame4"},
        {UiElement::Social,      "FriendsFrame"},
        // These four arrive with the load-on-demand addons, which now load —
        // so making them work is what put a second window beside the client's
        // at every profession, trainer, auctioneer and guild bank. The panels
        // themselves are finished and waiting; this only decides which of the
        // two is on screen.
        {UiElement::TradeSkill,   "TradeSkillFrame", true},
        {UiElement::ClassTrainer, "ClassTrainerFrame", true},
        {UiElement::AuctionHouse, "AuctionFrame", true},
        {UiElement::GuildBank,    "GuildBankFrame", true},
        {UiElement::Inspect,      "InspectFrame", true},
        // BARBER_SHOP_OPEN is fired and the achievements micro button belongs
        // to the bar this branch has taken over, so both of these can open
        // beside the client's own.
        {UiElement::Achievements, "AchievementFrame", true},
        {UiElement::BarberShop,   "BarberShopFrame", true},
        // These three cannot appear yet: TAXIMAP_OPENED, PET_STABLE_SHOW and
        // ITEM_TEXT_BEGIN are not fired. Named anyway, because that is a fact
        // about what this client reaches rather than a decision about which
        // window should win, and firing any of those events later would
        // otherwise put a second window on screen with nothing to say why.
        {UiElement::Taxi,         "TaxiFrame"},
        {UiElement::Stable,       "PetStableFrame"},
        {UiElement::Book,         "ItemTextFrame"},
        // All three open from the micro buttons, which belong to the bar this
        // branch draws — so they appear without anyone deciding they should,
        // beside this client's own escape menu, settings and ticket window.
        {UiElement::GameMenu,   "GameMenuFrame InterfaceOptionsFrame "
                                "VideoOptionsFrame AudioOptionsFrame"},
        {UiElement::Help,       "HelpFrame TicketStatusFrame"},
        {UiElement::BattlegroundScore, "WorldStateScoreFrame"},
        // Reachable only since their APIs were finished: a window whose
        // functions all answer opens where before it stayed empty and
        // unnoticed. This client draws a totem bar and a talent screen of its
        // own.
        //
        // ReputationFrame is deliberately absent. It is one of
        // CHARACTERFRAME_SUBFRAMES, so it belongs to the character frame rather
        // than standing alone — and that is owned, with this client's whole
        // character screen already gated on it. Suppressing the frame blanked
        // the Reputation tab of a window FrameXML is supposed to be drawing.
        {UiElement::Totems,     "TotemFrame MultiCastActionBarFrame"},
        {UiElement::Talents,    "PlayerTalentFrame", /*lazy=*/true},
        // Errors are fired as UI_ERROR_MESSAGE, which UIErrorsFrame listens
        // for — so every refusal the server sends was shown twice, once by
        // each interface.
        {UiElement::UiErrors,   "UIErrorsFrame"},
        // This client draws combo points on both the player and the target
        // frame, so FrameXML's separate display is a third set of them.
        {UiElement::TargetFrame, "ComboFrame"},
        // Found by the unaccounted-element check on its first run. The world
        // map is neither handed over nor hidden, so FrameXML's draws over this
        // client's own. It appears in the check list, which is what made it
        // look accounted for on every reading by eye.
        {UiElement::WorldMap,   "WorldMapFrame WorldMapDetailFrame "
                                "WorldMapButton WorldMapZoneMinimapDropDown"},
        // WatchFrameTitle is the "Objectives" label, and the buttons beside it
        // ride on the same frame.
        {UiElement::QuestTracker, "WatchFrame"},
    };
}  // namespace

std::vector<std::string> frameXmlSuppressedFrames() {

    std::vector<std::string> out;
    for (const Suppress& s : kSuppress) {
        if (frameXmlOwns(s.element)) continue;   // it is the one in use
        std::string all(s.frames);
        size_t at = 0;
        while (at < all.size()) {
            const size_t sp = all.find(' ', at);
            std::string one = all.substr(
                at, sp == std::string::npos ? std::string::npos : sp - at);
            if (!one.empty()) out.push_back(std::move(one));
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
    }
    return out;
}

std::vector<std::string> frameXmlLazySuppressedFrames() {
    std::vector<std::string> out;
    for (const Suppress& s : kSuppress) {
        if (!s.lazy || !s.frames) continue;
        std::string all(s.frames);
        size_t at = 0;
        while (at < all.size()) {
            const size_t sp = all.find(' ', at);
            std::string one = all.substr(
                at, sp == std::string::npos ? std::string::npos : sp - at);
            if (!one.empty()) out.push_back(std::move(one));
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
    }
    return out;
}

void frameXmlReportUnaccountedElements() {
    // Every element must be one thing or the other: drawn by FrameXML with
    // this client's version gated off, or drawn by this client with FrameXML's
    // hidden. An element that is neither is drawn twice, and that is not
    // visible from either list on its own — it is the gap between them.
    //
    // Thirteen windows were in that gap and nobody noticed until they were
    // looked for: the vendor, the loot window, the bank, the party frames, the
    // friends list, the quest giver, the gossip list, the mailbox, and then
    // the trade skill, trainer, auction and guild bank panels once their
    // addons started loading. The buff bar and the durability warning made
    // fifteen. Saying so at startup is cheaper than finding the sixteenth the
    // same way.
    const std::vector<std::string> suppressed = frameXmlSuppressedFrames();
    for (const Entry& e : kElements) {
        if (frameXmlOwns(e.element)) continue;
        // Suppressed elements contribute frame names; an element that
        // contributes none while not being owned is unaccounted for.
        bool hasFrames = false;
        for (const Suppress& sup : kSuppress) {
            if (sup.element == e.element && sup.frames && *sup.frames) {
                hasFrames = true;
                break;
            }
        }
        if (!hasFrames) {
            LOG_WARNING("FrameXML: '", e.name, "' is neither handed over nor "
                        "suppressed — if FrameXML draws it, it is on screen "
                        "twice");
        }
    }
}

std::vector<std::string> frameXmlCheckFrames() {
    // One row per element: what has to exist for it to have arrived. Chosen as
    // the frame itself, the art that frames it, and the parts that carry live
    // data — which between them separate "never built" from "built and empty"
    // from "built and misplaced".
    struct Check { UiElement element; const char* frames; };
    static const Check kChecks[] = {
        {UiElement::PlayerFrame,  "PlayerFrame PlayerFrameTexture PlayerPortrait "
                                  "PlayerFrameHealthBar PlayerFrameManaBar PlayerName "
                                  "PlayerLevelText "
                                  // The numbers on the bars. Built and empty,
                                  // built and sized to nothing, and never built
                                  // are three different faults that look the
                                  // same on screen.
                                  "PlayerFrameHealthBarText PlayerFrameManaBarText"},
        {UiElement::TargetFrame,  "TargetFrame TargetFrameTextureFrame TargetFramePortrait "
                                  "TargetFrameHealthBar TargetFrameManaBar "
                                  "TargetFrameTextureFrameName TargetFrameNameBackground"},
        // The pet's cast bar belongs with the pet frame rather than with
        // the player's cast bar: this client draws it inside
        // renderPetFrame, so it goes when that does.
        {UiElement::PetFrame,     "PetFrame PetFrameHealthBar PetFrameManaBar "
                                  "PetCastingBarFrame"},
        {UiElement::Minimap,      "Minimap MinimapBorder MinimapZoomIn MinimapZoneText"},
        // The extra bars as well as the main one: this client draws its own
        // second and third bars from the settings, so naming only MainMenuBar
        // left four more stacked on top of them.
        {UiElement::ActionBar,    "MainMenuBar MainMenuBarArtFrame MainMenuBarLeftEndCap "
                                  "MainMenuBarRightEndCap ActionButton1 ActionButton12 "
                                  "MultiBarBottomLeft MultiBarBottomRight "
                                  "MultiBarLeft MultiBarRight"},
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
                                    "MagicResFrame1 CharacterMainHandSlot "
                                    // The rotate arrows sit on the model
                                    // frame's top-left corner, which is also
                                    // where the name sits — so where each one
                                    // actually lands is the question.
                                    "CharacterModelFrameRotateLeftButton "
                                    "CharacterModelFrameRotateRightButton"},
        {UiElement::Bags,         "ContainerFrame1 ContainerFrame1Item1 "
                                  "ContainerFrame1Name"},
        // A bag's background is assembled from a top, up to two middles and a
        // bottom, each a slice of one atlas positioned against the one above
        // it. When the stack is wrong the art shows a seam in the wrong place
        // and stops short of the frame, and only the individual rects and
        // slices say which piece is at fault.
        // Filed under the bag bar rather than the bags: FrameXML's containers
        // are opened from the bag bar, which this branch hands over, while the
        // "bags" element only decides whether this client's own bag window is
        // suppressed. Filed there they would never be looked at.
        {UiElement::BagBar,       "ContainerFrame2 ContainerFrame2Portrait "
                                  "ContainerFrame2BackgroundTop "
                                  "ContainerFrame2BackgroundMiddle1 "
                                  "ContainerFrame2BackgroundBottom"},
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
