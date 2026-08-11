// chat_utils.cpp — Shared chat utility functions.
// Extracted from chat_panel_utils.cpp (Phase 6.6 of chat_panel_ref.md).

#include "ui/chat/chat_utils.hpp"
#include "game/game_handler.hpp"
#include "game/character.hpp"
#include "game/text_tokens.hpp"
#include <vector>

namespace wowee { namespace ui { namespace chat_utils {

std::string replaceGenderPlaceholders(const std::string& text,
                                       game::GameHandler& gameHandler) {
    // Moved to the game layer: the chat handler has to resolve these before a
    // line reaches the interface, and it cannot reach into the UI to do it.
    return game::resolveTextTokens(text, gameHandler);
}

std::string getEntityDisplayName(const std::shared_ptr<game::Entity>& entity) {
    if (entity->getType() == game::ObjectType::PLAYER) {
        auto player = std::static_pointer_cast<game::Player>(entity);
        if (!player->getName().empty()) return player->getName();
    } else if (entity->getType() == game::ObjectType::UNIT) {
        auto unit = std::static_pointer_cast<game::Unit>(entity);
        if (!unit->getName().empty()) return unit->getName();
    } else if (entity->getType() == game::ObjectType::GAMEOBJECT) {
        auto go = std::static_pointer_cast<game::GameObject>(entity);
        if (!go->getName().empty()) return go->getName();
    }
    return "Unknown";
}

} // namespace chat_utils
} // namespace ui
} // namespace wowee
