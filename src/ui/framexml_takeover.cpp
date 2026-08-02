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
                bool known = (name == "mainmenubar");
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

namespace {

/// Whether a name covers this element as part of something larger.
///
/// The client draws its action bar, bag bar, micro menu and the two thin bars
/// above them as separate pieces, because it built them separately. FrameXML
/// draws all of them as MainMenuBar: one frame, one strip of art, the griffins
/// at either end. Handing over "actionbar" alone therefore leaves the client's
/// bag bar and micro menu sitting on top of FrameXML's, in the same place, and
/// the result reads as one bar drawn twice rather than a replacement that half
/// worked.
bool coveredByGroup(const std::string& name, UiElement element) {
    if (name != "mainmenubar") return false;
    switch (element) {
        case UiElement::ActionBar:
        case UiElement::StanceBar:
        case UiElement::BagBar:
        case UiElement::MicroMenu:
        case UiElement::XpBar:
        case UiElement::RepBar:
            return true;
        default:
            return false;
    }
}

} // namespace

bool frameXmlOwns(UiElement element) {
    const auto& names = requested();
    if (names.empty()) return false;
    if (names.count("all")) return true;
    if (names.count(std::string(uiElementName(element)))) return true;
    for (const std::string& n : names) {
        if (coveredByGroup(n, element)) return true;
    }
    return false;
}

std::string_view uiElementName(UiElement element) {
    for (const Entry& e : kElements) {
        if (e.element == element) return e.name;
    }
    return "";
}

} // namespace wowee::ui
