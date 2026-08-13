// The names a WMO's group file might be stored under.
//
// A root names its groups by index and says nothing about their extension, so
// every loader tries more than one. Five places did that and disagreed on how
// many: three tried the root's own extension, then ".wmo", then ".WMO"; one
// stopped after two; one tried ".wmo" alone.
//
// On a filesystem that cares about case - every Linux install, and the one the
// extracted archives usually land on - a building whose groups are spelled
// ".WMO" loaded its interior when the terrain streamed it in and silently did
// not when a transport or a spawned doodad asked for the same file. The root
// loads either way, so what is left is an empty shell and no error.
#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

#include "pipeline/wmo_group_path.hpp"

using wowee::pipeline::wmoGroupCandidates;

TEST_CASE("the index is three digits", "[wmo-group]") {
    // Group 7 is _007, not _7: the archives pad it and a loader that does not
    // asks for a file that is not there.
    const auto names = wmoGroupCandidates("World\\wmo\\Stormwind.wmo", 7);
    REQUIRE_FALSE(names.empty());
    CHECK(names[0] == "World\\wmo\\Stormwind_007.wmo");
}

TEST_CASE("a hundredth group still fits", "[wmo-group]") {
    const auto names = wmoGroupCandidates("Base.wmo", 123);
    CHECK(names[0] == "Base_123.wmo");
}

TEST_CASE("both spellings of the extension are tried", "[wmo-group]") {
    // The whole point. A root spelled one way may sit beside groups spelled
    // the other, and only one of the five loaders used to reach the second.
    const auto names = wmoGroupCandidates("Base.WMO", 0);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "Base_000.WMO");
    CHECK(names[1] == "Base_000.wmo");
}

TEST_CASE("the root's own spelling is tried first", "[wmo-group]") {
    // It is the name the archive actually used in the common case, so the
    // fallbacks are only reached when it is wrong.
    const auto names = wmoGroupCandidates("Base.Wmo", 3);
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Base_003.Wmo");
    CHECK(names[1] == "Base_003.wmo");
    CHECK(names[2] == "Base_003.WMO");
}

TEST_CASE("a name is not tried twice", "[wmo-group]") {
    // A root already spelled ".wmo" would otherwise ask the archive for the
    // same file two or three times over for every group of every building.
    const auto names = wmoGroupCandidates("Base.wmo", 0);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "Base_000.wmo");
    CHECK(names[1] == "Base_000.WMO");
}

TEST_CASE("only a WMO extension is replaced", "[wmo-group]") {
    // One caller cut four characters off whatever it was handed. A root
    // recorded without an extension then lost the tail of its own name and
    // matched nothing at all.
    const auto names = wmoGroupCandidates("World\\wmo\\Stormwind", 1);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "World\\wmo\\Stormwind_001.wmo");
    CHECK(names[1] == "World\\wmo\\Stormwind_001.WMO");
}

TEST_CASE("a short name is left alone", "[wmo-group]") {
    const auto names = wmoGroupCandidates("a.b", 0);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "a.b_000.wmo");
}
