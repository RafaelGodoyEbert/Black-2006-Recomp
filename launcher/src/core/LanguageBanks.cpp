#include "core/LanguageBanks.h"

#include "core/Settings.h"
#include "core/Sha256.h"
#include "core/Text.h"

#include <algorithm>

namespace black::core {
namespace fs = std::filesystem;
namespace {

fs::path gameBankPath(const ProjectLayout& project) {
    return project.gameDirectory() / "language" / "strings" / "MainUS.bin";
}

fs::path gameFontPath(const ProjectLayout& project, std::string_view fontName) {
    return project.gameDirectory() / "language" / "fonts" / fontName;
}

fs::path preservedEnglishFontPath(const ProjectLayout& project, std::string_view fontName) {
    return project.localDirectory() / "language-banks" / "en-US" / "fonts" / fontName;
}

std::string hashOf(const fs::path& path) {
    const auto digest = sha256File(path);
    return digest ? Sha256::toHex(digest.value()) : std::string();
}

/// Copies beside the target and renames over it, so the game never sees half a bank.
Result<bool> replaceFile(const fs::path& source, const fs::path& target, bool overwrite) {
    std::error_code error;
    const fs::path temporary = target.string() + ".launcher-tmp";
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, error);
    if (!error) {
        if (!overwrite && fs::exists(target, error)) {
            fs::remove(temporary, error);
            return false;
        }
        fs::rename(temporary, target, error);
    }
    if (error) {
        fs::remove(temporary, error);
        return Error{"Could not write " + toUtf8(target)};
    }
    return true;
}

}  // namespace

bool isMachineDraft(std::string_view language) {
    return language == "es" || language == "fr" || language == "de" || language == "it";
}

fs::path languageBankPath(const ProjectLayout& project, std::string_view language) {
    return project.localDirectory() / "language-banks" / fs::path(language) / "MainUS.bin";
}

Result<bool> preserveEnglishBank(const ProjectLayout& project, std::string_view retailEnglishSha256) {
    const fs::path english = languageBankPath(project, "en-US");
    const fs::path game = gameBankPath(project);
    std::error_code error;
    if (fs::is_regular_file(english, error)) {
        return false;
    }
    if (!fs::is_regular_file(game, error)) {
        return Error{"The game's menu text (game/language/strings/MainUS.bin) is missing."};
    }
    if (hashOf(game) != retailEnglishSha256) {
        return Error{"The game's MainUS.bin is not the retail English one. Put the original back (from your disc) at " +
                     toUtf8(english) + " before changing the menu language."};
    }
    fs::create_directories(english.parent_path(), error);
    return replaceFile(game, english, false);
}

fs::path languageFontPath(const ProjectLayout& project, std::string_view language, std::string_view fontName) {
    const fs::path translationFont = project.root() / "translations" / fs::path(language) / "fonts" / fontName;
    std::error_code error;
    if (fs::is_regular_file(translationFont, error)) {
        return translationFont;
    }
    return project.localDirectory() / "language-banks" / fs::path(language) / "fonts" / fontName;
}

Result<bool> preserveEnglishFonts(const ProjectLayout& project) {
    std::error_code error;
    bool preservedAny = false;
    for (const char* fontName : {"Big.bin", "Small.bin"}) {
        const fs::path english = preservedEnglishFontPath(project, fontName);
        const fs::path game = gameFontPath(project, fontName);
        if (fs::is_regular_file(english, error)) {
            continue;
        }
        if (!fs::is_regular_file(game, error)) {
            continue;
        }
        fs::path candidate = game;
        const fs::path backupDir = game.parent_path() / "backups";
        if (fs::is_directory(backupDir, error)) {
            const std::string stem = fs::path(fontName).stem().string();
            for (const auto& entry : fs::directory_iterator(backupDir, error)) {
                if (entry.is_regular_file() && entry.path().filename().string().rfind(stem, 0) == 0) {
                    candidate = entry.path();
                    break;
                }
            }
        }
        fs::create_directories(english.parent_path(), error);
        auto res = replaceFile(candidate, english, false);
        if (!res) {
            return res;
        }
        preservedAny = true;
    }
    return preservedAny;
}

Result<bool> installLanguageFonts(const ProjectLayout& project, std::string_view language) {
    std::error_code error;
    const auto preserved = preserveEnglishFonts(project);
    if (!preserved) {
        return preserved;
    }

    for (const char* fontName : {"Big.bin", "Small.bin"}) {
        const fs::path target = gameFontPath(project, fontName);
        if (!fs::is_regular_file(target, error)) {
            continue;
        }

        const fs::path custom = languageFontPath(project, language, fontName);
        const fs::path english = preservedEnglishFontPath(project, fontName);

        fs::path source;
        if (fs::is_regular_file(custom, error)) {
            source = custom;
        } else if (fs::is_regular_file(english, error)) {
            source = english;
        }

        if (!source.empty() && fs::is_regular_file(source, error)) {
            if (hashOf(source) != hashOf(target)) {
                auto res = replaceFile(source, target, true);
                if (!res) {
                    return res;
                }
            }
        }
    }
    return true;
}

Result<bool> installLanguageBank(const ProjectLayout& project, std::string_view language,
                                 std::string_view retailEnglishSha256) {
    if (std::find(choices::kLanguages.begin(), choices::kLanguages.end(), language) == choices::kLanguages.end()) {
        return Error{"Unknown menu language " + std::string(language)};
    }
    const auto fontResult = installLanguageFonts(project, language);
    if (!fontResult) {
        return fontResult;
    }
    const fs::path target = gameBankPath(project);
    const fs::path source = languageBankPath(project, language);
    std::error_code error;
    if (!fs::is_regular_file(target, error)) {
        return Error{"The game's menu text (game/language/strings/MainUS.bin) is missing."};
    }
    if (language != "en-US" && !fs::is_regular_file(source, error)) {
        return Error{"The " + std::string(language) + " menu text is missing: " + toUtf8(source)};
    }
    const auto preserved = preserveEnglishBank(project, retailEnglishSha256);
    if (!preserved) {
        return preserved;
    }
    if (preserved.value() && language == "en-US") {
        return true;
    }
    if (hashOf(source) == hashOf(target)) {
        return true;
    }
    return replaceFile(source, target, true);
}

}  // namespace black::core
