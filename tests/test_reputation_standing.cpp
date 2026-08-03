#include <catch_amalgamated.hpp>

#include "game/reputation_standing.hpp"

using namespace wowee::game;

// These thresholds used to be written out twice — once for the panel this
// client draws and once for the original interface's GetFactionInfo. They
// agreed only because someone checked, and the top of the last band did not:
// exalted was given the width of revered, so a faction at exalted drew an
// almost empty bar. There is one table now, and this is what pins it.

TEST_CASE("Each standing claims the value at its own floor", "[reputation]") {
    for (const auto& band : kReputationStandings) {
        INFO(band.name);
        REQUIRE(reputationStandingFor(band.floor).id == band.id);
        REQUIRE(reputationStandingFor(band.ceiling).id == band.id);
    }
}

TEST_CASE("The bands meet without a gap or an overlap", "[reputation]") {
    for (int i = 0; i < 7; ++i) {
        INFO(kReputationStandings[i].name);
        // One past the top of a band is the bottom of the next. A gap would
        // leave a value in no standing at all; an overlap would put it in two.
        REQUIRE(kReputationStandings[i].ceiling + 1 == kReputationStandings[i + 1].floor);
    }
}

TEST_CASE("Standings are numbered as the interface numbers them",
          "[reputation]") {
    // GetFactionInfo hands the id straight to the interface, which indexes its
    // own colours and names by it, so one-based and in order is not cosmetic.
    for (int i = 0; i < 8; ++i) {
        REQUIRE(kReputationStandings[i].id == i + 1);
    }
}

TEST_CASE("Exalted is a thousand wide, not open-ended", "[reputation]") {
    // The bar reads out of a thousand. Giving exalted the width of revered
    // drew a faction that had earned it as though it had barely started.
    const auto& exalted = kReputationStandings[7];
    REQUIRE(exalted.id == 8);
    REQUIRE(exalted.ceiling - exalted.floor + 1 == 1000);
}

TEST_CASE("Below hated is still hated", "[reputation]") {
    // The server does not send lower, but a value that fell through every band
    // would otherwise be answered with whatever the loop ended on.
    REQUIRE(reputationStandingFor(-99999).id == 1);
    REQUIRE(reputationStandingFor(kReputationStandings[0].floor - 1).id == 1);
}

TEST_CASE("Neutral is where a faction with no history sits", "[reputation]") {
    REQUIRE(reputationStandingFor(0).id == 4);
    REQUIRE(std::string(reputationStandingFor(0).name) == "Neutral");
}
