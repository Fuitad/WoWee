// A graphics setting read from the config file has to reach something.
//
// Loading parses each key into a pending field and stops there. Those fields
// are what the panel displays; the renderer learns a value only when
// applySettingSideEffects is called for it, which happened when a slider moved
// and when a preset was chosen, and never when settings were loaded.
//
// So a view distance chosen on the login screen was written to disk, read back
// into the panel at startup, shown correctly on the slider, and never applied.
// The panel looked like it did nothing.
//
// The oracle is the source: every graphics key applySettingSideEffects knows
// how to apply has to be in the list the loader walks, or it is a setting that
// only takes effect after being touched by hand.
#include <catch_amalgamated.hpp>

#include "test_support.hpp"

#include <fstream>
#include <regex>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "ui/graphics_defaults.hpp"
#include "ui/graphics_presets.hpp"

namespace {



// The names between a `constexpr const char* <list>[] = { ... };`
std::set<std::string> namesIn(const std::string& source, const std::string& list) {
    const size_t start = source.find("kGraphics" + list);
    if (start == std::string::npos) return {};
    const size_t end = source.find("};", start);
    const std::string body = source.substr(start, end - start);
    std::set<std::string> out;
    const std::regex name(R"RX("([a-z]+)")RX");
    for (std::sregex_iterator it(body.begin(), body.end(), name), last;
         it != last; ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}

}  // namespace

TEST_CASE("the test can read the source it checks", "[settings]") {
    const std::string panel = wowee::test::slurp("src/ui/settings_panel.cpp");
    REQUIRE(panel.size() > 1000);
    REQUIRE(panel.find("kGraphicsApplyKeys") != std::string::npos);
}

TEST_CASE("every setting a preset sets is also applied on load", "[settings]") {
    // A preset walks its own key list to push values out. Loading has to push
    // at least the same set, or choosing a preset and restarting gives two
    // different pictures.
    const std::string panel = wowee::test::slurp("src/ui/settings_panel.cpp");
    const auto presetKeys = namesIn(panel, "PresetKeys");
    const auto applyKeys = namesIn(panel, "ApplyKeys");
    REQUIRE(presetKeys.size() >= 10);
    REQUIRE(applyKeys.size() >= presetKeys.size());

    for (const std::string& key : presetKeys) {
        INFO("preset key not applied on load: " << key);
        CHECK(applyKeys.count(key) == 1);
    }
}

TEST_CASE("the settings the user reported are on the applied list",
          "[settings]") {
    const auto applyKeys =
        namesIn(wowee::test::slurp("src/ui/settings_panel.cpp"), "ApplyKeys");
    for (const char* key : {"viewdistance", "groundclutter", "shadows",
                            "shadowdistance", "waterrefraction", "brightness"}) {
        INFO(key);
        CHECK(applyKeys.count(key) == 1);
    }
}

TEST_CASE("the graphics defaults have one home", "[settings]") {
    // The same two numbers were written in the settings panel, in the login
    // screen's copy of the same struct, and in the reset button's constant.
    CHECK(wowee::ui::kDefaultViewDistance == 1900.0f);
    CHECK(wowee::ui::kDefaultGroundClutter == 70);

    // And they have to be reachable on the sliders that show them, or the
    // panel opens on a value it cannot represent.
    CHECK(wowee::ui::kDefaultViewDistance >= 400.0f);
    CHECK(wowee::ui::kDefaultViewDistance <= 2400.0f);
    CHECK(wowee::ui::kDefaultGroundClutter >= 0);
    CHECK(wowee::ui::kDefaultGroundClutter <= 150);
}

TEST_CASE("no default is spelled out a second time", "[settings]") {
    // The literals themselves, so a new copy has to be a deliberate act.
    const std::string panelHeader = wowee::test::slurp("include/ui/settings_panel.hpp");
    const std::string authHeader = wowee::test::slurp("include/ui/auth_screen.hpp");
    REQUIRE(panelHeader.size() > 500);
    REQUIRE(authHeader.size() > 500);

    CHECK(authHeader.find("kDefaultViewDistance") != std::string::npos);
    CHECK(authHeader.find("kDefaultGroundClutter") != std::string::npos);
    // The old value, in either header, would mean a fourth copy came back.
    CHECK(authHeader.find("1200.0f") == std::string::npos);
}

// ---------------------------------------------------------------------------
// The same question, asked of every setting rather than only the graphics ones.
//
// The list above covers what a preset sets, which is graphics. The rest of the
// settings are applied at startup too, but by hand, from five separate places -
// applyCameraControlSettings, a vsync check in setServices, a frame cap latch, a
// lens flare block, and the bag lines in loadSettings. Nothing checked that a
// setting had one of those, so field of view had none: loadSettings applied it
// behind `if (auto* renderer = services_.renderer)`, and loadSettings runs from
// the constructor, where services have not been injected yet. The camera is
// built asking for 60. The slider read the saved value, the world never did.

namespace {

/// key -> the pending field it reads, from the binding table that already
/// exists to map one to the other.
std::map<std::string, std::string> fieldBindings(const std::string& panel) {
    std::map<std::string, std::string> out;
    const std::regex bind(R"RX(\{\.key = "([a-z0-9_]+)",\s*\.as\w+\s*=\s*&SettingsPanel::(\w+))RX");
    for (std::sregex_iterator it(panel.begin(), panel.end(), bind), last;
         it != last; ++it) {
        out[(*it)[1].str()] = (*it)[2].str();
    }
    return out;
}

/// The keys applySettingSideEffects knows how to push somewhere.
std::set<std::string> sideEffectKeys(const std::string& panel) {
    const size_t start = panel.find("void SettingsPanel::applySettingSideEffects");
    REQUIRE(start != std::string::npos);
    const size_t end = panel.find("\n}\n", start);
    const std::string body = panel.substr(start, end - start);

    std::set<std::string> out;
    const std::regex key(R"RX(key == "([a-z0-9_]+)")RX");
    for (std::sregex_iterator it(body.begin(), body.end(), key), last;
         it != last; ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}

}  // namespace

TEST_CASE("every setting reaches its target at startup", "[settings]") {
    const std::string panel = wowee::test::slurp("src/ui/settings_panel.cpp");
    const std::string screen = wowee::test::slurp("src/ui/game_screen.cpp");
    REQUIRE(panel.size() > 1000);
    REQUIRE(screen.size() > 1000);

    const auto bindings = fieldBindings(panel);
    const auto sideEffects = sideEffectKeys(panel);
    const auto applyKeys = namesIn(panel, "ApplyKeys");
    REQUIRE(bindings.size() >= 40);
    REQUIRE(sideEffects.size() >= 40);

    // The ones that reach their target another way. Each is here because it
    // does, not because it is allowed not to.
    const std::map<std::string, const char*> otherwise = {
        // The inventory screen is a member of GameScreen rather than a service
        // pointer, so it already exists while the constructor reads the file
        // and loadSettings can hand these straight to it.
        {"bagscale", "applied to the inventoryScreen member in loadSettings"},
        {"separatebags", "applied to the inventoryScreen member in loadSettings"},
        {"showkeyring", "applied to the inventoryScreen member in loadSettings"},
        // Applying a preset on load would push its values over the individual
        // settings that were just read.
        {"graphicspreset", "would overwrite the settings loaded beside it"},
        // Reached by a call rather than by reading the field.
        {"windowuiscale", "applyWindowUiScale() is called from GameScreen"},
    };

    for (const std::string& key : sideEffects) {
        if (applyKeys.count(key)) continue;  // on the graphics load list

        auto explained = otherwise.find(key);
        if (explained != otherwise.end()) {
            INFO(key << " is excused because: " << explained->second);
            CHECK(true);
            continue;
        }

        auto bound = bindings.find(key);
        INFO(key << " has a side effect, is not applied on load, and has no"
                    " field binding to find it by");
        REQUIRE(bound != bindings.end());

        INFO(key << " is loaded into " << bound->second
                 << ", which nothing in game_screen.cpp hands to anything -"
                    " so the value is shown on the control and never applied");
        CHECK(screen.find(bound->second) != std::string::npos);
    }
}

TEST_CASE("field of view is applied where the camera exists", "[settings]") {
    // The specific one this found. It is applied from applyCameraControlSettings
    // because that runs once the renderer has been injected; applying it while
    // the config is being read cannot work, and looked like it did.
    const std::string screen = wowee::test::slurp("src/ui/game_screen.cpp");
    const std::string loader = wowee::test::slurp("src/ui/game_screen_minimap.cpp");

    const size_t at = screen.find("void GameScreen::applyCameraControlSettings");
    REQUIRE(at != std::string::npos);
    const std::string body = screen.substr(at, screen.find("\n}\n", at) - at);
    INFO("applyCameraControlSettings no longer applies the saved fov");
    CHECK(body.find("setFov") != std::string::npos);
    CHECK(body.find("pendingFov") != std::string::npos);

    // And it is not applied again from the loader, where it cannot work.
    const size_t load = loader.find("void GameScreen::loadSettings");
    REQUIRE(load != std::string::npos);
    INFO("the loader applies fov again, from where there is no renderer");
    CHECK(loader.find("camera->setFov", load) == std::string::npos);
}

// ---------------------------------------------------------------------------
// A preset named High has to mean one thing.
//
// The settings window and the login screen each had their own copy of the
// preset values, and they had drifted in four of ten columns: High was 350
// yards of shadow with 4x MSAA and no FXAA in game, and 250 yards with 2x and
// FXAA on at the login screen. Medium disagreed about clutter and
// multisampling, and Low still turned shadows off there after the client
// stopped honouring that at all.

TEST_CASE("the presets are written once", "[settings]") {
    const std::string login = wowee::test::slurp("src/ui/auth_screen.cpp");
    REQUIRE(login.size() > 1000);

    const size_t at = login.find("void AuthScreen::applyPresetToState");
    REQUIRE(at != std::string::npos);
    const std::string body = login.substr(at, login.find("\n}\n", at) - at);

    INFO("the login screen builds its presets from the shared table");
    CHECK(body.find("kGraphicsPresets") != std::string::npos);

    // The numbers it used to carry. Any of them back here means a second
    // opinion about what a preset is.
    for (const char* literal : {"75.0f", "150.0f", "250.0f", "400.0f",
                                "600.0f", "1000.0f", "1600.0f", "2400.0f"}) {
        INFO("the login screen spells out " << literal << " again");
        CHECK(body.find(literal) == std::string::npos);
    }
}

TEST_CASE("no preset asks for shadows to be off", "[settings]") {
    // setShadowsEnabled holds them on, so a preset that asks is a preset that
    // writes shadows=0 to the config and has nothing act on it.
    for (int i = 0; i < wowee::ui::kGraphicsPresetCount; ++i) {
        INFO("preset index " << i << " turns shadows off");
        CHECK(wowee::ui::kGraphicsPresets[i].shadows);
    }
}

TEST_CASE("the presets climb", "[settings]") {
    // Each step up is meant to ask for more than the one below it. A column
    // that goes backwards is a preset that improves something by turning it
    // down, which is how the two copies were spotted disagreeing.
    for (int i = 1; i < wowee::ui::kGraphicsPresetCount; ++i) {
        const auto& lo = wowee::ui::kGraphicsPresets[i - 1];
        const auto& hi = wowee::ui::kGraphicsPresets[i];
        INFO("preset " << i << " asks for less than " << (i - 1));
        CHECK(hi.viewDistance >= lo.viewDistance);
        CHECK(hi.shadowDistance >= lo.shadowDistance);
        CHECK(hi.antiAliasing >= lo.antiAliasing);
        CHECK(hi.parallaxQuality >= lo.parallaxQuality);
        CHECK(hi.groundClutter >= lo.groundClutter);
    }
}
