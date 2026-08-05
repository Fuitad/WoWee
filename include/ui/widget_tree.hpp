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
#include <string_view>
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

/// Which of a button's several textures a region is, if any. A button carries
/// art for each state and shows one of them; without knowing which is which,
/// all of them draw at once and the button wears its disabled art over its
/// normal art with the highlight permanently on top.
enum class ButtonArt : uint8_t { None, Normal, Pushed, Highlight, Disabled,
                                 Checked, DisabledChecked };

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
    /// What CreateFrame was asked for — "Button", "StatusBar", "Texture".
    /// FrameXML branches on this constantly, and answering "Frame" for
    /// everything makes every one of those branches take the wrong side.
    std::string objectType = "Frame";
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
    /// Whether the frame may be dragged around the screen, and which mouse
    /// buttons begin a drag on it. WoW keeps these separate: a bag window is
    /// movable and registers the left button for drag, while an item button
    /// registers for drag without being movable — dragging it picks the item up
    /// instead of moving the button.
    bool movable = false;
    /// Whether StartSizing will pick this frame up. A frame declares it in XML
    /// or is told by SetResizable; the chat window turns it off while docked.
    bool resizable = false;
    /// The bounds a resize is held inside, from SetMinResize/SetMaxResize.
    /// Zero means unbounded in that direction.
    float minResizeW = 0.0f;
    float minResizeH = 0.0f;
    float maxResizeW = 0.0f;
    float maxResizeH = 0.0f;
    /// Whether dragging is stopped at the screen edge. FrameXML declares it on
    /// 33 frames and addons set it on nearly every window they let the player
    /// move; without it a window dragged past the edge is gone for good, since
    /// the only way back is a drag on a title bar that is no longer reachable.
    bool clampedToScreen = false;
    // How far past each screen edge a clamped frame may sit. Positive is
    // inward, so a positive right lets that much hang off and a negative one
    // holds it clear. Only read when clampedToScreen is set.
    float clampInsetL = 0.0f, clampInsetR = 0.0f;
    float clampInsetT = 0.0f, clampInsetB = 0.0f;
    /// Whether clicking this frame brings it to the front of its strata. WoW
    /// calls it toplevel, and it is what stops one window staying buried under
    /// another once two overlap. FrameXML declares it on 102 frames.
    bool topLevel = false;
    /// Whether the frame receives OnKeyDown and OnKeyUp, and whether it lets
    /// the key through afterwards. WoW consumes by default and passes on only
    /// when asked, which is why a dialog swallows the movement keys while it
    /// is up and nothing else does.
    bool keyboardEnabled = false;
    bool propagateKeys = false;
    /// The frame's own scale, and the product of it with every scale above it.
    ///
    /// A frame's width, height and anchor offsets are in its own units, and
    /// WoW multiplies them by the effective scale to place it. Addons lean on
    /// this to fit a panel where it would not otherwise go. When nothing sets a
    /// scale every effScale is 1 and the arithmetic in layoutWidget is a
    /// no-op, so a tree that never scales lays out exactly as it did before.
    /// How far in from each edge the frame actually answers the mouse.
    ///
    /// Positive shrinks, negative expands — WoW's sense. PaperDollFrame is the
    /// case that matters: it covers the whole character sheet and takes the
    /// mouse, and declares 30 off its right and 45 off its bottom so the
    /// transparent parts do not swallow clicks meant for what is behind them.
    float hitInsetLeft = 0.0f, hitInsetRight = 0.0f;
    float hitInsetTop = 0.0f, hitInsetBottom = 0.0f;

    /// Where a running Translation animation has moved the frame to, in
    /// interface units. Kept apart from the anchors so an animation that is
    /// stopped or interrupted leaves no trace in them — nudging the anchors
    /// instead would make every played animation permanent.
    float animOffsetX = 0.0f, animOffsetY = 0.0f;

    /// The rect this frame last reported through OnSizeChanged. Kept on the
    /// widget rather than in a map beside it: the pass runs every frame over
    /// every widget, and a hash lookup each was paying for a comparison that
    /// two floats here answer directly.
    float lastReportedW = -1.0f, lastReportedH = -1.0f;

    /// How far a button's label shifts while the button is held. Declared as
    /// <PushedTextOffset> on 11 templates; it is the small movement that makes
    /// a button feel pressed rather than merely recoloured.
    float pushedTextOffsetX = 0.0f, pushedTextOffsetY = 0.0f;

    /// A button's label colour while the cursor is over it and while it is
    /// disabled, from <HighlightFont> and <DisabledFont>. Unset means draw it
    /// the usual way, so a template that names neither is unaffected.
    bool  hasHighlightColor = false, hasDisabledColor = false;
    float highlightColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float disabledColor[4]  = {0.5f, 0.5f, 0.5f, 1.0f};

    float scale = 1.0f;
    float effScale = 1.0f;
    bool dragLeft = false;
    bool dragRight = false;
    /// Whether a drag put this frame where it is. The single anchor a move
    /// leaves behind has to give way the next time the interface positions the
    /// frame itself: a bag is re-anchored every time it opens, with a bare
    /// SetPoint and no ClearAllPoints, and a leftover anchor on a different
    /// point turns that into two constraints on one axis — which sizes the
    /// frame from them and opens a bag with no width.
    bool userMoved = false;
    /// The facing a model frame was told to show, in radians. FrameXML rotates
    /// a paperdoll by keeping its own running total and calling SetRotation
    /// with it, so this is absolute rather than a delta.
    float modelFacing = 0.0f;
    /// A label whose size came from measuring its own text, and the text that
    /// was measured. Without the second, a label sized once keeps that size
    /// forever — the character sheet's level line stayed the width of the
    /// placeholder its XML shipped with, long after it read something else.
    bool autoSized = false;
    std::string measuredText;

    FrameStrata strata = FrameStrata::Medium;
    bool strataExplicit = false;
    int level = 0;
    bool levelExplicit = false;
    DrawLayer layer = DrawLayer::Artwork;
    int subLevel = 0;

    // Texture regions.
    std::string texturePath;
    float texCoord[4] = {0.0f, 1.0f, 0.0f, 1.0f};   ///< left, right, top, bottom

    /// SetTexCoord's other form: a UV per corner, which is how the interface
    /// rotates a texture. Eight numbers in WoW's order — upper-left,
    /// lower-left, upper-right, lower-right — and only meaningful while
    /// texCoordRotated is set.
    ///
    /// The paperdoll's flyout arrow is the case that matters: the same art
    /// serves the left-hand slots pointing down and the right-hand slots
    /// pointing sideways, and the sideways one is the rotated form. Reading
    /// only the first four numbers of it mapped the art into a 16x38 frame at
    /// the wrong angle, which drew as a pale vertical bar beside the slot.
    bool  texCoordRotated = false;
    float texCoordQuad[8] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
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
    /// A slider shares the bar's range and value but is dragged rather than
    /// filled, and draws a thumb at the value instead of a fill to it.
    bool  isSlider = false;
    /// A cooldown darkens what it covers and wipes clear as the time runs out.
    /// Start is on the same clock GetTime answers with; zero duration means
    /// nothing is running.
    /// An edit box holds its own text and a cursor into it, rather than the
    /// font string a label uses: what is typed has to survive between frames
    /// and the caret has to know where it sits.
    /// A scroll frame shows a window onto a taller child. The child is laid
    /// out at its full size and moved by the scroll offset; what falls outside
    /// the frame is clipped rather than drawn.
    /// Whether the frame asked for the wheel. False by default, as in WoW,
    /// where a frame ignores it until EnableMouseWheel is called — which is
    /// what keeps the wheel zooming the camera everywhere else.
    bool  wheelEnabled = false;
    /// A disabled button is greyed and takes no clicks. True by default, as a
    /// button is until something disables it.
    bool  enabled = true;
    /// Whether this region is one of its owner's state textures, and which.
    ButtonArt buttonArt = ButtonArt::None;
    /// Whether a check button is checked, which decides between its checked
    /// art and none.
    bool  checked = false;
    /// A state the interface asked for outright, overriding what the mouse is
    /// doing. ActionButton_UpdateState holds a toggled ability's button down
    /// this way, and nothing about the cursor should undo that.
    enum class Forced : uint8_t { None, Normal, Pushed, Disabled };
    Forced forcedState = Forced::None;
    /// Highlight held on regardless of the cursor, which is how a selected tab
    /// stays lit once it has been clicked.
    bool  highlightLocked = false;

    bool  isScrollFrame = false;
    uint32_t scrollChild = 0;
    float scrollX = 0.0f, scrollY = 0.0f;
    /// The range last reported to the interface. A scroll bar sizes and
    /// enables itself from OnScrollRangeChanged, so the change has to be
    /// noticed and announced rather than merely being true.
    float reportedRangeX = -1.0f, reportedRangeY = -1.0f;

    bool  isEditBox = false;
    /// Whether this box takes the keyboard the moment it appears.
    ///
    /// Declared on 42 boxes in FrameXML and false on 40 of them, which is the
    /// point of the attribute — a box that grabs focus on sight takes the
    /// keyboard away from whatever the player was doing. The two that ask for
    /// it are the channel-name field and the box that names an equipment set,
    /// where typing is the only reason the dialog opened.
    bool  editAutoFocus = false;
    /// Where an edit box's text starts inside its own frame. WoW's default is
    /// nothing; the four units used here until now were a stand-in, and a box
    /// whose art has a wide border drew its text on top of it.
    float textInsetLeft = 4.0f, textInsetRight = 4.0f;
    float textInsetTop = 0.0f, textInsetBottom = 0.0f;
    std::string editText;
    size_t cursorPos = 0;
    /// What has been typed into this box before, oldest first, and where the
    /// arrow keys are in it.
    ///
    /// FrameXML hands each sent line to AddHistoryLine and then leaves the
    /// walking of it to the client, which is why the chat box recalls what you
    /// typed in the real game and did nothing here. -1 means "not in the
    /// history", which is the state the box returns to at the end of it.
    std::vector<std::string> editHistory;
    int editHistoryPos = -1;
    /// How many the box was declared to keep, from historyLines. Zero means
    /// none, which several boxes ask for outright.
    int editHistoryLines = 0;
    /// Declared with ignoreArrows: left and right do not move the cursor, so
    /// they reach the game instead and a player can turn while typing.
    bool editIgnoreArrows = false;
    bool  editFocused = false;
    bool  editNumeric = false;
    bool  editMultiLine = false;
    int   editMaxLetters = 0;   ///< Zero is no limit, which is WoW's default.

    /// Whether a label too long for its box breaks onto another line. On, as
    /// WoW has it — a FontString given a width wraps inside it, and nothing
    /// here did, so every one of them drew a single line straight out of its
    /// own frame.
    bool  wordWrap = true;
    /// Break inside a word when a single word is wider than the box. Thirty-six
    /// labels ask for it, all of them prose in a narrow column.
    bool  nonSpaceWrap = false;
    /// Lines the last wrap produced, so the height a wrapped label reports
    /// matches what is drawn. FrameXML sizes panels from GetStringHeight.
    ///
    /// Mutable because it is a record of what the renderer did, not part of
    /// what the frame is — the draw pass holds every widget const and this is
    /// the one thing it learns.
    mutable int wrappedLines = 1;

    bool  isCooldown = false;
    double cooldownStart = 0.0;
    double cooldownDuration = 0.0;
    /// The shaded wedge grows instead of shrinking. An ability's cooldown
    /// shrinks as it becomes usable again; an aura's timer on a unit frame
    /// grows as the aura runs out, and the target frame and the totem bar both
    /// ask for that.
    bool  cooldownReverse = false;
    /// A bright line along the sweeping edge, which the target frame and the
    /// rune bar ask for.
    bool  cooldownDrawEdge = false;
    float sliderStep = 0.0f;
    std::string thumbTexture;
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

    /// A scrolling message frame keeps its own lines rather than a single
    /// string: chat is a list that grows at one end and falls off the other,
    /// and the frame draws as many as fit from the bottom up.
    bool  isMessageFrame = false;
    struct Message { std::string text; float color[4]; float age = 0.0f; };
    std::deque<Message> messages;
    /// How long a message stays before it fades out, in seconds, and how long
    /// the fade itself takes. Zero means it never goes — which is what a chat
    /// frame wants and what UIErrorsFrame very much does not: it declares
    /// displayDuration="5" and every error was staying on screen for good.
    float messageDuration = 0.0f;
    float messageFadeDuration = 3.0f;
    /// Whether a new message goes above the ones already there. UIErrorsFrame
    /// asks for insertMode="TOP"; a chat frame does not.
    bool  messagesInsertTop = false;
    /// Extra space between message lines, which SetPadding sets. WoW's default
    /// is none; the 15% used here is the leading a line already carries.
    float messagePadding = 0.0f;
    size_t maxMessages = 128;
    /// How far back through the history the frame has been scrolled, in lines.
    int   messageScroll = 0;

    /// A tooltip holds lines the same way, but draws them from the top and
    /// sizes itself to fit them — which is the part a chat frame does not do,
    /// because a tooltip has no size of its own until it has something to say.
    bool  isTooltip = false;
    struct TooltipLine {
        std::string left, right;
        float lc[4]; float rc[4];
        /// Whether this line breaks to fit rather than making the whole
        /// tooltip as wide as itself. FrameXML asks for it on every line of
        /// prose — an item's flavour text, a spell's description, a newbie
        /// tip — and the flag was read off the call and dropped, so one long
        /// sentence stretched the tooltip across the screen.
        bool wrap = false;
        /// Lines the wrap produced, filled by the sizing pass so the draw and
        /// the height agree on how tall this line is.
        mutable int lines = 1;
    };
    std::vector<TooltipLine> tooltipLines;
    /// A floor on the width the sizing pass may settle on, from
    /// SetMinimumWidth. FrameXML uses it to keep a money frame from being
    /// clipped by a tooltip narrower than the coins it is about to draw:
    /// SetTooltipMoney measures the money frame, asks the tooltip whether it is
    /// already that wide, and widens it if not.
    float tooltipMinWidth = 0.0f;

    // FontString regions.
    std::string text;
    float fontHeight = 12.0f;
    /// The colour a ColorSelect frame is showing, as r, g, b. Its own state
    /// rather than the frame's tint: a colour picker draws its wheel in every
    /// colour and this is only the one being chosen.
    float pickerColor[3] = {1.0f, 1.0f, 1.0f};
    /// The same colour as hue, saturation and value, which is what the wheel
    /// and the bar actually move. Kept beside the RGB rather than derived from
    /// it because the conversion is lossy in exactly the places a picker sits:
    /// black has no hue and grey has no saturation, so dragging the value bar
    /// down to zero and back would lose the colour that was being chosen.
    float pickerHSV[3] = {0.0f, 0.0f, 1.0f};

    /// Which part of a colour picker this region is, if any. The four are
    /// declared in XML as their own elements rather than inside a Layer —
    /// <ColorWheelTexture> and friends — and each is placed and drawn from the
    /// colour its ColorSelect parent holds.
    enum class ColorRole : uint8_t { None, Wheel, WheelThumb, Value, ValueThumb };
    ColorRole colorRole = ColorRole::None;

    /// Which named font object this region's type settings came from, so
    /// GetFontObject can hand it back. The fields copied out of one do not add
    /// up to the object itself, and FrameXML passes the object around — the
    /// options panels read a control's font object to get the colour to put a
    /// label back to when the control is re-enabled.
    std::string fontObjectName;
    /// Drawn added to what is under it rather than over it. Art authored for
    /// this has no alpha channel at all — it is a glow on black, and black is
    /// what adds nothing. Drawn the ordinary way it is a black slab instead,
    /// which is what covered the player frame while it pulsed.
    bool blendAdd = false;

    /// A texture the client renders rather than one read from a file — a unit
    /// portrait is a live view of the character, not an image on disk. Zero
    /// means the path above is used instead.
    uint64_t externalTexture = 0;

    /// The typeface a font object named, as it wrote it. Empty means
    /// whatever the renderer is already using.
    std::string fontFace;
    /// A dark copy of the text, offset. Distinct from the outline, which
    /// surrounds the glyphs on all sides; a shadow falls on one.
    bool  hasShadow = false;
    float shadowX = 1.0f, shadowY = -1.0f;
    float shadowColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    /// NORMAL or THICK, as a font object writes it. Empty is no outline.
    std::string fontOutline;
    /// Extra space between wrapped lines, which FrameXML reads back.
    float lineSpacing = 0.0f;
    std::string justifyH = "CENTER";
    /// TOP, MIDDLE or BOTTOM. FrameXML declares it on 93 font strings — a
    /// multi-line label in a fixed box sits differently for each.
    std::string justifyV = "MIDDLE";

    /// Minimap zoom step, 0 to 4. Kept here rather than in Lua because the
    /// interface sets it through one button and reads it back through another.
    int zoomLevel = 0;

    // Filled in by layout(). Screen rect in WoW coordinates: origin bottom-left.
    float left = 0.0f, bottom = 0.0f, rectW = 0.0f, rectH = 0.0f;
    /// Shown, with every ancestor shown. This is what WoW's IsVisible answers
    /// and what decides whether an OnUpdate runs.
    ///
    /// Distinct from `visible` below, which is this AND has somewhere to be
    /// drawn. A frame with no anchors is not drawn — that is WoW's rule and
    /// the reason a stray unanchored panel does not land in the middle of the
    /// screen — but it is still running. Eight of FrameXML's driver frames are
    /// exactly that: created by CreateFrame, never positioned, existing only
    /// to carry an OnUpdate. frameFadeManager drives every fade in the
    /// interface, frameFlashManager every flash, AnimUpdateFrame the whole
    /// animation system.
    bool  visibleChain = false;
    bool  visible = false;      ///< visibleChain, and anchored somewhere
    /// Whether the interface has been told this is on screen. Visibility is
    /// not a property a frame sets — it is shown, and every ancestor shown too
    /// — so becoming visible has to be noticed rather than announced at the
    /// point something was hidden three levels up.
    bool  reportedVisible = false;
    /// The nearest scroll frame above this one, or zero. Everything under a
    /// scroll frame is drawn clipped to it, which is what makes a window onto
    /// a taller child a window rather than a spill.
    uint32_t clipTo = 0;
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

    /// Set a size, and have it read back before the next layout.
    ///
    /// FrameXML sizes things and measures them in the same breath: a container
    /// frame sets the height of each piece of its background art and then adds
    /// those heights up to size itself. Storing only the requested size and
    /// answering GetHeight from the last laid-out rect makes that sum the
    /// *previous* frame's numbers, so the art and the buttons inside it end up
    /// describing two different frames.
    void setWidth(uint32_t id, float width);
    void setHeight(uint32_t id, float height);

    /// Pin a frame where it currently sits, on one anchor to its parent.
    ///
    /// What StartMoving does before the cursor takes over: a frame anchored to
    /// something else cannot be dragged without the drag fighting the anchor,
    /// so the anchors are replaced by a single one describing where it is now.
    void pinToCurrentPosition(uint32_t id);

    /// Shift every anchor by the same amount, which moves the frame.
    void nudge(uint32_t id, float dx, float dy);

    /// Put a frame in front of everything else in its strata.
    ///
    /// Strata come first in the draw order, so this only moves the frame within
    /// its own — a DIALOG frame raised above its peers still sits under a
    /// TOOLTIP one, which is what the strata are for.
    void raise(uint32_t id);
    /// The reverse, for Lower().
    void lower(uint32_t id);
    /// Shift every descendant that carries its own level, so a raised window
    /// takes what is inside it along. Public because SetFrameLevel needs the
    /// same behaviour.
    void shiftExplicitLevels(uint32_t id, int delta);

    /// The frame currently following the cursor, if any.
    ///
    /// StartMoving and StopMovingOrSizing are called from Lua, and the cursor
    /// is read by the input loop, so the two need somewhere to meet. It lives
    /// here beside the pressed and hovered frames rather than in the input
    /// loop, because that is what the bindings can already reach.
    uint32_t movingWidget() const { return movingWid_; }
    void setMovingWidget(uint32_t id) { movingWid_ = id; }

    /// The frame a size grabber is dragging, and which corner it took hold of.
    ///
    /// Same arrangement as the moving frame above and for the same reason:
    /// StartSizing is called from Lua and the cursor is read by the input loop.
    /// The corner matters because dragging the left edge has to move the frame
    /// as well as resize it — its right edge must stay where it is.
    uint32_t sizingWidget() const { return sizingWid_; }
    const std::string& sizingPoint() const { return sizingPoint_; }
    void setSizingWidget(uint32_t id, const std::string& point) {
        sizingWid_ = id;
        sizingPoint_ = point;
    }

    /// Grow or shrink a frame being sized, by a cursor delta in interface
    /// units. Held inside SetMinResize/SetMaxResize.
    void resizeBy(uint32_t id, const std::string& point, float dx, float dy);
    void addPoint(uint32_t id, const Anchor& anchor);
    void setAllPoints(uint32_t id, uint32_t relativeTo);

    /// Resolve every widget's rect and visibility for a screen of this size.
    /// Lays the tree out for a window of this many pixels.
    ///
    /// FrameXML's coordinates are not pixels. The interface is authored against
    /// a virtual screen 768 units tall — a 232x100 unit frame is meant to look
    /// the same size on every display — so the tree is laid out in those units
    /// and the renderer multiplies by the scale on the way to the screen.
    /// Treating them as pixels drew the whole interface at half size on a
    /// 1528-tall window and at double on a 384-tall one.
    void layout(float pixelW, float pixelH);

    /// Pixels per interface unit, from the last layout.
    float uiScale() const { return uiScale_; }

    /// The screen-filling frame everything else hangs off.
    uint32_t rootId() const { return rootId_; }

    /// Records a frame as a scroll frame, and keeps the list of them. Walking
    /// every widget each frame to find a handful is the kind of cost that does
    /// not show up until the interface is large, which it now is.
    /// Move a frame under a different parent.
    ///
    /// SetParent wrote a field on the Lua table and nothing else, so GetParent
    /// answered the new parent while the frame went on being laid out, clipped
    /// and shown or hidden by the old one. QuestInfo reparents every element of
    /// a quest into either the quest giver's panel or the quest log's scroll
    /// child on each display, and the consolidated buff container moves buffs
    /// in and out of itself, so both were placing frames that stayed where they
    /// were.
    ///
    /// Everything inherited — visibility, clipping, strata, level, scale — is
    /// resolved from the parent during layout, so this only has to fix the link
    /// and the two children lists.
    void setParent(uint32_t id, uint32_t newParent);

    void markScrollFrame(uint32_t id);

    /// Remember that a texture is meant to show the player's portrait.
    ///
    /// SetPortraitTexture names the texture to fill, and the handle it should
    /// be filled with is rebuilt whenever the portrait's render target is —
    /// so the assignment has to happen every frame rather than once here. A
    /// list, for the same reason scroll frames are a list: the alternative is
    /// scanning every widget in the tree for a flag, every frame.
    void markPlayerPortrait(uint32_t id);
    void unmarkPlayerPortrait(uint32_t id);

    /// What the mouse is doing, so state art can be chosen. The engine owns
    /// this — it is the only thing that knows what is under the cursor and
    /// what is being held — and the tree needs it to decide which of a
    /// button's textures to draw.
    void setInteraction(uint32_t hovered, uint32_t pressed) {
        hoveredId_ = hovered;
        pressedId_ = pressed;
    }
    /// Which frame is being held, for anything that draws differently while a
    /// button is down. The tree already chooses the pushed texture from this;
    /// the label moves with it.
    uint32_t pressedWidget() const { return pressedId_; }
    /// Which frame the cursor is over, for anything that draws differently
    /// under it — a button's label lightens in WoW, and that is a font object
    /// the template names rather than a colour the renderer invents.
    uint32_t hoveredWidget() const { return hoveredId_; }
    const std::vector<uint32_t>& scrollFrames() const { return scrollFrames_; }
    const std::vector<uint32_t>& playerPortraits() const { return playerPortraits_; }

    /// The widget published under this name, or null. Names are unique in
    /// FrameXML by convention, and the last one to claim a name wins, which is
    /// what a lookup by name means there too.
    Widget* findByName(std::string_view name);
    const Widget* findByName(std::string_view name) const;

    /// The height the interface is authored against. Blizzard's own number.
    static constexpr float kInterfaceHeight = 768.0f;

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
    float uiScale_ = 1.0f;
    std::vector<uint32_t> scrollFrames_;
    std::vector<uint32_t> playerPortraits_;
    uint32_t hoveredId_ = 0;
    uint32_t pressedId_ = 0;

    /// Whether a state texture should be drawn given what the mouse is doing.
    bool buttonArtVisible(const Widget& w) const;
    std::deque<Widget> widgets_;   ///< Index 0 is a placeholder; id == index.
    uint32_t rootId_ = 0;
    uint32_t movingWid_ = 0;
    uint32_t sizingWid_ = 0;
    std::string sizingPoint_;
    uint32_t nextOrder_ = 1;
    std::vector<const Widget*> drawOrder_;
};

/// Hue-saturation-value to red-green-blue and back, with hue in turns rather
/// than degrees because everything that reads it here is an angle around a
/// wheel or a fraction of one.
///
/// Free functions beside the tree rather than inside the picker, because three
/// places need them and they are the sort of arithmetic that gets written
/// slightly differently each time it is written again: the binding converting
/// a colour it was handed, the renderer drawing every hue into the wheel, and
/// the input mapping a cursor back onto it.
void hsvToRgb(const float hsv[3], float rgb[3]);
void rgbToHsv(const float rgb[3], float hsv[3]);

} // namespace ui
} // namespace wowee
