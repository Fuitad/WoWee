// world_map_facade.hpp — Public API for the world map system.
// Drop-in replacement for the monolithic WorldMap class (Phase 10 of refactoring plan).
// Facade pattern — hides internal complexity behind the same public interface.
#pragma once

#include "rendering/world_map/world_map_types.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>

namespace wowee {
namespace rendering {
class VkContext;
}
namespace pipeline { class AssetManager; }
namespace rendering {
namespace world_map {

class WorldMapFacade {
public:
    /// Backward-compatible alias for old WorldMap::QuestPoi usage.
    using QuestPoi = QuestPOI;

    WorldMapFacade();
    ~WorldMapFacade();

    bool initialize(VkContext* ctx, pipeline::AssetManager* am);
    void shutdown();

    /// Off-screen composite pass — call BEFORE the main render pass begins.
    void compositePass(VkCommandBuffer cmd);

    /// ImGui overlay — call INSIDE the main render pass (during ImGui frame).
    void render(const glm::vec3& playerRenderPos,
                int screenWidth, int screenHeight,
                float playerYawDeg = 0.0f);

    void setMapName(const std::string& name);
    void setServerExplorationMask(const std::vector<uint32_t>& masks, bool hasData);
    /// The zone the server says the player is in. Used in preference to working
    /// it out from map geometry, which can only guess between overlapping zones.
    void setPlayerZoneId(uint32_t zoneId);
    void setPartyDots(std::vector<PartyDot> dots);
    void setTaxiNodes(std::vector<TaxiNode> nodes);
    void setQuestPois(std::vector<QuestPOI> pois);
    void setCorpsePos(bool hasCorpse, glm::vec3 renderPos);
    /// The spirit healer a release would send the player to.
    void setGraveyardPos(bool hasGraveyard, glm::vec3 renderPos);

    /// Draw the map into this rect instead of a window of its own.
    ///
    /// In pixels from the top-left. The map is an ImGui window that centres
    /// and sizes itself, which is right when this client's interface owns it
    /// and wrong when FrameXML does: there the map belongs inside the frame
    /// FrameXML drew for it, and the title bar belongs to that frame rather
    /// than to a window underneath it. Unset, nothing changes.
    void setFrameRect(float x, float y, float w, float h);
    void clearFrameRect();
    /// Nearby rare/rare-elite creatures currently spawned near the player.
    void setRares(std::vector<RareMark> rares);
    /// Nearby chest-type game objects currently spawned near the player.
    void setChests(std::vector<ChestMark> chests);

    bool isOpen() const;
    void close();

    /// Flight-map (taxi selection) mode — opens the map locked to the player's
    /// continent with interactive flight nodes (see TaxiNodeLayer flight-map
    /// rendering). routeProvider maps a destination node id to the hop chain
    /// used to draw the route; onSelect fires with the chosen destination;
    /// onClose fires when the player dismisses the map (Escape / X).
    void openTaxiMap(std::function<std::vector<uint32_t>(uint32_t)> routeProvider,
                     std::function<void(uint32_t)> onSelect,
                     std::function<void()> onClose);
    /// Silent close (no onClose callback) — used when the game state already
    /// closed the flight master window.
    void closeTaxiMap();
    bool isTaxiMapOpen() const;

    // ── What the interface needs to draw the same map itself ─────────
    //
    // The client keeps all of this and drew it only from here. FrameXML's
    // world map asks for the same two things through GetMapOverlayInfo and
    // GetMapLandmarkInfo, and had no way to reach them — the facade's whole
    // public surface was setters.

    /// The overlays for whatever the map is showing, in draw order. Empty at
    /// continent and cosmic level, which have no per-zone art.
    std::vector<OverlayEntry> currentOverlays() const;

    /// One area POI, already projected into the [0,1] map space the interface
    /// places its pins in.
    struct Landmark {
        std::string name;
        std::string description;
        uint32_t iconType = 0;
        float x = 0.0f, y = 0.0f;
    };
    /// The POIs on the shown map. Only those belonging to it, and only those
    /// that project inside it.
    std::vector<Landmark> currentLandmarks() const;

    /// The area id the map is showing, or zero at continent and cosmic level.
    uint32_t currentAreaId() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee

// Backward-compatible alias for gradual migration
namespace wowee {
namespace rendering {
using WorldMap = world_map::WorldMapFacade;
} // namespace rendering
} // namespace wowee
