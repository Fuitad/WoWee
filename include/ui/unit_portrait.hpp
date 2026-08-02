#pragma once

// A live head-and-shoulders view of a unit, for the interface's portraits.
//
// WoW's portraits are not pictures on disk: they are the character itself,
// rendered small. The same offscreen pass the character-select screen uses
// already frames a face when zoomed all the way in, so this is that pass kept
// running while in the world, at a size a portrait needs.
//
// One unit for now — the player. Target and party portraits want the same
// thing and differ only in whose appearance is loaded.

#include <cstdint>
#include <memory>

namespace wowee {
namespace game { class GameHandler; }
namespace pipeline { class AssetManager; }
namespace rendering { class CharacterPreview; class Renderer; }

namespace ui {

class UnitPortrait {
public:
    /// How much of the character to show. A portrait is the face in a circle;
    /// the paperdoll wants the whole figure in a tall rectangle. The offscreen
    /// pass is the same either way — only the framing differs, which is why
    /// this is one class and not two.
    enum class Framing { Face, FullBody };

    UnitPortrait();
    ~UnitPortrait();

    /// Builds the offscreen view on first use and keeps it in step with the
    /// player's appearance and equipment afterwards. Safe to call every frame;
    /// it reloads the model only when something about it actually changed.
    void update(game::GameHandler& gameHandler, pipeline::AssetManager* assets,
                rendering::Renderer* renderer, float deltaTime);

    /// Set before the first update, since framing is applied when the model
    /// loads and the model loads once.
    void setFraming(Framing framing) { framing_ = framing; }

    /// The rendered portrait, or zero until the first composite has run. The
    /// value is a VkDescriptorSet, carried as an integer so this header does
    /// not drag Vulkan into the widget tree.
    uint64_t textureId() const;

    void shutdown(rendering::Renderer* renderer);

private:
    std::unique_ptr<rendering::CharacterPreview> preview_;
    bool initialized_ = false;
    Framing framing_ = Framing::Face;
    bool registered_ = false;

    // What the loaded model was built from, so a reload happens only on a real
    // change rather than every frame.
    uint64_t loadedGuid_ = 0;
    uint32_t loadedAppearance_ = 0;
    uint8_t  loadedFacialFeatures_ = 0;
    size_t   loadedEquipHash_ = 0;
};

} // namespace ui
} // namespace wowee
