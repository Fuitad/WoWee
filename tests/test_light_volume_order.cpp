// The order two light volumes are blended in, when they weigh the same.
//
// Light.dbc's volumes overlap and the branch for "inside the inner radius"
// assigns the constant 1.0, so equal weights are the ordinary case rather than
// a coincidence: 166 points on the shipped maps sit inside two such volumes
// and 23 inside three, while only the top two are blended.
//
// Weight alone therefore leaves the order undecided, and std::sort is unstable
// and decides it differently for different sequences. Walking kept changing
// the sequence - distant volumes crossing their outer radius enter and leave
// the list - so the chosen pair flipped from frame to frame. The blended
// colours are smoothed over time and survived it; the skybox is whichever
// volume comes first and names one, and that is swapped outright. The sky
// flickered while moving and stood still when the player did.
//
// What is asserted here is that the order is total: any two volumes that are
// not the same volume compare one way round and stay that way, whatever else
// is in the list.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "rendering/light_volume_order.hpp"

using wowee::rendering::lightVolumeOrderedBefore;

namespace {

struct Volume {
    float weight;
    float outerRadius;
    uint32_t id;
};

bool before(const Volume& a, const Volume& b) {
    return lightVolumeOrderedBefore(a.weight, a.outerRadius, a.id,
                                    b.weight, b.outerRadius, b.id);
}

}  // namespace

TEST_CASE("a heavier volume comes first", "[lighting]") {
    const Volume heavy{1.0f, 500.0f, 7};
    const Volume light{0.25f, 100.0f, 3};
    CHECK(before(heavy, light));
    CHECK_FALSE(before(light, heavy));
}

TEST_CASE("equal weights are decided by the tighter volume", "[lighting]") {
    // Both contain the player fully. The smaller one describes somewhere more
    // specific - a city inside a continent - so it is the one that wins.
    const Volume city{1.0f, 300.0f, 90};
    const Volume continent{1.0f, 20000.0f, 2};
    CHECK(before(city, continent));
    CHECK_FALSE(before(continent, city));
}

TEST_CASE("two volumes of the same extent still cannot swap", "[lighting]") {
    const Volume lower{1.0f, 300.0f, 11};
    const Volume higher{1.0f, 300.0f, 12};
    CHECK(before(lower, higher));
    CHECK_FALSE(before(higher, lower));
}

TEST_CASE("a volume never comes before itself", "[lighting]") {
    const Volume one{1.0f, 300.0f, 11};
    CHECK_FALSE(before(one, one));
}

TEST_CASE("the chosen pair does not depend on what else is in the list",
          "[lighting]") {
    // The bug, as the player experienced it. Three volumes tie at 1.0 and only
    // two are kept; around them, volumes far enough away to weigh almost
    // nothing come and go as the player walks. Under an order that decides
    // ties, the two kept are the same two every time.
    const std::vector<Volume> tied = {
        {1.0f, 20000.0f, 2},   // the continent
        {1.0f, 900.0f, 47},    // a zone
        {1.0f, 240.0f, 618},   // a courtyard
    };

    std::vector<Volume> expected = tied;
    std::sort(expected.begin(), expected.end(), before);
    expected.resize(2);

    std::mt19937 rng(20260813);
    for (int round = 0; round < 200; ++round) {
        std::vector<Volume> list = tied;
        // Whatever else happens to be in range this frame, in whatever order
        // the volume table happens to hold it.
        const int extras = static_cast<int>(rng() % 6);
        for (int i = 0; i < extras; ++i) {
            list.push_back({static_cast<float>(rng() % 100) / 1000.0f,
                            static_cast<float>(500 + rng() % 4000),
                            1000 + static_cast<uint32_t>(rng() % 500)});
        }
        std::shuffle(list.begin(), list.end(), rng);

        std::partial_sort(list.begin(), list.begin() + 2, list.end(), before);
        INFO("round " << round << " with " << extras << " extra volumes");
        CHECK(list[0].id == expected[0].id);
        CHECK(list[1].id == expected[1].id);
    }
}
