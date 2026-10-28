#include "TempFolder.h"
#include "core/Files.h"
#include "core/LanguageBanks.h"
#include "core/Sha256.h"

#include <doctest/doctest.h>

using namespace black::core;

namespace {

std::string text(const std::filesystem::path& path) {
    const auto bytes = readFile(path);
    return bytes ? std::string(bytes.value().begin(), bytes.value().end()) : std::string("<missing>");
}

std::string hashOf(std::string_view data) {
    Sha256 hash;
    hash.update(std::as_bytes(std::span(data)));
    return Sha256::toHex(hash.finish());
}

}  // namespace

TEST_CASE("Changing the menu language keeps the retail English bank first") {
    const black::tests::TempFolder folder;
    const ProjectLayout project(folder.path());
    folder.write("game/language/strings/MainUS.bin", "english");
    folder.write("local/language-banks/pt-BR/MainUS.bin", "portugues");
    const std::string retail = hashOf("english");
    const auto game = folder.path() / "game/language/strings/MainUS.bin";

    REQUIRE(installLanguageBank(project, "pt-BR", retail).ok());
    CHECK(text(game) == "portugues");
    CHECK(text(languageBankPath(project, "en-US")) == "english");

    REQUIRE(installLanguageBank(project, "en-US", retail).ok());
    CHECK(text(game) == "english");

    CHECK_FALSE(installLanguageBank(project, "fr", retail).ok());  // no French bank
    CHECK_FALSE(installLanguageBank(project, "xx", retail).ok());
}

TEST_CASE("An unrecognised game bank is never kept as English") {
    const black::tests::TempFolder folder;
    const ProjectLayout project(folder.path());
    folder.write("game/language/strings/MainUS.bin", "modified");
    folder.write("local/language-banks/pt-BR/MainUS.bin", "portugues");
    const auto installed = installLanguageBank(project, "pt-BR", hashOf("english"));
    REQUIRE_FALSE(installed.ok());
    CHECK(installed.error().message.find("retail English") != std::string::npos);
    CHECK(text(folder.path() / "game/language/strings/MainUS.bin") == "modified");
}

TEST_CASE("Language fonts are swapped for pt-BR and restored for other languages") {
    const black::tests::TempFolder folder;
    const ProjectLayout project(folder.path());
    folder.write("game/language/strings/MainUS.bin", "english_bank");
    folder.write("local/language-banks/pt-BR/MainUS.bin", "portugues_bank");
    folder.write("game/language/fonts/Big.bin", "english_big_font");
    folder.write("game/language/fonts/Small.bin", "english_small_font");
    folder.write("translations/pt-BR/fonts/Big.bin", "ptbr_big_font");
    folder.write("translations/pt-BR/fonts/Small.bin", "ptbr_small_font");

    const std::string retailBank = hashOf("english_bank");
    const auto bigFont = folder.path() / "game/language/fonts/Big.bin";
    const auto smallFont = folder.path() / "game/language/fonts/Small.bin";

    REQUIRE(installLanguageBank(project, "pt-BR", retailBank).ok());
    CHECK(text(bigFont) == "ptbr_big_font");
    CHECK(text(smallFont) == "ptbr_small_font");

    REQUIRE(installLanguageBank(project, "en-US", retailBank).ok());
    CHECK(text(bigFont) == "english_big_font");
    CHECK(text(smallFont) == "english_small_font");
}
