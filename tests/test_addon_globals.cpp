// What an unloaded addon will define, read out of its own files.
//
// The cost of the two failures is not symmetric. A name this misses keeps
// answering the truthy no-op it answers today - the bug being fixed, no worse
// than before. A name it wrongly claims reads as absent for the whole session,
// so a global FrameXML needs is gone. Every case below is one of the second
// kind except where marked.

#include <catch_amalgamated.hpp>

#include "addons/addon_globals.hpp"

#include <string>
#include <vector>

using wowee::addons::collectLuaGlobals;
using wowee::addons::collectXmlNames;

namespace {

bool has(const std::vector<std::string>& v, const std::string& name) {
    for (const auto& s : v) if (s == name) return true;
    return false;
}

TEST_CASE("a lua function declaration is a global") {
    std::vector<std::string> out;
    collectLuaGlobals(
        "function CombatText_UpdateDisplayedMessages()\n"
        "end\n"
        "\tfunction IndentedStillGlobal()\n"
        "end\n"
        "function TimeManagerClockButton_OnLoad(self)\n"
        "end\n",
        out);
    REQUIRE(has(out, "CombatText_UpdateDisplayedMessages"));
    REQUIRE(has(out, "IndentedStillGlobal"));
    REQUIRE(has(out, "TimeManagerClockButton_OnLoad"));
}

TEST_CASE("locals and indented assignments are not claimed") {
    std::vector<std::string> out;
    collectLuaGlobals(
        "local function HelperNobodyElseSees()\n"
        "end\n"
        "local NotAGlobal = 5;\n"
        "  IndentedAssignment = 5;\n"   // a global, but indistinguishable
        "if Something == Other then\n"  // a comparison, not an assignment
        "end\n",
        out);
    REQUIRE(!has(out, "HelperNobodyElseSees"));
    REQUIRE(!has(out, "NotAGlobal"));
    REQUIRE(!has(out, "IndentedAssignment"));
    REQUIRE(!has(out, "Something"));
}

TEST_CASE("a file-scope assignment is a global") {
    std::vector<std::string> out;
    collectLuaGlobals("MAX_ARENA_TEAM_MEMBERS = 10;\n"
                      "ArenaEnemyFrames = nil\n",
                      out);
    REQUIRE(has(out, "MAX_ARENA_TEAM_MEMBERS"));
    REQUIRE(has(out, "ArenaEnemyFrames"));
}

TEST_CASE("a named xml frame is a global") {
    std::vector<std::string> out;
    collectXmlNames(
        "<Frame name=\"AchievementFrame\" parent=\"UIParent\">\n"
        "<Button name=\"TimeManagerClockButton\" inherits=\"X\">\n",
        out);
    REQUIRE(has(out, "AchievementFrame"));
    REQUIRE(has(out, "TimeManagerClockButton"));
}

TEST_CASE("templates and built names are not claimed") {
    std::vector<std::string> out;
    collectXmlNames(
        // A template's name never becomes a global.
        "<Button name=\"MacroButtonTemplate\" virtual=\"true\">\n"
        // ...written on the other side of the name, which reading only the
        // text before it would miss.
        "<Frame virtual=\"true\" name=\"AnotherTemplate\">\n"
        // Built from the parent's name at runtime, so it is not this literal.
        "<Texture name=\"$parentIcon\">\n",
        out);
    REQUIRE(!has(out, "MacroButtonTemplate"));
    REQUIRE(!has(out, "AnotherTemplate"));
    REQUIRE(out.empty());
}

}  // namespace

