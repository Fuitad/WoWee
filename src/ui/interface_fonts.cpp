#include "ui/interface_fonts.hpp"

#include <imgui.h>
#include <cfloat>

#include <algorithm>
#include <unordered_map>

namespace wowee::ui {

namespace {

/// The lower-case stem of a path, with any directory and extension removed.
/// "Fonts\\MORPHEUS.ttf" and "morpheus.ttf" both come out as "morpheus", which
/// is what lets a font object written one way find a file named the other.
std::string faceKey(const std::string& pathOrName) {
    const size_t slash = pathOrName.find_last_of("\\/");
    std::string stem = (slash == std::string::npos) ? pathOrName
                                                    : pathOrName.substr(slash + 1);
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return stem;
}

std::unordered_map<std::string, ImFont*>& faces() {
    static std::unordered_map<std::string, ImFont*> map;
    return map;
}

} // namespace

void registerInterfaceFace(const std::string& pathOrName, ImFont* font) {
    if (!font) return;
    faces()[faceKey(pathOrName)] = font;
}

ImFont* interfaceFace(const std::string& pathOrName) {
    if (pathOrName.empty()) return nullptr;
    const auto it = faces().find(faceKey(pathOrName));
    return (it == faces().end()) ? nullptr : it->second;
}

ImFont* interfaceFaceOrDefault(const std::string& fontFace) {
    // The order the renderer uses. The interface's own default rather than this
    // client's, because ImGui draws with whatever face was added first and that
    // is deliberately the built-in one.
    if (ImFont* f = interfaceFace(fontFace)) return f;
    if (ImFont* f = interfaceFace("frizqt__")) return f;
    return (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetFont() : nullptr;
}

float interfaceFontSize(float fontHeight) {
    if (fontHeight > 0.0f) return fontHeight;
    // What the renderer falls back to: the current size, not a flat twelve.
    return (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetFontSize() : 12.0f;
}

float interfaceTextWidth(const std::string& text, const std::string& fontFace,
                         float fontHeight) {
    if (text.empty()) return 0.0f;
    const float size = interfaceFontSize(fontHeight);
    if (ImFont* font = interfaceFaceOrDefault(fontFace)) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
    }
    // No context yet — during the FrameXML load there may be no frame in
    // flight to ask. An estimate is far better than nothing: the alternative
    // is answering zero, and MoneyFrame does SetWidth(GetTextWidth() +
    // iconWidth), which then places three buttons on top of each other.
    return static_cast<float>(text.size()) * size * 0.5f;
}

} // namespace wowee::ui
