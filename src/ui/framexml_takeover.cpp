#include "ui/framexml_takeover.hpp"

#include "core/logger.hpp"

#include <array>
#include <cstdlib>
#include <set>
#include <string>

namespace wowee::ui {

namespace {

struct Entry { UiElement element; std::string_view name; };

// One row per element, and the only place a name is written down.
constexpr std::array<Entry, 14> kElements{{
    {UiElement::PlayerFrame,  "playerframe"},
    {UiElement::TargetFrame,  "targetframe"},
    {UiElement::PetFrame,     "petframe"},
    {UiElement::FocusFrame,   "focusframe"},
    {UiElement::ActionBar,    "actionbar"},
    {UiElement::StanceBar,    "stancebar"},
    {UiElement::BagBar,       "bagbar"},
    {UiElement::MicroMenu,    "micromenu"},
    {UiElement::XpBar,        "xpbar"},
    {UiElement::RepBar,       "repbar"},
    {UiElement::CastBar,      "castbar"},
    {UiElement::Minimap,      "minimap"},
    {UiElement::Chat,         "chat"},
    {UiElement::QuestTracker, "questtracker"},
}};

/// Parsed once. An unknown name is reported rather than dropped: a typo would
/// otherwise read as a replacement that quietly did not happen.
const std::set<std::string>& requested() {
    static const std::set<std::string> names = [] {
        std::set<std::string> out;
        const char* raw = std::getenv("WOWEE_FRAMEXML_UI");
        if (!raw || !*raw) return out;

        std::string value(raw);
        size_t start = 0;
        while (start <= value.size()) {
            const size_t comma = value.find(',', start);
            std::string one = value.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            const size_t b = one.find_first_not_of(" \t");
            const size_t e = one.find_last_not_of(" \t");
            one = (b == std::string::npos) ? std::string() : one.substr(b, e - b + 1);
            if (!one.empty()) out.insert(one);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        if (out.count("all") == 0) {
            for (const std::string& name : out) {
                bool known = false;
                for (const Entry& e : kElements) known |= (e.name == name);
                if (!known) LOG_WARNING("WOWEE_FRAMEXML_UI: no element called '", name, "'");
            }
        }
        if (!out.empty()) {
            std::string list;
            for (const std::string& n : out) { list += n; list += ' '; }
            LOG_WARNING("FrameXML is drawing these instead of the client: ", list);
        }
        return out;
    }();
    return names;
}

} // namespace

bool frameXmlOwns(UiElement element) {
    const auto& names = requested();
    if (names.empty()) return false;
    if (names.count("all")) return true;
    return names.count(std::string(uiElementName(element))) > 0;
}

std::string_view uiElementName(UiElement element) {
    for (const Entry& e : kElements) {
        if (e.element == element) return e.name;
    }
    return "";
}

} // namespace wowee::ui
