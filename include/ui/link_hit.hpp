#pragma once

// The one place the interface's coordinate space is converted to and from.
//
// Two passes meet over a hyperlink and they do not agree about y. The draw
// pass works in screen pixels with y growing down, because that is what ImGui
// hands it. The input pass works in interface units with y growing up, because
// that is what the widget tree holds and what hitTest compares against — its
// own comment says so: "top is the upper edge, and y grows upward here".
//
// A link rect filed in the first space and tested in the second misses by the
// interface scale and by the whole height of the screen, and nothing says so:
// both sides compile, both run, and every click quietly lands on nothing. That
// is what happened, and it is why both halves of the conversion live here
// rather than one at each end where they can drift apart.
//
// Testable because it is arithmetic. The invariant worth pinning is not either
// conversion on its own but the round trip: a link drawn at a screen position
// must be found by a click at that same position on screen.

#include "ui/widget_tree.hpp"

namespace wowee {
namespace ui {

/// A mouse position in window pixels, as the interface tree sees it.
///
/// ImGui measures from the top-left and the tree from the bottom-left, so the
/// height comes in to flip it; the scale is the interface scale the tree is
/// laid out at.
inline void mouseToTreeSpace(float& x, float& y, float screenH, float scale) {
    y = screenH - y;
    if (scale > 0.0f) { x /= scale; y /= scale; }
}

/// A run of link text drawn at (x, y) in screen pixels, as a rect in tree
/// space. `y` is the top of the line, as ImGui draws it, and `lineH` its
/// height — so the *bottom* in tree space comes off y + lineH.
inline LinkRect linkRectFromDraw(uint32_t owner, const std::string& link,
                                 const std::string& text,
                                 float x, float y, float runW, float lineH,
                                 float screenH, float scale) {
    const float s = (scale > 0.0f) ? scale : 1.0f;
    LinkRect r;
    r.widget = owner;
    r.link = link;
    r.text = text;
    r.x0 = x / s;
    r.x1 = (x + runW) / s;
    r.y0 = (screenH - (y + lineH)) / s;
    r.y1 = (screenH - y) / s;
    return r;
}

}  // namespace ui
}  // namespace wowee
