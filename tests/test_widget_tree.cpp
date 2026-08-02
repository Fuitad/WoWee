#include <catch_amalgamated.hpp>

#include "ui/widget_tree.hpp"

#include <string>

using namespace wowee::ui;

// The anchor solver is what every frame's position comes out of, and it is the
// part of a widget system that is wrong in ways nothing reports: a frame lands
// somewhere plausible and only looks wrong against the art it was meant to sit
// on. Pin the rules directly.
//
// Coordinates are WoW's: origin bottom-left, y upward.

namespace {
constexpr float kScreenW = 1024.0f;
constexpr float kScreenH = 768.0f;
}

TEST_CASE("Anchor point names resolve to rect fractions", "[widget][anchor]") {
    auto p = [](const char* n) { return resolveAnchorPoint(n); };

    REQUIRE(p("BOTTOMLEFT").fx == Catch::Approx(0.0f));
    REQUIRE(p("BOTTOMLEFT").fy == Catch::Approx(0.0f));
    REQUIRE(p("TOPRIGHT").fx == Catch::Approx(1.0f));
    REQUIRE(p("TOPRIGHT").fy == Catch::Approx(1.0f));
    REQUIRE(p("CENTER").fx == Catch::Approx(0.5f));
    REQUIRE(p("CENTER").fy == Catch::Approx(0.5f));

    // The combined names carry both halves; TOP must not be read as "not
    // bottom, therefore centred".
    REQUIRE(p("TOP").fx == Catch::Approx(0.5f));
    REQUIRE(p("TOP").fy == Catch::Approx(1.0f));
    REQUIRE(p("LEFT").fx == Catch::Approx(0.0f));
    REQUIRE(p("LEFT").fy == Catch::Approx(0.5f));

    REQUIRE(p("topleft").fx == Catch::Approx(0.0f));   // case-insensitive
    REQUIRE(p("NONSENSE").fx == Catch::Approx(0.5f));  // unknown falls to CENTER
}

TEST_CASE("UIParent fills the screen", "[widget][layout]") {
    WidgetTree tree;
    tree.layout(kScreenW, kScreenH);
    const Widget* root = tree.get(tree.root());
    REQUIRE(root != nullptr);
    REQUIRE(root->left == Catch::Approx(0.0f));
    REQUIRE(root->bottom == Catch::Approx(0.0f));
    REQUIRE(root->rectW == Catch::Approx(kScreenW));
    REQUIRE(root->rectH == Catch::Approx(kScreenH));
}

TEST_CASE("One anchor plus a size positions the frame", "[widget][layout]") {
    WidgetTree tree;
    const uint32_t f = tree.create(WidgetKind::Frame, 0, "F");
    Widget* w = tree.get(f);
    w->width = 100.0f;
    w->height = 50.0f;

    Anchor a;
    a.point = "BOTTOMLEFT";
    a.relativePoint = "BOTTOMLEFT";
    a.x = 10.0f;
    a.y = 20.0f;
    tree.addPoint(f, a);

    tree.layout(kScreenW, kScreenH);
    REQUIRE(w->left == Catch::Approx(10.0f));
    REQUIRE(w->bottom == Catch::Approx(20.0f));
    REQUIRE(w->rectW == Catch::Approx(100.0f));
    REQUIRE(w->rectH == Catch::Approx(50.0f));
}

TEST_CASE("Anchoring by CENTER offsets from the middle of the parent",
          "[widget][layout]") {
    WidgetTree tree;
    const uint32_t f = tree.create(WidgetKind::Frame, 0, "F");
    Widget* w = tree.get(f);
    w->width = 200.0f;
    w->height = 100.0f;
    Anchor a;   // defaults are CENTER to CENTER
    tree.addPoint(f, a);

    tree.layout(kScreenW, kScreenH);
    // Its centre lands on the screen centre, so its corner is half its size away.
    REQUIRE(w->left == Catch::Approx(kScreenW * 0.5f - 100.0f));
    REQUIRE(w->bottom == Catch::Approx(kScreenH * 0.5f - 50.0f));
}

TEST_CASE("Two opposing anchors derive the size", "[widget][layout]") {
    // This is what SetAllPoints relies on, and what most of FrameXML's
    // backgrounds and borders use instead of ever stating a size.
    WidgetTree tree;
    const uint32_t f = tree.create(WidgetKind::Frame, 0, "F");
    Widget* w = tree.get(f);
    w->width = 1.0f;    // deliberately wrong; the anchors must win
    w->height = 1.0f;

    Anchor tl; tl.point = "TOPLEFT";     tl.relativePoint = "TOPLEFT";     tl.x =  40.0f; tl.y = -30.0f;
    Anchor br; br.point = "BOTTOMRIGHT"; br.relativePoint = "BOTTOMRIGHT"; br.x = -60.0f; br.y =  50.0f;
    tree.addPoint(f, tl);
    tree.addPoint(f, br);

    tree.layout(kScreenW, kScreenH);
    REQUIRE(w->left == Catch::Approx(40.0f));
    REQUIRE(w->bottom == Catch::Approx(50.0f));
    REQUIRE(w->rectW == Catch::Approx(kScreenW - 40.0f - 60.0f));
    REQUIRE(w->rectH == Catch::Approx(kScreenH - 30.0f - 50.0f));
}

TEST_CASE("SetAllPoints matches the target exactly", "[widget][layout]") {
    WidgetTree tree;
    const uint32_t parent = tree.create(WidgetKind::Frame, 0, "P");
    Widget* p = tree.get(parent);
    p->width = 300.0f;
    p->height = 200.0f;
    Anchor pa; pa.point = "BOTTOMLEFT"; pa.relativePoint = "BOTTOMLEFT"; pa.x = 12.0f; pa.y = 34.0f;
    tree.addPoint(parent, pa);

    const uint32_t tex = tree.create(WidgetKind::Texture, parent, "");
    tree.setAllPoints(tex, parent);

    tree.layout(kScreenW, kScreenH);
    const Widget* t = tree.get(tex);
    REQUIRE(t->left == Catch::Approx(p->left));
    REQUIRE(t->bottom == Catch::Approx(p->bottom));
    REQUIRE(t->rectW == Catch::Approx(p->rectW));
    REQUIRE(t->rectH == Catch::Approx(p->rectH));
}

TEST_CASE("A frame can anchor to a sibling, not just its parent", "[widget][layout]") {
    WidgetTree tree;
    const uint32_t a = tree.create(WidgetKind::Frame, 0, "A");
    tree.get(a)->width = 50.0f;
    tree.get(a)->height = 50.0f;
    Anchor aa; aa.point = "BOTTOMLEFT"; aa.relativePoint = "BOTTOMLEFT"; aa.x = 100.0f; aa.y = 100.0f;
    tree.addPoint(a, aa);

    const uint32_t b = tree.create(WidgetKind::Frame, 0, "B");
    tree.get(b)->width = 50.0f;
    tree.get(b)->height = 50.0f;
    Anchor ba;
    ba.point = "LEFT";
    ba.relativeTo = a;
    ba.relativePoint = "RIGHT";
    ba.x = 8.0f;
    tree.addPoint(b, ba);

    tree.layout(kScreenW, kScreenH);
    // Sits just right of A, vertically centred on it.
    REQUIRE(tree.get(b)->left == Catch::Approx(158.0f));
    REQUIRE(tree.get(b)->bottom == Catch::Approx(100.0f));
}

TEST_CASE("Hiding a frame hides everything under it", "[widget][layout]") {
    WidgetTree tree;
    const uint32_t parent = tree.create(WidgetKind::Frame, 0, "P");
    tree.get(parent)->width = 100.0f;
    tree.get(parent)->height = 100.0f;
    tree.addPoint(parent, Anchor{});

    const uint32_t tex = tree.create(WidgetKind::Texture, parent, "");
    tree.get(tex)->texturePath = "Interface\\Buttons\\Button-Backpack-Up.blp";
    tree.setAllPoints(tex, parent);

    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().size() == 1);

    tree.get(parent)->shown = false;
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().empty());
}

TEST_CASE("Draw order runs strata, then level, then layer", "[widget][draworder]") {
    WidgetTree tree;
    auto addTexture = [&](uint32_t parent, DrawLayer layer, const char* name) {
        const uint32_t t = tree.create(WidgetKind::Texture, parent, name);
        Widget* w = tree.get(t);
        w->texturePath = "x.blp";
        w->layer = layer;
        tree.setAllPoints(t, parent);
        return t;
    };

    const uint32_t lowFrame = tree.create(WidgetKind::Frame, 0, "Low");
    tree.get(lowFrame)->strata = FrameStrata::Low;
    tree.get(lowFrame)->strataExplicit = true;
    tree.get(lowFrame)->width = 10.0f;
    tree.get(lowFrame)->height = 10.0f;
    tree.addPoint(lowFrame, Anchor{});

    const uint32_t highFrame = tree.create(WidgetKind::Frame, 0, "High");
    tree.get(highFrame)->strata = FrameStrata::High;
    tree.get(highFrame)->strataExplicit = true;
    tree.get(highFrame)->width = 10.0f;
    tree.get(highFrame)->height = 10.0f;
    tree.addPoint(highFrame, Anchor{});

    // Deliberately created in the wrong order: an OVERLAY in a low stratum must
    // still fall behind a BACKGROUND in a high one.
    const uint32_t highBackground = addTexture(highFrame, DrawLayer::Background, "highBg");
    const uint32_t lowOverlay     = addTexture(lowFrame,  DrawLayer::Overlay,    "lowOver");
    const uint32_t lowBackground  = addTexture(lowFrame,  DrawLayer::Background, "lowBg");

    tree.layout(kScreenW, kScreenH);
    const auto& order = tree.drawOrder();
    REQUIRE(order.size() == 3);

    auto positionOf = [&](uint32_t id) {
        for (size_t i = 0; i < order.size(); ++i) if (order[i]->id == id) return i;
        return order.size();
    };
    REQUIRE(positionOf(lowBackground) < positionOf(lowOverlay));
    REQUIRE(positionOf(lowOverlay) < positionOf(highBackground));
}

TEST_CASE("A child frame draws over its parent", "[widget][draworder]") {
    WidgetTree tree;
    const uint32_t parent = tree.create(WidgetKind::Frame, 0, "P");
    tree.get(parent)->width = 100.0f;
    tree.get(parent)->height = 100.0f;
    tree.addPoint(parent, Anchor{});
    const uint32_t parentArt = tree.create(WidgetKind::Texture, parent, "pa");
    tree.get(parentArt)->texturePath = "p.blp";
    tree.setAllPoints(parentArt, parent);

    const uint32_t child = tree.create(WidgetKind::Frame, parent, "C");
    tree.setAllPoints(child, parent);
    const uint32_t childArt = tree.create(WidgetKind::Texture, child, "ca");
    tree.get(childArt)->texturePath = "c.blp";
    tree.setAllPoints(childArt, child);

    tree.layout(kScreenW, kScreenH);
    const auto& order = tree.drawOrder();
    REQUIRE(order.size() == 2);
    REQUIRE(order[0]->id == parentArt);
    REQUIRE(order[1]->id == childArt);
}

TEST_CASE("Nothing to draw is not drawn", "[widget][draworder]") {
    WidgetTree tree;
    const uint32_t parent = tree.create(WidgetKind::Frame, 0, "P");
    tree.get(parent)->width = 100.0f;
    tree.get(parent)->height = 100.0f;
    tree.addPoint(parent, Anchor{});

    // A frame is a container and paints nothing itself.
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().empty());

    // A texture with no source, and a font string with no text, likewise.
    const uint32_t empty = tree.create(WidgetKind::Texture, parent, "");
    tree.setAllPoints(empty, parent);
    const uint32_t blank = tree.create(WidgetKind::FontString, parent, "");
    tree.setAllPoints(blank, parent);
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().empty());

    // A solid colour counts as a source even with no file behind it.
    tree.get(empty)->solidColor = true;
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().size() == 1);
}

TEST_CASE("A zero-sized region is skipped rather than drawn degenerate",
          "[widget][draworder]") {
    WidgetTree tree;
    const uint32_t parent = tree.create(WidgetKind::Frame, 0, "P");
    tree.get(parent)->width = 100.0f;
    tree.get(parent)->height = 100.0f;
    tree.addPoint(parent, Anchor{});

    const uint32_t tex = tree.create(WidgetKind::Texture, parent, "");
    tree.get(tex)->texturePath = "x.blp";
    tree.addPoint(tex, Anchor{});   // anchored, but never given a size

    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().empty());
}

TEST_CASE("Two anchors at the same point do not blow the rect apart",
          "[widget][layout]") {
    // Anchoring a frame twice at the same point is redundant rather than a size
    // constraint. Solving it as one would divide by a near-zero spread and throw
    // the rect off the screen.
    WidgetTree tree;
    const uint32_t f = tree.create(WidgetKind::Frame, 0, "F");
    tree.get(f)->width = 80.0f;
    tree.get(f)->height = 40.0f;

    Anchor a; a.point = "CENTER"; a.relativePoint = "CENTER";
    Anchor b; b.point = "CENTER"; b.relativePoint = "CENTER"; b.x = 5.0f;
    tree.addPoint(f, a);
    tree.addPoint(f, b);

    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.get(f)->rectW == Catch::Approx(80.0f));
    REQUIRE(tree.get(f)->rectH == Catch::Approx(40.0f));
    REQUIRE(std::abs(tree.get(f)->left) < kScreenW);
}

// ── Hit testing ─────────────────────────────────────────────────────────────

namespace {
uint32_t makeButton(WidgetTree& tree, float x, float y, float w, float h,
                    FrameStrata strata = FrameStrata::Medium) {
    const uint32_t id = tree.create(WidgetKind::Frame, 0, "");
    Widget* f = tree.get(id);
    f->width = w;
    f->height = h;
    f->mouseEnabled = true;
    f->strata = strata;
    f->strataExplicit = true;
    Anchor a; a.point = "BOTTOMLEFT"; a.relativePoint = "BOTTOMLEFT"; a.x = x; a.y = y;
    tree.addPoint(id, a);
    return id;
}
}

TEST_CASE("A frame is only hit inside its rect", "[widget][hittest]") {
    WidgetTree tree;
    const uint32_t b = makeButton(tree, 100.0f, 100.0f, 50.0f, 40.0f);
    tree.layout(kScreenW, kScreenH);

    REQUIRE(tree.hitTest(125.0f, 120.0f) == b);   // middle
    REQUIRE(tree.hitTest(100.0f, 100.0f) == b);   // corner counts
    REQUIRE(tree.hitTest(99.0f, 120.0f) == 0);    // just left
    REQUIRE(tree.hitTest(125.0f, 141.0f) == 0);   // just above
}

TEST_CASE("A frame without the mouse enabled is transparent to clicks",
          "[widget][hittest]") {
    // WoW's default, and the reason a plain Frame used as a container does not
    // steal clicks from whatever is underneath it.
    WidgetTree tree;
    const uint32_t f = makeButton(tree, 0.0f, 0.0f, 100.0f, 100.0f);
    tree.get(f)->mouseEnabled = false;
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.hitTest(50.0f, 50.0f) == 0);
}

TEST_CASE("A hidden frame cannot be clicked", "[widget][hittest]") {
    WidgetTree tree;
    const uint32_t f = makeButton(tree, 0.0f, 0.0f, 100.0f, 100.0f);
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.hitTest(50.0f, 50.0f) == f);

    tree.get(f)->shown = false;
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.hitTest(50.0f, 50.0f) == 0);
}

TEST_CASE("The frame drawn on top is the frame that gets the click",
          "[widget][hittest]") {
    // The whole point: what the player can see is what they hit. Overlapping
    // frames must resolve the same way the draw order does, or a click lands on
    // something buried.
    WidgetTree tree;
    const uint32_t low  = makeButton(tree, 0.0f, 0.0f, 100.0f, 100.0f, FrameStrata::Low);
    const uint32_t high = makeButton(tree, 0.0f, 0.0f, 100.0f, 100.0f, FrameStrata::High);
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.hitTest(50.0f, 50.0f) == high);

    // With strata equal, the later frame is on top and takes it.
    tree.get(high)->strata = FrameStrata::Low;
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.hitTest(50.0f, 50.0f) == high);
    REQUIRE(low != high);
}

TEST_CASE("A child frame takes the click from its parent", "[widget][hittest]") {
    WidgetTree tree;
    const uint32_t parent = makeButton(tree, 0.0f, 0.0f, 200.0f, 200.0f);
    const uint32_t child = tree.create(WidgetKind::Frame, parent, "");
    tree.get(child)->width = 50.0f;
    tree.get(child)->height = 50.0f;
    tree.get(child)->mouseEnabled = true;
    Anchor a; a.point = "BOTTOMLEFT"; a.relativePoint = "BOTTOMLEFT"; a.x = 10.0f; a.y = 10.0f;
    tree.addPoint(child, a);

    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.hitTest(30.0f, 30.0f) == child);    // over the child
    REQUIRE(tree.hitTest(150.0f, 150.0f) == parent); // parent elsewhere
}

TEST_CASE("A zero-sized frame is never hit", "[widget][hittest]") {
    WidgetTree tree;
    const uint32_t f = tree.create(WidgetKind::Frame, 0, "");
    tree.get(f)->mouseEnabled = true;
    tree.addPoint(f, Anchor{});   // anchored but never sized
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.hitTest(kScreenW * 0.5f, kScreenH * 0.5f) == 0);
}

// ── Backdrop and status bar geometry ────────────────────────────────────────

TEST_CASE("A frame with a backdrop draws; a bare frame does not",
          "[widget][backdrop]") {
    WidgetTree tree;
    const uint32_t f = tree.create(WidgetKind::Frame, 0, "F");
    tree.get(f)->width = 100.0f;
    tree.get(f)->height = 60.0f;
    tree.addPoint(f, Anchor{});

    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().empty());          // a container paints nothing

    tree.get(f)->hasBackdrop = true;
    tree.get(f)->bgFile = "Interface\\Tooltips\\UI-Tooltip-Background";
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().size() == 1);
    REQUIRE(tree.drawOrder()[0]->id == f);
}

TEST_CASE("A frame's backdrop draws beneath its own regions",
          "[widget][backdrop][draworder]") {
    // The backdrop is the panel; anything the frame owns belongs on top of it.
    WidgetTree tree;
    const uint32_t f = tree.create(WidgetKind::Frame, 0, "F");
    tree.get(f)->width = 100.0f;
    tree.get(f)->height = 60.0f;
    tree.get(f)->hasBackdrop = true;
    tree.addPoint(f, Anchor{});

    const uint32_t art = tree.create(WidgetKind::Texture, f, "");
    tree.get(art)->texturePath = "x.blp";
    tree.setAllPoints(art, f);

    tree.layout(kScreenW, kScreenH);
    const auto& order = tree.drawOrder();
    REQUIRE(order.size() == 2);
    REQUIRE(order[0]->id == f);
    REQUIRE(order[1]->id == art);
}

TEST_CASE("Status bar fill is clamped and survives a degenerate range",
          "[widget][statusbar]") {
    WidgetTree tree;
    const uint32_t b = tree.create(WidgetKind::Frame, 0, "B");
    Widget* w = tree.get(b);
    w->isStatusBar = true;
    w->barMin = 0.0f;
    w->barMax = 100.0f;

    w->barValue = 50.0f;
    REQUIRE(w->barFraction() == Catch::Approx(0.5f));
    w->barValue = 0.0f;
    REQUIRE(w->barFraction() == Catch::Approx(0.0f));
    w->barValue = 100.0f;
    REQUIRE(w->barFraction() == Catch::Approx(1.0f));

    // Out of range clamps rather than overflowing the bar.
    w->barValue = 250.0f;
    REQUIRE(w->barFraction() == Catch::Approx(1.0f));
    w->barValue = -10.0f;
    REQUIRE(w->barFraction() == Catch::Approx(0.0f));

    // A bar whose range was never set, or set backwards, reads empty instead of
    // dividing by nothing — health frames are created before their values are
    // known and would otherwise flash full or NaN on the first frame.
    w->barMin = 0.0f; w->barMax = 0.0f; w->barValue = 5.0f;
    REQUIRE(w->barFraction() == Catch::Approx(0.0f));
    w->barMin = 100.0f; w->barMax = 0.0f;
    REQUIRE(w->barFraction() == Catch::Approx(0.0f));
}

TEST_CASE("A status bar with no texture and no backdrop is not drawn",
          "[widget][statusbar]") {
    WidgetTree tree;
    const uint32_t b = tree.create(WidgetKind::Frame, 0, "B");
    tree.get(b)->isStatusBar = true;
    tree.get(b)->width = 80.0f;
    tree.get(b)->height = 10.0f;
    tree.addPoint(b, Anchor{});
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().empty());

    tree.get(b)->barTexture = "bar.blp";
    tree.layout(kScreenW, kScreenH);
    REQUIRE(tree.drawOrder().size() == 1);
}

TEST_CASE("The interface is laid out in units, whatever the display is",
          "[widget][layout]") {
    // FrameXML is authored against a virtual screen 768 units tall, so a frame
    // of a given size looks the same on every display. The tree works in those
    // units and the renderer multiplies once. Getting this wrong is not a
    // rounding error: treating units as pixels drew the whole interface at
    // half size on a 1528-tall window.
    //
    // Only the height sets the scale. The width follows from it, which is why
    // a wide display shows more of the world beside the same-sized frames
    // rather than larger ones — and why a portrait display, where the height
    // is the long side, draws the interface bigger. That is what the original
    // client does too.
    WidgetTree tree;

    SECTION("a 768-tall display is one unit per pixel") {
        tree.layout(1024.0f, 768.0f);
        REQUIRE(tree.uiScale() == Catch::Approx(1.0f));
        REQUIRE(tree.get(tree.root())->rectW == Catch::Approx(1024.0f));
        REQUIRE(tree.get(tree.root())->rectH == Catch::Approx(768.0f));
    }

    SECTION("a 1440p ultrawide is wider in units, not taller") {
        tree.layout(3440.0f, 1440.0f);
        REQUIRE(tree.uiScale() == Catch::Approx(1440.0f / 768.0f));
        REQUIRE(tree.get(tree.root())->rectH == Catch::Approx(768.0f));
        REQUIRE(tree.get(tree.root())->rectW == Catch::Approx(3440.0f * 768.0f / 1440.0f));
    }

    SECTION("a portrait display is narrow in units and scaled up") {
        // 1080x1920 rotated. The virtual screen is 432 units across, so the
        // interface is drawn at two and a half times size against very little
        // width — which is the original client's behaviour on the same
        // monitor, not a fault to correct here.
        tree.layout(1080.0f, 1920.0f);
        REQUIRE(tree.uiScale() == Catch::Approx(2.5f));
        REQUIRE(tree.get(tree.root())->rectW == Catch::Approx(432.0f));
        REQUIRE(tree.get(tree.root())->rectH == Catch::Approx(768.0f));
    }

    SECTION("a frame keeps its authored size in units on every display") {
        const uint32_t f = tree.create(WidgetKind::Frame, tree.root(), "F");
        tree.get(f)->width = 232.0f;
        tree.get(f)->height = 100.0f;
        tree.addPoint(f, Anchor{"BOTTOMLEFT", 0, "BOTTOMLEFT", 0.0f, 0.0f});
        for (auto wh : {std::pair<float, float>{1024.0f, 768.0f},
                        {2560.0f, 1440.0f},
                        {1080.0f, 1920.0f}}) {
            tree.layout(wh.first, wh.second);
            REQUIRE(tree.get(f)->rectW == Catch::Approx(232.0f));
            REQUIRE(tree.get(f)->rectH == Catch::Approx(100.0f));
        }
    }
}

TEST_CASE("A frame's anchors can be read back as they were set",
          "[widget][anchor]") {
    // FrameXML reads a point and puts it straight back to move something — a
    // dragged chat window, a frame the panel manager shifts aside. Anything
    // less than the anchor it was given means that round trip moves the frame,
    // and a constant means it moves to the same place every time.
    WidgetTree tree;
    const uint32_t a = tree.create(WidgetKind::Frame, tree.root(), "A");
    const uint32_t b = tree.create(WidgetKind::Frame, tree.root(), "B");

    tree.addPoint(b, Anchor{"TOPLEFT", a, "BOTTOMRIGHT", 7.0f, -3.0f});
    tree.addPoint(b, Anchor{"BOTTOMRIGHT", 0, "BOTTOMRIGHT", -4.0f, 5.0f});

    const Widget* w = tree.get(b);
    REQUIRE(w->anchors.size() == 2);
    REQUIRE(w->anchors[0].point == "TOPLEFT");
    REQUIRE(w->anchors[0].relativeTo == a);
    REQUIRE(w->anchors[0].relativePoint == "BOTTOMRIGHT");
    REQUIRE(w->anchors[0].x == Catch::Approx(7.0f));
    REQUIRE(w->anchors[0].y == Catch::Approx(-3.0f));
    // Zero means "my parent" rather than a frame that was never named, which
    // is what SetPoint's own default means too.
    REQUIRE(w->anchors[1].relativeTo == 0);

    // Clearing and re-applying the first anchor must land the frame where it
    // already was, which is the round trip the interface actually performs.
    tree.get(a)->width = 40.0f;
    tree.get(a)->height = 20.0f;
    tree.addPoint(a, Anchor{"BOTTOMLEFT", 0, "BOTTOMLEFT", 0.0f, 0.0f});
    tree.get(b)->width = 10.0f;
    tree.get(b)->height = 10.0f;
    tree.clearPoints(b);
    tree.addPoint(b, Anchor{"TOPLEFT", a, "BOTTOMRIGHT", 7.0f, -3.0f});
    tree.layout(1024.0f, 768.0f);
    const float left = tree.get(b)->left;
    const float bottom = tree.get(b)->bottom;

    const Anchor readBack = tree.get(b)->anchors[0];
    tree.clearPoints(b);
    tree.addPoint(b, readBack);
    tree.layout(1024.0f, 768.0f);
    REQUIRE(tree.get(b)->left == Catch::Approx(left));
    REQUIRE(tree.get(b)->bottom == Catch::Approx(bottom));
}

TEST_CASE("A widget knows what type it is", "[widget]") {
    // FrameXML branches on this: whether to treat something as a region it can
    // position, whether a frame is a Button worth clicking. Answering "Frame"
    // for everything makes each of those branches take the wrong side, and
    // silently, because the answer is a plausible string rather than nothing.
    WidgetTree tree;
    const uint32_t button = tree.create(WidgetKind::Frame, tree.root(), "B");
    tree.get(button)->objectType = "Button";
    const uint32_t tex = tree.create(WidgetKind::Texture, button, "T");
    tree.get(tex)->objectType = "Texture";

    REQUIRE(tree.get(button)->objectType == "Button");
    REQUIRE(tree.get(tex)->objectType == "Texture");
    // A widget created without being told keeps the type a plain frame has,
    // which is what CreateFrame's own default argument means.
    REQUIRE(tree.get(tree.root())->objectType == "Frame");
}

TEST_CASE("A scroll frame is a window onto a taller child", "[widget][scroll]") {
    // Scrolling down means seeing content further down the child, which is the
    // child moving up — and up is a larger bottom in these coordinates. The
    // whole feature was absent: SetVerticalScroll did nothing and every getter
    // answered zero, so a scroll bar had nothing to report and nothing to move.
    WidgetTree tree;
    const uint32_t frame = tree.create(WidgetKind::Frame, tree.root(), "S");
    tree.get(frame)->isScrollFrame = true;
    tree.get(frame)->width = 200.0f;
    tree.get(frame)->height = 100.0f;
    tree.addPoint(frame, Anchor{"BOTTOMLEFT", 0, "BOTTOMLEFT", 0.0f, 0.0f});

    const uint32_t child = tree.create(WidgetKind::Frame, frame, "SChild");
    tree.get(child)->width = 200.0f;
    tree.get(child)->height = 300.0f;
    // Anchored so its top sits at the frame's top, as a scroll child is.
    tree.addPoint(child, Anchor{"TOPLEFT", frame, "TOPLEFT", 0.0f, 0.0f});
    tree.get(frame)->scrollChild = child;

    tree.layout(1024.0f, 768.0f);
    const float unscrolled = tree.get(child)->bottom;

    tree.get(frame)->scrollY = 50.0f;
    tree.layout(1024.0f, 768.0f);
    REQUIRE(tree.get(child)->bottom == Catch::Approx(unscrolled + 50.0f));

    // Everything under the frame is clipped to it, however deep — a scroll
    // child holds frames of its own.
    const uint32_t grandchild = tree.create(WidgetKind::Frame, child, "SGrand");
    tree.get(grandchild)->width = 10.0f;
    tree.get(grandchild)->height = 10.0f;
    tree.layout(1024.0f, 768.0f);
    REQUIRE(tree.get(child)->clipTo == frame);
    REQUIRE(tree.get(grandchild)->clipTo == frame);
    // The frame itself is not clipped by itself.
    REQUIRE(tree.get(frame)->clipTo == 0);
}

TEST_CASE("Only a frame that asked for the wheel takes it", "[widget][scroll]") {
    // As in WoW, where a frame ignores the wheel until EnableMouseWheel is
    // called. It matters in both directions: a frame that did not ask must not
    // swallow the wheel, or the camera stops zooming wherever the interface
    // happens to be; and the frame that did ask is usually not the one under
    // the cursor, since a scroll child fills its parent and takes the hit.
    WidgetTree tree;
    const uint32_t scroll = tree.create(WidgetKind::Frame, tree.root(), "S");
    tree.get(scroll)->isScrollFrame = true;
    tree.get(scroll)->wheelEnabled = true;
    tree.get(scroll)->mouseEnabled = true;
    tree.get(scroll)->width = 100.0f;
    tree.get(scroll)->height = 100.0f;
    tree.addPoint(scroll, Anchor{"BOTTOMLEFT", 0, "BOTTOMLEFT", 0.0f, 0.0f});

    const uint32_t child = tree.create(WidgetKind::Frame, scroll, "SChild");
    tree.get(child)->mouseEnabled = true;
    tree.get(child)->width = 100.0f;
    tree.get(child)->height = 300.0f;
    tree.addPoint(child, Anchor{"TOPLEFT", scroll, "TOPLEFT", 0.0f, 0.0f});

    tree.layout(1024.0f, 768.0f);

    // The child is what the cursor lands on; the scroll frame above it is what
    // should handle the wheel, which is the walk up the dispatch performs.
    // hitTest measures upward from the bottom, as the tree does; the caller
    // is what flips the cursor into it.
    const uint32_t hit = tree.hitTest(50.0f, 50.0f);
    REQUIRE(hit == child);
    REQUIRE_FALSE(tree.get(child)->wheelEnabled);
    REQUIRE(tree.get(tree.get(child)->parent)->wheelEnabled);
}

TEST_CASE("Scrolled out of sight is out of reach", "[widget][scroll][hittest]") {
    // Clipping without this is only half the feature: the part of a scroll
    // child above or below the window is not drawn, so it must not answer
    // clicks either — a quest log that reacts to entries nobody can see is
    // worse than one that does not scroll at all.
    WidgetTree tree;
    const uint32_t scroll = tree.create(WidgetKind::Frame, tree.root(), "S");
    tree.get(scroll)->isScrollFrame = true;
    tree.get(scroll)->width = 100.0f;
    tree.get(scroll)->height = 100.0f;
    tree.addPoint(scroll, Anchor{"BOTTOMLEFT", 0, "BOTTOMLEFT", 0.0f, 200.0f});

    // An entry inside the child, positioned below the window rather than in it.
    const uint32_t child = tree.create(WidgetKind::Frame, scroll, "SChild");
    tree.get(child)->width = 100.0f;
    tree.get(child)->height = 300.0f;
    tree.addPoint(child, Anchor{"TOPLEFT", scroll, "TOPLEFT", 0.0f, 0.0f});
    tree.get(scroll)->scrollChild = child;

    const uint32_t entry = tree.create(WidgetKind::Frame, child, "SEntry");
    tree.get(entry)->mouseEnabled = true;
    tree.get(entry)->width = 100.0f;
    tree.get(entry)->height = 20.0f;
    tree.addPoint(entry, Anchor{"BOTTOMLEFT", child, "BOTTOMLEFT", 0.0f, 0.0f});

    tree.layout(1024.0f, 768.0f);
    // The entry sits at the bottom of a 300-tall child hanging below a 100-tall
    // window, so it is well outside it.
    const Widget* e = tree.get(entry);
    REQUIRE(e->clipTo == scroll);
    REQUIRE(tree.hitTest(50.0f, e->bottom + 10.0f) == 0);

    // Scrolled far enough, the same entry comes into the window and answers.
    tree.get(scroll)->scrollY = 200.0f;
    tree.layout(1024.0f, 768.0f);
    const Widget* moved = tree.get(entry);
    REQUIRE(tree.hitTest(50.0f, moved->bottom + 10.0f) == entry);
}

TEST_CASE("Scroll frames are tracked as they are marked", "[widget][scroll]") {
    // The range has to be re-checked every frame, and walking every widget to
    // find a handful of scroll frames is the kind of cost that does not show
    // up until the interface is large — which, with FrameXML loaded, it is.
    WidgetTree tree;
    const uint32_t a = tree.create(WidgetKind::Frame, tree.root(), "A");
    const uint32_t b = tree.create(WidgetKind::Frame, tree.root(), "B");
    tree.create(WidgetKind::Frame, tree.root(), "C");

    tree.markScrollFrame(a);
    tree.markScrollFrame(b);
    // Marking twice must not list it twice: SetScrollChild and CreateFrame
    // both mark, and the same frame goes through both.
    tree.markScrollFrame(a);

    REQUIRE(tree.scrollFrames().size() == 2);
    REQUIRE(tree.get(a)->isScrollFrame);
    REQUIRE(tree.get(b)->isScrollFrame);
}

TEST_CASE("Visibility is a state to be noticed, not an event to be sent",
          "[widget][layout]") {
    // Hiding a frame hides everything under it, and none of those had Hide
    // called on them — so anything watching for a frame to go away has to
    // compare what layout resolved rather than listen at the point something
    // was hidden three levels up. This is the property that makes it possible.
    WidgetTree tree;
    const uint32_t panel = tree.create(WidgetKind::Frame, tree.root(), "P");
    tree.get(panel)->width = 100.0f;
    tree.get(panel)->height = 100.0f;
    tree.addPoint(panel, Anchor{"CENTER", 0, "CENTER", 0.0f, 0.0f});

    const uint32_t inner = tree.create(WidgetKind::Frame, panel, "PInner");
    tree.get(inner)->width = 50.0f;
    tree.get(inner)->height = 50.0f;
    tree.addPoint(inner, Anchor{"CENTER", panel, "CENTER", 0.0f, 0.0f});

    tree.layout(1024.0f, 768.0f);
    REQUIRE(tree.get(panel)->visible);
    REQUIRE(tree.get(inner)->visible);

    // Only the panel is hidden; the child is still shown in its own right.
    tree.get(panel)->shown = false;
    tree.layout(1024.0f, 768.0f);
    REQUIRE_FALSE(tree.get(panel)->visible);
    REQUIRE_FALSE(tree.get(inner)->visible);
    REQUIRE(tree.get(inner)->shown);

    tree.get(panel)->shown = true;
    tree.layout(1024.0f, 768.0f);
    REQUIRE(tree.get(inner)->visible);
}

TEST_CASE("A button shows one state texture, not all of them",
          "[widget][draworder]") {
    // A button carries art for each state and shows one. Drawing all of them
    // puts the disabled art over the normal art with the highlight permanently
    // on top, which is not a subtle fault — every button in the interface
    // looks hovered and wrong at once.
    WidgetTree tree;
    const uint32_t button = tree.create(WidgetKind::Frame, tree.root(), "B");
    tree.get(button)->objectType = "Button";
    tree.get(button)->width = 100.0f;
    tree.get(button)->height = 20.0f;
    tree.addPoint(button, Anchor{"CENTER", 0, "CENTER", 0.0f, 0.0f});

    auto art = [&](const char* name, ButtonArt kind) {
        const uint32_t t = tree.create(WidgetKind::Texture, button, name);
        tree.get(t)->texturePath = "Interface\\Art";
        tree.get(t)->buttonArt = kind;
        tree.setAllPoints(t, button);
        return t;
    };
    const uint32_t normal    = art("BN", ButtonArt::Normal);
    const uint32_t pushed    = art("BP", ButtonArt::Pushed);
    const uint32_t highlight = art("BH", ButtonArt::Highlight);
    const uint32_t disabled  = art("BD", ButtonArt::Disabled);

    auto drawn = [&](uint32_t id) {
        for (const Widget* w : tree.drawOrder()) if (w->id == id) return true;
        return false;
    };

    // Idle: normal only.
    tree.setInteraction(0, 0);
    tree.layout(1024.0f, 768.0f);
    REQUIRE(drawn(normal));
    REQUIRE_FALSE(drawn(pushed));
    REQUIRE_FALSE(drawn(highlight));
    REQUIRE_FALSE(drawn(disabled));

    // Hovered: the highlight joins it. The cursor lands on the button's own
    // art rather than the button, so hovering a child must count.
    tree.setInteraction(normal, 0);
    tree.layout(1024.0f, 768.0f);
    REQUIRE(drawn(highlight));
    REQUIRE(drawn(normal));

    // Held: pushed replaces normal.
    tree.setInteraction(button, button);
    tree.layout(1024.0f, 768.0f);
    REQUIRE(drawn(pushed));
    REQUIRE_FALSE(drawn(normal));

    // Disabled: only the disabled art, and no highlight however hovered.
    tree.get(button)->enabled = false;
    tree.setInteraction(button, 0);
    tree.layout(1024.0f, 768.0f);
    REQUIRE(drawn(disabled));
    REQUIRE_FALSE(drawn(normal));
    REQUIRE_FALSE(drawn(highlight));
}
