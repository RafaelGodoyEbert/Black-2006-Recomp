#pragma once

#include "app/MenuFlow.h"
#include "core/ProjectLayout.h"
#include "core/SteamShortcut.h"

#include <functional>
#include <optional>
#include <vector>

namespace black::app {

/// Adding a local Steam shortcut is a separate, explicit action. Discovery and
/// page navigation never change Steam's files. Operations can be replaced in tests.
class SteamFlow final : public MenuFlow {
public:
    struct Operations {
        std::function<core::Result<std::vector<core::SteamAccount>>()> findAccounts;
        std::function<core::Result<core::SteamShortcutResult>(const core::SteamAccount&, const core::SteamShortcut&)>
            addShortcut;
    };

    explicit SteamFlow(const core::ProjectLayout& project);
    SteamFlow(const core::ProjectLayout& project, Operations operations);
    SteamFlow(core::SteamShortcut shortcut, Operations operations);

    void update(double, std::size_t) override {}
    [[nodiscard]] Page page() override;
    Outcome handle(MenuEvent event) override;
    Outcome back() override;
    [[nodiscard]] bool busy() const override { return false; }

private:
    enum class Screen { Accounts, Ready, Unavailable, Done };

    void findAccounts();
    void addShortcut();

    core::SteamShortcut m_shortcut;
    Operations m_operations;
    std::vector<core::SteamAccount> m_accounts;
    std::size_t m_account = 0;
    Screen m_screen = Screen::Unavailable;
    std::optional<core::SteamShortcutResult> m_result;
    std::string m_problem;
};

}  // namespace black::app
