// Which interface draws what, in the configuration a run actually gets.
//
// The transition's state is "FrameXML draws all of it", and nothing held that
// still. It is not obvious from reading either: the default list names
// forty-nine elements and there are fifty-two, and the other three —
// actionbar, stancebar, xpbar, repbar — are covered by naming "mainmenubar",
// because this client draws its bar as separate pieces where FrameXML draws
// one strip with the griffins on the ends. So the claim rests on a list and a
// grouping rule agreeing, in a file where either can be edited alone.
//
// Getting it wrong is quiet. An element that falls out of the defaults is not
// an error: this client simply draws its own version again, correctly, and the
// only symptom is that the interface stops being the one under test.

#include <catch_amalgamated.hpp>

#include "ui/framexml_takeover.hpp"

#include <cstdlib>
#include <string>

using namespace wowee::ui;

namespace {

/// The environment decides this, and it is read once and cached — so it has to
/// be cleared before the first question, not inside the first test case.
/// Otherwise a developer who happens to be running with WOWEE_FRAMEXML_UI set
/// gets a failure that says nothing about the code.
const bool kCleanEnvironment = [] {
    ::unsetenv("WOWEE_FRAMEXML_UI");
    ::unsetenv("WOWEE_LOAD_FRAMEXML");
    return true;
}();

}  // namespace

TEST_CASE("The last element is still the last one", "[takeover]") {
    // The loop below counts up to Petition, so a new element added after it
    // would go unchecked and this is what notices. Named rather than counted,
    // because the enum has no sentinel and adding one would make every switch
    // over it non-exhaustive.
    REQUIRE(kCleanEnvironment);
    REQUIRE(uiElementName(UiElement::Petition) == "petition");
}

TEST_CASE("A default run hands every element to FrameXML", "[takeover]") {
    REQUIRE(kCleanEnvironment);
    std::string notOwned;
    int total = 0;
    for (int i = 0; i <= static_cast<int>(UiElement::Petition); ++i) {
        const auto element = static_cast<UiElement>(i);
        ++total;
        if (!frameXmlOwns(element)) {
            notOwned += uiElementName(element);
            notOwned += ' ';
        }
    }
    // Fifty-two of them, and the count is asserted so that an element quietly
    // removed shows up as loudly as one quietly unowned.
    CHECK(total == 52);
    INFO("still drawn by this client: " << notOwned);
    CHECK(notOwned.empty());
}

TEST_CASE("The bar's pieces are owned through the whole bar", "[takeover]") {
    REQUIRE(kCleanEnvironment);
    // These four are not in the default list by name. They are owned because
    // "mainmenubar" covers them, and that is the part worth stating on its own
    // — if the grouping rule were dropped, the test above would fail and this
    // one says why.
    CHECK(frameXmlOwns(UiElement::ActionBar));
    CHECK(frameXmlOwns(UiElement::StanceBar));
    CHECK(frameXmlOwns(UiElement::XpBar));
    CHECK(frameXmlOwns(UiElement::RepBar));
    CHECK(frameXmlOwns(UiElement::BagBar));
    CHECK(frameXmlOwns(UiElement::MicroMenu));
}

TEST_CASE("Owning everything means suppressing nothing", "[takeover]") {
    REQUIRE(kCleanEnvironment);
    // The handover has two halves and the case above only pins one. Suppression
    // is what stops FrameXML's own frames being drawn while this client draws
    // its version, so with every element owned the list must be empty.
    //
    // The failure this guards is not subtle in effect and is entirely silent in
    // cause: the renderer sets shown = false on every name in that list, every
    // frame, so a suppression entry surviving for an element we own means the
    // panel simply never appears — with nothing logged, because suppressing a
    // frame is not an error.
    std::string suppressed;
    for (const std::string& name : frameXmlSuppressedFrames()) {
        suppressed += name;
        suppressed += ' ';
    }
    INFO("hidden despite being owned: " << suppressed);
    CHECK(suppressed.empty());
}
