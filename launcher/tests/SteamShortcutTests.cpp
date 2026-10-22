#include "TempFolder.h"
#include "core/Files.h"
#include "core/SteamShortcut.h"
#include "core/Text.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace black::core;
namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

void text(Bytes& bytes, std::string_view value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}

void stringField(Bytes& bytes, std::string_view key, std::string_view value) {
    bytes.push_back(1);
    text(bytes, key);
    text(bytes, value);
}

void scalar(Bytes& bytes, std::uint8_t type, std::string_view key, std::uint64_t value, unsigned count = 4) {
    bytes.push_back(type);
    text(bytes, key);
    for (unsigned index = 0; index < count; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

Bytes emptyLibrary() {
    Bytes data{0};
    text(data, "shortcuts");
    data.insert(data.end(), {8, 8});
    return data;
}

Bytes otherRecord(std::string_view index = "0") {
    Bytes record{0};
    text(record, index);
    stringField(record, "AppName", "Another game");
    stringField(record, "Exe", "\"/another/game\"");
    stringField(record, "LaunchOptions", "--custom --arguments");
    scalar(record, 2, "appid", 1234);
    scalar(record, 3, "future_float", 0x7FC00001U);
    scalar(record, 4, "future_pointer", 0xF0123456U);
    scalar(record, 6, "future_color", 0x12345678U);
    scalar(record, 7, "future_unsigned", 0xF123456789ABCDEFULL, 8);
    scalar(record, 10, "future_signed", 0x8123456789ABCDEFULL, 8);
    record.push_back(5);
    text(record, "future_wide");
    record.insert(record.end(), {'H', 0, 'D', 0, 0, 0});
    record.push_back(0);
    text(record, "tags");
    stringField(record, "0", "Favorites");
    stringField(record, "1", "Personal tag");
    record.insert(record.end(), {8, 8});
    return record;
}

void insertRecord(Bytes& library, const Bytes& record) {
    library.insert(library.end() - 2, record.begin(), record.end());
}

void put(const fs::path& path, const Bytes& data) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));  // NOLINT
    REQUIRE(output.good());
}

Bytes get(const fs::path& path) {
    const auto data = readFile(path);
    REQUIRE(data.ok());
    return data.value();
}

bool contains(const Bytes& data, const Bytes& part) {
    return std::search(data.begin(), data.end(), part.begin(), part.end()) != data.end();
}

bool contains(const Bytes& data, std::string_view part) {
    const Bytes bytes(part.begin(), part.end());
    return contains(data, bytes);
}

SteamShortcut sampleShortcut() {
#ifdef _WIN32
    return {L"C:\\Games\\BLACK PC\\BLACK Launcher.exe", L"C:\\Games\\BLACK PC"};
#else
    return {"/games/BLACK PC/BLACK Launcher", "/games/BLACK PC"};
#endif
}

SteamShortcut localShortcut(const black::tests::TempFolder& folder) {
    folder.write("BLACK PC/BLACK Launcher", "synthetic launcher");
    return {folder.path() / "BLACK PC" / "BLACK Launcher", folder.path() / "BLACK PC"};
}

SteamAccount localAccount(const black::tests::TempFolder& folder) {
    const fs::path config = folder.path() / "Steam" / "userdata" / "123" / "config";
    fs::create_directories(config);
    return {config, "Synthetic account"};
}

Result<bool> closedSteam() {
    return false;
}

}  // namespace

TEST_CASE("Steam binary shortcut preserves all unrelated bytes and tags") {
    const SteamShortcut shortcut = sampleShortcut();
    Bytes original = emptyLibrary();
    const Bytes first = otherRecord("3");
    const Bytes second = otherRecord("17");
    insertRecord(original, first);
    insertRecord(original, second);
    const auto added = steam_shortcut::append(original, shortcut);
    REQUIRE(added.ok());
    CHECK_FALSE(added.value().alreadyPresent);
    CHECK(contains(added.value().bytes, first));
    CHECK(contains(added.value().bytes, second));
    CHECK(std::equal(original.begin(), original.end() - 2, added.value().bytes.begin()));
    const Bytes nextIndex{0, '1', '8', 0};
    CHECK(contains(added.value().bytes, nextIndex));
    CHECK(contains(added.value().bytes, '"' + toUtf8(shortcut.executable) + '"'));
    CHECK(contains(added.value().bytes, '"' + toUtf8(shortcut.startDirectory) + '"'));
    CHECK(contains(added.value().bytes, "--play"));
    const auto again = steam_shortcut::append(added.value().bytes, shortcut);
    REQUIRE(again.ok());
    CHECK(again.value().alreadyPresent);
    CHECK(again.value().bytes == added.value().bytes);
}

TEST_CASE("Steam shortcut app ID has an independent CRC32 oracle") {
    SteamShortcut shortcut = sampleShortcut();
    shortcut.appName = "BLACK";
    const auto added = steam_shortcut::append({}, shortcut);
    REQUIRE(added.ok());
    Bytes expected;
#ifdef _WIN32
    // Python zlib.crc32(b'"C:\\Games\\BLACK PC\\BLACK Launcher.exe"BLACK') | 0x80000000.
    scalar(expected, 2, "appid", 0x856FE3C2U);
#else
    // Python zlib.crc32(b'"/games/BLACK PC/BLACK Launcher"BLACK') | 0x80000000.
    scalar(expected, 2, "appid", 0x8356EECDU);
#endif
    CHECK(contains(added.value().bytes, expected));
}

TEST_CASE("Steam shortcuts keep Unicode and distinct installations") {
    SteamShortcut shortcut = sampleShortcut();
    shortcut.executable = shortcut.executable.parent_path() / pathFromUtf8("Jo\xC3\xA3o \xE6\xB5\x8B\xE8\xAF\x95") /
                          shortcut.executable.filename();
    shortcut.startDirectory = shortcut.executable.parent_path();
    const auto added = steam_shortcut::append({}, shortcut);
    REQUIRE(added.ok());
    CHECK(contains(added.value().bytes, toUtf8(shortcut.executable)));
    CHECK(steam_shortcut::append(added.value().bytes, shortcut).value().alreadyPresent);
    SteamShortcut other = shortcut;
    other.executable = other.executable.parent_path() / "second installation" / other.executable.filename();
    other.startDirectory = other.executable.parent_path();
    const auto second = steam_shortcut::append(added.value().bytes, other);
    REQUIRE(second.ok());
    CHECK_FALSE(second.value().alreadyPresent);
    CHECK(contains(second.value().bytes, toUtf8(shortcut.executable)));
    CHECK(contains(second.value().bytes, toUtf8(other.executable)));
}

TEST_CASE("Steam shortcut identities require launch options and reject conflicting IDs") {
    SteamShortcut settings = sampleShortcut();
    settings.launchOptions = "--settings";
    const auto old = steam_shortcut::append({}, settings);
    REQUIRE(old.ok());
    const auto play = steam_shortcut::append(old.value().bytes, sampleShortcut());
    REQUIRE_FALSE(play.ok());
    CHECK(play.error().message.find("ID") != std::string::npos);
}

TEST_CASE("Steam malformed files fail closed including truncation unknown types and duplicate keys") {
    const SteamShortcut shortcut = sampleShortcut();
    Bytes original = emptyLibrary();
    insertRecord(original, otherRecord());
    for (std::size_t length = 1; length < original.size(); ++length) {
        CAPTURE(length);
        CHECK_FALSE(steam_shortcut::append(std::span(original).first(length), shortcut).ok());
    }
    Bytes trailing = original;
    trailing.push_back(0);
    CHECK_FALSE(steam_shortcut::append(trailing, shortcut).ok());
    Bytes unknown{0};
    text(unknown, "0");
    scalar(unknown, 42, "unrecognized", 0);
    unknown.push_back(8);
    Bytes library = emptyLibrary();
    insertRecord(library, unknown);
    CHECK_FALSE(steam_shortcut::append(library, shortcut).ok());
    library = original;
    insertRecord(library, otherRecord());
    CHECK_FALSE(steam_shortcut::append(library, shortcut).ok());
    Bytes duplicate{0};
    text(duplicate, "0");
    stringField(duplicate, "Exe", "one");
    stringField(duplicate, "exe", "two");
    duplicate.push_back(8);
    library = emptyLibrary();
    insertRecord(library, duplicate);
    CHECK_FALSE(steam_shortcut::append(library, shortcut).ok());
    library = emptyLibrary();
    insertRecord(library, otherRecord("not-an-index"));
    CHECK_FALSE(steam_shortcut::append(library, shortcut).ok());
    library = emptyLibrary();
    insertRecord(library, otherRecord("4294967295"));
    CHECK_FALSE(steam_shortcut::append(library, shortcut).ok());
}

TEST_CASE("Steam binary parser bounds depth strings and whole files") {
    const SteamShortcut shortcut = sampleShortcut();
    Bytes deep{0};
    text(deep, "shortcuts");
    for (unsigned i = 0; i < 40; ++i) {
        deep.push_back(0);
        text(deep, "0");
    }
    deep.insert(deep.end(), 42, 8);
    CHECK_FALSE(steam_shortcut::append(deep, shortcut).ok());
    Bytes huge = emptyLibrary();
    huge.resize(16 * 1024 * 1024 + 1);
    CHECK_FALSE(steam_shortcut::append(huge, shortcut).ok());
    Bytes longString{0};
    text(longString, "0");
    stringField(longString, "AppName", std::string(65537, 'a'));
    longString.push_back(8);
    Bytes library = emptyLibrary();
    insertRecord(library, longString);
    CHECK_FALSE(steam_shortcut::append(library, shortcut).ok());
    SteamShortcut invalid = shortcut;
    invalid.launchOptions = std::string("--play\0hidden", 13);
    CHECK_FALSE(steam_shortcut::append({}, invalid).ok());
}

TEST_CASE("Steam registration creates one file then repeats without writes") {
    const black::tests::TempFolder folder;
    const SteamAccount account = localAccount(folder);
    const SteamShortcut shortcut = localShortcut(folder);
    const auto added = steam_shortcut::add(account, shortcut, closedSteam);
    REQUIRE(added.ok());
    CHECK_FALSE(added.value().alreadyPresent);
    CHECK(added.value().backupPath.empty());
    const fs::path library = account.configDirectory / "shortcuts.vdf";
    const Bytes original = get(library);
    const auto timestamp = fs::last_write_time(library);
    const auto again = steam_shortcut::add(account, shortcut, closedSteam);
    REQUIRE(again.ok());
    CHECK(again.value().alreadyPresent);
    CHECK(again.value().backupPath.empty());
    CHECK(get(library) == original);
    CHECK(fs::last_write_time(library) == timestamp);
    CHECK(std::distance(fs::directory_iterator(account.configDirectory), fs::directory_iterator{}) == 1);
}

TEST_CASE("Steam registration backs up exact bytes before an atomic replacement") {
    const black::tests::TempFolder folder;
    const SteamAccount account = localAccount(folder);
    const SteamShortcut shortcut = localShortcut(folder);
    Bytes original = emptyLibrary();
    insertRecord(original, otherRecord("7"));
    const fs::path library = account.configDirectory / "shortcuts.vdf";
    put(library, original);
    const auto added = steam_shortcut::add(account, shortcut, closedSteam);
    REQUIRE(added.ok());
    CHECK_FALSE(added.value().backupPath.empty());
    CHECK(get(added.value().backupPath) == original);
    CHECK(get(library) != original);
    CHECK(contains(get(library), otherRecord("7")));
}

TEST_CASE("Steam running and process inspection failures never write") {
    const black::tests::TempFolder folder;
    const SteamAccount account = localAccount(folder);
    const SteamShortcut shortcut = localShortcut(folder);
    const fs::path library = account.configDirectory / "shortcuts.vdf";
    const Bytes original = emptyLibrary();
    put(library, original);
    const auto running = steam_shortcut::add(account, shortcut, [] { return Result<bool>(true); });
    REQUIRE_FALSE(running.ok());
    CHECK(running.error().message.find("Exit Steam") != std::string::npos);
    const auto failed = steam_shortcut::add(account, shortcut, [] { return Result<bool>(Error{"inspection failed"}); });
    REQUIRE_FALSE(failed.ok());
    CHECK(get(library) == original);
    CHECK(std::distance(fs::directory_iterator(account.configDirectory), fs::directory_iterator{}) == 1);
    unsigned observations = 0;
    const auto startedDuringWrite =
        steam_shortcut::add(account, shortcut, [&observations] { return Result<bool>(++observations > 1); });
    REQUIRE_FALSE(startedDuringWrite.ok());
    CHECK(get(library) == original);
}

TEST_CASE("Steam changed libraries are not overwritten after writing a candidate") {
    const black::tests::TempFolder folder;
    const SteamAccount account = localAccount(folder);
    const SteamShortcut shortcut = localShortcut(folder);
    const fs::path library = account.configDirectory / "shortcuts.vdf";
    Bytes original = emptyLibrary();
    put(library, original);
    Bytes changed = original;
    insertRecord(changed, otherRecord());
    unsigned observations = 0;
    const auto result = steam_shortcut::add(account, shortcut, [&] {
        if (++observations == 2) {
            put(library, changed);
        }
        return Result<bool>(false);
    });
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("changed") != std::string::npos);
    CHECK(get(library) == changed);
}

TEST_CASE("Steam registration refuses empty malformed missing and directory targets") {
    const black::tests::TempFolder folder;
    const SteamAccount account = localAccount(folder);
    const SteamShortcut shortcut = localShortcut(folder);
    const fs::path library = account.configDirectory / "shortcuts.vdf";
    for (const Bytes& data : {Bytes{}, Bytes{0, 1, 2}, Bytes{8}}) {
        put(library, data);
        CHECK_FALSE(steam_shortcut::add(account, shortcut, closedSteam).ok());
        CHECK(get(library) == data);
        CHECK(std::distance(fs::directory_iterator(account.configDirectory), fs::directory_iterator{}) == 1);
    }
    fs::remove(library);
    fs::create_directory(library);
    CHECK_FALSE(steam_shortcut::add(account, shortcut, closedSteam).ok());
    CHECK(fs::is_directory(library));
    SteamAccount missing{folder.path() / "not present", "Missing"};
    CHECK_FALSE(steam_shortcut::add(missing, shortcut, closedSteam).ok());
    SteamShortcut moved = shortcut;
    moved.executable /= "not present";
    CHECK_FALSE(steam_shortcut::add(account, moved, closedSteam).ok());
}

TEST_CASE("Steam discovers multiple accounts and safely resolves installation aliases") {
    const black::tests::TempFolder folder;
    const fs::path root = folder.path() / "Steam";
    folder.write("Steam/userdata/123/config/localconfig.vdf", "fixture");
    folder.write("Steam/userdata/456/config/localconfig.vdf", "fixture");
    folder.write("Steam/userdata/0/config/localconfig.vdf", "not an account");
    folder.write("Steam/userdata/not-an-id/config/localconfig.vdf", "not an account");
    folder.write("Steam/config/loginusers.vdf",
                 "\"users\"\n{\n\"76561197960265851\" { \"PersonaName\" \"Test Name\" }\n}");
    const auto found = findSteamAccounts({root, root, folder.path() / "missing"});
    REQUIRE(found.ok());
    REQUIRE(found.value().size() == 2);
    CHECK(fs::equivalent(found.value()[0].configDirectory, root / "userdata" / "123" / "config"));
    CHECK(found.value()[0].displayName == "Test Name (123)");
    CHECK(found.value()[1].displayName == "Steam account 456");
    folder.write("Steam/config/loginusers.vdf", "\"users\" { malformed");
    CHECK(findSteamAccounts({root}).value()[0].displayName == "Steam account 123");
}

#ifdef _WIN32
TEST_CASE("Steam failed atomic replacement retains the original and exact backup") {
    const black::tests::TempFolder folder;
    const SteamAccount account = localAccount(folder);
    const SteamShortcut shortcut = localShortcut(folder);
    const fs::path library = account.configDirectory / "shortcuts.vdf";
    const Bytes original = emptyLibrary();
    put(library, original);
    HANDLE locked = INVALID_HANDLE_VALUE;
    unsigned observations = 0;
    const auto result = steam_shortcut::add(account, shortcut, [&] {
        if (++observations == 2) {
            locked = CreateFileW(library.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        return Result<bool>(false);
    });
    const bool lockOpened = locked != INVALID_HANDLE_VALUE;
    if (lockOpened) {
        CloseHandle(locked);
    }
    REQUIRE(lockOpened);
    REQUIRE_FALSE(result.ok());
    CHECK(get(library) == original);
    unsigned backups = 0;
    for (const auto& entry : fs::directory_iterator(account.configDirectory)) {
        if (entry.path().extension() == ".bak") {
            ++backups;
            CHECK(get(entry.path()) == original);
        }
        CHECK(entry.path().extension() != ".tmp");
    }
    CHECK(backups == 1);
}
#endif

TEST_CASE("Steam rejects symlinked library files without modifying their target") {
    const black::tests::TempFolder folder;
    const SteamAccount account = localAccount(folder);
    const SteamShortcut shortcut = localShortcut(folder);
    const fs::path target = folder.path() / "outside.vdf";
    const Bytes original = emptyLibrary();
    put(target, original);
    const fs::path link = account.configDirectory / "shortcuts.vdf";
    std::error_code error;
    fs::create_symlink(target, link, error);
    if (error) {
        MESSAGE("Symlink creation unavailable on this host; Linux CI exercises this safety arm.");
        return;
    }
    const auto result = steam_shortcut::add(account, shortcut, closedSteam);
    CHECK_FALSE(result.ok());
    CHECK(get(target) == original);
    CHECK(fs::is_symlink(fs::symlink_status(link)));
    fs::remove(link);
}
