#include "ui/escape_action.hpp"

namespace wowee::ui {

EscapeAction resolveEscape(const EscapeState& s) {
    // In order, and the order is the behaviour. Two rules hold it together:
    //
    // This client's own windows go first, because they are drawn over
    // everything and closing what is underneath while one is up would take the
    // wrong thing away.
    if (s.settingsWindowShown)     return EscapeAction::CloseSettingsWindow;
    if (s.clientMenuShown)         return EscapeAction::CloseClientMenu;
    // A cast in progress goes before any window: Escape cancelling a cast is
    // what the key is most often for, and a window that happens to be open
    // should not eat it.
    if (s.casting)                 return EscapeAction::CancelCast;
    // Then everything the server believes is open, each closed through the
    // client so the closing packet is sent.
    if (s.lootOpen)                return EscapeAction::CloseLoot;
    if (s.gossipOpen)              return EscapeAction::CloseGossip;
    if (s.vendorOpen)              return EscapeAction::CloseVendor;
    if (s.barberShopOpen)          return EscapeAction::CloseBarberShop;
    if (s.bankOpen)                return EscapeAction::CloseBank;
    if (s.guildBankOpen)           return EscapeAction::CloseGuildBank;
    if (s.trainerOpen)             return EscapeAction::CloseTrainer;
    if (s.mailboxOpen)             return EscapeAction::CloseMailbox;
    if (s.auctionHouseOpen)        return EscapeAction::CloseAuctionHouse;
    if (s.questDetailsOpen)        return EscapeAction::DeclineQuest;
    if (s.questOfferRewardOpen)    return EscapeAction::CloseQuestOfferReward;
    if (s.questRequestItemsOpen)   return EscapeAction::CloseQuestRequestItems;
    if (s.tradeOpen)               return EscapeAction::CancelTrade;
    // Nothing this client owns is open. What is left is the interface's own —
    // the character sheet, the spellbook, the quest log, the world map — and
    // whether one of those is up is its answer to give, not ours.
    return EscapeAction::AskTheInterface;
}

EscapeOutcome resolveAfterInterface(bool interfaceClosedAPanel,
                                    bool frameXmlOwnsMenu) {
    if (interfaceClosedAPanel) return EscapeOutcome::InterfaceClosedAPanel;
    // Whoever draws the menu is who Escape has to ask.
    return frameXmlOwnsMenu ? EscapeOutcome::ToggleInterfaceMenu
                            : EscapeOutcome::OpenClientMenu;
}

const char* escapeActionName(EscapeAction action) {
    switch (action) {
        case EscapeAction::None:                    return "nothing";
        case EscapeAction::CloseSettingsWindow:     return "close the settings window";
        case EscapeAction::CloseClientMenu:         return "close this client's menu";
        case EscapeAction::CancelCast:              return "cancel the cast";
        case EscapeAction::CloseLoot:               return "close loot";
        case EscapeAction::CloseGossip:             return "close gossip";
        case EscapeAction::CloseVendor:             return "close the vendor";
        case EscapeAction::CloseBarberShop:         return "close the barber shop";
        case EscapeAction::CloseBank:               return "close the bank";
        case EscapeAction::CloseGuildBank:          return "close the guild bank";
        case EscapeAction::CloseTrainer:            return "close the trainer";
        case EscapeAction::CloseMailbox:            return "close the mailbox";
        case EscapeAction::CloseAuctionHouse:       return "close the auction house";
        case EscapeAction::DeclineQuest:            return "decline the quest";
        case EscapeAction::CloseQuestOfferReward:   return "close the quest reward";
        case EscapeAction::CloseQuestRequestItems:  return "close the quest items";
        case EscapeAction::CancelTrade:             return "cancel the trade";
        case EscapeAction::AskTheInterface:         return "ask the interface";
    }
    return "?";
}

const char* escapeOutcomeName(EscapeOutcome outcome) {
    switch (outcome) {
        case EscapeOutcome::InterfaceClosedAPanel:
            return "the interface closed a panel of its own";
        case EscapeOutcome::ToggleInterfaceMenu:
            return "toggle the interface's game menu";
        case EscapeOutcome::OpenClientMenu:
            return "open this client's own menu";
    }
    return "?";
}

}  // namespace wowee::ui
