#include "app/SteamFlow.h"

#include "app/Translations.h"

#include <utility>

namespace black::app {
namespace {

core::SteamShortcut shortcutFor(const core::ProjectLayout& project) {
    core::SteamShortcut shortcut;
#ifdef _WIN32
    shortcut.executable = project.root() / "BLACK Launcher.exe";
#else
    shortcut.executable = project.root() / "BLACK Launcher";
#endif
    shortcut.startDirectory = project.root();
    return shortcut;
}

}  // namespace

SteamFlow::SteamFlow(const core::ProjectLayout& project)
    : SteamFlow(project, {[] { return core::findSteamAccounts(); },
                          [](const core::SteamAccount& account, const core::SteamShortcut& shortcut) {
                              return core::addSteamShortcut(account, shortcut);
                          }}) {}

SteamFlow::SteamFlow(const core::ProjectLayout& project, Operations operations)
    : SteamFlow(shortcutFor(project), std::move(operations)) {}

SteamFlow::SteamFlow(core::SteamShortcut shortcut, Operations operations)
    : m_shortcut(std::move(shortcut)), m_operations(std::move(operations)) {
    findAccounts();
}

void SteamFlow::findAccounts() {
    m_accounts.clear();
    m_account = 0;
    m_result.reset();
    m_problem.clear();
    const auto found = m_operations.findAccounts();
    if (!found) {
        m_problem = trf("Steam accounts could not be found: {0}", {found.error().message});
        m_screen = Screen::Unavailable;
        return;
    }
    m_accounts = found.value();
    m_screen = m_accounts.empty() ? Screen::Unavailable : m_accounts.size() == 1 ? Screen::Ready : Screen::Accounts;
}

void SteamFlow::addShortcut() {
    const auto added = m_operations.addShortcut(m_accounts[m_account], m_shortcut);
    if (!added) {
        m_problem = trf("Could not add BLACK to Steam: {0}", {added.error().message});
        return;
    }
    m_result = added.value();
    m_problem.clear();
    m_screen = Screen::Done;
}

Page SteamFlow::page() {
    Page page;
    page.title = tr("ADD TO STEAM");
    page.notice = m_problem;
    page.noticeIsProblem = !m_problem.empty();
    switch (m_screen) {
        case Screen::Accounts:
            page.id = "steam-accounts";
            page.notice = tr("Choose the Steam account whose Library should contain BLACK.");
            for (const auto& account : m_accounts) {
                page.rows.push_back({account.displayName, "", tr("Use this Steam account."), true});
            }
            page.rows.push_back({tr("CHECK AGAIN"), "", tr("Looks for Steam accounts again."), true});
            page.rows.push_back({tr("BACK"), "", "", true});
            break;
        case Screen::Ready:
            page.id = "steam-ready";
            page.initialRow = 1;
            if (m_problem.empty()) {
                page.notice =
                    tr("Fully exit Steam, then choose Add to Steam. This adds a shortcut to your existing "
                       "installation.");
            }
            page.rows = {
                {tr("STEAM ACCOUNT"), m_accounts[m_account].displayName, "", false},
                {m_problem.empty() ? tr("ADD TO STEAM") : tr("TRY AGAIN"), "",
                 tr("Starts BLACK from Steam with this installation and your saved settings."), true},
            };
            if (m_accounts.size() > 1) {
                page.rows.push_back({tr("CHOOSE ANOTHER ACCOUNT"), "", tr("Return to the Steam account list."), true});
            }
            page.rows.push_back({tr("BACK"), "", "", true});
            break;
        case Screen::Unavailable:
            page.id = "steam-unavailable";
            if (m_problem.empty()) {
                page.notice =
                    tr("No Steam accounts were found. Install Steam and sign in once, fully exit it, then "
                       "check again.");
            }
            page.rows = {{tr("CHECK AGAIN"), "", tr("Looks for Steam accounts again."), true},
                         {tr("BACK"), "", "", true}};
            break;
        case Screen::Done:
            page.id = "steam-done";
            page.title = m_result->alreadyPresent ? tr("ALREADY IN STEAM") : tr("ADDED TO STEAM");
            page.notice = tr("Open Steam to find BLACK in your Library.");
            page.rows = {{tr("DONE"), "", "", true}};
            break;
    }
    return page;
}

MenuFlow::Outcome SteamFlow::handle(MenuEvent event) {
    if (event.step != 0) {
        return Outcome::Stay;
    }
    switch (m_screen) {
        case Screen::Accounts:
            if (event.row < m_accounts.size()) {
                m_account = event.row;
                m_problem.clear();
                m_screen = Screen::Ready;
            } else if (event.row == m_accounts.size()) {
                findAccounts();
            } else if (event.row == m_accounts.size() + 1) {
                return Outcome::Back;
            }
            break;
        case Screen::Ready:
            if (event.row == 1) {
                addShortcut();
            } else if (m_accounts.size() > 1 && event.row == 2) {
                m_problem.clear();
                m_screen = Screen::Accounts;
            } else if (event.row == (m_accounts.size() > 1 ? 3u : 2u)) {
                return Outcome::Back;
            }
            break;
        case Screen::Unavailable:
            if (event.row == 0) {
                findAccounts();
            } else if (event.row == 1) {
                return Outcome::Back;
            }
            break;
        case Screen::Done:
            if (event.row == 0) {
                return Outcome::Back;
            }
            break;
    }
    return Outcome::Stay;
}

MenuFlow::Outcome SteamFlow::back() {
    if (m_screen == Screen::Ready && m_accounts.size() > 1) {
        m_problem.clear();
        m_screen = Screen::Accounts;
        return Outcome::Stay;
    }
    return Outcome::Back;
}

}  // namespace black::app
