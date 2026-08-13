// Which of a sound bank's samples to play.
//
// The combat, movement and spell banks each wrote this out. It has one rule
// worth stating: choose among the samples that *loaded*, not among all of
// them. Indexing the whole library instead is a one-character difference and
// a bank whose files are half missing then falls silent half the time it is
// asked to play, reporting nothing either way.
#include <catch_amalgamated.hpp>

#include <random>
#include <set>
#include <string>
#include <vector>

#include "audio/sample_load.hpp"

using wowee::audio::pickLoadedSample;

namespace {

struct FakeSample {
    std::string path;
    bool loaded = false;
};

std::vector<FakeSample> bank(std::initializer_list<bool> loadedFlags) {
    std::vector<FakeSample> out;
    int i = 0;
    for (bool loaded : loadedFlags) {
        out.push_back({"sound" + std::to_string(i++) + ".wav", loaded});
    }
    return out;
}

}  // namespace

TEST_CASE("an empty bank plays nothing", "[audio]") {
    std::mt19937 gen(1234);
    const std::vector<FakeSample> empty;
    CHECK(pickLoadedSample(empty, gen) == nullptr);
}

TEST_CASE("a bank whose files all failed to load plays nothing", "[audio]") {
    // Not a crash and not a silent index-zero: the caller has to be able to
    // tell "nothing loaded" from "here is one".
    std::mt19937 gen(1234);
    const auto library = bank({false, false, false});
    CHECK(pickLoadedSample(library, gen) == nullptr);
}

TEST_CASE("only loaded samples are ever chosen", "[audio]") {
    // The rule. Two of five loaded, and a thousand draws must never land on
    // one of the three that did not.
    std::mt19937 gen(99);
    const auto library = bank({false, true, false, false, true});
    std::set<std::string> chosen;
    for (int i = 0; i < 1000; ++i) {
        const FakeSample* got = pickLoadedSample(library, gen);
        REQUIRE(got != nullptr);
        REQUIRE(got->loaded);
        chosen.insert(got->path);
    }
    // And both of them are reachable, so it is not pinned to the first.
    CHECK(chosen == std::set<std::string>{"sound1.wav", "sound4.wav"});
}

TEST_CASE("a single loaded sample is always the answer", "[audio]") {
    std::mt19937 gen(7);
    const auto library = bank({false, false, true});
    for (int i = 0; i < 20; ++i) {
        const FakeSample* got = pickLoadedSample(library, gen);
        REQUIRE(got != nullptr);
        CHECK(got->path == "sound2.wav");
    }
}

TEST_CASE("the last sample is reachable", "[audio]") {
    // An off-by-one in the distribution's upper bound would never pick it, and
    // that is the shape this kind of loop gets wrong.
    std::mt19937 gen(2024);
    const auto library = bank({true, true, true, true});
    std::set<std::string> chosen;
    for (int i = 0; i < 500; ++i) {
        chosen.insert(pickLoadedSample(library, gen)->path);
    }
    CHECK(chosen.size() == 4);
    CHECK(chosen.count("sound3.wav") == 1);
}
