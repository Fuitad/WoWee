// The catalog CLI's path rules, which the editor tools depend on.
#include <catch_amalgamated.hpp>
#include "cli_catalog_paths.hpp"

using namespace wowee::editor::cli;

TEST_CASE("the base name a JSON sidecar belongs to", "[cli]") {
    CHECK(baseFromJsonPath("zones.wgfs.json", ".wgfs") == "zones");
    CHECK(baseFromJsonPath("zones.wgfs", ".wgfs") == "zones");
    CHECK(baseFromJsonPath("zones", ".wgfs") == "zones");

    SECTION("an extension that is not four letters") {
        // Sixty handlers measured the combined suffix with a literal 10, which
        // is the length of ".wxxx.json" and right only while every extension is
        // four letters.
        CHECK(baseFromJsonPath("sky.wol.json", ".wol") == "sky");
        CHECK(baseFromJsonPath("sky.wol", ".wol") == "sky");
    }

    SECTION("a plain .json keeps a name that never had the extension") {
        // Stripping ".json" and then the extension separately arrives at the
        // same answer only when both are present in that order — this is the
        // case that made the combined suffix worth trying first.
        CHECK(baseFromJsonPath("zones.json", ".wgfs") == "zones");
    }

    SECTION("a path with folders keeps them") {
        CHECK(baseFromJsonPath("out/data/zones.wgfs.json", ".wgfs") == "out/data/zones");
    }
}

TEST_CASE("an extension is removed only when it is there", "[cli]") {
    CHECK(withoutExt("zones.wgfs", ".wgfs") == "zones");
    CHECK(withoutExt("zones", ".wgfs") == "zones");
    CHECK(withoutExt("zones.wtkn", ".wgfs") == "zones.wtkn");
    CHECK(withoutExt("", ".wgfs").empty());
}
