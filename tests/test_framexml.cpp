#include <catch_amalgamated.hpp>

#include "ui/xml_parser.hpp"
#include "ui/framexml_emitter.hpp"

#include <string>

using namespace wowee::ui;

namespace {
bool has(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
XmlNode parseOrFail(const std::string& src) {
    XmlNode root;
    std::string err;
    REQUIRE(parseXml(src, root, err));
    INFO(err);
    return root;
}
}

// ── Parser ──────────────────────────────────────────────────────────────────

TEST_CASE("Attributes, nesting and self-closing elements", "[framexml][xml]") {
    XmlNode root = parseOrFail(
        "<Ui><Frame name='A' hidden='true'><Size><AbsDimension x='10' y='20'/></Size></Frame></Ui>");
    REQUIRE(root.name == "Ui");
    REQUIRE(root.children.size() == 1);
    const XmlNode& f = root.children[0];
    REQUIRE(f.name == "Frame");
    REQUIRE(f.attrOr("name", "") == "A");
    REQUIRE(f.attrBool("hidden"));
    const XmlNode* dim = f.child("Size")->child("AbsDimension");
    REQUIRE(dim != nullptr);
    REQUIRE(dim->attrFloat("x") == Catch::Approx(10.0f));
    REQUIRE(dim->attrFloat("y") == Catch::Approx(20.0f));
}

TEST_CASE("The declaration and comments are skipped", "[framexml][xml]") {
    XmlNode root = parseOrFail(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!-- a comment, possibly with <tags> inside -->\n"
        "<Ui><!-- another --><Frame name=\"A\"/></Ui>");
    REQUIRE(root.name == "Ui");
    REQUIRE(root.children.size() == 1);
    REQUIRE(root.children[0].attrOr("name", "") == "A");
}

TEST_CASE("CDATA is taken verbatim", "[framexml][xml]") {
    // The reason CDATA matters: this is Lua, and decoding entities inside it
    // would turn every comparison into something that will not compile.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"A\"><Scripts><OnLoad><![CDATA[\n"
        "if a < b and c > d then self:Show() end\n"
        "]]></OnLoad></Scripts></Frame></Ui>");
    const XmlNode* onLoad = root.children[0].child("Scripts")->child("OnLoad");
    REQUIRE(onLoad != nullptr);
    REQUIRE(has(onLoad->text, "a < b and c > d"));
}

TEST_CASE("Entities decode outside CDATA", "[framexml][xml]") {
    XmlNode root = parseOrFail("<Ui><Frame text=\"Fish &amp; Chips &lt;3\"/></Ui>");
    REQUIRE(root.children[0].attrOr("text", "") == "Fish & Chips <3");
}

TEST_CASE("Malformed input is reported, not thrown", "[framexml][xml]") {
    // One bad file among a hundred must not take the rest of the interface down.
    XmlNode root;
    std::string err;
    REQUIRE_FALSE(parseXml("<Ui><Frame></Ui>", root, err));
    REQUIRE_FALSE(err.empty());

    REQUIRE_FALSE(parseXml("", root, err));
    REQUIRE_FALSE(parseXml("<Ui><Frame name=unquoted/></Ui>", root, err));
}

// ── Emitter ─────────────────────────────────────────────────────────────────

TEST_CASE("A frame emits CreateFrame with its type and parent", "[framexml][emit]") {
    XmlNode root = parseOrFail("<Ui><Button name=\"MyButton\" parent=\"UIParent\"/></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "CreateFrame(\"Button\", \"MyButton\", UIParent)"));
}

TEST_CASE("Size and anchors become the same calls a script would make",
          "[framexml][emit]") {
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\">"
        "<Size><AbsDimension x=\"128\" y=\"64\"/></Size>"
        "<Anchors><Anchor point=\"TOPLEFT\" relativePoint=\"BOTTOMRIGHT\">"
        "<Offset><AbsDimension x=\"5\" y=\"-7\"/></Offset></Anchor></Anchors>"
        "</Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, ":SetSize(128"));
    REQUIRE(has(r.lua, ":SetPoint(\"TOPLEFT\""));
    REQUIRE(has(r.lua, "\"BOTTOMRIGHT\""));
    REQUIRE(has(r.lua, "5.000000, -7.000000"));
}

TEST_CASE("$parent expands against the frame that owns the region",
          "[framexml][emit]") {
    // Nearly every region in the original interface is named this way, and
    // getting it wrong means none of them are reachable by name.
    REQUIRE(substituteParent("$parentText", "FooFrame") == "FooFrameText");
    REQUIRE(substituteParent("PlainName", "FooFrame") == "PlainName");
    REQUIRE(substituteParent("", "FooFrame").empty());

    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"FooFrame\"><Layers><Layer level=\"BACKGROUND\">"
        "<Texture name=\"$parentBg\" file=\"Interface\\Foo\" setAllPoints=\"true\"/>"
        "</Layer></Layers></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "CreateTexture(\"FooFrameBg\", \"BACKGROUND\")"));
    REQUIRE(has(r.lua, "SetTexture(\"Interface\\\\Foo\")"));
    REQUIRE(has(r.lua, ":SetAllPoints("));
}

TEST_CASE("A virtual frame becomes a template rather than a frame",
          "[framexml][emit]") {
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"MyTemplate\" virtual=\"true\">"
        "<Size><AbsDimension x=\"32\" y=\"32\"/></Size></Frame>"
        "<Frame name=\"Real\" inherits=\"MyTemplate\"/></Ui>");
    const EmitResult r = emitFrameXml(root);

    REQUIRE(has(r.lua, "__WoweeTemplates[\"MyTemplate\"] = function(self)"));
    // The template must not create a frame of its own; that is the whole
    // meaning of virtual.
    REQUIRE_FALSE(has(r.lua, "CreateFrame(\"Frame\", \"MyTemplate\""));
    REQUIRE(has(r.lua, "CreateFrame(\"Frame\", \"Real\""));
    REQUIRE(has(r.lua, "__WoweeTemplates[\"MyTemplate\"]("));
}

TEST_CASE("Inheriting several templates applies them in order", "[framexml][emit]") {
    XmlNode root = parseOrFail("<Ui><Frame name=\"R\" inherits=\"A, B\"/></Ui>");
    const EmitResult r = emitFrameXml(root);
    const size_t a = r.lua.find("__WoweeTemplates[\"A\"](");
    const size_t b = r.lua.find("__WoweeTemplates[\"B\"](");
    REQUIRE(a != std::string::npos);
    REQUIRE(b != std::string::npos);
    REQUIRE(a < b);
}

TEST_CASE("Scripts bind both inline bodies and named functions",
          "[framexml][emit]") {
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Scripts>"
        "<OnLoad><![CDATA[ self:SetAlpha(0.5) ]]></OnLoad>"
        "<OnClick function=\"MyHandler\"/>"
        "</Scripts></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, ":SetScript(\"OnLoad\", function(self, ...)"));
    REQUIRE(has(r.lua, "self:SetAlpha(0.5)"));
    REQUIRE(has(r.lua, ":SetScript(\"OnClick\", MyHandler)"));
    // OnLoad is expected to run once the frame is built, which is what every
    // handler in FrameXML assumes about itself.
    REQUIRE(has(r.lua, "GetScript(\"OnLoad\")"));
}

TEST_CASE("Referenced files are reported rather than loaded", "[framexml][emit]") {
    XmlNode root = parseOrFail(
        "<Ui><Script file=\"Foo.lua\"/><Include file=\"Bar.xml\"/></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(r.scriptFiles.size() == 1);
    REQUIRE(r.scriptFiles[0] == "Foo.lua");
    REQUIRE(r.includeFiles.size() == 1);
    REQUIRE(r.includeFiles[0] == "Bar.xml");
}

TEST_CASE("Nested frames are parented to the frame containing them",
          "[framexml][emit]") {
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"Outer\"><Frames>"
        "<Button name=\"$parentBtn\"/>"
        "</Frames></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "CreateFrame(\"Button\", \"OuterBtn\", __w[1])"));
}

TEST_CASE("Strings that reach Lua are escaped", "[framexml][emit]") {
    // Texture paths are full of backslashes, and a quote in label text would
    // otherwise end the string and leave the rest as code.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Layers><Layer>"
        "<FontString text=\"He said &quot;hi&quot;\"/>"
        "<Texture file=\"Interface\\Icons\\Foo\"/>"
        "</Layer></Layers></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "\\\"hi\\\""));
    REQUIRE(has(r.lua, "Interface\\\\Icons\\\\Foo"));
}

TEST_CASE("$parent inside a template resolves to the frame that inherits it",
          "[framexml][emit]") {
    // The subtlety that makes templates work at all. A region named $parentBg in
    // a template must become FooFrameBg on the frame inheriting it, not
    // TemplateNameBg — the template's own name is never the answer, and every
    // frame sharing that template would collide on it if it were.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"MyTemplate\" virtual=\"true\"><Layers><Layer>"
        "<Texture name=\"$parentBg\" setAllPoints=\"true\"/>"
        "</Layer></Layers></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);

    REQUIRE(has(r.lua, "GetName()"));
    REQUIRE(has(r.lua, "\"Bg\""));
    REQUIRE_FALSE(has(r.lua, "\"MyTemplateBg\""));
}

TEST_CASE("Button art declared outside a Layer is still created",
          "[framexml][emit]") {
    // <NormalTexture> and <ButtonText> are regions like any other, just
    // declared as their own element with an implied layer and a setter. The
    // emitter ignored all of them, so the names they declare never existed —
    // _G["DropDownList1Button1NormalText"] among them, which is what stopped
    // UIDropDownMenu loading. The highlight belongs on its own layer, and the
    // label above the art rather than under it.
    XmlNode root = parseOrFail(
        "<Ui><Button name=\"MyButton\">"
        "<NormalTexture name=\"$parentNormalTexture\" file=\"Art\\\\Face\"/>"
        "<HighlightTexture name=\"$parentHighlight\"/>"
        "<ButtonText name=\"$parentNormalText\"/>"
        "</Button></Ui>");
    const EmitResult r = emitFrameXml(root);

    REQUIRE(has(r.lua, "\"MyButtonNormalTexture\""));
    REQUIRE(has(r.lua, "\"MyButtonNormalText\""));
    REQUIRE(has(r.lua, ":SetNormalTexture("));
    REQUIRE(has(r.lua, ":SetFontString("));
    REQUIRE(has(r.lua, "\"HIGHLIGHT\""));
    REQUIRE(has(r.lua, "\"OVERLAY\""));
    // A font string, not a texture — the element name does not say so.
    REQUIRE(has(r.lua, "CreateFontString(\"MyButtonNormalText\""));
}

TEST_CASE("A nested frame in a template also resolves $parent at replay time",
          "[framexml][emit]") {
    // The same rule as the region above, and it was the region that had it. A
    // child frame named $parentScrollBar was emitted with the template's own
    // name baked in, so every scroll frame in FrameXML created a global called
    // UIPanelScrollFrameTemplateScrollBar and overwrote the last one, while the
    // _G[self:GetName().."ScrollBar"] its handlers look up never existed. That
    // one line accounted for most of the files that would not load.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"MyTemplate\" virtual=\"true\"><Frames>"
        "<Frame name=\"$parentScrollBar\"/>"
        "</Frames></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);

    REQUIRE(has(r.lua, "GetName()"));
    REQUIRE(has(r.lua, "\"ScrollBar\""));
    REQUIRE_FALSE(has(r.lua, "\"MyTemplateScrollBar\""));
}

TEST_CASE("Outside a template $parent is resolved when emitted",
          "[framexml][emit]") {
    // No reason to defer it where the owning frame is already known; a literal
    // keeps the generated code readable.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"FooFrame\"><Layers><Layer>"
        "<Texture name=\"$parentBg\"/></Layer></Layers></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "\"FooFrameBg\""));
    REQUIRE_FALSE(has(r.lua, "GetName()"));
}

TEST_CASE("A nested frame anchors to its container, not the screen",
          "[framexml][emit]") {
    // FrameXML nests constantly, and an unqualified anchor means "my parent".
    // Reading it as UIParent puts anything inside a panel somewhere else on the
    // screen entirely.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"Outer\"><Frames>"
        "<StatusBar name=\"$parentBar\"><Anchors>"
        "<Anchor point=\"BOTTOMLEFT\" relativePoint=\"BOTTOMLEFT\">"
        "<Offset><AbsDimension x=\"5\" y=\"5\"/></Offset></Anchor>"
        "</Anchors></StatusBar>"
        "</Frames></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "CreateFrame(\"StatusBar\", \"OuterBar\", __w[1])"));
    REQUIRE(has(r.lua, ":SetPoint(\"BOTTOMLEFT\", __w[1],"));
    REQUIRE_FALSE(has(r.lua, ":SetPoint(\"BOTTOMLEFT\", UIParent,"));
}

TEST_CASE("An anchor inside a template resolves its parent at replay time",
          "[framexml][emit]") {
    // The containing frame is not known while emitting a template — it is
    // whichever frame inherits it — so the parent has to be asked for then.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"T\" virtual=\"true\"><Anchors>"
        "<Anchor point=\"CENTER\"/></Anchors></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "self:GetParent()"));
}

TEST_CASE("A FontString's inherits names a font object, not a template",
          "[framexml][emit]") {
    // Frames inherit templates; FontStrings inherit shared font objects, and
    // that is where their size and colour come from. FrameXML does it more than
    // three thousand times, so treating it as a template would leave every
    // label the same size in the same colour.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Layers><Layer>"
        "<FontString name=\"$parentT\" inherits=\"GameFontNormalLarge\" text=\"Hi\"/>"
        "</Layer></Layers></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, ":SetFontObject(\"GameFontNormalLarge\")"));
    REQUIRE_FALSE(has(r.lua, "__WoweeTemplates[\"GameFontNormalLarge\"]"));
}

TEST_CASE("A Texture's inherits is not treated as a font object",
          "[framexml][emit]") {
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Layers><Layer>"
        "<Texture name=\"$parentTex\" inherits=\"SomeTextureTemplate\"/>"
        "</Layer></Layers></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE_FALSE(has(r.lua, "SetFontObject"));
}

TEST_CASE("Handler bodies get their arguments by name", "[framexml][emit]") {
    // Blizzard's inline scripts use their argument names without declaring
    // them. Passed positionally instead, an OnUpdate body's `elapsed` is nil
    // and the first arithmetic on it fails — which is most of FrameXML's
    // OnUpdate handlers.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Scripts>"
        "<OnUpdate><![CDATA[ self.t = (self.t or 0) + elapsed ]]></OnUpdate>"
        "<OnClick><![CDATA[ if button == \"LeftButton\" then self:Hide() end ]]></OnClick>"
        "<OnEvent><![CDATA[ if event == \"PLAYER_LOGIN\" then self:Show() end ]]></OnEvent>"
        "<OnValueChanged><![CDATA[ self:SetAlpha(value) ]]></OnValueChanged>"
        "</Scripts></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);

    REQUIRE(has(r.lua, "function(self, elapsed, ...)"));
    REQUIRE(has(r.lua, "function(self, button, down, ...)"));
    REQUIRE(has(r.lua, "function(self, event,"));
    REQUIRE(has(r.lua, "function(self, value, ...)"));
    REQUIRE_FALSE(has(r.lua, "local arg1, arg2, arg3, arg4 = ..."));
}

TEST_CASE("A handler with no named arguments still takes self",
          "[framexml][emit]") {
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Scripts>"
        "<OnShow><![CDATA[ self:SetAlpha(1) ]]></OnShow>"
        "</Scripts></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "function(self, ...)"));
}

TEST_CASE("Every handler is vararg whatever its named arguments",
          "[framexml][emit]") {
    // A body is free to use `...` whatever handler it belongs to, and a
    // parameter list without it does not merely lose the values — it fails to
    // compile, taking the whole template with it.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Scripts>"
        "<OnEvent><![CDATA[ local a, b = ...; self:SetAlpha(1) ]]></OnEvent>"
        "<OnUpdate><![CDATA[ local x = select(1, ...) ]]></OnUpdate>"
        "</Scripts></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE(has(r.lua, "function(self, elapsed, ...)"));
    REQUIRE(has(r.lua, "function(self, event,"));
    // Both signatures end in varargs.
    size_t at = 0, sigs = 0;
    while ((at = r.lua.find(", ...)", at)) != std::string::npos) { ++sigs; at += 5; }
    REQUIRE(sigs >= 2);
}

TEST_CASE("A $parent relativeTo becomes a name, not a bare symbol",
          "[framexml][emit]") {
    // $parentBg is not a Lua identifier. Pasted in as one it is a syntax error,
    // which does not lose the anchor — it loses the whole file.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"FooFrame\"><Layers><Layer>"
        "<Texture name=\"$parentBg\"/>"
        "<Texture name=\"$parentIcon\"><Anchors>"
        "<Anchor point=\"LEFT\" relativeTo=\"$parentBg\" relativePoint=\"RIGHT\"/>"
        "</Anchors></Texture>"
        "</Layer></Layers></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE_FALSE(has(r.lua, "$parent"));
    REQUIRE(has(r.lua, "\"FooFrameBg\""));
}

TEST_CASE("Temporaries do not run into Lua's local-variable limit",
          "[framexml][emit]") {
    // Lua allows 200 locals per function. A large file declares far more
    // widgets than that, and going over does not degrade — the whole chunk
    // refuses to compile. FriendsFrame and InterfaceOptionsPanels both did.
    std::string xml = "<Ui><Frame name=\"Big\"><Layers><Layer>";
    for (int i = 0; i < 300; ++i) {
        xml += "<Texture name=\"$parentT" + std::to_string(i) + "\"/>";
    }
    xml += "</Layer></Layers></Frame></Ui>";
    XmlNode root = parseOrFail(xml);
    const EmitResult r = emitFrameXml(root);

    // One table, not three hundred locals.
    REQUIRE(has(r.lua, "local __w = {}"));
    size_t locals = 0, at = 0;
    while ((at = r.lua.find("local ", at)) != std::string::npos) { ++locals; at += 6; }
    REQUIRE(locals == 1);
}

TEST_CASE("An empty function attribute is not emitted as a handler name",
          "[framexml][emit]") {
    // SetScript("X", ) is a syntax error, so this loses the whole file rather
    // than the one handler.
    XmlNode root = parseOrFail(
        "<Ui><Frame name=\"F\"><Scripts>"
        "<OnMouseWheel function=\"\"/>"
        "<OnShow function=\"RealHandler\"/>"
        "</Scripts></Frame></Ui>");
    const EmitResult r = emitFrameXml(root);
    REQUIRE_FALSE(has(r.lua, "SetScript(\"OnMouseWheel\", )"));
    REQUIRE(has(r.lua, "SetScript(\"OnShow\", RealHandler)"));
}
