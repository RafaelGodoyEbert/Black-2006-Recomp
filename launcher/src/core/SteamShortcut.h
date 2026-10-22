#pragma once

#include "core/Result.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace black::core {

struct SteamAccount {
    std::filesystem::path configDirectory;
    std::string displayName;
};

struct SteamShortcut {
    std::filesystem::path executable;
    std::filesystem::path startDirectory;
    std::string launchOptions = "--play";
    std::string appName = "BLACK";
};

struct SteamShortcutResult {
    bool alreadyPresent = false;
    std::filesystem::path backupPath;
};

/// Finds local Steam accounts; each result must be selected explicitly by the player.
[[nodiscard]] Result<std::vector<SteamAccount>> findSteamAccounts();
/// Explicit roots support portable Steam installations and isolated discovery fixtures.
[[nodiscard]] Result<std::vector<SteamAccount>> findSteamAccounts(const std::vector<std::filesystem::path>& roots);

/// Adds a non-Steam shortcut only while Steam is closed; preserves existing entries and backs up before replacing.
[[nodiscard]] Result<SteamShortcutResult> addSteamShortcut(const SteamAccount& account, const SteamShortcut& shortcut);

namespace steam_shortcut {

struct Update {
    std::vector<std::uint8_t> bytes;
    bool alreadyPresent = false;
};

/// Bounded binary KeyValues transformation. Unrelated entries retain their exact original bytes.
[[nodiscard]] Result<Update> append(std::span<const std::uint8_t> original, const SteamShortcut& shortcut);
/// Injectable process observation for deterministic filesystem tests; the normal entry point uses OS detection.
[[nodiscard]] Result<SteamShortcutResult> add(const SteamAccount& account, const SteamShortcut& shortcut,
                                              const std::function<Result<bool>()>& steamIsRunning);

}  // namespace steam_shortcut
}  // namespace black::core
