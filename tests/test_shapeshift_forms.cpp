// The stance bar's three questions, which have to be about the same list.
//
// FrameXML builds the bar by walking 1..GetNumShapeshiftForms() and asking
// GetShapeshiftFormInfo about each index, then binding CastShapeshiftForm to
// the same index. Those were three separate lists: the count walked eight
// druid forms, the other two a fixed six in a different order. A druid who had
// learned Aquatic Form was counted a button the bar could not describe, and
// every index past the first mismatch showed one form and cast another.
//
// The oracle is retail's contract rather than the old code: the i-th form is
// the i-th form *the character has*, and all three bindings mean the same i.
#include <catch_amalgamated.hpp>

#include <set>

#include "game/protocol_constants.hpp"
#include "game/shapeshift_forms.hpp"

using wowee::game::allShapeshiftForms;
using wowee::game::knownShapeshiftForms;

namespace {

constexpr uint8_t kDruid = 11;
constexpr uint8_t kPriest = 5;
constexpr uint8_t kMage = 8;

}  // namespace

TEST_CASE("a form the character has not learned is not on the bar",
          "[shapeshift]") {
    // The case that named this: a level 14 priest was offered Shadowform,
    // which is learned at 40.
    const std::set<uint32_t> nothing;
    CHECK(knownShapeshiftForms(kPriest, nothing).empty());

    const std::set<uint32_t> shadow{wowee::game::SPELL_SHADOWFORM};
    REQUIRE(knownShapeshiftForms(kPriest, shadow).size() == 1);
    CHECK(std::string(knownShapeshiftForms(kPriest, shadow)[0].name) == "Shadowform");
}

TEST_CASE("the forms keep bar order, not the order they were learned",
          "[shapeshift]") {
    // Cat is third in the table and Bear first, whichever the player learned
    // first. An index means a position on the bar.
    const std::set<uint32_t> known{wowee::game::SPELL_CAT_FORM,
                                   wowee::game::SPELL_BEAR_FORM};
    const auto forms = knownShapeshiftForms(kDruid, known);
    REQUIRE(forms.size() == 2);
    CHECK(forms[0].spellId == wowee::game::SPELL_BEAR_FORM);
    CHECK(forms[1].spellId == wowee::game::SPELL_CAT_FORM);
}

TEST_CASE("Aquatic and Flight Form are forms", "[shapeshift]") {
    // Both were in the list the count used and in neither of the other two, so
    // a druid who had them was counted buttons that described nothing.
    const std::set<uint32_t> known{wowee::game::SPELL_BEAR_FORM,
                                   wowee::game::SPELL_AQUATIC_FORM,
                                   wowee::game::SPELL_FLIGHT_FORM};
    const auto forms = knownShapeshiftForms(kDruid, known);
    REQUIRE(forms.size() == 3);
    CHECK(forms[1].spellId == wowee::game::SPELL_AQUATIC_FORM);
    CHECK(forms[2].spellId == wowee::game::SPELL_FLIGHT_FORM);
}

TEST_CASE("every index the count allows describes a form", "[shapeshift]") {
    // The invariant the bar depends on, stated directly: for any set of known
    // spells, walking 1..count and indexing the same list never runs off the
    // end, because they are the same list.
    const std::set<uint32_t> known{
        wowee::game::SPELL_BEAR_FORM, wowee::game::SPELL_AQUATIC_FORM,
        wowee::game::SPELL_CAT_FORM, wowee::game::SPELL_TRAVEL_FORM,
        wowee::game::SPELL_MOONKIN_FORM, wowee::game::SPELL_TREE_OF_LIFE,
        wowee::game::SPELL_FLIGHT_FORM, wowee::game::SPELL_SWIFT_FLIGHT};

    const auto forms = knownShapeshiftForms(kDruid, known);
    CHECK(forms.size() == 8);
    for (size_t i = 0; i < forms.size(); ++i) {
        INFO("index " << (i + 1));
        CHECK(forms[i].spellId != 0);
        CHECK(forms[i].name != nullptr);
        CHECK(forms[i].icon != nullptr);
    }
}

TEST_CASE("every form has a distinct form id and spell", "[shapeshift]") {
    // The form id is what "is this one active" compares against, so two forms
    // sharing one would both light up. Dire Bear is deliberately not in the
    // table for exactly this reason: it shares Bear's slot.
    for (uint8_t classId : {uint8_t{1}, uint8_t{4}, kPriest, uint8_t{6}, kDruid}) {
        std::set<uint8_t> formIds;
        std::set<uint32_t> spellIds;
        for (const auto& form : allShapeshiftForms(classId)) {
            INFO("class " << int(classId) << " form " << form.name);
            CHECK(formIds.insert(form.formId).second);
            CHECK(spellIds.insert(form.spellId).second);
        }
    }
}

TEST_CASE("a class with no stance bar has no forms", "[shapeshift]") {
    CHECK(allShapeshiftForms(kMage).empty());
    const std::set<uint32_t> known{wowee::game::SPELL_BEAR_FORM};
    CHECK(knownShapeshiftForms(kMage, known).empty());
}
