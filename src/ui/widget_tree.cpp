#include "ui/widget_tree.hpp"

#include <algorithm>
#include <cmath>

namespace wowee {
namespace ui {

namespace {

bool contains(const std::string& s, const char* sub) {
    return s.find(sub) != std::string::npos;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    return s;
}

int strataRank(FrameStrata s) { return static_cast<int>(s); }
int layerRank(DrawLayer l) { return static_cast<int>(l); }

} // namespace

AnchorPoint resolveAnchorPoint(const std::string& rawName) {
    const std::string name = upper(rawName);
    AnchorPoint p;
    // Vertical: TOP is 1, BOTTOM is 0, neither is centred. Tested before the
    // horizontal half because the names combine (TOPLEFT is both).
    if (contains(name, "TOP"))         p.fy = 1.0f;
    else if (contains(name, "BOTTOM")) p.fy = 0.0f;
    else                               p.fy = 0.5f;

    if (contains(name, "LEFT"))        p.fx = 0.0f;
    else if (contains(name, "RIGHT"))  p.fx = 1.0f;
    else                               p.fx = 0.5f;
    return p;
}

DrawLayer parseDrawLayer(const std::string& rawName) {
    const std::string n = upper(rawName);
    if (n == "BACKGROUND") return DrawLayer::Background;
    if (n == "BORDER")     return DrawLayer::Border;
    if (n == "OVERLAY")    return DrawLayer::Overlay;
    if (n == "HIGHLIGHT")  return DrawLayer::Highlight;
    return DrawLayer::Artwork;   // Blizzard's default
}

FrameStrata parseStrata(const std::string& rawName) {
    const std::string n = upper(rawName);
    if (n == "WORLD")             return FrameStrata::World;
    if (n == "BACKGROUND")        return FrameStrata::Background;
    if (n == "LOW")               return FrameStrata::Low;
    if (n == "HIGH")              return FrameStrata::High;
    if (n == "DIALOG")            return FrameStrata::Dialog;
    if (n == "FULLSCREEN")        return FrameStrata::Fullscreen;
    if (n == "FULLSCREEN_DIALOG") return FrameStrata::FullscreenDialog;
    if (n == "TOOLTIP")           return FrameStrata::Tooltip;
    return FrameStrata::Medium;
}

WidgetTree::WidgetTree() {
    widgets_.emplace_back();          // id 0 is "none"
    rootId_ = create(WidgetKind::Frame, 0, "UIParent");
}

uint32_t WidgetTree::create(WidgetKind kind, uint32_t parent, const std::string& name) {
    const uint32_t id = static_cast<uint32_t>(widgets_.size());
    widgets_.emplace_back();
    Widget& w = widgets_.back();
    w.id = id;
    w.kind = kind;
    w.name = name;
    w.creationOrder = nextOrder_++;
    // Regions belong to the frame that made them; a frame with no parent hangs
    // off the root, which is what UIParent is for.
    if (parent == 0 && id != rootId_ && rootId_ != 0) parent = rootId_;
    w.parent = parent;
    if (parent != 0 && parent < widgets_.size()) {
        widgets_[parent].children.push_back(id);
        // Its place in the stack, known now rather than at the first layout.
        //
        // GetFrameLevel answers with this, and FrameXML asks during OnLoad —
        // RaiseFrameLevel is frame:SetFrameLevel(frame:GetFrameLevel() + 1),
        // and a frame that has never been laid out answered zero. So the
        // adjustment was computed against nothing: MainMenuBarArtFrame set
        // itself to 1 rather than to one above its parent, its buttons
        // followed, and the bar they sit on stayed above all of them and took
        // every click. Elsewhere the same sum went negative.
        w.effLevel = widgets_[parent].effLevel + 1;
        w.effStrata = widgets_[parent].effStrata;
    }
    return id;
}

void WidgetTree::setParent(uint32_t id, uint32_t newParent) {
    Widget* w = get(id);
    if (!w || id == rootId_) return;
    // Nothing named, or the root's own rule: a frame with no parent hangs off
    // the screen, which is what UIParent is.
    if (newParent == 0) newParent = rootId_;
    if (newParent == w->parent) return;
    if (!get(newParent)) return;

    // A frame cannot be put inside itself or inside anything it contains —
    // layout walks children and would never come back.
    for (uint32_t up = newParent; up != 0;) {
        if (up == id) return;
        const Widget* p = get(up);
        if (!p) break;
        up = p->parent;
    }

    if (Widget* old = get(w->parent)) {
        auto& kids = old->children;
        kids.erase(std::remove(kids.begin(), kids.end(), id), kids.end());
    }
    w->parent = newParent;
    get(newParent)->children.push_back(id);
}

void WidgetTree::markScrollFrame(uint32_t id) {
    Widget* w = get(id);
    if (!w || w->isScrollFrame) return;
    w->isScrollFrame = true;
    scrollFrames_.push_back(id);
}

void WidgetTree::markPlayerPortrait(uint32_t id) {
    if (id == 0) return;
    for (uint32_t existing : playerPortraits_) {
        if (existing == id) return;
    }
    playerPortraits_.push_back(id);
}

void WidgetTree::unmarkPlayerPortrait(uint32_t id) {
    // Put the frame's own art back. The player's face is pushed into
    // externalTexture every frame for as long as the id is on this list, and
    // dropping it off the list only stops the updates — the last face stays.
    //
    // The target frame is the one that shows: SetPortraitTexture marks it while
    // the player is targeting themselves and unmarks it for anything else, so
    // the next target wore the player's face. A game object made that obvious,
    // having no portrait of its own to overwrite it with.
    if (auto* w = get(id)) w->externalTexture = 0;
    for (size_t i = 0; i < playerPortraits_.size(); ++i) {
        if (playerPortraits_[i] != id) continue;
        playerPortraits_[i] = playerPortraits_.back();
        playerPortraits_.pop_back();
        return;
    }
}

Widget* WidgetTree::findByName(std::string_view name) {
    if (name.empty()) return nullptr;
    // Backwards, so the last frame to take the name is the one found — the
    // same rule as the global it was published under.
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        if (it->id != 0 && it->name == name) return &*it;
    }
    return nullptr;
}

const Widget* WidgetTree::findByName(std::string_view name) const {
    return const_cast<WidgetTree*>(this)->findByName(name);
}

Widget* WidgetTree::get(uint32_t id) {
    if (id == 0 || id >= widgets_.size()) return nullptr;
    return &widgets_[id];
}

const Widget* WidgetTree::get(uint32_t id) const {
    if (id == 0 || id >= widgets_.size()) return nullptr;
    return &widgets_[id];
}

void WidgetTree::clearPoints(uint32_t id) {
    if (Widget* w = get(id)) w->anchors.clear();
}

void WidgetTree::setWidth(uint32_t id, float width) {
    Widget* w = get(id);
    if (!w) return;
    // Zero on a font string means "as wide as your text", not "no width".
    // That is WoW's convention and the interface leans on it:
    // PanelTemplates_TabResize ends with tabText:SetWidth(0) for a tab that
    // is not being capped, meaning let the label size itself.
    //
    // Taken literally it left the label zero wide, and a region with no width
    // is not drawn at all — which is why every tab on the character sheet had
    // its text set correctly and showed nothing. Clearing the measured mark
    // is what lets it be measured again; without that the label keeps the
    // zero, because it has already been measured once and its text has not
    // changed since.
    if (width <= 0.0f && w->kind == WidgetKind::FontString) {
        w->autoSized = false;
        w->measuredText.clear();
    }
    w->width = width;
    // Provisional, so a read before the next layout sees what was just set.
    // The layout overwrites it from the anchors, which is the final answer
    // where anchors decide the size.
    w->rectW = width;
}

void WidgetTree::setHeight(uint32_t id, float height) {
    Widget* w = get(id);
    if (!w) return;
    w->height = height;
    w->rectH = height;
}

void WidgetTree::pinToCurrentPosition(uint32_t id) {
    Widget* w = get(id);
    if (!w) return;
    const Widget* parent = get(w->parent);
    const float px = parent ? parent->left : 0.0f;
    const float py = parent ? parent->bottom : 0.0f;

    // One anchor leaves the size to be stated rather than solved, so a frame
    // that was sized by two opposing corners keeps the size it had rather than
    // collapsing the moment it is picked up.
    if (w->width <= 0.0f)  w->width  = w->rectW;
    if (w->height <= 0.0f) w->height = w->rectH;

    Anchor a;
    a.point = "BOTTOMLEFT";
    a.relativePoint = "BOTTOMLEFT";
    a.relativeTo = 0;   // the parent
    a.x = w->left - px;
    a.y = w->bottom - py;
    w->anchors.clear();
    w->anchors.push_back(a);
    w->userMoved = true;
}

namespace {
/// Pulls a frame inside the screen it must stay within.
///
/// An axis where the frame is larger than the screen is left alone: there is
/// no position that satisfies both edges, and snapping to one of them moves
/// the frame for no benefit.
/// Keep a frame on screen, allowing for the insets it declared.
///
/// The insets move the edges of the rectangle that has to stay on screen,
/// which is not the same as the frame's own rectangle. Positive is inward, as
/// everywhere else in WoW: a positive right inset lets that much of the frame
/// hang past the right edge, and a negative one holds it that much clear of it.
///
/// The world map names the case exactly — SetClampRectInsets(0, 0, 0, -60)
/// with "don't overlap the xp/rep bars" beside it, so a negative bottom keeps
/// the frame sixty above the bottom edge rather than letting it reach.
void clampInside(const Widget& screen, float rectW, float rectH,
                 float& left, float& bottom,
                 float insetL = 0.0f, float insetR = 0.0f,
                 float insetT = 0.0f, float insetB = 0.0f) {
    const float loX = screen.left - insetL;
    const float hiX = screen.left + screen.rectW - rectW + insetR;
    const float loY = screen.bottom - insetB;
    const float hiY = screen.bottom + screen.rectH - rectH + insetT;
    if (hiX >= loX) left   = std::clamp(left,   loX, hiX);
    if (hiY >= loY) bottom = std::clamp(bottom, loY, hiY);
}
}  // namespace

// Resize from whichever corner the grabber took hold of.
//
// The point names the corner that MOVES. Dragging BOTTOMRIGHT grows the frame
// right and down, so its top-left stays put and only the size changes; dragging
// TOPLEFT has to move the frame as well, because the corner the player is not
// touching must not travel. Getting that wrong makes a frame walk across the
// screen as it is resized, which is the usual way this is done wrongly.
void WidgetTree::resizeBy(uint32_t id, const std::string& point,
                          float dx, float dy) {
    Widget* w = get(id);
    if (!w) return;

    // A frame sized by two opposing anchors has no width of its own to change,
    // so pin it to what it is currently drawn at first — the same reason
    // pinToCurrentPosition does this before a move.
    if (w->width <= 0.0f)  w->width  = w->rectW;
    if (w->height <= 0.0f) w->height = w->rectH;

    const bool movesLeft   = point.find("LEFT")   != std::string::npos;
    const bool movesBottom = point.find("BOTTOM") != std::string::npos;
    // A corner with neither LEFT nor RIGHT in it does not change the width, and
    // the same for TOP/BOTTOM and the height — "BOTTOM" alone is a bottom edge.
    const bool changesW = movesLeft || point.find("RIGHT") != std::string::npos;
    const bool changesH = movesBottom || point.find("TOP") != std::string::npos;

    const float oldW = w->width, oldH = w->height;
    if (changesW) w->width  += movesLeft   ? -dx : dx;
    if (changesH) w->height += movesBottom ? -dy : dy;

    // Bounds. A zero maximum means unbounded, which is how a frame that never
    // called SetMaxResize reads.
    if (w->minResizeW > 0.0f) w->width  = std::max(w->width,  w->minResizeW);
    if (w->minResizeH > 0.0f) w->height = std::max(w->height, w->minResizeH);
    if (w->maxResizeW > 0.0f) w->width  = std::min(w->width,  w->maxResizeW);
    if (w->maxResizeH > 0.0f) w->height = std::min(w->height, w->maxResizeH);
    // Never inside out, whatever the bounds say.
    w->width  = std::max(w->width, 1.0f);
    w->height = std::max(w->height, 1.0f);

    // Move by however much the size actually changed, not by the cursor delta:
    // once a bound is reached the frame must stop rather than keep sliding.
    if (movesLeft)   { const float d = w->width  - oldW; for (Anchor& a : w->anchors) a.x -= d; }
    if (movesBottom) { const float d = w->height - oldH; for (Anchor& a : w->anchors) a.y -= d; }
}

void WidgetTree::nudge(uint32_t id, float dx, float dy) {
    Widget* w = get(id);
    if (!w) return;
    // A clamped frame stops at the screen edge. The rect used is the one the
    // last layout produced, which is a frame behind the cursor and close
    // enough — the alternative is re-solving the whole tree per mouse move.
    //
    // A frame already outside is pulled back rather than pinned where it is:
    // that is what lets one recover, and it is what WoW does when a clamped
    // frame is restored from saved variables at a smaller resolution.
    if (w->clampedToScreen && w->rectW > 0.0f && w->rectH > 0.0f) {
        if (const Widget* screen = get(rootId_)) {
            // Back to a delta, because a drag moves the anchors rather than
            // the rect: the clamped position is what the anchors have to add
            // up to, not something that can be written to left/bottom here.
            float left = w->left + dx, bottom = w->bottom + dy;
            clampInside(*screen, w->rectW, w->rectH, left, bottom,
                        w->clampInsetL, w->clampInsetR, w->clampInsetT, w->clampInsetB);
            dx = left - w->left;
            dy = bottom - w->bottom;
        }
    }
    for (Anchor& a : w->anchors) { a.x += dx; a.y += dy; }
}

/// Move every descendant that carries its own level by the same amount.
///
/// A child's level is relative to its parent in WoW, and FrameXML sets levels
/// freely — RaiseFrameLevelByTwo alone is used throughout. Without this, a
/// raised window keeps its own art in front but leaves anything that set its
/// own level behind: the character sheet's name label sat at 6 while the panel
/// it belongs to went to 174, and sorting by level drew the name underneath
/// the panel, where it cannot be seen.
void WidgetTree::shiftExplicitLevels(uint32_t id, int delta) {
    if (delta == 0) return;
    const Widget* w = get(id);
    if (!w) return;
    // A copy, because get() invalidates nothing but the recursion below may.
    const std::vector<uint32_t> kids = w->children;
    for (uint32_t child : kids) {
        if (Widget* c = get(child)) {
            if (c->levelExplicit) c->level += delta;
        }
        shiftExplicitLevels(child, delta);
    }
}

void WidgetTree::raise(uint32_t id) {
    Widget* w = get(id);
    if (!w) return;
    int highest = w->effLevel;
    for (const Widget& other : widgets_) {
        if (other.id == 0 || other.id == id) continue;
        if (other.effStrata != w->effStrata) continue;
        if (other.effLevel > highest) highest = other.effLevel;
    }
    // Explicit from here on, or the next layout would recompute it from the
    // parent and undo the raise immediately.
    const int newLevel = highest + 1;
    shiftExplicitLevels(id, newLevel - w->effLevel);
    w->level = newLevel;
    w->levelExplicit = true;
}

void WidgetTree::lower(uint32_t id) {
    Widget* w = get(id);
    if (!w) return;
    int lowest = w->effLevel;
    for (const Widget& other : widgets_) {
        if (other.id == 0 || other.id == id) continue;
        if (other.effStrata != w->effStrata) continue;
        if (other.effLevel < lowest) lowest = other.effLevel;
    }
    // Never below zero: a negative level sorts under the root and the frame
    // stops being drawn at all.
    const int newLevel = (lowest > 0) ? lowest - 1 : 0;
    shiftExplicitLevels(id, newLevel - w->effLevel);
    w->level = newLevel;
    w->levelExplicit = true;
}

void WidgetTree::addPoint(uint32_t id, const Anchor& anchor) {
    Widget* w = get(id);
    if (!w) return;
    // One anchor per point: setting a point that is already set replaces it
    // rather than adding a second. FrameXML depends on this, because it
    // repositions frames with a bare SetPoint and no ClearAllPoints —
    // UIParentManageFramePositions moves the durability frame with
    // SetPoint("TOPRIGHT", ...), expecting it to displace the TOPRIGHT the XML
    // declared. Keeping both left two constraints on the same edge, which is
    // not a solvable system; the first won, and every frame Blizzard
    // repositions this way stayed where its XML put it. The durability frame
    // sat forty units past the right edge of the screen.
    // The interface positioning a frame that a drag had moved starts from
    // scratch, because the anchor the move left is on whichever point it was
    // picked up by and would otherwise fight the one being set.
    if (w->userMoved) {
        w->userMoved = false;
        w->anchors.clear();
    }
    for (Anchor& existing : w->anchors) {
        if (existing.point == anchor.point) {
            existing = anchor;
            return;
        }
    }
    w->anchors.push_back(anchor);
}

void WidgetTree::setAllPoints(uint32_t id, uint32_t relativeTo) {
    Widget* w = get(id);
    if (!w) return;
    w->anchors.clear();
    // Two opposing corners, which is exactly what makes the size fall out of
    // the solver below rather than needing an explicit one.
    Anchor tl; tl.point = "TOPLEFT";     tl.relativePoint = "TOPLEFT";     tl.relativeTo = relativeTo;
    Anchor br; br.point = "BOTTOMRIGHT"; br.relativePoint = "BOTTOMRIGHT"; br.relativeTo = relativeTo;
    w->anchors.push_back(tl);
    w->anchors.push_back(br);
}

void WidgetTree::layout(float pixelW, float pixelH) {
    // How many pixels one interface unit is worth. Everything below works in
    // units; only the renderer and hit testing convert.
    uiScale_ = (pixelH > 0.0f) ? (pixelH / kInterfaceHeight) : 1.0f;
    const float screenW = (uiScale_ > 0.0f) ? (pixelW / uiScale_) : pixelW;
    const float screenH = kInterfaceHeight;

    Widget& rootW = widgets_[rootId_];
    rootW.left = 0.0f;
    rootW.bottom = 0.0f;
    rootW.rectW = screenW;
    rootW.rectH = screenH;
    rootW.visibleChain = rootW.shown;
    rootW.visible = rootW.shown;
    rootW.effStrata = rootW.strata;
    rootW.effLevel = 0;
    rootW.effScale = 1.0f;

    for (uint32_t child : rootW.children) layoutWidget(child, screenW, screenH);
    collectDrawOrder();
}

void WidgetTree::layoutWidget(uint32_t id, float screenW, float screenH) {
    Widget* w = get(id);
    if (!w) return;
    const Widget* parent = get(w->parent);

    // A frame with no anchor points is not displayed. That is WoW's rule, and
    // without it every frame FrameXML declares without anchors — a money
    // frame, a dropdown, a quest reward panel — falls to the centre-on-parent
    // default and sits in the middle of the screen looking like a bug in
    // something else. Regions differ: an unanchored one fills its parent, and
    // that is handled below.
    //
    // The root is the exception: it is the screen, and has nothing to anchor
    // to.
    const bool unanchoredFrame = (w->kind == WidgetKind::Frame) &&
                                 w->anchors.empty() && id != rootId_;
    // Two questions, and they are not the same one. Running is shown with
    // every ancestor shown; drawing additionally needs somewhere to be drawn.
    // Inherited from the parent's chain rather than its `visible`, or a child
    // of an unanchored driver frame would stop running too.
    w->visibleChain = w->shown && (!parent || parent->visibleChain);
    // Drawing inherits from the parent's *drawing*, not from the chain: an
    // anchored child of an unanchored frame has nowhere to be either, because
    // the thing it is anchored to has no position. Deriving this from the
    // chain instead put those children back on screen.
    w->visible = w->shown && (!parent || parent->visible) && !unanchoredFrame;
    // Clipping is inherited: anything under a scroll frame is bounded by it,
    // however deep, because a scroll child holds frames of its own.
    w->clipTo = parent ? (parent->isScrollFrame ? parent->id : parent->clipTo) : 0;
    // Strata and level are inherited unless the widget set its own. A child
    // frame sits one level above its parent so it draws over it, which is what
    // makes a button's own regions land on top of the frame holding it.
    w->effStrata = w->strataExplicit ? w->strata : (parent ? parent->effStrata : FrameStrata::Medium);
    w->effLevel  = w->levelExplicit  ? w->level  : (parent ? parent->effLevel + 1 : 0);
    // Multiplied down the chain, so scaling a window scales everything in it.
    w->effScale  = (parent ? parent->effScale : 1.0f) * w->scale;
    const float es = w->effScale;

    // Solve each axis from the anchors. An anchor says "this fraction of my rect
    // sits at that point", which is one linear constraint; two constraints with
    // different fractions give the size as well as the position, and that is how
    // a frame pinned at two opposing corners gets sized without anyone calling
    // SetSize.
    struct Constraint { float f; float target; };
    std::vector<Constraint> cx, cy;
    cx.reserve(w->anchors.size());
    cy.reserve(w->anchors.size());

    for (const Anchor& a : w->anchors) {
        const Widget* rel = (a.relativeTo != 0) ? get(a.relativeTo) : parent;
        float relLeft, relBottom, relW, relH;
        if (rel) {
            relLeft = rel->left; relBottom = rel->bottom; relW = rel->rectW; relH = rel->rectH;
        } else {
            relLeft = 0.0f; relBottom = 0.0f; relW = screenW; relH = screenH;
        }
        const AnchorPoint rp = resolveAnchorPoint(a.relativePoint);
        const AnchorPoint mp = resolveAnchorPoint(a.point);
        // The offset is in this frame's units; the anchor it hangs from is
        // already resolved, so only the offset is scaled.
        cx.push_back({mp.fx, relLeft   + rp.fx * relW + a.x * es});
        cy.push_back({mp.fy, relBottom + rp.fy * relH + a.y * es});
    }

    auto solveAxis = [](const std::vector<Constraint>& cs, float explicitSize,
                        float parentOrigin, float parentSize,
                        float& outOrigin, float& outSize) {
        if (cs.empty()) {
            // Unanchored: WoW leaves this undefined, and centring on the parent
            // is the least surprising thing to draw.
            outSize = explicitSize;
            outOrigin = parentOrigin + (parentSize - explicitSize) * 0.5f;
            return;
        }
        // A frame is resized on an axis only when two anchors pin *opposite
        // edges* of it — a 0 and a 1. That is the rule WoW follows, and the
        // difference from "any two fractions that differ" is not academic.
        //
        // The reputation rows are built from both: the XML anchors each row's
        // TOPRIGHT under the previous row, and ReputationFrame_SetRowType then
        // adds a LEFT anchor to set the indent. On x those are 0 and 1, so the
        // row correctly spans from its indent to the frame's right edge. On y
        // they are 1 (a top edge) and 0.5 (a centre) — not opposite edges, and
        // nothing WoW would resize from.
        //
        // Deriving a height from an edge and a centre gave twice the distance
        // between them, which for a row anchored near the top of a frame whose
        // centre is halfway down is most of the frame. Every faction row was
        // stretched to that height and drawn on top of the last, which is why
        // the reputation tab showed a stack of overlapping names behind two
        // enormous yellow bars.
        size_t lo = 0, hi = 0;
        for (size_t i = 1; i < cs.size(); ++i) {
            if (cs[i].f < cs[lo].f) lo = i;
            if (cs[i].f > cs[hi].f) hi = i;
        }
        if (cs[lo].f < 0.01f && cs[hi].f > 0.99f) {
            outSize = cs[hi].target - cs[lo].target;
            outOrigin = cs[lo].target;
        } else {
            // Positioned by the first anchor, which is the one the XML gave
            // it: a point added later is refining where it sits, not replacing
            // what it hangs from.
            outSize = explicitSize;
            outOrigin = cs[0].target - cs[0].f * outSize;
        }
    };

    const float pLeft   = parent ? parent->left   : 0.0f;
    const float pBottom = parent ? parent->bottom : 0.0f;
    const float pW      = parent ? parent->rectW  : screenW;
    const float pH      = parent ? parent->rectH  : screenH;

    // A region that says nothing about where it is or how big fills its
    // parent. That is WoW's default for a Texture or FontString declared in a
    // Layer with neither <Size> nor <Anchors>, and it is not a rare shorthand:
    // PlayerFrameTexture is the entire player frame's art and MinimapBorder is
    // the ring around the minimap, and both are written this way. Centring
    // them at no size instead meant they were laid out to nothing, never
    // reached the draw order, and so were never even uploaded.
    if (w->kind != WidgetKind::Frame && w->anchors.empty() &&
        w->width <= 0.0f && w->height <= 0.0f && parent && !w->isTooltip) {
        w->left   = parent->left;
        w->bottom = parent->bottom;
        w->rectW  = parent->rectW;
        w->rectH  = parent->rectH;
    } else {
        solveAxis(cx, w->width * es,  pLeft,   pW, w->left,   w->rectW);
        solveAxis(cy, w->height * es, pBottom, pH, w->bottom, w->rectH);
    }
    // After the solve, so it displaces the result rather than becoming another
    // constraint on it.
    w->left   += w->animOffsetX;
    w->bottom += w->animOffsetY;

    // The scroll offset, applied to the child a scroll frame holds. Scrolling
    // down means seeing content further down a taller child, which is the
    // child moving up — and up is a larger bottom in these coordinates.
    if (parent && parent->isScrollFrame && parent->scrollChild == id) {
        w->left   -= parent->scrollX;
        w->bottom += parent->scrollY;
    }

    // A clamped frame stays on screen however it was placed, not only when it
    // was dragged there.
    //
    // GameTooltipTemplate declares clampedToScreen="true" and every tooltip in
    // the interface inherits it, but the clamp lived only in the drag path —
    // and a tooltip is never dragged. It is anchored beside whatever it
    // describes, so one owned by a frame near an edge simply ran off it: the
    // minimap's calendar button put its tooltip past the right of the screen,
    // where it was laid out, drawn, and invisible.
    //
    // Before the children, so they follow the clamped position rather than the
    // one it was moved out of.
    if (w->clampedToScreen && id != rootId_ &&
        w->rectW > 0.0f && w->rectH > 0.0f) {
        if (const Widget* screen = get(rootId_)) {
            clampInside(*screen, w->rectW, w->rectH, w->left, w->bottom,
                        w->clampInsetL, w->clampInsetR, w->clampInsetT, w->clampInsetB);
        }
    }

    for (uint32_t child : w->children) layoutWidget(child, screenW, screenH);
}

uint32_t WidgetTree::hitTest(float x, float y) const {
    const Widget* best = nullptr;
    for (const Widget& w : widgets_) {
        if (w.id == 0 || w.kind != WidgetKind::Frame) continue;
        if (!w.visible || !w.mouseEnabled) continue;
        if (w.rectW <= 0.0f || w.rectH <= 0.0f) continue;
        // The hit rect, which is the frame's rect brought in by its insets.
        // Top and bottom are named the way WoW names them: top is the upper
        // edge, and y grows upward here, so it comes off bottom + height.
        const float hx0 = w.left + w.hitInsetLeft;
        const float hx1 = w.left + w.rectW - w.hitInsetRight;
        const float hy0 = w.bottom + w.hitInsetBottom;
        const float hy1 = w.bottom + w.rectH - w.hitInsetTop;
        if (hx1 <= hx0 || hy1 <= hy0) continue;  // inset to nothing: unclickable
        if (x < hx0 || x > hx1) continue;
        if (y < hy0 || y > hy1) continue;
        // Scrolled out of sight is out of reach. A scroll frame shows a window
        // onto a taller child, and the part of that child above or below the
        // window is not drawn — so it must not be clickable either, or a quest
        // log answers clicks on entries nobody can see.
        if (w.clipTo != 0) {
            const Widget* clip = get(w.clipTo);
            if (clip && (x < clip->left || x > clip->left + clip->rectW ||
                         y < clip->bottom || y > clip->bottom + clip->rectH)) {
                continue;
            }
        }
        if (!best) { best = &w; continue; }
        // Same comparison the draw order uses, read the other way round: the
        // last thing painted is the first thing clicked.
        const int sa = strataRank(w.effStrata), sb = strataRank(best->effStrata);
        if (sa != sb) { if (sa > sb) best = &w; continue; }
        if (w.effLevel != best->effLevel) { if (w.effLevel > best->effLevel) best = &w; continue; }
        if (w.creationOrder > best->creationOrder) best = &w;
    }
    return best ? best->id : 0;
}

bool WidgetTree::buttonArtVisible(const Widget& w) const {
    if (w.buttonArt == ButtonArt::None) return true;

    // The art belongs to the frame holding it, not to itself: it is the button
    // that is hovered, pressed or disabled.
    const Widget* owner = get(w.parent);
    if (!owner) return true;

    // Hovered counts for anything under the button too, since its own regions
    // sit on top of it and are what the cursor actually lands on.
    bool hovered = false;
    for (uint32_t at = hoveredId_; at != 0; ) {
        if (at == owner->id) { hovered = true; break; }
        const Widget* a = get(at);
        if (!a) break;
        at = a->parent;
    }
    bool pressed = false;
    for (uint32_t at = pressedId_; at != 0; ) {
        if (at == owner->id) { pressed = true; break; }
        const Widget* a = get(at);
        if (!a) break;
        at = a->parent;
    }

    // A state asked for outright wins over what the cursor is doing.
    switch (owner->forcedState) {
        case Widget::Forced::Pushed:   pressed = true;  break;
        case Widget::Forced::Normal:   pressed = false; break;
        case Widget::Forced::Disabled: break;
        case Widget::Forced::None:     break;
    }
    const bool usable = owner->enabled &&
                        owner->forcedState != Widget::Forced::Disabled;

    switch (w.buttonArt) {
        case ButtonArt::Highlight:       return (hovered || owner->highlightLocked) && usable;
        case ButtonArt::Disabled:        return !usable;
        case ButtonArt::Pushed:          return usable && pressed;
        case ButtonArt::Normal:          return usable && !pressed;
        case ButtonArt::Checked:         return owner->checked && usable;
        case ButtonArt::DisabledChecked: return owner->checked && !usable;
        default:                         return true;
    }
}

void WidgetTree::collectDrawOrder() {
    drawOrder_.clear();
    for (const Widget& w : widgets_) {
        if (w.id == 0) continue;
        if (!w.visible) continue;
        if (w.alpha <= 0.001f) continue;
        // Frames are containers, except when they carry a backdrop or are a
        // status bar — then the frame itself has something to paint, and it
        // paints underneath its own regions because they sit a level above it.
        // A frame the client renders into paints itself, the same as one with
        // a backdrop. The paperdoll's model frame is a frame, not a texture,
        // so without this the character would be rendered and never drawn.
        if (w.kind == WidgetKind::Frame && !w.hasBackdrop && !w.isStatusBar &&
            w.externalTexture == 0 &&
            !(w.isMessageFrame && !w.messages.empty()) &&
            !(w.isTooltip && !w.tooltipLines.empty())) continue;
        if (w.rectW <= 0.0f || w.rectH <= 0.0f) continue;
        if (w.kind == WidgetKind::Texture && w.texturePath.empty() &&
            !w.solidColor && w.externalTexture == 0) continue;
        if (w.kind == WidgetKind::Frame && w.isStatusBar && w.barTexture.empty() &&
            !w.hasBackdrop) continue;
        if (w.kind == WidgetKind::FontString && w.text.empty()) continue;
        // A button shows one of its state textures, not all of them.
        if (!buttonArtVisible(w)) continue;
        drawOrder_.push_back(&w);
    }

    std::sort(drawOrder_.begin(), drawOrder_.end(),
              [](const Widget* a, const Widget* b) {
                  const int sa = strataRank(a->effStrata), sb = strataRank(b->effStrata);
                  if (sa != sb) return sa < sb;
                  if (a->effLevel != b->effLevel) return a->effLevel < b->effLevel;
                  const int la = layerRank(a->layer), lb = layerRank(b->layer);
                  if (la != lb) return la < lb;
                  if (a->subLevel != b->subLevel) return a->subLevel < b->subLevel;
                  // Ties resolve by creation order, so a region added later sits
                  // on top of one added earlier — the same rule the real client
                  // uses within a layer.
                  return a->creationOrder < b->creationOrder;
              });
}

void hsvToRgb(const float hsv[3], float rgb[3]) {
    const float h = hsv[0] - std::floor(hsv[0]);   // one turn, wrapped
    const float s = std::clamp(hsv[1], 0.0f, 1.0f);
    const float v = std::clamp(hsv[2], 0.0f, 1.0f);
    // The wheel in six segments: within each, one channel is full, one is
    // rising or falling across the segment, and one is at the saturation floor.
    const float sector = h * 6.0f;
    const int i = static_cast<int>(sector) % 6;
    const float f = sector - std::floor(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (i) {
        case 0: rgb[0] = v; rgb[1] = t; rgb[2] = p; break;
        case 1: rgb[0] = q; rgb[1] = v; rgb[2] = p; break;
        case 2: rgb[0] = p; rgb[1] = v; rgb[2] = t; break;
        case 3: rgb[0] = p; rgb[1] = q; rgb[2] = v; break;
        case 4: rgb[0] = t; rgb[1] = p; rgb[2] = v; break;
        default: rgb[0] = v; rgb[1] = p; rgb[2] = q; break;
    }
}

void rgbToHsv(const float rgb[3], float hsv[3]) {
    const float r = rgb[0], g = rgb[1], b = rgb[2];
    const float hi = std::max(r, std::max(g, b));
    const float lo = std::min(r, std::min(g, b));
    hsv[2] = hi;
    const float span = hi - lo;
    hsv[1] = (hi > 0.0f) ? span / hi : 0.0f;
    if (span <= 0.0f) {
        // Grey has no hue. Zero rather than anything cleverer, and the caller
        // that cares keeps the hue it already had instead of asking.
        hsv[0] = 0.0f;
        return;
    }
    float h;
    if (hi == r)      h = (g - b) / span;
    else if (hi == g) h = 2.0f + (b - r) / span;
    else              h = 4.0f + (r - g) / span;
    h /= 6.0f;
    hsv[0] = h - std::floor(h);
}

} // namespace ui
} // namespace wowee
