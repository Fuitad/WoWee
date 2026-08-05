// lua_services.hpp — Dependency-injected services for Lua bindings.
// Replaces Application::getInstance() calls in domain API files (§5.2).
#pragma once

#include <functional>
#include <cstdint>
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

    /// Run a macro body, as RunMacroText() does — one command per line,
    /// through the same path the action bar uses for a macro button.
    std::function<void(const std::string&)> runMacroText;

    /// Ask for the interface to be reloaded, as ReloadUI() does.
    ///
    /// A request rather than the act: reloading shuts the Lua state down and
    /// builds a new one, and ReloadUI is called from inside that state — by a
    /// static popup's OnAccept, or by /reload going through the interface. Doing
    /// it there frees the machinery running the call. The application performs
    /// it between frames instead.
    std::function<void()> requestReloadUI;

    /// Screen gamma, for the interface's own video options. Callbacks rather
    /// than a renderer pointer, to keep this header off the rendering ones.
    std::function<float()> getGamma;
    std::function<void(float)> setGamma;

    /// What the world map is showing, for the interface's own map.
    ///
    /// Callbacks and plain structs rather than the facade, on the same
    /// principle as the gamma pair above: this header stays off the rendering
    /// ones. The client keeps every one of these and drew them only itself, so
    /// FrameXML's map had a full set of readers and nothing to read.
    struct MapOverlay {
        std::string texture;   ///< texture prefix; the tiles are texture1..N
        int width = 0, height = 0;
        int offsetX = 0, offsetY = 0;
    };
    std::function<std::vector<MapOverlay>()> getMapOverlays;

    struct MapLandmark {
        std::string name;
        std::string description;
        int   icon = 0;
        float x = 0.0f, y = 0.0f;   ///< [0,1] across the map being shown
    };
    std::function<std::vector<MapLandmark>()> getMapLandmarks;

    /// The zone under a point on the map, in [0,1] map space, or empty. This
    /// is what names the label under the cursor.
    std::function<std::string(float, float)> getMapZoneNameAt;
    /// Drill into that zone, as clicking it does. False when there is none.
    std::function<bool(float, float)> clickMapPoint;
    /// Step out one level: zone to continent, continent to world.
    std::function<void()> zoomMapOut;

    /// The map's two dropdowns and its zoom-out button, which set the map from
    /// a continent index and a zone index rather than from a point on it.
    /// Both one-based, as the interface counts them; zone zero is the
    /// continent itself and continent zero is the world.
    std::function<std::vector<std::string>()> getMapContinentNames;
    std::function<std::vector<std::string>(int)> getMapZoneNames;
    std::function<void(int, int)> setMapByIndex;
    /// What is shown now, in those same indices, and whether there is a level
    /// above it. The button that asks the last of these was disabled always.
    std::function<int()> getMapContinentIndex;
    std::function<int()> getMapZoneIndex;
    std::function<bool()> canZoomMapOut;
    /// Back to the zone the player is standing in, which the interface asks
    /// for every time it shows the map.
    std::function<void()> showPlayerMapZone;
    /// The WorldMapArea id being shown, and setting the map from one. Zero
    /// when a continent rather than a zone is shown, which is the branch the
    /// interface takes to ask about continents instead.
    std::function<uint32_t()> getMapWorldAreaId;
    std::function<void(uint32_t)> setMapWorldAreaId;

    /// The zone the player is standing in, worked out from the terrain under
    /// them and refreshed every frame.
    ///
    /// The server's zone is only told to us on SMSG_INIT_WORLD_STATES, which
    /// arrives when the server notices a zone change and not otherwise — so
    /// reading that alone leaves the name stale, naming the last zone the
    /// server announced rather than the one being walked through. The real
    /// client works this out locally for exactly that reason.
    std::function<uint32_t()> getLiveZoneId;

    /// Nameplates over hostile and neutral units, for nameplateShowEnemies.
    ///
    /// There is no counterpart for nameplateShowFriends: this client always
    /// draws player names and has no switch for them, so that CVar is stored
    /// and answered but changes nothing.
    std::function<bool()> getNameplatesShown;
    std::function<void(bool)> setNameplatesShown;

    /// Whether the minimap turns with the camera, for rotateMinimap.
    std::function<bool()> getMinimapRotate;
    std::function<void(bool)> setMinimapRotate;

    /// The barber shop's selectors, for the interface's own barber panel.
    ///
    /// Selector numbers are FrameXML's BarberShopFrameSelector IDs: 1 hair
    /// style, 2 hair colour, 3 facial hair, 4 skin. The state behind them used
    /// to be built inside this client's own barber window, so with the panel
    /// handed over nothing had built it — these reach a version that does not
    /// depend on who is drawing.
    std::function<bool(int selector, std::string& name, bool& isCurrent)> getBarberStyleInfo;
    std::function<void(int selector, int direction)> setNextBarberStyle;
    std::function<uint32_t()> getBarberTotalCost;
    std::function<void()> barberReset;
    /// The Okay button. blizzard_barbershopui.xml names ApplyBarberShopStyle
    /// as an OnClick handler attribute, which is why nothing noticed it was
    /// unbound — the readiness report reads script bodies, not attributes.
    std::function<void()> barberApply;

    /// Whether the camera is inside a WMO, for IsIndoors and IsOutdoors.
    ///
    /// The renderer has tracked this all along and the macro conditionals
    /// [indoors] and [outdoors] already read it; only the Lua bindings were
    /// stubbed, answering a flat false and a flat true. Two paths to the same
    /// question, and only one of them was ever improved.
    std::function<bool()> isPlayerIndoors;

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

    /// Save a screenshot, as the client's own binding and /screenshot do.
    ///
    /// Routed through the same call rather than reimplemented, so both put the
    /// file in the same place under the same name.
    std::function<void()> takeScreenshot;
};

} // namespace wowee::addons
