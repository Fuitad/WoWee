#pragma once

#include <cstdint>

// The eight standings a reputation can be at, and where each begins and ends.
//
// These thresholds were written out twice — once for the panel this client
// draws and once for the original interface's GetFactionInfo — and the two
// disagreeing would put the same faction at different standings depending on
// which window was open. Numbers like these do not fail when they drift; they
// render something plausible and wrong.
//
// Exalted is a thousand wide like the rest rather than open-ended: the bar
// reads out of a thousand, and giving it the width of revered drew a faction at
// exalted as an almost empty bar.

namespace wowee::game {

struct ReputationStanding {
    int         id;        ///< 1..8, as the interface numbers them
    const char* name;
    int32_t     floor;     ///< lowest value still at this standing
    int32_t     ceiling;   ///< highest value still at this standing
};

inline constexpr ReputationStanding kReputationStandings[8] = {
    {1, "Hated",      -42000, -6001},
    {2, "Hostile",     -6000, -3001},
    {3, "Unfriendly",  -3000,    -1},
    {4, "Neutral",         0,  2999},
    {5, "Friendly",     3000,  8999},
    {6, "Honored",      9000, 20999},
    {7, "Revered",     21000, 41999},
    {8, "Exalted",     42000, 42999},
};

/// Which standing a raw reputation value falls in. Below hated is still hated;
/// the server does not send lower.
inline constexpr const ReputationStanding& reputationStandingFor(int32_t value) {
    for (int i = 7; i > 0; --i) {
        if (value >= kReputationStandings[i].floor) return kReputationStandings[i];
    }
    return kReputationStandings[0];
}

} // namespace wowee::game
