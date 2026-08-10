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
#include <string>

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

/// A setting's value as a string, the way a CVar carries one.
///
/// A whole number with no decimal point, a fraction without the trailing zeros.
/// std::to_string gives six decimals for everything, and the options panels
/// compare some of these as strings — a checkbox tests `value == "1"`, which
/// "1.000000" fails, and the box unticks itself every time the panel opens.
///
/// Written twice before this, once on each side of the bridge, ninety minutes
/// apart.
inline std::string settingNumberText(double v) {
    if (v == static_cast<long long>(v)) return std::to_string(static_cast<long long>(v));
    std::string s = std::to_string(v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

/// Whether a setting string means on. Empty and "0" are the only falses — a
/// CVar arrives as a string, and in Lua every string including "0" is true, so
/// the test cannot be left to the caller.
inline bool settingIsOn(const std::string& v) { return !v.empty() && v != "0"; }

/// Every client setting FrameXML has no control of its own for.
///
/// The categories match the settings window's tabs so the two read the same way
/// round.
const SettingDesc* clientSettingsSchema(std::size_t& count);

}  // namespace ui
}  // namespace wowee
