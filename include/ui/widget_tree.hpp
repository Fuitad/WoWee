#pragma once

// A retained widget tree with WoW's anchor layout.
//
// This is the thing the addon API was missing. CreateFrame answered, events
// dispatched, and CreateTexture handed back a table whose every method was a
// no-op — so an addon could compute and react but could not put a pixel on the
// screen. The API looked supported and nothing drew.
//
// The same tree is what FrameXML targets, because FrameXML is only Lua and XML
// over a widget system. Building it once serves both: addons that draw, and a
// route to running the original interface rather than imitating it.
//
// Deliberately free of Vulkan and ImGui so the layout rules can be tested
// without a device. Rendering lives in widget_renderer.
//
// Coordinates follow WoW, not the screen: the origin is the BOTTOM-left and y
// grows upward. Converting at the point of drawing keeps every anchor rule here
// readable against Blizzard's own documentation, rather than mirrored.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace wowee {
namespace ui {

/// Where within a rect a point sits. Fractions of width and height, with y
/// measured from the bottom: BOTTOMLEFT is (0,0) and TOPRIGHT is (1,1).
struct AnchorPoint {
    float fx = 0.0f;
    float fy = 0.0f;
};

/// Resolve a WoW point name. Unknown names resolve to CENTER, which is what an
/// unanchored frame falls back to anyway.
AnchorPoint resolveAnchorPoint(const std::string& name);

enum class WidgetKind : uint8_t { Frame, Texture, FontString };

/// Blizzard's five layers within a frame, drawn in this order.
enum class DrawLayer : uint8_t { Background, Border, Artwork, Overlay, Highlight };
DrawLayer parseDrawLayer(const std::string& name);

/// Frame strata, drawn in this order. Everything in a higher stratum draws over
/// everything in a lower one regardless of level.
enum class FrameStrata : uint8_t {
    World, Background, Low, Medium, High, Dialog,
    Fullscreen, FullscreenDialog, Tooltip
};
FrameStrata parseStrata(const std::string& name);

struct Anchor {
    std::string point = "CENTER";
    uint32_t relativeTo = 0;      ///< Widget id; 0 means "my parent".
    std::string relativePoint = "CENTER";
    float x = 0.0f;
    float y = 0.0f;
};

struct Widget {
    uint32_t id = 0;
    WidgetKind kind = WidgetKind::Frame;
    uint32_t parent = 0;
    std::vector<uint32_t> children;
    std::string name;
    uint32_t creationOrder = 0;

    std::vector<Anchor> anchors;
    float width = 0.0f;
    float height = 0.0f;
    bool shown = true;
    float alpha = 1.0f;
    /// Whether this frame takes the mouse. False by default, as in WoW, where a
    /// plain Frame is transparent to clicks until EnableMouse is called; Buttons
    /// switch it on for themselves.
    bool mouseEnabled = false;

    FrameStrata strata = FrameStrata::Medium;
    bool strataExplicit = false;
    int level = 0;
    bool levelExplicit = false;
    DrawLayer layer = DrawLayer::Artwork;
    int subLevel = 0;

    // Texture regions.
    std::string texturePath;
    float texCoord[4] = {0.0f, 1.0f, 0.0f, 1.0f};   ///< left, right, top, bottom
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool solidColor = false;    ///< SetTexture(r,g,b[,a]) rather than a file.

    // Backdrop, the bordered panel look most of the original interface is
    // built from. The edge file is a strip of eight square tiles — verified
    // against the art: UI-Tooltip-Border is 128x16 and UI-DialogBox-Border
    // 256x32, both exactly eight tiles wide.
    bool hasBackdrop = false;
    std::string bgFile;
    std::string edgeFile;
    bool  tileBackground = false;
    float edgeSize = 16.0f;
    float insetLeft = 0.0f, insetRight = 0.0f, insetTop = 0.0f, insetBottom = 0.0f;
    float backdropColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    // StatusBar. Health, mana, cast bars and experience are all this one type.
    bool  isStatusBar = false;
    float barMin = 0.0f, barMax = 1.0f, barValue = 0.0f;
    std::string barTexture;
    float barColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool  barVertical = false;

    /// Fraction filled, clamped. A zero or inverted range reads as empty rather
    /// than dividing by nothing.
    float barFraction() const {
        const float span = barMax - barMin;
        if (span <= 0.0f) return 0.0f;
        const float f = (barValue - barMin) / span;
        return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }

    // FontString regions.
    std::string text;
    float fontHeight = 12.0f;
    /// Extra space between wrapped lines, which FrameXML reads back.
    float lineSpacing = 0.0f;
    std::string justifyH = "CENTER";

    // Filled in by layout(). Screen rect in WoW coordinates: origin bottom-left.
    float left = 0.0f, bottom = 0.0f, rectW = 0.0f, rectH = 0.0f;
    bool  visible = false;      ///< shown, and every ancestor shown too
    FrameStrata effStrata = FrameStrata::Medium;
    int   effLevel = 0;
};

class WidgetTree {
public:
    WidgetTree();

    /// The screen-sized root every unparented widget hangs from. WoW calls it
    /// UIParent and addons anchor to it by name constantly.
    uint32_t root() const { return rootId_; }

    uint32_t create(WidgetKind kind, uint32_t parent, const std::string& name);
    Widget* get(uint32_t id);
    const Widget* get(uint32_t id) const;
    size_t size() const { return widgets_.size(); }

    /// Anchor helpers. clearPoints is SetPoint's implicit reset when a frame is
    /// re-anchored from scratch, and what SetAllPoints does before pinning both
    /// corners.
    void clearPoints(uint32_t id);
    void addPoint(uint32_t id, const Anchor& anchor);
    void setAllPoints(uint32_t id, uint32_t relativeTo);

    /// Resolve every widget's rect and visibility for a screen of this size.
    void layout(float screenW, float screenH);

    /// The frame under a point, or 0. Topmost wins, by the same ordering that
    /// decides what draws over what — so whatever the player can see on top is
    /// what they click. Regions are never hit: in WoW a texture is not a mouse
    /// target, its frame is.
    uint32_t hitTest(float x, float y) const;

    /// Widgets to draw, in the order to draw them. Only those that resolved to a
    /// visible, non-empty rect. Valid until the next layout().
    const std::vector<const Widget*>& drawOrder() const { return drawOrder_; }

private:
    void layoutWidget(uint32_t id, float screenW, float screenH);
    void collectDrawOrder();

    /// A deque, not a vector, because get() hands out a pointer into this and
    /// create() grows it. A vector reallocates, and any pointer taken before a
    /// create would dangle after one — a use-after-free waiting on the first
    /// caller that holds a Widget* across creating a child. A deque keeps
    /// references valid when it grows, which is the guarantee this needs.
    std::deque<Widget> widgets_;   ///< Index 0 is a placeholder; id == index.
    uint32_t rootId_ = 0;
    uint32_t nextOrder_ = 1;
    std::vector<const Widget*> drawOrder_;
};

} // namespace ui
} // namespace wowee
