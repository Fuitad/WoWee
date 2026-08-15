// The rules this session collapsed out of many copies into one.
//
// Each of these was written between three and a hundred and forty-one times
// before it was a function. That is the point of the exercise and also the risk
// of it: a rule with one home is a rule one mistake can break everywhere, and
// what used to be a hundred and forty-one chances to notice is now none.
//
// These are the cases I checked by hand while consolidating - the ones that
// explain why the function is shaped the way it is, and the ones that were
// actually wrong in one of the copies.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "core/character_paths.hpp"
#include "game/inventory.hpp"
#include "game/item_text.hpp"
#include "game/world_packets.hpp"
#include "pipeline/item_textures.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/wowee_binary_io.hpp"
#include "pipeline/wowee_vertex_sanitize.hpp"
#include "ui/graphics_choices.hpp"
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
        // true - so the test cannot be left to the caller.
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

        // A default outside the control's own range cannot be selected, so
        // pressing Defaults would move the value somewhere the slider then
        // refuses to show - and the panel would read back a different number
        // from the one that was just written.
        if (d.kind == ui::SettingKind::Bool) {
            CHECK((d.defaultValue == 0.0f || d.defaultValue == 1.0f));
        } else {
            CHECK(d.defaultValue >= d.minValue);
            CHECK(d.defaultValue <= d.maxValue);
        }

        if (d.kind == ui::SettingKind::Enum) {
            // The value is the chosen index, so the choices have to cover the
            // range exactly - one label per value from min to max.
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
        // - or, for a category, is silently added to a panel that has already
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

TEST_CASE("a graphics choice maps to as many values as it offers words",
          "[settings]") {
    // A dropdown offers words and stores the index chosen; a table turns that
    // index into the sample count or the scale factor it means. Both tables
    // were written out twice, and two copies of a lookup indexed by a stored
    // number do not fail when they drift - they apply the wrong one, and only
    // on one of the two paths.
    //
    // Held against the schema's own choice list rather than against a number,
    // so adding a fifth word to either dropdown fails here.
    std::size_t count = 0;
    const ui::SettingDesc* schema = ui::clientSettingsSchema(count);
    REQUIRE(schema != nullptr);

    auto choiceCount = [&](const std::string& key) -> int {
        for (std::size_t i = 0; i < count; ++i) {
            if (key != schema[i].key) continue;
            const std::string choices = schema[i].choices;
            if (choices.empty()) return 0;
            return 1 + static_cast<int>(std::count(choices.begin(), choices.end(), '|'));
        }
        return -1;
    };

    CHECK(choiceCount("antialiasing") == ui::kMsaaChoiceCount);
    CHECK(choiceCount("fsrquality") == ui::kFsrQualityChoiceCount);

    SECTION("and every choice maps to a distinct value") {
        std::set<int> samples;
        for (int i = 0; i < ui::kMsaaChoiceCount; ++i) {
            samples.insert(static_cast<int>(ui::msaaSamplesForChoice(i)));
        }
        CHECK(samples.size() == static_cast<size_t>(ui::kMsaaChoiceCount));

        std::set<float> scales;
        for (int i = 0; i < ui::kFsrQualityChoiceCount; ++i) {
            scales.insert(ui::fsrScaleForChoice(i));
        }
        CHECK(scales.size() == static_cast<size_t>(ui::kFsrQualityChoiceCount));
    }

    SECTION("an index past the last choice clamps rather than reads past it") {
        // One of the two callers clamped and one did not.
        CHECK(ui::msaaSamplesForChoice(99) ==
              ui::msaaSamplesForChoice(ui::kMsaaChoiceCount - 1));
        CHECK(ui::fsrScaleForChoice(-4) == ui::fsrScaleForChoice(0));
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

TEST_CASE("which equipped slot an item is compared against", "[item]") {
    using wowee::game::EquipSlot;
    using wowee::game::comparableEquipSlots;

    SECTION("the pairs, where an item could be in either") {
        // The reason this returns a list rather than one slot: a ring in the
        // second finger is still the ring to compare a new ring against, and a
        // one-hander may be in either hand.
        CHECK(comparableEquipSlots(11) ==
              std::vector<EquipSlot>{EquipSlot::RING1, EquipSlot::RING2});
        CHECK(comparableEquipSlots(12) ==
              std::vector<EquipSlot>{EquipSlot::TRINKET1, EquipSlot::TRINKET2});
        CHECK(comparableEquipSlots(13) ==
              std::vector<EquipSlot>{EquipSlot::MAIN_HAND, EquipSlot::OFF_HAND});
    }

    SECTION("the INVTYPEs that share a slot") {
        // A robe is chest, a shield and a held-in-off-hand are both off hand,
        // and thrown and guns are both ranged. Each of these was a fall-through
        // in two switches that had to agree.
        CHECK(comparableEquipSlots(20) == comparableEquipSlots(5));   // robe, chest
        CHECK(comparableEquipSlots(14) == comparableEquipSlots(23));  // shield, holdable
        CHECK(comparableEquipSlots(25) == comparableEquipSlots(15));  // thrown, bow
        CHECK(comparableEquipSlots(26) == comparableEquipSlots(15));  // gun, bow
        CHECK(comparableEquipSlots(17) == comparableEquipSlots(21));  // two-hand, main hand
    }

    SECTION("a bag is compared against every bag slot") {
        CHECK(comparableEquipSlots(18).size() == 4);
        CHECK(comparableEquipSlots(18).front() == EquipSlot::BAG1);
        CHECK(comparableEquipSlots(18).back() == EquipSlot::BAG4);
    }

    SECTION("what is not equipped has nothing to compare") {
        CHECK(comparableEquipSlots(0).empty());   // not equippable
        CHECK(comparableEquipSlots(24).empty());  // ammo - a quiver slot this client has no equivalent for
        CHECK(comparableEquipSlots(200).empty());
    }

    SECTION("a weapon is what has damage to put beside another's") {
        // The shield sits in a weapon slot and is not one: comparing its
        // damage per second against a sword's would be comparing zero.
        CHECK(wowee::game::isWeaponInventoryType(13));
        CHECK(wowee::game::isWeaponInventoryType(17));
        CHECK(wowee::game::isWeaponInventoryType(26));
        CHECK_FALSE(wowee::game::isWeaponInventoryType(14));
        CHECK_FALSE(wowee::game::isWeaponInventoryType(23));
        CHECK_FALSE(wowee::game::isWeaponInventoryType(1));
    }
}

TEST_CASE("an amount of money as a line of text", "[item]") {
    // Two file-scope copies of this existed, and a third file reached one of
    // them by forward-declaring it across translation units - which linked
    // only because neither was in an anonymous namespace.
    CHECK(game::formatCopperAmount(12345) == "1g 23s 45c");
    CHECK(game::formatCopperAmount(10000) == "1g");
    CHECK(game::formatCopperAmount(100) == "1s");

    SECTION("a part that is zero is left out") {
        // Five gold and three copper, with no silver between them.
        CHECK(game::formatCopperAmount(50003) == "5g 3c");
    }

    SECTION("nothing at all still says something") {
        // Not the empty string: every caller is putting this in a sentence,
        // and "Looted: " with nothing after it reads as a bug.
        CHECK(game::formatCopperAmount(0) == "0c");
    }

    SECTION("the split, which twenty-one places did by hand") {
        const auto coins = game::splitCopper(12345);
        CHECK(coins.gold == 1);
        CHECK(coins.silver == 23);
        CHECK(coins.copper == 45);
    }

    SECTION("a price keeps the zeros under its highest coin") {
        // A cost is read by position, so the coins below the highest one stay
        // even at zero. This is the opposite rule from a looted amount, and
        // four places wrote it out as the same three-branch snprintf.
        CHECK(game::formatCopperPrice(50003) == "5g 0s 3c");
        CHECK(game::formatCopperPrice(12345) == "1g 23s 45c");
        // Below a gold the gold is not shown at all: several callers printed
        // all three unconditionally and priced a vendor trinket at "0g 0s 45c".
        CHECK(game::formatCopperPrice(45) == "45c");
        CHECK(game::formatCopperPrice(305) == "3s 5c");
        CHECK(game::formatCopperPrice(0) == "0c");
    }

    SECTION("an amount too large for a uint32 of copper") {
        // splitCopper takes a 64-bit amount because a price times a stack can
        // overflow before it is ever split - the vendor's total is computed
        // that way.
        const auto coins = game::splitCopper(uint64_t(5'000'000) * 10000);
        CHECK(coins.gold == 5'000'000);
        CHECK(coins.silver == 0);
        CHECK(coins.copper == 0);
    }
}

TEST_CASE("power types are named by POWER_*", "[spell]") {
    // The spellbook had Focus at 4, which is Happiness - so a spell costing
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

TEST_CASE("an item's colour by quality", "[item]") {
    // Six tables carried these eight colours. They are the retail ones, and a
    // player reads quality off the colour before they read the name, so a
    // table that drifts renames an epic without saying so - which is how one
    // of the six came to write heirlooms in a later expansion's cyan.
    CHECK(std::string(game::itemQualityColorHex(0)) == "ff9d9d9d");  // poor
    CHECK(std::string(game::itemQualityColorHex(1)) == "ffffffff");  // common
    CHECK(std::string(game::itemQualityColorHex(2)) == "ff1eff00");  // uncommon
    CHECK(std::string(game::itemQualityColorHex(3)) == "ff0070dd");  // rare
    CHECK(std::string(game::itemQualityColorHex(4)) == "ffa335ee");  // epic
    CHECK(std::string(game::itemQualityColorHex(5)) == "ffff8000");  // legendary
    CHECK(std::string(game::itemQualityColorHex(6)) == "ffe6cc80");  // artifact
    // Heirloom is the artifact gold in 3.3.5a, not cyan.
    CHECK(std::string(game::itemQualityColorHex(7)) == "ffe6cc80");

    SECTION("a quality this expansion does not have reads as common") {
        CHECK(std::string(game::itemQualityColorHex(8)) == "ffffffff");
        CHECK(std::string(game::itemQualityColorHex(4000)) == "ffffffff");
    }

    SECTION("the six-digit form is the eight-digit one without its alpha") {
        // Half the callers write |cff and half write |c, so the two forms have
        // to stay the same colour or the same item links in two shades.
        for (uint32_t q = 0; q < 9; ++q) {
            CHECK(std::string(game::itemQualityColorHex(q)).substr(2) ==
                  game::itemQualityColorHexRGB(q));
        }
    }
}

TEST_CASE("an item hyperlink as 3.3.5a writes one", "[item]") {
    // Nine fields after "item:": id, enchant, four gems, suffix, unique id and
    // level. Sixteen places built this by hand; the ones that wrote eight gave
    // an addon a differently shaped link from the one a shift-click produced.
    CHECK(game::itemChatLink(3299, 0, "Fractured Canine") ==
          "|cff9d9d9d|Hitem:3299:0:0:0:0:0:0:0:0|h[Fractured Canine]|h|r");
    CHECK(game::itemChatLink(19019, 4, "Thunderfury") ==
          "|cffa335ee|Hitem:19019:0:0:0:0:0:0:0:0|h[Thunderfury]|h|r");

    SECTION("an enchant and a suffix keep the field count") {
        // The guild bank wrote eight fields where everything else wrote nine,
        // dropping the level, so a link from a bank slot had a different shape
        // from the same item linked out of a bag.
        const std::string link = game::itemChatLink(19019, 4, "Thunderfury", 2504, -37);
        CHECK(link == "|cffa335ee|Hitem:19019:2504:0:0:0:0:-37:0:0"
                      "|h[Thunderfury]|h|r");
        const std::string plain = game::itemChatLink(19019, 4, "Thunderfury");
        auto fields = [](const std::string& s) {
            const size_t start = s.find("|Hitem:") + 7;
            return std::count(s.begin() + static_cast<long>(start),
                              s.begin() + static_cast<long>(s.find("|h[")), ':');
        };
        CHECK(fields(link) == fields(plain));
    }

    SECTION("an out-of-range quality still produces a usable link") {
        const std::string link = game::itemChatLink(1, 99, "X");
        CHECK(link.find("|Hitem:1:") != std::string::npos);
        CHECK(link.substr(0, 10) == "|cffffffff");
    }
}

TEST_CASE("the .skin beside a model", "[m2]") {
    CHECK(pipeline::skinPathForM2("Character\\Human\\Male\\HumanMale.m2") ==
          "Character\\Human\\Male\\HumanMale00.skin");

    SECTION("a path with no extension keeps its whole name") {
        // The common spelling chopped three characters off the end, which is
        // right for ".m2" and takes the last three letters of a name that has
        // no extension - asking for a file that cannot exist. A model with no
        // skin draws nothing rather than reporting a bad path.
        CHECK(pipeline::skinPathForM2("Creature\\Wolf\\Wolf") == "Creature\\Wolf\\Wolf00.skin");
    }

    SECTION("a dot in a folder is not an extension") {
        CHECK(pipeline::skinPathForM2("World\\v2.0\\Tree.m2") == "World\\v2.0\\Tree00.skin");
        CHECK(pipeline::skinPathForM2("World\\v2.0\\Tree") == "World\\v2.0\\Tree00.skin");
    }
}

TEST_CASE("the .m2 a model reference means", "[m2]") {
    // WMO doodads, ADT doodads and GameObjectDisplayInfo all name models with
    // the extension the art was authored with; what ships is .m2.
    CHECK(pipeline::modelPathToM2("World\\Tree.mdx") == "World\\Tree.m2");

    SECTION(".mdl too, which is the half three of the nine copies missed") {
        // Those three left the reference as ".mdl", the asset manager found
        // nothing, and the doodad did not appear - with nothing logged, since
        // a model that is not there is an ordinary thing.
        CHECK(pipeline::modelPathToM2("World\\Tree.mdl") == "World\\Tree.m2");
    }

    SECTION("the extension is matched whatever its case") {
        CHECK(pipeline::modelPathToM2("World\\Tree.MDX") == "World\\Tree.m2");
        CHECK(pipeline::modelPathToM2("World\\Tree.Mdl") == "World\\Tree.m2");
    }

    SECTION("anything else is left exactly as it is") {
        // This rewrites a known alias; it does not guess. An .m2 is already
        // right, and a path with no extension is not a model reference this
        // rule has anything to say about.
        CHECK(pipeline::modelPathToM2("World\\Tree.m2") == "World\\Tree.m2");
        CHECK(pipeline::modelPathToM2("World\\Tree.blp") == "World\\Tree.blp");
        CHECK(pipeline::modelPathToM2("World\\Tree") == "World\\Tree");
        CHECK(pipeline::modelPathToM2("") == "");
        CHECK(pipeline::modelPathToM2(".md") == ".md");
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
        // wrong - and 9 is the id no race has.
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
    // meant does not fail - it draws a face.
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

TEST_CASE("a vertex whose floats are not numbers", "[formats]") {
    // The loop is not the point; the replacements are. Two of the four are not
    // zero, and they are the two a fourth copy of this would get wrong.
    struct Vec3 { float x, y, z; };
    struct Vec2 { float x, y; };
    struct ColouredVertex {
        Vec3 position;
        Vec3 normal;
        Vec2 texCoord;
        float color[4];
    };

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    ColouredVertex v{{nan, 1.0f, inf}, {nan, nan, nan}, {inf, 2.0f}, {nan, nan, nan, nan}};
    pipeline::sanitizeVertex(v);

    SECTION("a finite value is left exactly as it was") {
        CHECK(v.position.y == 1.0f);
        CHECK(v.texCoord.y == 2.0f);
    }

    SECTION("position and texture coordinates go to zero") {
        CHECK(v.position.x == 0.0f);
        CHECK(v.position.z == 0.0f);
        CHECK(v.texCoord.x == 0.0f);
    }

    SECTION("a normal becomes +Z, not nothing") {
        // (0,0,0) is not a direction. Lighting divides by its length, so a
        // zeroed normal replaces a NaN with a different NaN one step later.
        CHECK(v.normal.x == 0.0f);
        CHECK(v.normal.y == 0.0f);
        CHECK(v.normal.z == 1.0f);
    }

    SECTION("a colour becomes opaque white, not transparent black") {
        // Zero here would hide the triangle rather than repair it.
        for (int c = 0; c < 4; ++c) CHECK(v.color[c] == 1.0f);
    }

    SECTION("a vertex with no colour is handled the same way") {
        // The model format's vertex carries bone weights where the building
        // format's carries a colour; one function serves both.
        struct PlainVertex { Vec3 position; Vec3 normal; Vec2 texCoord; };
        PlainVertex p{{nan, nan, nan}, {nan, nan, nan}, {nan, nan}};
        pipeline::sanitizeVertex(p);
        CHECK(p.position.x == 0.0f);
        CHECK(p.normal.z == 1.0f);
        CHECK(p.texCoord.y == 0.0f);
    }
}

TEST_CASE("a format's extension is added only when it is missing", "[formats]") {
    CHECK(pipeline::normalizePath("zones", ".wtkn") == "zones.wtkn");
    CHECK(pipeline::normalizePath("zones.wtkn", ".wtkn") == "zones.wtkn");

    SECTION("an extension that is not four letters") {
        // Every per-format copy compared the last five characters against a
        // four-letter extension, which is right for fifty of them and wrong for
        // .wol - so that format wrote its own and lost the string cap with it.
        CHECK(pipeline::normalizePath("sky", ".wol") == "sky.wol");
        CHECK(pipeline::normalizePath("sky.wol", ".wol") == "sky.wol");
    }

    SECTION("a name shorter than the extension") {
        CHECK(pipeline::normalizePath("a", ".wtkn") == "a.wtkn");
    }
}

// Which stat an item's stat slot sets, which three readers of the same block
// share.
//
// SMSG_ITEM_QUERY_SINGLE_RESPONSE carries its stats as (type, value) pairs and
// every expansion reads them the same way, so applyStat is the one place that
// says what a type means. The TBC reader used to spell the mapping out again
// and differed in a single line: it skipped type 0 before the switch. That is
// ITEM_MOD_MANA, confirmed against the server's ItemTemplate.h, so a +Mana
// item lost its mana on TBC and kept it on either side of TBC.
TEST_CASE("an item's stat slot keeps every type, including mana", "[shared]") {
    using wowee::game::ItemQueryResponseData;

    SECTION("the five named stats land in their own fields") {
        ItemQueryResponseData data;
        data.applyStat(3, 11);
        data.applyStat(4, 12);
        data.applyStat(5, 13);
        data.applyStat(6, 14);
        data.applyStat(7, 15);
        CHECK(data.agility == 11);
        CHECK(data.strength == 12);
        CHECK(data.intellect == 13);
        CHECK(data.spirit == 14);
        CHECK(data.stamina == 15);
        CHECK(data.extraStats.empty());
    }

    SECTION("type 0 is mana and is kept, not dropped") {
        ItemQueryResponseData data;
        data.applyStat(0, 240);
        REQUIRE(data.extraStats.size() == 1);
        CHECK(data.extraStats[0].statType == 0u);
        CHECK(data.extraStats[0].statValue == 240);
    }

    SECTION("a zero value is not worth a line") {
        ItemQueryResponseData data;
        data.applyStat(0, 0);
        data.applyStat(43, 0);
        CHECK(data.extraStats.empty());
    }
}
