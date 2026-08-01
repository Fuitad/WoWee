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
    REQUIRE(has(r.lua, "CreateFrame(\"Button\", \"OuterBtn\", __x0)"));
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
