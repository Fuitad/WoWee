#pragma once

/**
 * settings_schema.hpp — the client's settings, described once.
 *
 * The settings window draws these as ImGui controls. FrameXML's Interface
 * Options needs the same list to build its own panel out of, and an addon that
 * asks what this client can be told needs it too. Three readers, one list.
 *
 * Only the settings with no Blizzard equivalent belong here. The ones a
 * Blizzard panel already has a control for — view distance, mouse speed, the
 * sound volumes — are bound to their CVar instead, in kClientCVars, so that
 * FrameXML's own control drives them rather than a second one appearing beside
 * it.
 */

#include <cstddef>

namespace wowee {
namespace ui {

/// What kind of control a setting wants.
enum class SettingKind {
    Bool,   ///< a checkbox
    Int,    ///< a slider over whole numbers
    Float,  ///< a slider over a range
};

/// One setting, as something outside this client can understand it.
struct SettingDesc {
    const char* key;       ///< what get/set answer to; stable, not shown
    const char* label;     ///< what a person reads
    SettingKind kind;
    float minValue;        ///< ignored for Bool
    float maxValue;
    float step;
    const char* category;  ///< which panel it belongs on
};

/// Every client setting FrameXML has no control of its own for.
///
/// The categories match the settings window's tabs so the two read the same way
/// round.
const SettingDesc* clientSettingsSchema(std::size_t& count);

}  // namespace ui
}  // namespace wowee
