// The schema has to be consistent with itself before anything reading it can be
// right.
//
// Three readers build controls out of these rows - the settings window, the
// options panels, and an addon asking what this client can be told - and none
// of them is in a position to notice when a row contradicts itself. A dropdown
// whose range says three entries while its labels name two does not fail; it
// offers two and writes indices the labels cannot name. That is exactly what
// the login screen's parallax dropdown did, from its own hand-written copy of
// the same scale, and it silently downgraded anyone who chose High.
//
// So these check the rows against themselves. They read the compiled schema
// rather than the source, which is also how a row behind #ifndef NDEBUG gets
// counted the way the build actually has it.
#include <catch_amalgamated.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ui/settings_schema.hpp"

using namespace wowee::ui;

namespace {

std::vector<SettingDesc> schema() {
    std::size_t count = 0;
    const SettingDesc* rows = clientSettingsSchema(count);
    REQUIRE(rows != nullptr);
    REQUIRE(count > 0);
    return std::vector<SettingDesc>(rows, rows + count);
}

/// The choice labels of an Enum row, split the way every reader splits them.
std::vector<std::string> choicesOf(const SettingDesc& d) {
    std::vector<std::string> out;
    const std::string all = d.choices ? d.choices : "";
    if (all.empty()) return out;
    std::size_t start = 0;
    while (true) {
        const std::size_t bar = all.find('|', start);
        out.push_back(all.substr(start, bar == std::string::npos ? bar : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return out;
}

}  // namespace

TEST_CASE("a setting key names one setting", "[settings][schema]") {
    // get and set take a key. Two rows answering to one key means the second is
    // unreachable and whichever reader walks the list first wins.
    std::set<std::string> seen;
    for (const auto& d : schema()) {
        INFO("duplicate key: " << d.key);
        CHECK(seen.insert(d.key).second);
    }
}

TEST_CASE("every row is fit to draw", "[settings][schema]") {
    for (const auto& d : schema()) {
        INFO("key: " << d.key);
        CHECK(d.key != nullptr);
        CHECK(std::string(d.key).empty() == false);
        CHECK(std::string(d.label).empty() == false);   // the control would have no name
        CHECK(std::string(d.category).empty() == false);  // it would belong to no panel
    }
}

TEST_CASE("a dropdown names every value it can hold", "[settings][schema]") {
    // The general form of the parallax quality bug: a scale whose labels and
    // whose range disagree. The value written is the chosen index, so the
    // labels are the range.
    for (const auto& d : schema()) {
        if (d.kind != SettingKind::Enum) continue;
        INFO("key: " << d.key << "  choices: " << (d.choices ? d.choices : "(null)"));
        const auto labels = choicesOf(d);
        CHECK(labels.size() >= 2);              // a dropdown of one is not a choice
        CHECK(d.minValue == 0.0f);              // an index starts at zero
        CHECK(static_cast<int>(labels.size()) == static_cast<int>(d.maxValue) + 1);
        for (const auto& label : labels) {
            INFO("empty label in " << d.key);
            CHECK(label.empty() == false);
        }
    }
}

TEST_CASE("a setting's default is a value it can hold", "[settings][schema]") {
    // A default outside the range is a control that moves the moment it is
    // shown, and a "restore defaults" that does not restore.
    for (const auto& d : schema()) {
        INFO("key: " << d.key << "  default: " << d.defaultValue);
        switch (d.kind) {
            case SettingKind::Bool:
                CHECK((d.defaultValue == 0.0f || d.defaultValue == 1.0f));
                break;
            case SettingKind::Enum:
                CHECK(d.defaultValue >= 0.0f);
                CHECK(d.defaultValue <= d.maxValue);
                CHECK(d.defaultValue == static_cast<float>(static_cast<int>(d.defaultValue)));
                break;
            case SettingKind::Int:
            case SettingKind::Float:
                CHECK(d.minValue < d.maxValue);
                CHECK(d.step > 0.0f);
                CHECK(d.defaultValue >= d.minValue);
                CHECK(d.defaultValue <= d.maxValue);
                break;
        }
    }
}

TEST_CASE("a panel opens with a heading of its own", "[settings][schema]") {
    // An empty section continues the one above. For the first row of a category
    // there is no one above within that category, so its controls would be
    // drawn under whatever heading the previous category ended on - on the
    // right panel, under the wrong title, with nothing failing.
    std::string category;
    for (const auto& d : schema()) {
        if (d.category == category) continue;
        category = d.category;
        INFO("the first row of " << category << " (" << d.key << ") names no section");
        CHECK(std::string(d.section).empty() == false);
    }
}

TEST_CASE("a category is written in one run", "[settings][schema]") {
    // The order is the order they are read, so a category interrupted by
    // another and picked up again draws two panels with the same name - or one
    // panel missing the rows that came late, depending on which reader.
    std::set<std::string> closed;
    std::string category;
    for (const auto& d : schema()) {
        if (d.category == category) continue;
        INFO("category " << d.category << " starts again at " << d.key);
        CHECK(closed.count(d.category) == 0);
        if (!category.empty()) closed.insert(category);
        category = d.category;
    }
}

TEST_CASE("a control that depends on another names one that exists", "[settings][schema]") {
    // enabledWhen is read by asking for the other setting's value. A key that
    // is not in the schema reads as empty, which is false, so the control is
    // greyed out for good - it is offered, it explains itself, and it can never
    // be touched.
    std::set<std::string> keys;
    for (const auto& d : schema()) keys.insert(d.key);

    for (const auto& d : schema()) {
        const std::string test = d.enabledWhen ? d.enabledWhen : "";
        if (test.empty()) continue;

        std::string other = test;
        const std::size_t bang = test.find("!=");
        const std::size_t eq = test.find('=');
        if (bang != std::string::npos) other = test.substr(0, bang);
        else if (eq != std::string::npos) other = test.substr(0, eq);

        INFO(d.key << " is enabled when \"" << test << "\", and " << other
                   << " is not a setting");
        CHECK(keys.count(other) == 1);
        CHECK(other != d.key);  // a control gating itself never turns on
    }
}

TEST_CASE("a control gated on a dropdown names an index it has", "[settings][schema]") {
    // "upscaling=2" is a comparison against a chosen index. An index the
    // dropdown cannot reach is a control nothing can ever enable.
    std::map<std::string, const SettingDesc*> byKey;
    auto rows = schema();
    for (const auto& d : rows) byKey[d.key] = &d;

    for (const auto& d : rows) {
        const std::string test = d.enabledWhen ? d.enabledWhen : "";
        const std::size_t bang = test.find("!=");
        const std::size_t eq = test.find('=');
        if (bang == std::string::npos && eq == std::string::npos) continue;

        const std::size_t at = (bang != std::string::npos) ? bang : eq;
        const std::string other = test.substr(0, at);
        const std::string want = test.substr(at + (bang != std::string::npos ? 2 : 1));

        auto it = byKey.find(other);
        if (it == byKey.end()) continue;  // the previous test reports this
        if (it->second->kind != SettingKind::Enum) continue;

        INFO(d.key << " is enabled when \"" << test << "\", and " << other
                   << " has no such index");
        const int index = std::stoi(want);
        CHECK(index >= 0);
        CHECK(index <= static_cast<int>(it->second->maxValue));
    }
}
