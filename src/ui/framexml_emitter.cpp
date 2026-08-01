#include "ui/framexml_emitter.hpp"

#include "ui/xml_parser.hpp"

#include <algorithm>
#include <sstream>

namespace wowee {
namespace ui {

namespace {

/// Element names that produce a frame rather than a region. Everything here is
/// created through CreateFrame with its own type, so a Button gets a Button's
/// behaviour even where the widget system does not yet distinguish them.
bool isFrameElement(const std::string& n) {
    static const char* kFrames[] = {
        "Frame", "Button", "CheckButton", "StatusBar", "Slider", "EditBox",
        "ScrollFrame", "ScrollingMessageFrame", "MessageFrame", "SimpleHTML",
        "ColorSelect", "Model", "PlayerModel", "DressUpModel", "TabardModel",
        "Cooldown", "GameTooltip", "MovieFrame", "ArchaeologyDigSiteFrame"
    };
    for (const char* f : kFrames) if (n == f) return true;
    return false;
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    out += "\"";
    return out;
}

/// A <Size> or <Offset> can be written as a child <AbsDimension x= y=> or, in
/// later files, as attributes directly on the element.
bool readDimension(const XmlNode& node, float& x, float& y) {
    if (const XmlNode* abs = node.child("AbsDimension")) {
        x = abs->attrFloat("x", 0.0f);
        y = abs->attrFloat("y", 0.0f);
        return true;
    }
    if (node.attr("x") || node.attr("y")) {
        x = node.attrFloat("x", 0.0f);
        y = node.attrFloat("y", 0.0f);
        return true;
    }
    return false;
}

struct Emitter {
    EmitResult result;
    int temp = 0;
    /// True while emitting a template body. Inside one the owning frame is not
    /// known until the template is replayed, so $parent has to be resolved then
    /// rather than now.
    bool runtimeParentName = false;

    std::string nextVar() { return "__x" + std::to_string(temp++); }

    /// The Lua expression for a region or frame's name. A literal where the
    /// owning frame is known, and a concatenation against the real frame's name
    /// where it is not — which is what makes $parentBackdrop inside a template
    /// become FooFrameBackdrop on the frame that inherits it, rather than
    /// naming itself after the template.
    std::string nameArg(const std::string& rawName, const std::string& parentName,
                        const std::string& selfVar) {
        if (rawName.empty()) return "nil";
        const std::string token = "$parent";
        const bool isParented = rawName.compare(0, token.size(), token) == 0;
        if (!isParented) return quote(rawName);
        const std::string suffix = rawName.substr(token.size());
        if (runtimeParentName) {
            return "((" + selfVar + ":GetName() or \"\") .. " + quote(suffix) + ")";
        }
        return quote(parentName + suffix);
    }

    void line(const std::string& s) { result.lua += s; result.lua += "\n"; }

    void emitScripts(const XmlNode& scripts, const std::string& var) {
        for (const XmlNode& s : scripts.children) {
            // <OnClick function="Foo"/> names an existing global; an inline body
            // is a function literal. Both end up as the same SetScript call.
            if (const std::string* fn = s.attr("function")) {
                line(var + ":SetScript(" + quote(s.name) + ", " + *fn + ")");
                continue;
            }
            std::string body = s.text;
            if (body.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            // Blizzard's handlers take self and the script's own arguments, and
            // reference them by those names without declaring them.
            line(var + ":SetScript(" + quote(s.name) +
                 ", function(self, ...) local arg1, arg2, arg3, arg4 = ...; " + body + " end)");
        }
    }

    void emitRegion(const XmlNode& node, const std::string& parentVar,
                    const std::string& parentName, const std::string& layerName) {
        const bool isTexture = (node.name == "Texture");
        const std::string var = nextVar();
        const std::string rawName = node.attrOr("name", "");
        const std::string name = substituteParent(rawName, parentName);

        line("local " + var + " = " + parentVar +
             (isTexture ? ":CreateTexture(" : ":CreateFontString(") +
             nameArg(rawName, parentName, parentVar) + ", " + quote(layerName) + ")");

        if (const std::string* file = node.attr("file")) {
            line(var + ":SetTexture(" + quote(*file) + ")");
        }
        if (const std::string* text = node.attr("text")) {
            line(var + ":SetText(" + quote(*text) + ")");
        }
        if (const std::string* j = node.attr("justifyH")) {
            line(var + ":SetJustifyH(" + quote(*j) + ")");
        }
        if (node.attrBool("setAllPoints")) {
            line(var + ":SetAllPoints(" + parentVar + ")");
        }
        if (node.attrBool("hidden")) line(var + ":Hide()");

        if (const XmlNode* size = node.child("Size")) {
            float w = 0, h = 0;
            if (readDimension(*size, w, h))
                line(var + ":SetSize(" + std::to_string(w) + ", " + std::to_string(h) + ")");
        }
        if (const XmlNode* tc = node.child("TexCoords")) {
            line(var + ":SetTexCoord(" + std::to_string(tc->attrFloat("left", 0.0f)) + ", " +
                 std::to_string(tc->attrFloat("right", 1.0f)) + ", " +
                 std::to_string(tc->attrFloat("top", 0.0f)) + ", " +
                 std::to_string(tc->attrFloat("bottom", 1.0f)) + ")");
        }
        if (const XmlNode* col = node.child("Color")) {
            const std::string args =
                std::to_string(col->attrFloat("r", 1.0f)) + ", " +
                std::to_string(col->attrFloat("g", 1.0f)) + ", " +
                std::to_string(col->attrFloat("b", 1.0f)) + ", " +
                std::to_string(col->attrFloat("a", 1.0f));
            line(var + (isTexture ? ":SetVertexColor(" : ":SetTextColor(") + args + ")");
        }
        if (const XmlNode* anchors = node.child("Anchors")) emitAnchors(*anchors, var, parentVar);
    }

    void emitAnchors(const XmlNode& anchors, const std::string& var,
                     const std::string& parentVar) {
        for (const XmlNode& a : anchors.children) {
            if (a.name != "Anchor") continue;
            const std::string point = a.attrOr("point", "CENTER");
            // relativeTo names a frame; without one the anchor is to the parent,
            // which is what leaving it out means.
            std::string relative = parentVar;
            if (const std::string* rt = a.attr("relativeTo")) relative = *rt;
            const std::string relPoint = a.attrOr("relativePoint", point);

            float ox = 0, oy = 0;
            if (const XmlNode* off = a.child("Offset")) readDimension(*off, ox, oy);
            if (a.attr("x") || a.attr("y")) {
                ox = a.attrFloat("x", ox);
                oy = a.attrFloat("y", oy);
            }
            line(var + ":SetPoint(" + quote(point) + ", " + relative + ", " +
                 quote(relPoint) + ", " + std::to_string(ox) + ", " + std::to_string(oy) + ")");
        }
    }

    void emitFrame(const XmlNode& node, const std::string& parentVar,
                   const std::string& parentName) {
        const std::string rawName = node.attrOr("name", "");
        const std::string name = substituteParent(rawName, parentName);
        const bool isVirtual = node.attrBool("virtual");

        if (isVirtual) {
            // A template is not built now. It is recorded so a later inherits=
            // can replay it onto a real frame, which is the only thing "virtual"
            // means in FrameXML.
            if (name.empty()) {
                result.warnings.push_back("virtual frame with no name was skipped");
                return;
            }
            Emitter inner;
            inner.temp = 0;
            inner.runtimeParentName = true;
            inner.emitFrameBody(node, "self", name);
            line("__WoweeTemplates[" + quote(name) + "] = function(self)");
            result.lua += inner.result.lua;
            for (auto& w : inner.result.warnings) result.warnings.push_back(w);
            line("end");
            return;
        }

        const std::string var = nextVar();
        const std::string parentArg = node.attr("parent")
            ? *node.attr("parent")
            : (parentVar.empty() ? "UIParent" : parentVar);
        line("local " + var + " = CreateFrame(" + quote(node.name) + ", " +
             (name.empty() ? "nil" : quote(name)) + ", " + parentArg + ")");

        // Templates apply before the frame's own settings, so anything stated
        // here overrides what it inherited — the order FrameXML relies on.
        if (const std::string* inherits = node.attr("inherits")) {
            std::stringstream ss(*inherits);
            std::string one;
            while (std::getline(ss, one, ',')) {
                one.erase(0, one.find_first_not_of(" \t"));
                one.erase(one.find_last_not_of(" \t") + 1);
                if (one.empty()) continue;
                line("if __WoweeTemplates[" + quote(one) + "] then __WoweeTemplates[" +
                     quote(one) + "](" + var + ") else __WoweeMissingTemplate(" +
                     quote(one) + ") end");
            }
        }
        emitFrameBody(node, var, name.empty() ? parentName : name);
    }

    void emitFrameBody(const XmlNode& node, const std::string& var,
                       const std::string& name) {
        if (const XmlNode* size = node.child("Size")) {
            float w = 0, h = 0;
            if (readDimension(*size, w, h))
                line(var + ":SetSize(" + std::to_string(w) + ", " + std::to_string(h) + ")");
        }
        if (const std::string* strata = node.attr("frameStrata")) {
            line(var + ":SetFrameStrata(" + quote(*strata) + ")");
        }
        if (node.attr("enableMouse")) {
            line(var + ":EnableMouse(" + (node.attrBool("enableMouse") ? "true" : "false") + ")");
        }
        if (const XmlNode* anchors = node.child("Anchors")) {
            emitAnchors(*anchors, var, node.attr("parent") ? *node.attr("parent") : "UIParent");
        }
        if (const XmlNode* layers = node.child("Layers")) {
            for (const XmlNode& layer : layers->children) {
                if (layer.name != "Layer") continue;
                const std::string level = layer.attrOr("level", "ARTWORK");
                for (const XmlNode& region : layer.children) {
                    if (region.name == "Texture" || region.name == "FontString")
                        emitRegion(region, var, name, level);
                }
            }
        }
        if (const XmlNode* frames = node.child("Frames")) {
            for (const XmlNode& child : frames->children) {
                if (isFrameElement(child.name)) emitFrame(child, var, name);
            }
        }
        // Scripts last: OnLoad runs against a frame that is already built, which
        // is what every handler in FrameXML assumes.
        if (const XmlNode* scripts = node.child("Scripts")) {
            emitScripts(*scripts, var);
            if (scripts->child("OnLoad")) {
                line("if " + var + ":GetScript(\"OnLoad\") then " +
                     var + ":GetScript(\"OnLoad\")(" + var + ") end");
            }
        }
        if (node.attrBool("hidden")) line(var + ":Hide()");
    }
};

} // namespace

std::string substituteParent(const std::string& name, const std::string& parentName) {
    const std::string token = "$parent";
    if (name.compare(0, token.size(), token) != 0) return name;
    return parentName + name.substr(token.size());
}

EmitResult emitFrameXml(const XmlNode& root) {
    Emitter e;
    if (root.name != "Ui") {
        e.result.warnings.push_back("root element is <" + root.name + ">, expected <Ui>");
    }
    for (const XmlNode& node : root.children) {
        if (node.name == "Script") {
            if (const std::string* file = node.attr("file")) e.result.scriptFiles.push_back(*file);
            else if (!node.text.empty()) e.result.lua += node.text + "\n";
        } else if (node.name == "Include") {
            if (const std::string* file = node.attr("file")) e.result.includeFiles.push_back(*file);
        } else if (isFrameElement(node.name)) {
            e.emitFrame(node, "", "");
        }
    }
    return std::move(e.result);
}

} // namespace ui
} // namespace wowee
