// The lenient numeric argument, which is what the interface actually hands to
// bindings that want a number.
//
// FrameXML passes a widget's own output straight through without touching it.
// AuctionFrameBrowse_SearchHelper is the case that named this: it hands
// BrowseMinLevel:GetText() and IsUsableCheckButton:GetChecked() to
// QueryAuctionItems, so the level bounds arrive as the *text of an edit box*
// and the usable flag as whatever GetChecked answers.
//
// luaL_optnumber falls back to its default for nil and none only. Anything
// else that will not convert raises instead — and an empty edit box gives ""
// rather than nil, so the commonest search in the game, type a name and press
// Search, died on argument two before a byte went out. There was no error on
// screen: a raise inside a click handler is swallowed, so the browse tab
// simply answered every search with no items.
//
// This is a pure function of the Lua stack, so it can be tested without a
// game, a window or an interface — which is the point. The fault it exists to
// prevent is invisible at every other level.

#include <catch_amalgamated.hpp>

#include "addons/lua_api_helpers.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

using wowee::addons::luaOptNumberText;

namespace {

/// A state with one value pushed, so each case reads as what the interface
/// would have handed over.
struct OneArg {
    lua_State* L;
    // No libraries: these cases are the stack alone, and this build of
    // Lua leaves the package library out.
    OneArg() : L(luaL_newstate()) {}
    ~OneArg() { lua_close(L); }
    OneArg(const OneArg&) = delete;
    OneArg& operator=(const OneArg&) = delete;

    double readString(const char* s, double fallback) {
        lua_pushstring(L, s);
        const double v = luaOptNumberText(L, -1, fallback);
        lua_pop(L, 1);
        return v;
    }
};

}  // namespace

TEST_CASE("An absent argument takes the default", "[lua][args]") {
    OneArg a;
    // Nothing pushed at all: the position does not exist.
    REQUIRE(luaOptNumberText(a.L, 1, 7.0) == Catch::Approx(7.0));
    lua_pushnil(a.L);
    REQUIRE(luaOptNumberText(a.L, -1, 7.0) == Catch::Approx(7.0));
}

TEST_CASE("A number is itself", "[lua][args]") {
    OneArg a;
    lua_pushnumber(a.L, 42.5);
    REQUIRE(luaOptNumberText(a.L, -1, 0.0) == Catch::Approx(42.5));
}

TEST_CASE("An empty edit box is the default, not a raise", "[lua][args]") {
    OneArg a;
    // The auction browse's level boxes start empty and GetText answers "".
    // luaL_optnumber raises here; this is the whole reason the helper exists.
    REQUIRE(a.readString("", 0.0) == Catch::Approx(0.0));
    REQUIRE(a.readString("   ", 3.0) == Catch::Approx(3.0));
}

TEST_CASE("A typed number arrives as text and still counts", "[lua][args]") {
    OneArg a;
    // What the player typed into the same box.
    REQUIRE(a.readString("12", 0.0) == Catch::Approx(12.0));
    REQUIRE(a.readString("  80  ", 0.0) == Catch::Approx(80.0));
    REQUIRE(a.readString("-5", 0.0) == Catch::Approx(-5.0));
}

TEST_CASE("Text that is not a number takes the default", "[lua][args]") {
    OneArg a;
    REQUIRE(a.readString("abc", 1.0) == Catch::Approx(1.0));
    REQUIRE(a.readString("80g", 1.0) == Catch::Approx(1.0));
}

TEST_CASE("A checkbox's answer counts as one or nothing", "[lua][args]") {
    OneArg a;
    // GetChecked answers 1 or nil in 3.3.5, but a boolean reaches these
    // bindings from other callers and from addons, and luaL_optnumber treats
    // neither as absent — false is not nil, so the unticked box raised too.
    lua_pushboolean(a.L, 1);
    REQUIRE(luaOptNumberText(a.L, -1, 9.0) == Catch::Approx(1.0));
    lua_pop(a.L, 1);
    lua_pushboolean(a.L, 0);
    REQUIRE(luaOptNumberText(a.L, -1, 9.0) == Catch::Approx(0.0));
}

TEST_CASE("Anything else takes the default rather than raising", "[lua][args]") {
    OneArg a;
    lua_newtable(a.L);
    REQUIRE(luaOptNumberText(a.L, -1, 4.0) == Catch::Approx(4.0));
}
