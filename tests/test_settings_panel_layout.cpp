// Do the options panels fit on the panels?
//
// The settings screens are generated: the schema says what the controls are and
// the Lua says where they go. Nothing else checks the arithmetic between those
// two, and nothing can — a control laid out past the bottom of the panel
// registers, refreshes, answers its value and reads back correctly. It is
// simply not on screen, and no behavioural test has an opinion about that.
//
// A search box added to the root panel in this session was drawn straight
// through the two blocks under it for exactly that reason: every check passed.
//
// So this one does the geometry. The column bounds are read out of the Lua
// itself rather than copied here, so the two cannot drift: change COLUMN_BOTTOM
// in the panel builder and this test lays the schema out against the new one.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <regex>
#include <string>
#include <vector>

#include "addons/addon_lua_snippets.hpp"
#include "ui/settings_schema.hpp"

using namespace wowee;

namespace {

/// A `local NAME = <number>` out of the panel builder's own source.
int luaConstant(const std::string& source, const std::string& name) {
    const std::regex pattern("local\\s+" + name + "\\s*=\\s*(-?[0-9]+)");
    std::smatch m;
    REQUIRE(std::regex_search(source, m, pattern));
    return std::stoi(m[1]);
}

/// The heights the builder reserves, which are its three `reserve(...)` calls.
constexpr int kHeadingHeight = 32;
constexpr int kCheckButtonHeight = 27;
constexpr int kSliderHeight = 50;  // and dropdowns, which reserve the same

}  // namespace

TEST_CASE("every settings panel fits in its two columns", "[settings]") {
    const std::string lua = addons::kWoweeOptionsPanelLua;
    const int columnTop = luaConstant(lua, "COLUMN_TOP");
    const int columnBottom = luaConstant(lua, "COLUMN_BOTTOM");
    REQUIRE(columnBottom < columnTop);

    std::size_t count = 0;
    const auto* schema = ui::clientSettingsSchema(count);
    REQUIRE(count > 0);

    // Walk the schema exactly as buildPanel does: a category is a panel, a
    // section adds a heading, and a control that will not fit moves to the
    // second column — after which there is nowhere else to go.
    std::string category, section;
    int column = 1;
    int y = columnTop;
    for (std::size_t i = 0; i <= count; ++i) {
        const bool last = (i == count);
        const std::string thisCategory = last ? std::string() : schema[i].category;
        if (last || thisCategory != category) {
            category = thisCategory;
            section.clear();
            column = 1;
            y = columnTop;
            if (last) break;
        }

        std::vector<int> reservations;
        const std::string thisSection = schema[i].section;
        if (!thisSection.empty() && thisSection != section) {
            section = thisSection;
            reservations.push_back(kHeadingHeight);
        }
        reservations.push_back(schema[i].kind == ui::SettingKind::Bool ? kCheckButtonHeight
                                                                      : kSliderHeight);

        for (int height : reservations) {
            if (y - height < columnBottom && column == 1) {
                column = 2;
                y = columnTop;
            }
            INFO("setting " << schema[i].key << " on panel " << schema[i].category
                            << " lands at " << (y - height) << " in column " << column
                            << ", past the bottom at " << columnBottom);
            CHECK(y - height >= columnBottom);
            y -= height;
        }
    }
}

TEST_CASE("a dropdown does not hang off the right of the panel", "[settings]") {
    // The dropdown is the widest control and the only one anchored back from
    // its column, because the template carries its own left inset. In the
    // second column that is the tightest fit on the panel.
    const std::string lua = addons::kWoweeOptionsPanelLua;
    const int columnWidth = luaConstant(lua, "COLUMN_WIDTH");

    // COLUMN_X is a table rather than a number, so it is read on its own.
    const std::regex columns("local\\s+COLUMN_X\\s*=\\s*\\{\\s*(-?[0-9]+)\\s*,\\s*(-?[0-9]+)");
    std::smatch m;
    REQUIRE(std::regex_search(lua, m, columns));
    const int secondColumnX = std::stoi(m[2]);

    // What the builder does: anchor back by 14, then UIDropDownMenu_SetWidth
    // with COLUMN_WIDTH - 60. The template adds about 25 units of its own
    // chrome on each side of that.
    const int left = secondColumnX - 14;
    const int right = left + (columnWidth - 60) + 25 * 2;

    // InterfaceOptionsFramePanelContainer, which is what the panels sit in.
    constexpr int kPanelWidth = 623;
    INFO("a dropdown in the second column reaches " << right << " of " << kPanelWidth);
    CHECK(right <= kPanelWidth);
}

TEST_CASE("the root panel's blocks do not sit inside each other", "[settings]") {
    // This panel is laid out by hand, not generated, so the check is against
    // what the Lua says each block needs — a "needs N" note beside every
    // anchor. It is the panel a search box was inserted into the middle of
    // last pass, on top of the two blocks that were already there.
    const std::string lua = addons::kWoweeOptionsPanelLua;

    struct Block { int top; int needs; };
    std::vector<Block> blocks;
    const std::regex anchored(
        R"(SetPoint\("TOPLEFT",\s*-?[0-9]+,\s*(-[0-9]+)\)\s*--\s*needs\s+([0-9]+))");
    for (auto it = std::sregex_iterator(lua.begin(), lua.end(), anchored);
         it != std::sregex_iterator(); ++it) {
        blocks.push_back({-std::stoi((*it)[1]), std::stoi((*it)[2])});
    }
    // Every block on the panel carries one; a new block without a note would
    // be invisible to this check, so the count is asserted rather than assumed.
    REQUIRE(blocks.size() == 12);

    std::sort(blocks.begin(), blocks.end(),
              [](const Block& a, const Block& b) { return a.top < b.top; });

    int previousBottom = 0;
    for (const Block& b : blocks) {
        INFO("a block at -" << b.top << " starts inside the one above it, "
             << "which runs to -" << previousBottom);
        CHECK(b.top >= previousBottom);
        previousBottom = b.top + b.needs;
    }

    // InterfaceOptionsFramePanelContainer is about 492 tall.
    constexpr int kPanelHeight = 492;
    INFO("the root panel's content ends at -" << previousBottom);
    CHECK(previousBottom <= kPanelHeight);
}
