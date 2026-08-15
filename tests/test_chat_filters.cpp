// The two chat filters the Social panel offers.
//
// Both are easy to write in a way that looks right and is wrong in one
// direction only: a spam filter that eats two people greeting each other, and
// a word filter that stars the middle of a place name.
#include <catch_amalgamated.hpp>
#include "game/chat_filters.hpp"

using namespace wowee::game;

namespace {
constexpr uint64_t kAlice = 0x11;
constexpr uint64_t kBob   = 0x22;

std::deque<RecentChatLine> history() {
    return {
        {kAlice, "WTS [Thunderfury] pst", 100.0},
        {kBob,   "hi", 100.0},
    };
}
}  // namespace

TEST_CASE("the same sender pasting the same line again is spam", "[chatfilter]") {
    CHECK(repeatsRecentLine(history(), kAlice, "WTS [Thunderfury] pst", 110.0));
    // Case and spacing changed is the cheapest way past a filter that only
    // compares raw text.
    CHECK(repeatsRecentLine(history(), kAlice, "wts  [Thunderfury]   PST ", 110.0));
}

TEST_CASE("two people saying the same thing is not spam", "[chatfilter]") {
    // Bob said "hi". Alice saying "hi" is a conversation.
    CHECK_FALSE(repeatsRecentLine(history(), kAlice, "hi", 110.0));
}

TEST_CASE("the same line long enough later is not spam", "[chatfilter]") {
    CHECK_FALSE(repeatsRecentLine(history(), kAlice, "WTS [Thunderfury] pst", 200.0));
}

TEST_CASE("a line with no sender is never spam", "[chatfilter]") {
    // System lines and anything the server sends unattributed.
    CHECK_FALSE(repeatsRecentLine(history(), 0, "WTS [Thunderfury] pst", 110.0));
}

TEST_CASE("a covered word is masked and the sentence still reads", "[chatfilter]") {
    const std::string out = maskProfanity("what the fuck was that");
    CHECK(out == "what the f*** was that");
    CHECK(out.size() == std::string("what the fuck was that").size());
}

TEST_CASE("the filter matches whole words only", "[chatfilter]") {
    // The case that makes a naive substring filter embarrassing: real names
    // and words that merely contain a covered one.
    CHECK(maskProfanity("Shitterton") == "Shitterton");
    CHECK(maskProfanity("classic") == "classic");
    CHECK(maskProfanity("assassin") == "assassin");
    CHECK(maskProfanity("Bastardsword") == "Bastardsword");
}

TEST_CASE("punctuation around a word does not hide it", "[chatfilter]") {
    CHECK(maskProfanity("(shit!)") == "(s***!)");
    CHECK(maskProfanity("SHIT") == "S***");
}

TEST_CASE("a clean line comes back untouched", "[chatfilter]") {
    const std::string clean = "Anyone for Deadmines? Need a healer.";
    CHECK(maskProfanity(clean) == clean);
}
