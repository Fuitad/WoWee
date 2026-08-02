#include "ui/unit_portrait.hpp"

#include "core/logger.hpp"
#include "game/game_handler.hpp"
#include "rendering/character_preview.hpp"
#include "rendering/renderer.hpp"

namespace wowee::ui {

namespace {

/// FNV-1a over what actually changes a character's look, so a reload happens
/// when the gear changes and not when a stat does.
size_t hashEquipment(const std::vector<game::EquipmentItem>& eq) {
    size_t h = 1469598103934665603ull;
    auto mix = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= static_cast<uint8_t>((v >> (i * 8)) & 0xFF);
            h *= 1099511628211ull;
        }
    };
    for (const auto& item : eq) {
        mix(item.displayModel);
        mix(item.inventoryType);
        mix(item.enchantment);
    }
    return h;
}

} // namespace

UnitPortrait::UnitPortrait() = default;

UnitPortrait::~UnitPortrait() = default;

void UnitPortrait::update(game::GameHandler& gameHandler,
                          pipeline::AssetManager* assets,
                          rendering::Renderer* renderer, float deltaTime) {
    if (!assets || !renderer) return;

    // The character list is where a player's appearance is described; the unit
    // in the world carries a display id, not the pieces it was built from.
    const game::Character* self = nullptr;
    for (const auto& c : gameHandler.getCharacters()) {
        if (c.guid == gameHandler.getPlayerGuid()) { self = &c; break; }
    }
    if (!self) return;

    if (!preview_) {
        preview_ = std::make_unique<rendering::CharacterPreview>();
        // Small: this is drawn into a circle a few dozen pixels across, and the
        // cost of the pass scales with the target.
        initialized_ = preview_->initialize(assets);
        if (!initialized_) {
            LOG_WARNING("UnitPortrait: could not build the offscreen view");
            preview_.reset();
            return;
        }
        renderer->registerPreview(preview_.get());
        registered_ = true;
    }

    const size_t equipHash = hashEquipment(self->equipment);
    const bool changed = (loadedGuid_ != self->guid) ||
                         (loadedAppearance_ != self->appearanceBytes) ||
                         (loadedFacialFeatures_ != self->facialFeatures) ||
                         (loadedEquipHash_ != equipHash);
    if (changed) {
        const uint8_t skin      =  self->appearanceBytes        & 0xFF;
        const uint8_t face      = (self->appearanceBytes >> 8)  & 0xFF;
        const uint8_t hairStyle = (self->appearanceBytes >> 16) & 0xFF;
        const uint8_t hairColor = (self->appearanceBytes >> 24) & 0xFF;

        if (preview_->loadCharacter(self->race, self->gender, skin, face,
                                    hairStyle, hairColor, self->facialFeatures,
                                    self->useFemaleModel)) {
            preview_->applyEquipment(self->equipment);
            preview_->setTransparentBackground(true);
            preview_->setPortraitFraming();
        }
        // Logged because a portrait that rebuilds every frame looks like one
        // that flickers, and the two are indistinguishable from outside.
        LOG_INFO("UnitPortrait: rebuilt for guid ", self->guid,
                 " appearance ", self->appearanceBytes);
        loadedGuid_ = self->guid;
        loadedAppearance_ = self->appearanceBytes;
        loadedFacialFeatures_ = self->facialFeatures;
        loadedEquipHash_ = equipHash;
    }

    preview_->update(deltaTime);
    preview_->render();
    preview_->requestComposite();
}

uint64_t UnitPortrait::textureId() const {
    if (!preview_) return 0;
    return reinterpret_cast<uint64_t>(preview_->getTextureId());
}

void UnitPortrait::shutdown(rendering::Renderer* renderer) {
    if (preview_ && registered_ && renderer) renderer->unregisterPreview(preview_.get());
    registered_ = false;
    preview_.reset();
    initialized_ = false;
}

} // namespace wowee::ui
