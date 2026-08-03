// lua_services.hpp — Dependency-injected services for Lua bindings.
// Replaces Application::getInstance() calls in domain API files (§5.2).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace wowee::core  { class Window; }
namespace wowee::audio { class AudioCoordinator; }
namespace wowee::game  { class ExpansionRegistry; }

namespace wowee::addons {

struct LuaServices {
    core::Window*            window            = nullptr;
    audio::AudioCoordinator* audioCoordinator  = nullptr;
    game::ExpansionRegistry* expansionRegistry = nullptr;

    /// Load a load-on-demand addon by name, as LoadAddOn() does.
    ///
    /// The interface asks for these itself: opening the talent frame is
    /// TalentFrame_LoadUI calling LoadAddOn("Blizzard_TalentUI"), and the same
    /// goes for the achievement, macro, key binding, trade skill and glyph
    /// panels. A stub answering "MISSING" left every one of them dead.
    ///
    /// Returns WoW's pair: loaded, and a reason when it did not. Reasons are
    /// WoW's own tokens — MISSING, DISABLED, LOAD_ON_DEMAND_ERROR.
    std::function<bool(const std::string& name, std::string& reason)> loadAddOn;
    /// Whether a named addon has been loaded, for IsAddOnLoaded.
    std::function<bool(const std::string& name)> isAddOnLoaded;

    /// Every icon this install carries, as paths SetTexture accepts.
    ///
    /// The macro and guild bank pickers are grids over this list: they ask how
    /// many there are and then for one at a time by index. Built from the asset
    /// manifest rather than a fixed list, so an install carrying different art
    /// offers what it actually has.
    std::function<const std::vector<std::string>&()> listIconTextures;
};

} // namespace wowee::addons
