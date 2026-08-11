// The rules this session collapsed out of many copies into one.
//
// Each of these was written between three and a hundred and forty-one times
// before it was a function. That is the point of the exercise and also the risk
// of it: a rule with one home is a rule one mistake can break everywhere, and
// what used to be a hundred and forty-one chances to notice is now none.
//
// These are the cases I checked by hand while consolidating — the ones that
// explain why the function is shaped the way it is, and the ones that were
// actually wrong in one of the copies.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "core/character_paths.hpp"
#include "game/item_text.hpp"
#include "pipeline/item_textures.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/wowee_binary_io.hpp"
#include "ui/settings_schema.hpp"

using namespace wowee;

TEST_CASE("a setting's value is written the way a CVar carries one", "[settings]") {
    // The fault this exists for: std::to_string gives six decimals for
    // everything, the options panels compare some values as strings, and a
    // checkbox testing `value == "1"` reads "1.000000" as off. Enable Sound
    // unticked itself every time the panel opened.
    CHECK(ui::settingNumberText(1.0) == "1");
    CHECK(ui::settingNumberText(0.0) == "0");
    CHECK(ui::settingNumberText(0.75) == "0.75");
    CHECK(ui::settingNumberText(1200.0) == "1200");

    SECTION("no trailing zeros, and no lone point") {
        CHECK(ui::settingNumberText(0.5) == "0.5");
        CHECK(ui::settingNumberText(0.640000) == "0.64");
    }

    SECTION("on is anything that is not empty and not zero") {
        // A CVar arrives as a string, and in Lua every string including "0" is
        // true — so the test cannot be left to the caller.
        CHECK(ui::settingIsOn("1"));
        CHECK(ui::settingIsOn("0.5"));
        CHECK_FALSE(ui::settingIsOn("0"));
        CHECK_FALSE(ui::settingIsOn(""));
    }
}

TEST_CASE("the settings schema is something a panel can be built from", "[settings]") {
    // The options panels are generated from this list rather than written out,
    // which means the list has to hold up on its own: a key that appears twice
    // is two controls writing one value and two frames fighting over one name,
    // and a dropdown whose choices do not line up with its range is a control
    // that either cannot reach a value or offers one the client will not take.
    std::size_t count = 0;
    const auto* schema = ui::clientSettingsSchema(count);
    REQUIRE(count > 0);

    std::vector<std::string> keys;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& d = schema[i];
        INFO("setting " << d.key);
        CHECK(std::string(d.key) != "");
        CHECK(std::string(d.label) != "");
        CHECK(std::string(d.category) != "");
        keys.push_back(d.key);

        if (d.kind == ui::SettingKind::Enum) {
            // The value is the chosen index, so the choices have to cover the
            // range exactly — one label per value from min to max.
            const std::string choices = d.choices;
            CHECK(choices != "");
            const auto labels =
                std::count(choices.begin(), choices.end(), '|') + 1;
            CHECK(labels == static_cast<long>(d.maxValue - d.minValue) + 1);
        } else if (d.kind != ui::SettingKind::Bool) {
            // SetValueStep(0) is a slider that cannot be moved, and an empty
            // range is one whose two ends are the same place.
            CHECK(d.maxValue > d.minValue);
            CHECK(d.step > 0.0f);
        }
    }

    SECTION("no key is claimed twice") {
        auto sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    }

    SECTION("a category's settings are together, and so are a section's") {
        // The panels are built by walking this list once: a category is a
        // panel and a section is a heading on it, so a row that turns up again
        // after the list has moved on gets a second heading with the same name
        // — or, for a category, is silently added to a panel that has already
        // been laid out.
        std::vector<std::string> seenCategories;
        std::vector<std::string> seenSections;
        std::string category, section;
        for (std::size_t i = 0; i < count; ++i) {
            const std::string thisCategory = schema[i].category;
            if (thisCategory != category) {
                INFO("category " << thisCategory << " is split");
                CHECK(std::find(seenCategories.begin(), seenCategories.end(),
                                thisCategory) == seenCategories.end());
                seenCategories.push_back(thisCategory);
                category = thisCategory;
                seenSections.clear();
                section.clear();
            }
            const std::string thisSection = schema[i].section;
            if (!thisSection.empty() && thisSection != section) {
                INFO("section " << thisSection << " in " << category << " is split");
                CHECK(std::find(seenSections.begin(), seenSections.end(),
                                thisSection) == seenSections.end());
                seenSections.push_back(thisSection);
                section = thisSection;
            }
        }
    }
}

TEST_CASE("item stat names follow ITEM_MOD, not the shifted table", "[item]") {
    // The chat item tooltip had its own table, shifted by one from 34 to 38 and
    // unrelated from 43 up, so a resilience item read as haste and an armour
    // penetration item as block value. These are the ids it disagreed on.
    CHECK(std::string(game::itemStatName(35)) == "Resilience");
    CHECK(std::string(game::itemStatName(36)) == "Haste Rating");
    CHECK(std::string(game::itemStatName(37)) == "Expertise Rating");
    CHECK(std::string(game::itemStatName(38)) == "Attack Power");
    CHECK(std::string(game::itemStatName(43)) == "Mana per 5 sec");
    CHECK(std::string(game::itemStatName(44)) == "Armor Penetration");
    CHECK(std::string(game::itemStatName(47)) == "Spell Penetration");
    CHECK(std::string(game::itemStatName(48)) == "Block Value");

    SECTION("34 is crit-taken, which has no name to show") {
        CHECK(game::itemStatName(34) == nullptr);
    }

    SECTION("the four hit ids share one name, as WoW writes them") {
        for (uint32_t id : {16u, 17u, 18u, 31u}) {
            CHECK(std::string(game::itemStatName(id)) == "Hit Rating");
        }
    }

    SECTION("an id no expansion sends has nothing to say") {
        CHECK(game::itemStatName(49) == nullptr);
        CHECK(game::itemStatName(999) == nullptr);
    }
}

TEST_CASE("power types are named by POWER_*", "[spell]") {
    // The spellbook had Focus at 4, which is Happiness — so a spell costing
    // Focus fell through to "Mana" and one costing Happiness said "Focus".
    CHECK(std::string(game::powerTypeName(2)) == "Focus");
    CHECK(std::string(game::powerTypeName(4)) == "Happiness");

    SECTION("runes are drawn rather than written, so 5 has no word") {
        CHECK(game::powerTypeName(5) == nullptr);
    }

    SECTION("an id none of the three expansions sends is not guessed at") {
        // Answering "Mana" for an unknown unit is worse than answering none.
        CHECK(game::powerTypeName(7) == nullptr);
    }
}

TEST_CASE("an item's bind line and spell line", "[item]") {
    CHECK(std::string(game::itemBindText(1)) == "Binds when picked up");
    // The one two of the three copies knew and the third did not.
    CHECK(std::string(game::itemBindText(4)) == "Quest Item");
    CHECK(game::itemBindText(0) == nullptr);

    SECTION("a recipe says Use, because the teaching is in the spell's text") {
        // The chat tooltip called this "Teaches" and skipped 4 and 6 entirely,
        // so those spell lines did not appear there at all.
        CHECK(std::string(game::itemSpellTriggerText(5)) == "Use");
        CHECK(std::string(game::itemSpellTriggerText(4)) == "Use");
        CHECK(std::string(game::itemSpellTriggerText(6)) == "Use");
        CHECK(std::string(game::itemSpellTriggerText(2)) == "Chance on Hit");
    }
}

TEST_CASE("the .skin beside a model", "[m2]") {
    CHECK(pipeline::skinPathForM2("Character\\Human\\Male\\HumanMale.m2") ==
          "Character\\Human\\Male\\HumanMale00.skin");

    SECTION("a path with no extension keeps its whole name") {
        // The common spelling chopped three characters off the end, which is
        // right for ".m2" and takes the last three letters of a name that has
        // no extension — asking for a file that cannot exist. A model with no
        // skin draws nothing rather than reporting a bad path.
        CHECK(pipeline::skinPathForM2("Creature\\Wolf\\Wolf") == "Creature\\Wolf\\Wolf00.skin");
    }

    SECTION("a dot in a folder is not an extension") {
        CHECK(pipeline::skinPathForM2("World\\v2.0\\Tree.m2") == "World\\v2.0\\Tree00.skin");
        CHECK(pipeline::skinPathForM2("World\\v2.0\\Tree") == "World\\v2.0\\Tree00.skin");
    }
}

TEST_CASE("where a cape's art might be", "[item]") {
    const auto c = pipeline::capeTextureCandidates("Cloak_A_01", false);
    REQUIRE(c.size() == 6);

    SECTION("both folders unsuffixed first, which is what the shipped art uses") {
        CHECK(c[0] == "Item\\ObjectComponents\\Cape\\Cloak_A_01.blp");
        CHECK(c[1] == "Item\\TextureComponents\\Cape\\Cloak_A_01.blp");
    }

    SECTION("then the wearer's suffix and the unisex one") {
        CHECK(c[2] == "Item\\ObjectComponents\\Cape\\Cloak_A_01_M.blp");
        CHECK(c[3] == "Item\\ObjectComponents\\Cape\\Cloak_A_01_U.blp");
    }

    SECTION("a female wearer asks for _F where a male asks for _M") {
        const auto f = pipeline::capeTextureCandidates("Cloak_A_01", true);
        CHECK(f[2] == "Item\\ObjectComponents\\Cape\\Cloak_A_01_F.blp");
    }

    SECTION("a name that already carries a folder is taken as given") {
        const auto d = pipeline::capeTextureCandidates("Item\\Special\\Cloak.blp", false);
        REQUIRE(d.size() == 1);
        CHECK(d[0] == "Item\\Special\\Cloak.blp");
    }

    SECTION("nothing to look for") {
        CHECK(pipeline::capeTextureCandidates("", false).empty());
    }
}

TEST_CASE("the eight texture regions are in ItemDisplayInfo's own order", "[item]") {
    // The numbering indexes a character's composite atlas, so the order is not
    // free. Region 0 is the upper arm and region 7 the foot.
    CHECK(std::string(pipeline::itemComponentDir(0)) == "ArmUpperTexture");
    CHECK(std::string(pipeline::itemComponentDir(7)) == "FootTexture");

    SECTION("out of range reads nothing rather than past the table") {
        CHECK(std::string(pipeline::itemComponentDir(-1)).empty());
        CHECK(std::string(pipeline::itemComponentDir(8)).empty());
    }
}

TEST_CASE("the folder a race's art lives in", "[character]") {
    // The entry nobody remembers, and the reason this is a table rather than a
    // convention: the Undead are filed under Scourge.
    CHECK(std::string(core::raceModelFolder(5)) == "Scourge");
    CHECK(std::string(core::raceModelFolder(4)) == "NightElf");
    CHECK(std::string(core::raceModelFolder(11)) == "Draenei");

    SECTION("an unknown race answers Human rather than nothing") {
        // A wrong body is more useful than no body while something upstream is
        // wrong — and 9 is the id no race has.
        CHECK(std::string(core::raceModelFolder(9)) == "Human");
        CHECK(std::string(core::raceModelFolder(0)) == "Human");
    }

    SECTION("the art path is the folder twice and the sex between") {
        CHECK(core::defaultBodySkinPath(4, 1) ==
              "Character\\NightElf\\Female\\NightElfFemaleSkin00_00.blp");
        CHECK(core::defaultPelvisPath(5, 0) ==
              "Character\\Scourge\\Male\\ScourgeMaleNakedPelvisSkin00_00.blp");
    }
}

TEST_CASE("the four appearance choices packed into PLAYER_BYTES", "[character]") {
    // Which byte is which is not guessable, and reading face where skin was
    // meant does not fail — it draws a face.
    const auto a = core::unpackAppearanceBytes(0x04030201u);
    CHECK(a.skinId == 1);
    CHECK(a.faceId == 2);
    CHECK(a.hairStyleId == 3);
    CHECK(a.hairColorId == 4);

    SECTION("the top byte is the hair colour, not a sign") {
        CHECK(core::unpackAppearanceBytes(0xFF000000u).hairColorId == 255);
        CHECK(core::unpackAppearanceBytes(0xFF000000u).skinId == 0);
    }
}

TEST_CASE("a format's extension is added only when it is missing", "[formats]") {
    CHECK(pipeline::normalizePath("zones", ".wtkn") == "zones.wtkn");
    CHECK(pipeline::normalizePath("zones.wtkn", ".wtkn") == "zones.wtkn");

    SECTION("an extension that is not four letters") {
        // Every per-format copy compared the last five characters against a
        // four-letter extension, which is right for fifty of them and wrong for
        // .wol — so that format wrote its own and lost the string cap with it.
        CHECK(pipeline::normalizePath("sky", ".wol") == "sky.wol");
        CHECK(pipeline::normalizePath("sky.wol", ".wol") == "sky.wol");
    }

    SECTION("a name shorter than the extension") {
        CHECK(pipeline::normalizePath("a", ".wtkn") == "a.wtkn");
    }
}
