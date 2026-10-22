#include "app/SteamFlow.h"
#include "app/Translations.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

using black::app::MenuFlow;
using black::app::SteamFlow;
using black::core::Error;
using black::core::Result;
using black::core::SteamAccount;
using black::core::SteamShortcut;
using black::core::SteamShortcutResult;

std::size_t row(const black::app::Page& page, std::string_view label) {
    const auto found =
        std::find_if(page.rows.begin(), page.rows.end(), [label](const auto& item) { return item.label == label; });
    REQUIRE(found != page.rows.end());
    return static_cast<std::size_t>(found - page.rows.begin());
}

struct Fixture {
    Fixture() { black::app::setLanguage("en-US"); }

    std::vector<SteamAccount> accounts = {{"account-one/config", "Player one"}};
    unsigned discoveries = 0;
    unsigned additions = 0;
    std::string discoveryError;
    std::string addError;
    bool alreadyPresent = false;
    SteamAccount selected;
    SteamShortcut registered;

    SteamFlow::Operations operations() {
        return {
            [this]() -> Result<std::vector<SteamAccount>> {
                ++discoveries;
                if (!discoveryError.empty()) {
                    return Error{discoveryError};
                }
                return accounts;
            },
            [this](const SteamAccount& account, const SteamShortcut& shortcut) -> Result<SteamShortcutResult> {
                ++additions;
                selected = account;
                registered = shortcut;
                if (!addError.empty()) {
                    return Error{addError};
                }
                return SteamShortcutResult{alreadyPresent, "shortcuts.backup.vdf"};
            },
        };
    }
};

SteamShortcut shortcut() {
    return {std::filesystem::path("BLACK installation") / "BLACK Launcher", "BLACK installation", "--play", "BLACK"};
}

}  // namespace

TEST_CASE("Steam pages only register after an explicit Add selection") {
    black::app::setLanguage("en-US");
    Fixture fixture;
    SteamFlow flow(shortcut(), fixture.operations());
    CHECK(fixture.discoveries == 1);
    CHECK(fixture.additions == 0);
    auto page = flow.page();
    CHECK(page.id == "steam-ready");
    CHECK(page.initialRow == row(page, "ADD TO STEAM"));
    CHECK(page.rows[row(page, "STEAM ACCOUNT")].value == "Player one");
    CHECK(page.notice.find("Fully exit Steam") != std::string::npos);
    flow.update(1.0, page.initialRow);
    CHECK(flow.handle({row(page, "ADD TO STEAM"), 1}) == MenuFlow::Outcome::Stay);
    CHECK(flow.handle({row(page, "STEAM ACCOUNT"), 0}) == MenuFlow::Outcome::Stay);
    CHECK(flow.handle({99, 0}) == MenuFlow::Outcome::Stay);
    CHECK(flow.back() == MenuFlow::Outcome::Back);
    CHECK(fixture.additions == 0);

    CHECK(flow.handle({row(page, "ADD TO STEAM"), 0}) == MenuFlow::Outcome::Stay);
    CHECK(fixture.additions == 1);
    CHECK(fixture.registered.executable == shortcut().executable);
    CHECK(fixture.registered.startDirectory == shortcut().startDirectory);
    CHECK(fixture.registered.launchOptions == "--play");
    CHECK(fixture.registered.appName == "BLACK");
    page = flow.page();
    CHECK(page.title == "ADDED TO STEAM");
    CHECK(page.notice == "Open Steam to find BLACK in your Library.");
    CHECK_FALSE(page.noticeIsProblem);
    CHECK(flow.handle({99, 0}) == MenuFlow::Outcome::Stay);
    CHECK(flow.handle({row(page, "DONE"), 0}) == MenuFlow::Outcome::Back);
    CHECK(fixture.additions == 1);
}

TEST_CASE("Steam account selection and navigation do not write shortcuts") {
    Fixture fixture;
    fixture.accounts.push_back({"account-two/config", "Player two"});
    SteamFlow flow(shortcut(), fixture.operations());
    auto page = flow.page();
    CHECK(page.id == "steam-accounts");
    flow.handle({row(page, "Player two"), 0});
    page = flow.page();
    CHECK(page.id == "steam-ready");
    CHECK(page.rows[row(page, "STEAM ACCOUNT")].value == "Player two");
    CHECK(fixture.additions == 0);
    flow.handle({row(page, "CHOOSE ANOTHER ACCOUNT"), 0});
    CHECK(flow.page().id == "steam-accounts");
    flow.handle({row(flow.page(), "Player one"), 0});
    CHECK(flow.back() == MenuFlow::Outcome::Stay);
    CHECK(flow.page().id == "steam-accounts");
    CHECK(fixture.additions == 0);
    flow.handle({row(flow.page(), "Player two"), 0});
    flow.handle({row(flow.page(), "ADD TO STEAM"), 0});
    CHECK(fixture.additions == 1);
    CHECK(fixture.selected.configDirectory == fixture.accounts[1].configDirectory);
}

TEST_CASE("Steam registration errors require an explicit retry and preserve already-added state") {
    Fixture fixture;
    fixture.addError = "Fully exit Steam before adding BLACK.";
    SteamFlow flow(shortcut(), fixture.operations());
    flow.handle({row(flow.page(), "ADD TO STEAM"), 0});
    auto page = flow.page();
    CHECK(page.id == "steam-ready");
    CHECK(page.noticeIsProblem);
    CHECK(page.notice.find(fixture.addError) != std::string::npos);
    CHECK(fixture.additions == 1);
    flow.update(2.0, row(page, "TRY AGAIN"));
    page = flow.page();
    CHECK(fixture.additions == 1);
    fixture.addError.clear();
    fixture.alreadyPresent = true;
    flow.handle({row(page, "TRY AGAIN"), 0});
    page = flow.page();
    CHECK(page.title == "ALREADY IN STEAM");
    CHECK(page.notice == "Open Steam to find BLACK in your Library.");
    CHECK_FALSE(page.noticeIsProblem);
    CHECK(fixture.additions == 2);
    CHECK(flow.back() == MenuFlow::Outcome::Back);
}

TEST_CASE("Steam discovery handles absent accounts and read errors without registration") {
    Fixture fixture;
    fixture.accounts.clear();
    SteamFlow flow(shortcut(), fixture.operations());
    auto page = flow.page();
    CHECK(page.id == "steam-unavailable");
    CHECK(page.notice.find("No Steam accounts were found") != std::string::npos);
    fixture.discoveryError = "Steam configuration could not be read.";
    flow.handle({row(page, "CHECK AGAIN"), 0});
    page = flow.page();
    CHECK(page.noticeIsProblem);
    CHECK(page.notice.find(fixture.discoveryError) != std::string::npos);
    fixture.discoveryError.clear();
    fixture.accounts.push_back({"new-account/config", "New player"});
    flow.handle({row(page, "CHECK AGAIN"), 0});
    CHECK(flow.page().id == "steam-ready");
    CHECK(fixture.discoveries == 3);
    CHECK(fixture.additions == 0);
}

TEST_CASE("Steam project shortcuts target the portable launcher and project working directory") {
    Fixture fixture;
    const black::core::ProjectLayout project(std::filesystem::absolute("BLACK portable installation"));
    SteamFlow flow(project, fixture.operations());
    flow.handle({row(flow.page(), "ADD TO STEAM"), 0});
#ifdef _WIN32
    CHECK(fixture.registered.executable == project.root() / "BLACK Launcher.exe");
#else
    CHECK(fixture.registered.executable == project.root() / "BLACK Launcher");
#endif
    CHECK(fixture.registered.startDirectory == project.root());
    CHECK(fixture.registered.launchOptions == "--play");
    CHECK(fixture.registered.appName == "BLACK");
}
