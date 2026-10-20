#include "TempFolder.h"
#include "app/SettingsFlow.h"
#include "app/Translations.h"
#include "core/Settings.h"

#include <doctest/doctest.h>

#include <algorithm>

namespace {

std::size_t row(const black::app::Page& page, std::string_view label) {
    const auto found =
        std::find_if(page.rows.begin(), page.rows.end(), [label](const auto& item) { return item.label == label; });
    REQUIRE(found != page.rows.end());
    return static_cast<std::size_t>(found - page.rows.begin());
}

}  // namespace

TEST_CASE("Gameplay settings persist independent opt-in choices") {
    const black::tests::TempFolder folder;
    folder.write("CMakeLists.txt", "\n");
    folder.write("scripts/generate.py", "\n");
    const auto project = black::core::ProjectLayout::locate(folder.path());
    REQUIRE(project.has_value());
    black::app::setLanguage("en-US");
    black::app::SettingsFlow flow(*project);
    auto page = flow.page();
    flow.handle({row(page, "DISPLAY"), 0});
    page = flow.page();
    CHECK(std::none_of(page.rows.begin(), page.rows.end(), [](const auto& item) {
        return item.label == "CROSSHAIR" || item.label == "HIT MARKERS" ||
               item.label == "OPTIONAL SECONDARY OBJECTIVES";
    }));
    flow.back();
    page = flow.page();
    flow.handle({row(page, "GAMEPLAY"), 0});
    page = flow.page();
    CHECK(page.title == "GAMEPLAY");
    const auto crosshair = row(page, "CROSSHAIR");
    const auto markers = row(page, "HIT MARKERS");
    const auto secondaries = row(page, "OPTIONAL SECONDARY OBJECTIVES");
    CHECK(page.rows[secondaries].value == "OFF");
    CHECK(page.rows[secondaries].help ==
          "Allow mission completion without the required secondary objectives. Primary objectives are still required.");
    CHECK(page.rows[crosshair].value == "OFF");
    CHECK(page.rows[markers].value == "OFF");
    flow.handle({secondaries, 1});
    CHECK(flow.page().rows[secondaries].value == "ON");
    flow.handle({markers, 1});
    CHECK(flow.page().rows[crosshair].value == "OFF");
    CHECK(flow.page().rows[markers].value == "ON");
    flow.handle({crosshair, 1});
    CHECK(flow.page().rows[crosshair].value == "CROSS");
    flow.handle({markers, 1});
    CHECK(flow.page().rows[markers].value == "OFF");
    const auto loaded = black::core::loadSettings(*project);
    CHECK(loaded.crosshair == "cross");
    CHECK(loaded.hitmarkers == "off");
    CHECK(loaded.optional_secondaries == "on");
    black::app::SettingsFlow reopened(*project);
    reopened.handle({row(reopened.page(), "GAMEPLAY"), 0});
    CHECK(reopened.page().rows[crosshair].value == "CROSS");
    CHECK(reopened.page().rows[markers].value == "OFF");
    CHECK(reopened.page().rows[secondaries].value == "ON");
    reopened.handle({secondaries, -1});
    CHECK(reopened.page().rows[secondaries].value == "OFF");
    CHECK(black::core::loadSettings(*project).optional_secondaries == "off");
    reopened.handle({crosshair, -1});
    CHECK(reopened.page().rows[crosshair].value == "OFF");
    auto changedInGame = black::core::loadSettings(*project);
    changedInGame.optional_secondaries = "on";
    REQUIRE(black::core::saveSettings(*project, changedInGame).ok());
    reopened.reload();
    CHECK(reopened.page().rows[secondaries].value == "ON");
}
