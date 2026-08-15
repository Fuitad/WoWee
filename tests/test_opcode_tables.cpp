// The opcode numbers, and the properties that make a table usable.
//
// An opcode number is how a handler finds its packet. A wrong number means the
// handler never runs and the packet is logged as unknown, if it is logged at
// all; two names sharing a number means whichever is looked up second can
// never be reached.
//
// All 1306 WotLK numbers were compared against AzerothCore\'s Opcodes.h and
// every one agreed, so the numbers themselves are not in doubt. What is worth
// holding is that they stay that way, and that the structure around them - the
// _extends inheritance the smaller tables use, and the absence of collisions -
// keeps working.
//
// The values below are recorded from that comparison. They are the ones whose
// being wrong would be felt first: the login handshake, the update block, and
// the movement acknowledgements, which a server will not proceed without.
#include <catch_amalgamated.hpp>

#include "test_support.hpp"

#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {



/// One expansion\'s opcodes, with _extends resolved and _remove applied.
std::map<std::string, int> opcodesOf(const std::string& expansion, int depth = 0) {
    std::map<std::string, int> out;
    if (depth > 4) return out;  // the loader refuses cycles; so does this
    const std::string json =
        wowee::test::slurp("Data/expansions/" + expansion + "/opcodes.json");
    if (json.empty()) return out;

    std::smatch ext;
    const std::regex extends(R"RX("_extends"\s*:\s*"\.\./(\w+)/opcodes\.json")RX");
    if (std::regex_search(json, ext, extends)) {
        out = opcodesOf(ext[1].str(), depth + 1);
    }

    const std::regex pair(R"RX("((?:SMSG|CMSG|MSG)_\w+)"\s*:\s*"(0x[0-9A-Fa-f]+)")RX");
    for (std::sregex_iterator it(json.begin(), json.end(), pair), end;
         it != end; ++it) {
        out[(*it)[1].str()] = static_cast<int>(std::stoul((*it)[2].str(), nullptr, 16));
    }

    const size_t rm = json.find("\"_remove\"");
    if (rm != std::string::npos) {
        const size_t open = json.find('[', rm);
        const size_t close = json.find(']', open);
        if (open != std::string::npos && close != std::string::npos) {
            const std::string body = json.substr(open, close - open);
            const std::regex name(R"RX("((?:SMSG|CMSG|MSG)_\w+)")RX");
            for (std::sregex_iterator it(body.begin(), body.end(), name), end;
                 it != end; ++it) {
                out.erase((*it)[1].str());
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("no two opcodes in one table share a number", "[opcodes]") {
    // A collision makes one of the two unreachable, and which one depends on
    // lookup order rather than on anything anybody decided.
    for (const char* expansion : {"classic", "tbc", "wotlk", "turtle"}) {
        const auto table = opcodesOf(expansion);
        INFO(expansion << ": " << table.size() << " opcodes");
        REQUIRE(table.size() > 500);

        std::map<int, std::string> byNumber;
        std::vector<std::string> collisions;
        for (const auto& [name, number] : table) {
            const auto found = byNumber.find(number);
            if (found != byNumber.end()) {
                collisions.push_back(found->second + " and " + name);
            } else {
                byNumber[number] = name;
            }
        }
        for (const auto& c : collisions) INFO("  " << c);
        CHECK(collisions.empty());
    }
}

TEST_CASE("the WotLK numbers are what the server uses", "[opcodes]") {
    // Recorded from AzerothCore\'s Opcodes.h, where all 1306 agreed.
    const std::map<std::string, int> oracle = {
        {"CMSG_PING", 476},  // 0x1DC
        {"CMSG_AUTH_SESSION", 493},  // 0x1ED
        {"CMSG_PLAYER_LOGIN", 61},  // 0x3D
        {"CMSG_MESSAGECHAT", 149},  // 0x95
        {"SMSG_UPDATE_OBJECT", 169},  // 0xA9
        {"SMSG_COMPRESSED_UPDATE_OBJECT", 502},  // 0x1F6
        {"SMSG_DESTROY_OBJECT", 170},  // 0xAA
        {"SMSG_MONSTER_MOVE", 221},  // 0xDD
        {"MSG_MOVE_HEARTBEAT", 238},  // 0xEE
        {"MSG_RANDOM_ROLL", 507},  // 0x1FB
        {"SMSG_MOVE_SET_CAN_FLY", 835},  // 0x343
        {"SMSG_MOVE_UNSET_CAN_FLY", 836},  // 0x344
        {"CMSG_MOVE_SET_CAN_FLY_ACK", 837},  // 0x345
        {"MSG_MOVE_TELEPORT_ACK", 199},  // 0xC7
        {"SMSG_LOGIN_SETTIMESPEED", 66},  // 0x42
        {"SMSG_MESSAGECHAT", 150},  // 0x96
    };
    const auto table = opcodesOf("wotlk");
    REQUIRE(table.size() > 1000);
    for (const auto& [name, number] : oracle) {
        INFO(name);
        const auto found = table.find(name);
        REQUIRE(found != table.end());
        CHECK(found->second == number);
    }
}

TEST_CASE("a table that extends another gets its opcodes", "[opcodes]") {
    // Turtle declares two keys and inherits the rest. Read without resolving
    // _extends it would have two opcodes and nothing would work.
    const auto turtle = opcodesOf("turtle");
    const auto classic = opcodesOf("classic");
    REQUIRE(classic.size() > 500);
    CHECK(turtle.size() > 500);
    CHECK(turtle.count("CMSG_PING") == 1);
    CHECK(turtle.at("CMSG_PING") == classic.at("CMSG_PING"));

    // And _remove takes one back out.
    CHECK(classic.count("MSG_SET_DUNGEON_DIFFICULTY") == 1);
    CHECK(turtle.count("MSG_SET_DUNGEON_DIFFICULTY") == 0);
}

TEST_CASE("the movement acknowledgements exist where they are sent",
          "[opcodes]") {
    // A server that asks the client to change a movement flag waits for the
    // acknowledgement before it accepts any further movement, so an ack whose
    // opcode does not resolve wedges the character in place.
    //
    // WotLK renamed these: TBC has both SMSG_MOVE_SET_FLIGHT and
    // SMSG_MOVE_SET_CAN_FLY, WotLK only the latter. Both spellings are
    // registered by the handler, and each expansion has to carry the one it
    // uses.
    const auto tbc = opcodesOf("tbc");
    CHECK(tbc.count("SMSG_MOVE_SET_FLIGHT") == 1);
    CHECK(tbc.count("CMSG_MOVE_FLIGHT_ACK") == 1);

    const auto wotlk = opcodesOf("wotlk");
    CHECK(wotlk.count("SMSG_MOVE_SET_CAN_FLY") == 1);
    CHECK(wotlk.count("SMSG_MOVE_UNSET_CAN_FLY") == 1);
    CHECK(wotlk.count("CMSG_MOVE_SET_CAN_FLY_ACK") == 1);
    CHECK(wotlk.count("MSG_MOVE_TELEPORT_ACK") == 1);
}
