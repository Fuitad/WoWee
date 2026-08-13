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
