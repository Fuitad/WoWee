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
