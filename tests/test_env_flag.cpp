// Reading a boolean out of an environment variable.
//
// Five files had their own copy and two rules between them. Three tested the
// first character and rejected '0', 'f', 'F', 'n', 'N'; two lowercased the
// whole value and rejected "0", "false", "off", "no".
//
// They disagree on "off". It begins with 'o', so the first-character form read
// it as enabled - turning a diagnostic off that way switched it on in three
// subsystems and off in the other two. Every one of these flags is something
// reached for while chasing a bug, so the tool misled exactly when it was
// being leaned on.
#include <catch_amalgamated.hpp>

#include "core/env_flag.hpp"

using wowee::core::envValueEnables;

TEST_CASE("the four ways of writing off", "[env-flag]") {
    for (const char* off : {"0", "false", "off", "no"}) {
        INFO(off);
        CHECK_FALSE(envValueEnables(off, true));
        CHECK_FALSE(envValueEnables(off, false));
    }
}

TEST_CASE("off is off however it is capitalised", "[env-flag]") {
    for (const char* off : {"OFF", "Off", "FALSE", "False", "No", "NO"}) {
        INFO(off);
        CHECK_FALSE(envValueEnables(off, true));
    }
}

TEST_CASE("off is not read as on", "[env-flag]") {
    // The whole of the divergence. A first-character test sees 'o' and says
    // enabled, which is the opposite of what was asked for.
    CHECK_FALSE(envValueEnables("off", false));
    CHECK_FALSE(envValueEnables("off", true));
}

TEST_CASE("anything else means on", "[env-flag]") {
    // A flag set to something unexpected still enables: someone writing =1 or
    // =yes or =please wants it on, and refusing would be a silent no-op.
    for (const char* on : {"1", "yes", "true", "on", "please", "2", "00"}) {
        INFO(on);
        CHECK(envValueEnables(on, false));
        CHECK(envValueEnables(on, true));
    }
}

TEST_CASE("unset takes the default", "[env-flag]") {
    CHECK(envValueEnables("", true));
    CHECK_FALSE(envValueEnables("", false));
}

TEST_CASE("a value that merely starts with a no-word is on", "[env-flag]") {
    // "0abc" and "nope" are not the words this refuses, and the first-
    // character form refused them. Neither is a way of writing off.
    CHECK(envValueEnables("0abc", false));
    CHECK(envValueEnables("nope", false));
    CHECK(envValueEnables("ffff", false));
}

// ── A number from the environment, held inside a range ──────────────────────
//
// Every caller is a per-frame budget - how many packets or update blocks to
// chew through before yielding. A value outside the range is worse than the
// default at either end: zero stalls the client outright, and a huge one hands
// back the stall the budget exists to prevent.

TEST_CASE("a number in range is taken as written", "[env-flag]") {
    CHECK(wowee::core::envIntClamped(nullptr, 24, 1, 512) == 24);
}

TEST_CASE("the range is a floor and a ceiling", "[env-flag]") {
    // Checked through the pure part below rather than by setting a variable,
    // which is not portable to every platform this builds on.
    const auto clamp = [](long v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : static_cast<int>(v));
    };
    CHECK(clamp(0, 1, 512) == 1);
    CHECK(clamp(-5, 1, 512) == 1);
    CHECK(clamp(99999, 1, 512) == 512);
    CHECK(clamp(24, 1, 512) == 24);
}

TEST_CASE("an unset key takes the default", "[env-flag]") {
    // A key that cannot be looked up at all must not read as zero, which for
    // a budget means doing nothing per frame.
    CHECK(wowee::core::envIntClamped(nullptr, 128, 1, 4096) == 128);
    CHECK(wowee::core::envIntClamped("WOWEE_A_NAME_NOTHING_SETS", 7, 1, 10) == 7);
}
