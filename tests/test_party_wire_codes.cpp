// The party operation and result codes, against the values the server sends.
//
// SMSG_PARTY_COMMAND_RESULT carries three fields: which operation was tried,
// who it was tried on, and how it went. Both of the enums here are read off
// that packet, and both are the kind that fail quietly - every value is a
// small integer, so a wrong one is not a malformed packet, it is a different
// operation.
//
// SWAP was 3. The server sends 4, and 3 is not an operation at all in 3.3.5a,
// so nothing could ever have compared equal to it.
//
// The oracle is AzerothCore: PARTY_OP_* in Server/WorldSession.h and the
// PartyResult enum in shared/SharedDefines.h, read once and recorded here.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <map>
#include <string>

#include "game/group_defines.hpp"

using wowee::game::PartyCommand;
using wowee::game::PartyResult;

namespace {

constexpr uint32_t value(PartyCommand c) { return static_cast<uint32_t>(c); }
constexpr uint32_t value(PartyResult r) { return static_cast<uint32_t>(r); }

}  // namespace

TEST_CASE("the party operations are the numbers the server sends", "[party-wire]") {
    // PARTY_OP_INVITE through PARTY_OP_SWAP. Note the gap: the list runs
    // 0, 1, 2, 4, and the missing 3 is what SWAP used to be set to.
    CHECK(value(PartyCommand::INVITE) == 0);
    CHECK(value(PartyCommand::UNINVITE) == 1);
    CHECK(value(PartyCommand::LEAVE) == 2);
    CHECK(value(PartyCommand::SWAP) == 4);
}

TEST_CASE("no operation is three", "[party-wire]") {
    // The value SWAP held. There is no PARTY_OP_* equal to 3, so an operation
    // compared against it can never match, and the packet's own field would
    // never be recognised as a swap.
    CHECK(value(PartyCommand::INVITE) != 3);
    CHECK(value(PartyCommand::UNINVITE) != 3);
    CHECK(value(PartyCommand::LEAVE) != 3);
    CHECK(value(PartyCommand::SWAP) != 3);
}

TEST_CASE("the party results are the numbers the server sends", "[party-wire]") {
    // ERR_PARTY_RESULT_OK through ERR_INVITE_RESTRICTED. 10 and 11 are not
    // used by this client version, which is why the list steps from 9 to 12.
    CHECK(value(PartyResult::OK) == 0);
    CHECK(value(PartyResult::BAD_PLAYER_NAME) == 1);
    CHECK(value(PartyResult::TARGET_NOT_IN_GROUP) == 2);
    CHECK(value(PartyResult::TARGET_NOT_IN_INSTANCE) == 3);
    CHECK(value(PartyResult::GROUP_FULL) == 4);
    CHECK(value(PartyResult::ALREADY_IN_GROUP) == 5);
    CHECK(value(PartyResult::NOT_IN_GROUP) == 6);
    CHECK(value(PartyResult::NOT_LEADER) == 7);
    CHECK(value(PartyResult::PLAYER_WRONG_FACTION) == 8);
    CHECK(value(PartyResult::IGNORING_YOU) == 9);
    CHECK(value(PartyResult::LFG_PENDING) == 12);
    CHECK(value(PartyResult::INVITE_RESTRICTED) == 13);
}

TEST_CASE("no two party codes share a value", "[party-wire]") {
    // A collision makes one of the pair unreachable, and every check above
    // would still pass. This is how SWAP's old value was survivable: nothing
    // held 3, so nothing contradicted it either.
    const std::map<std::string, uint32_t> operations = {
        {"INVITE", value(PartyCommand::INVITE)},
        {"UNINVITE", value(PartyCommand::UNINVITE)},
        {"LEAVE", value(PartyCommand::LEAVE)},
        {"SWAP", value(PartyCommand::SWAP)},
    };
    const std::map<std::string, uint32_t> results = {
        {"OK", value(PartyResult::OK)},
        {"BAD_PLAYER_NAME", value(PartyResult::BAD_PLAYER_NAME)},
        {"TARGET_NOT_IN_GROUP", value(PartyResult::TARGET_NOT_IN_GROUP)},
        {"TARGET_NOT_IN_INSTANCE", value(PartyResult::TARGET_NOT_IN_INSTANCE)},
        {"GROUP_FULL", value(PartyResult::GROUP_FULL)},
        {"ALREADY_IN_GROUP", value(PartyResult::ALREADY_IN_GROUP)},
        {"NOT_IN_GROUP", value(PartyResult::NOT_IN_GROUP)},
        {"NOT_LEADER", value(PartyResult::NOT_LEADER)},
        {"PLAYER_WRONG_FACTION", value(PartyResult::PLAYER_WRONG_FACTION)},
        {"IGNORING_YOU", value(PartyResult::IGNORING_YOU)},
        {"LFG_PENDING", value(PartyResult::LFG_PENDING)},
        {"INVITE_RESTRICTED", value(PartyResult::INVITE_RESTRICTED)},
    };

    for (const auto* table : {&operations, &results}) {
        std::map<uint32_t, std::string> seen;
        for (const auto& [name, code] : *table) {
            INFO(name << " = " << code);
            const auto found = seen.find(code);
            if (found != seen.end()) {
                INFO("shares a value with " << found->second);
                CHECK(false);
            }
            seen[code] = name;
        }
    }
}
