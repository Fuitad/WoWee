#pragma once

// What the Escape key does, as a decision separate from the doing of it.
//
// Escape is not one action. It is nineteen, ordered, and the first whose
// condition holds is the one that runs: close the settings window, else the
// client's own menu, else cancel the cast, else close whichever server-side
// window is open - and only when none of that applies does the question reach
// the interface and then the game menu.
//
// The order is the whole of the behaviour and none of it was checkable. It
// lived as a chain of `else if` inside a draw, every branch reading live game
// state, so the only way to ask what Escape would do in a given situation was
// to be in that situation. That chain has been read end to end seven times
// against a report that the key does nothing, and every reading found it
// sound - which is exactly what a reading is bad at: it confirms each link and
// says nothing about which link runs.
//
// So the decision is a function of a struct of facts, and the facts are
// gathered at the call site. Free of the game handler, the panels and ImGui so
// it can be tested directly, which is the point.

namespace wowee::ui {

/// What Escape resolved to. One per branch, in the order they are tested.
enum class EscapeAction {
    /// Nothing is bound, or the key was swallowed before the chain.
    None,
    /// Put a picked-up item back where it came from.
    ReturnHeldItem,
    CloseSettingsWindow,
    CloseClientMenu,
    CancelCast,
    CloseLoot,
    CloseGossip,
    CloseVendor,
    CloseBarberShop,
    CloseBank,
    CloseGuildBank,
    CloseTrainer,
    CloseMailbox,
    CloseAuctionHouse,
    DeclineQuest,
    CloseQuestOfferReward,
    CloseQuestRequestItems,
    CancelTrade,
    /// Every window this client owns is closed. Whether Escape has anything
    /// left to close is the interface's answer to give, so the caller asks it
    /// and then calls resolveAfterInterface with what it said.
    AskTheInterface,
};

/// The same enum continued, for what happens once the interface has answered.
enum class EscapeOutcome {
    /// FrameXML had a panel open and closed it.
    InterfaceClosedAPanel,
    /// Nothing left to close, and the interface draws the game menu.
    ToggleInterfaceMenu,
    /// Nothing left to close, and this client draws the game menu.
    OpenClientMenu,
};

/// Everything the chain reads, gathered before it is asked.
///
/// Named for the question each answers rather than for the call that answers
/// it, so the test can state a situation without a game handler.
struct EscapeState {
    /// The interface's focused edit box already took this press, in the event
    /// pump, before the poll that asks this question ever ran. Taking it is
    /// what closed the box - and it is also what cleared the focus, so the
    /// typing guard the poll consults answers no by the time it is asked.
    /// Without this the same press closes the box and then opens the game
    /// menu behind it; WoW closes the box and stops.
    bool interfaceConsumedKey = false;

    /// An item is on the cursor, picked up from a bag or a vendor.
    bool holdingItem = false;

    // This client's own windows.
    bool settingsWindowShown = false;
    bool clientMenuShown = false;

    // Things the server knows about. Each of these has to be closed through
    // the client so the closing packet is sent - hiding the frame instead
    // would leave the server believing the window is still open.
    bool casting = false;
    bool lootOpen = false;
    bool gossipOpen = false;
    bool vendorOpen = false;
    bool barberShopOpen = false;
    bool bankOpen = false;
    bool guildBankOpen = false;
    bool trainerOpen = false;
    bool mailboxOpen = false;
    bool auctionHouseOpen = false;
    bool questDetailsOpen = false;
    bool questOfferRewardOpen = false;
    bool questRequestItemsOpen = false;
    bool tradeOpen = false;
};

/// The first branch whose condition holds.
EscapeAction resolveEscape(const EscapeState& state);

/// What is left once the interface has been asked.
///
/// `interfaceClosedAPanel` is what the interface answered; `frameXmlOwnsMenu`
/// is whether the game menu has been handed over. The second decides which of
/// two menus opens, and getting it wrong is silent: this branch used to set
/// the flag behind *this* client's menu unconditionally, and that menu is only
/// drawn while the element is not handed over - so with the menu handed over,
/// Escape set a flag nobody read and nothing appeared.
EscapeOutcome resolveAfterInterface(bool interfaceClosedAPanel,
                                    bool frameXmlOwnsMenu);

/// The branch's name, for the one log line this chain reports.
const char* escapeActionName(EscapeAction action);
const char* escapeOutcomeName(EscapeOutcome outcome);

}  // namespace wowee::ui
