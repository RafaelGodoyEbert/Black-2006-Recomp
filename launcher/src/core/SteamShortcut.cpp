#include "core/SteamShortcut.h"

#include "core/Text.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// after windows.h, which declares the Windows types used by Tool Help
#include <tlhelp32.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace black::core {
namespace fs = std::filesystem;
namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t kMaxFile = std::size_t{16} * 1024 * 1024;
constexpr std::size_t kMaxString = std::size_t{64} * 1024;
constexpr std::size_t kMaxNodes = 100000;
constexpr unsigned kMaxDepth = 32;
constexpr std::uint64_t kSteamIdBase = 76561197960265728ULL;
constexpr std::string_view kMalformed =
    "Steam's shortcuts file is malformed or uses an unsupported format. "
    "It was left unchanged.";

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

template <typename T>
std::optional<T> number(std::string_view text) {
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

struct Node {
    std::uint8_t type = 0;
    std::string_view key;
    std::string_view text;
    std::uint32_t integer = 0;
    std::size_t end = 0;
    std::vector<Node> children;
};

// Binary KV1 types are documented by Valve's KeyValues::ReadAsBinary and ValvePython/vdf.
// We splice the new record before the root terminator instead of reserializing existing values.
class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::uint8_t> data) : m_data(data) {}

    bool list(std::vector<Node>& nodes, unsigned depth = 0) {  // NOLINT(misc-no-recursion): bounded to 32 levels
        if (depth > kMaxDepth) {
            return false;
        }
        std::set<std::string> keys;
        while (m_at < m_data.size()) {
            const std::uint8_t type = m_data[m_at++];
            if (type == 8) {
                return true;
            }
            Node node;
            node.type = type;
            if (++m_nodes > kMaxNodes || !string(node.key) || !keys.insert(lower(node.key)).second) {
                return false;
            }
            switch (type) {
                case 0:
                    if (!list(node.children, depth + 1)) {
                        return false;
                    }
                    break;
                case 1:
                    if (!string(node.text)) {
                        return false;
                    }
                    break;
                case 2:
                case 3:
                case 4:
                case 6:
                    if (m_data.size() - m_at < 4) {
                        return false;
                    }
                    for (unsigned shift = 0; shift < 32; shift += 8) {
                        node.integer |= std::uint32_t{m_data[m_at++]} << shift;
                    }
                    break;
                case 5: {
                    const std::size_t begin = m_at;
                    while (m_data.size() - m_at >= 2 && (m_data[m_at] != 0 || m_data[m_at + 1] != 0)) {
                        m_at += 2;
                        if (m_at - begin > kMaxString) {
                            return false;
                        }
                    }
                    if (m_data.size() - m_at < 2) {
                        return false;
                    }
                    m_at += 2;
                    break;
                }
                case 7:
                case 10:
                    if (m_data.size() - m_at < 8) {
                        return false;
                    }
                    m_at += 8;
                    break;
                default: return false;
            }
            node.end = m_at;
            nodes.push_back(std::move(node));
        }
        return false;
    }

    [[nodiscard]] bool finished() const { return m_at == m_data.size(); }

private:
    bool string(std::string_view& text) {
        const std::size_t begin = m_at;
        while (m_at < m_data.size() && m_data[m_at] != 0 && m_at - begin <= kMaxString) {
            ++m_at;
        }
        if (m_at == m_data.size() || m_at - begin > kMaxString) {
            return false;
        }
        text = {reinterpret_cast<const char*>(m_data.data() + begin), m_at - begin};  // NOLINT
        ++m_at;
        return true;
    }
    std::span<const std::uint8_t> m_data;
    std::size_t m_at = 0;
    std::size_t m_nodes = 0;
};

void cstring(Bytes& output, std::string_view text) {
    output.insert(output.end(), text.begin(), text.end());
    output.push_back(0);
}

void field(Bytes& output, std::string_view key, std::string_view value) {
    output.push_back(1);
    cstring(output, key);
    cstring(output, value);
}

void integerField(Bytes& output, std::string_view key, std::uint32_t value) {
    output.push_back(2);
    cstring(output, key);
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint32_t appId(std::string_view target, std::string_view name) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto part : {target, name}) {
        for (const char c : part) {
            crc ^= static_cast<unsigned char>(c);
            for (unsigned bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1U) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
            }
        }
    }
    return (crc ^ 0xFFFFFFFFU) | 0x80000000U;
}

bool cleanText(std::string_view value) {
    return value.size() <= kMaxString && value.find('\0') == std::string_view::npos &&
           value.find_first_of("\r\n") == std::string_view::npos;
}

std::string_view unquote(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return value;
}

bool sameTarget(std::string_view existing, const fs::path& executable) {
    try {
        const fs::path target = pathFromUtf8(std::string(unquote(existing))).lexically_normal();
#ifdef _WIN32
        return CompareStringOrdinal(target.c_str(), -1, executable.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
        return target == executable;
#endif
    } catch (const fs::filesystem_error&) {
        // An unrelated shortcut may have an encoding this host cannot represent; preserve its bytes.
        return false;
    }
}

const Node* child(const Node& parent, std::string_view key) {
    const auto found =
        std::ranges::find_if(parent.children, [key](const Node& n) { return lower(n.key) == lower(key); });
    return found == parent.children.end() ? nullptr : &*found;
}

Result<bool> safePath(const fs::path& path, bool missingLeaf = false) {
    if (!path.is_absolute() || path != path.lexically_normal()) {
        return Error{"Steam's configuration path must be an absolute, normalized path."};
    }
    fs::path current;
    for (const auto& component : path) {
        current /= component;
        std::error_code error;
        const fs::file_status status = fs::symlink_status(current, error);
        if ((!error && status.type() == fs::file_type::not_found) || error == std::errc::no_such_file_or_directory) {
            if (missingLeaf && current == path) {
                return true;
            }
            return Error{"Steam's configuration folder is missing. Sign in to Steam once, exit it, then try again."};
        }
        if (error) {
            return Error{"Could not inspect Steam's configuration path. It was left unchanged."};
        }
        bool linked = fs::is_symlink(status);
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            return Error{"Could not inspect Steam's configuration path. It was left unchanged."};
        }
        linked = linked || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#endif
        if (linked) {
            return Error{
                "Steam's configuration uses a linked path. It was left unchanged; add BLACK in Steam manually."};
        }
        if (current != path && !fs::is_directory(status)) {
            return Error{"Steam's configuration path contains a non-directory. It was left unchanged."};
        }
    }
    return true;
}

Result<Bytes> readBounded(const fs::path& path, std::size_t maximum = kMaxFile) {
    const auto safe = safePath(path);
    if (!safe) {
        return safe.error();
    }
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size > maximum) {
        return Error{"Steam's configuration file could not be read safely or is too large. It was left unchanged."};
    }
    Bytes data(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    auto* bytes = reinterpret_cast<char*>(data.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (!input || !input.read(bytes, static_cast<std::streamsize>(data.size())) ||
        input.peek() != std::char_traits<char>::eof()) {
        return Error{"Steam's configuration changed or could not be read. Try again after exiting Steam."};
    }
    return data;
}

Result<bool> steamRunning() {
#ifdef _WIN32
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return Error{"Could not check whether Steam is running. No shortcut was changed."};
    }
    PROCESSENTRY32W process{};
    process.dwSize = sizeof process;
    if (Process32FirstW(snapshot, &process) == 0) {
        CloseHandle(snapshot);
        return Error{"Could not check whether Steam is running. No shortcut was changed."};
    }
    do {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): Windows API fixed string buffer
        if (CompareStringOrdinal(process.szExeFile, -1, L"steam.exe", -1, TRUE) == CSTR_EQUAL) {
            CloseHandle(snapshot);
            return true;
        }
    } while (Process32NextW(snapshot, &process) != 0);
    const DWORD error = GetLastError();
    CloseHandle(snapshot);
    if (error != ERROR_NO_MORE_FILES) {
        return Error{"Could not finish checking whether Steam is running. No shortcut was changed."};
    }
#else
    std::error_code error;
    fs::directory_iterator processes("/proc", error);
    if (error) {
        return Error{"Could not check whether Steam is running. No shortcut was changed."};
    }
    const fs::directory_iterator end;
    for (; processes != end; processes.increment(error)) {
        if (error) {
            return Error{"Could not finish checking whether Steam is running. No shortcut was changed."};
        }
        const auto& process = *processes;
        if (!number<std::uint32_t>(process.path().filename().string())) {
            continue;
        }
        const fs::path commPath = process.path() / "comm";
        std::ifstream comm(commPath);
        std::string name;
        if (!std::getline(comm, name)) {
            if (fs::exists(commPath, error) || error) {
                return Error{
                    "Could not inspect a running process to confirm Steam is closed. No shortcut was changed."};
            }
            continue;  // process exited during inspection
        }
        if (name == "steam" || name == "steam.exe") {
            return true;
        }
    }
    if (error) {
        return Error{"Could not finish checking whether Steam is running. No shortcut was changed."};
    }
#endif
    return false;
}

std::string uniqueSuffix() {
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random;
    return std::to_string(stamp) + "-" + std::to_string(random()) + "-" + std::to_string(counter++);
}

Result<bool> writeExclusive(const fs::path& path, const Bytes& data) {
    const auto safe = safePath(path, true);
    if (!safe) {
        return safe.error();
    }
#ifdef _WIN32
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Error{"Could not create a Steam shortcut backup or temporary file. The original was left unchanged."};
    }
    DWORD written = 0;
    const bool success = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) != 0 &&
                         written == data.size() && FlushFileBuffers(file) != 0;
    CloseHandle(file);
#else
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (file < 0) {
        return Error{"Could not create a Steam shortcut backup or temporary file. The original was left unchanged."};
    }
    std::size_t at = 0;
    while (at < data.size()) {
        const auto written = write(file, data.data() + at, data.size() - at);
        if (written <= 0) {
            break;
        }
        at += static_cast<std::size_t>(written);
    }
    const bool success = at == data.size() && fsync(file) == 0;
    const int closed = close(file);
    if (closed != 0) {
        return Error{"Could not finish writing Steam's temporary file. The original was left unchanged."};
    }
#endif
    if (!success) {
        std::error_code ignored;
        fs::remove(path, ignored);
        return Error{"Could not finish writing Steam's backup or temporary file. The original was left unchanged."};
    }
    return true;
}

struct TemporaryFile {
    explicit TemporaryFile(fs::path file) : path(std::move(file)) {}
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    TemporaryFile(TemporaryFile&&) = delete;
    TemporaryFile& operator=(TemporaryFile&&) = delete;
    fs::path path;
    bool owned = false;
    ~TemporaryFile() {
        if (owned) {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    }
};

Result<bool> requireSteamClosed(const std::function<Result<bool>()>& query) {
    const auto running = query();
    if (!running) {
        return running.error();
    }
    if (running.value()) {
        return Error{"Exit Steam completely (Steam > Exit), then try Add to Steam again. Reopen Steam afterwards."};
    }
    return true;
}

// Text loginusers.vdf is optional: bounded, balanced quoted KV parsing; failures use numeric account IDs.
struct TextNode {
    std::string key;
    std::string value;
    std::vector<TextNode> children;
};

class TextReader {
public:
    explicit TextReader(std::string_view text) : m_text(text) {}
    bool list(std::vector<TextNode>& nodes, unsigned depth = 0) {  // NOLINT(misc-no-recursion): bounded to 32 levels
        if (depth > kMaxDepth) {
            return false;
        }
        while (true) {
            whitespace();
            if (m_at == m_text.size()) {
                return depth == 0;
            }
            if (m_text[m_at] == '}') {
                ++m_at;
                return depth != 0;
            }
            TextNode node;
            if (++m_nodes > kMaxNodes || !quoted(node.key)) {
                return false;
            }
            whitespace();
            if (m_at < m_text.size() && m_text[m_at] == '{') {
                ++m_at;
                if (!list(node.children, depth + 1)) {
                    return false;
                }
            } else if (!quoted(node.value)) {
                return false;
            }
            nodes.push_back(std::move(node));
        }
    }

private:
    void whitespace() {
        while (m_at < m_text.size()) {
            if (m_text[m_at] == ' ' || m_text[m_at] == '\t' || m_text[m_at] == '\r' || m_text[m_at] == '\n') {
                ++m_at;
            } else if (m_text.substr(m_at, 2) == "//") {
                const auto end = m_text.find('\n', m_at);
                m_at = end == std::string_view::npos ? m_text.size() : end;
            } else {
                break;
            }
        }
    }
    bool quoted(std::string& value) {
        whitespace();
        if (m_at == m_text.size() || m_text[m_at] != '"') {
            return false;
        }
        ++m_at;
        while (m_at < m_text.size()) {
            char c = m_text[m_at++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (m_at == m_text.size()) {
                    return false;
                }
                c = m_text[m_at++];
                if (c != '\\' && c != '"') {
                    return false;
                }
            }
            if (c == '\0' || value.size() >= kMaxString) {
                return false;
            }
            value += c;
        }
        return false;
    }
    std::string_view m_text;
    std::size_t m_at = 0;
    std::size_t m_nodes = 0;
};

std::map<std::string, std::string> accountNames(const fs::path& root) {
    const auto data = readBounded(root / "config" / "loginusers.vdf", std::size_t{1024} * 1024);
    if (!data) {
        return {};
    }
    const std::string text(data.value().begin(), data.value().end());
    std::vector<TextNode> nodes;
    TextReader parser(text);
    if (!parser.list(nodes) || nodes.size() != 1 || lower(nodes.front().key) != "users") {
        return {};
    }
    std::map<std::string, std::string> names;
    for (const auto& user : nodes.front().children) {
        const auto id = number<std::uint64_t>(user.key);
        if (!id || *id < kSteamIdBase || *id - kSteamIdBase > std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        std::string name;
        for (const auto& entry : user.children) {
            if (lower(entry.key) == "personaname" && cleanText(entry.value) && !entry.value.empty()) {
                name = entry.value;
                break;
            }
        }
        if (!name.empty()) {
            names[std::to_string(*id - kSteamIdBase)] = std::move(name);
        }
    }
    return names;
}

std::optional<fs::path> environmentPath(const char* key) {
#ifdef _WIN32
    std::array<wchar_t, 32768> value{};
    const auto name = pathFromUtf8(key).wstring();
    const DWORD length = GetEnvironmentVariableW(name.c_str(), value.data(), static_cast<DWORD>(value.size()));
    return length > 0 && length < value.size() ? std::optional(fs::path(value.data())) : std::nullopt;
#else
    const char* value = std::getenv(key);  // NOLINT(concurrency-mt-unsafe): environment is read-only after startup
    return value && *value ? std::optional(pathFromUtf8(value)) : std::nullopt;
#endif
}

}  // namespace

Result<steam_shortcut::Update> steam_shortcut::append(std::span<const std::uint8_t> original,
                                                      const SteamShortcut& shortcut) {
    if (original.size() > kMaxFile || !shortcut.executable.is_absolute() || !shortcut.startDirectory.is_absolute()) {
        return Error{"The shortcut needs absolute launcher and working-directory paths, and a bounded Steam file."};
    }
    const fs::path executable = shortcut.executable.lexically_normal();
    const std::string exe = toUtf8(executable);
    const std::string directory = toUtf8(shortcut.startDirectory.lexically_normal());
    if (!cleanText(exe) || !cleanText(directory) || !cleanText(shortcut.appName) || shortcut.appName.empty() ||
        !cleanText(shortcut.launchOptions) || exe.find('"') != std::string::npos ||
        directory.find('"') != std::string::npos) {
        return Error{"The shortcut contains an unsupported path or name. No Steam shortcut was changed."};
    }
    const std::string quotedExe = '"' + exe + '"';
    const std::uint32_t id = appId(quotedExe, shortcut.appName);
    Bytes data(original.begin(), original.end());
    if (data.empty()) {
        data.push_back(0);
        cstring(data, "shortcuts");
        data.insert(data.end(), {8, 8});
    }
    BinaryReader parser(data);
    std::vector<Node> roots;
    if (!parser.list(roots) || !parser.finished() || roots.size() != 1 || roots.front().type != 0 ||
        roots.front().key != "shortcuts") {
        return Error{std::string(kMalformed)};
    }
    std::uint32_t next = 0;
    bool collision = false;
    for (const auto& entry : roots.front().children) {
        const auto index = number<std::uint32_t>(entry.key);
        if (entry.type != 0 || !index || *index == std::numeric_limits<std::uint32_t>::max() ||
            entry.key != std::to_string(*index)) {
            return Error{std::string(kMalformed)};
        }
        next = std::max(next, *index + 1);
        const Node* target = child(entry, "exe");
        const Node* options = child(entry, "LaunchOptions");
        if (target != nullptr && options != nullptr && target->type == 1 && options->type == 1 &&
            sameTarget(target->text, executable) && options->text == shortcut.launchOptions) {
            return Update{Bytes(original.begin(), original.end()), true};
        }
        const Node* existingId = child(entry, "appid");
        collision = collision || (existingId != nullptr && existingId->type == 2 && existingId->integer == id);
    }
    if (collision) {
        return Error{
            "A different Steam shortcut already uses BLACK's shortcut ID. Remove or rename that shortcut "
            "in Steam first; your library was left unchanged."};
    }
    Bytes entry{0};
    cstring(entry, std::to_string(next));
    integerField(entry, "appid", id);
    field(entry, "AppName", shortcut.appName);
    field(entry, "Exe", quotedExe);
    field(entry, "StartDir", '"' + directory + '"');
    field(entry, "icon", "");
    field(entry, "ShortcutPath", "");
    field(entry, "LaunchOptions", shortcut.launchOptions);
    integerField(entry, "IsHidden", 0);
    integerField(entry, "AllowDesktopConfig", 1);
    integerField(entry, "AllowOverlay", 1);
    integerField(entry, "OpenVR", 0);
    integerField(entry, "Devkit", 0);
    field(entry, "DevkitGameID", "");
    integerField(entry, "DevkitOverrideAppID", 0);
    integerField(entry, "LastPlayTime", 0);
    field(entry, "FlatpakAppID", "");
    entry.push_back(0);
    cstring(entry, "tags");
    entry.insert(entry.end(), {8, 8});
    const std::size_t insertion = roots.front().end - 1;
    if (data.size() + entry.size() > kMaxFile) {
        return Error{"Steam's shortcuts file is too large to update safely. It was left unchanged."};
    }
    data.insert(data.begin() + static_cast<std::ptrdiff_t>(insertion), entry.begin(), entry.end());
    return Update{std::move(data), false};
}

Result<SteamShortcutResult> steam_shortcut::add(const SteamAccount& account, const SteamShortcut& shortcut,
                                                const std::function<Result<bool>()>& steamIsRunning) {
    const auto closed = requireSteamClosed(steamIsRunning);
    if (!closed) {
        return closed.error();
    }
    const auto safeDirectory = safePath(account.configDirectory);
    if (!safeDirectory) {
        return safeDirectory.error();
    }
    std::error_code error;
    if (!fs::is_directory(account.configDirectory, error) || error ||
        !fs::is_regular_file(shortcut.executable, error) || error ||
        !fs::is_directory(shortcut.startDirectory, error) || error) {
        return Error{"The Steam account, launcher or working directory is missing. No shortcut was changed."};
    }
    const fs::path path = account.configDirectory / "shortcuts.vdf";
    const auto safe = safePath(path, true);
    if (!safe) {
        return safe.error();
    }
    const bool existed = fs::exists(path, error);
    if (error) {
        return Error{"Could not inspect Steam's shortcuts file. It was left unchanged."};
    }
    const auto original = existed ? readBounded(path) : Result<Bytes>(Bytes{});
    if (!original) {
        return original.error();
    }
    if (existed && original.value().empty()) {
        return Error{std::string(kMalformed)};
    }
    const auto update = append(original.value(), shortcut);
    if (!update) {
        return update.error();
    }
    if (update.value().alreadyPresent) {
        return SteamShortcutResult{true, {}};
    }
    const std::string suffix = uniqueSuffix();
    const fs::path backup = existed ? account.configDirectory / ("shortcuts.vdf.black-" + suffix + ".bak") : fs::path{};
    if (existed) {
        const auto copied = writeExclusive(backup, original.value());
        if (!copied) {
            return copied.error();
        }
    }
    TemporaryFile temporary{account.configDirectory / ("shortcuts.vdf.black-" + suffix + ".tmp")};
    const auto written = writeExclusive(temporary.path, update.value().bytes);
    if (!written) {
        return written.error();
    }
    temporary.owned = true;
    const auto stillClosed = requireSteamClosed(steamIsRunning);
    const auto stillSafe = safePath(path, true);
    if (!stillClosed) {
        return stillClosed.error();
    }
    if (!stillSafe) {
        return stillSafe.error();
    }
    const bool nowExists = fs::exists(path, error);
    if (error || nowExists != existed) {
        return Error{"Steam's shortcuts changed during the update. The original was left unchanged; try again."};
    }
    if (existed) {
        const auto current = readBounded(path);
        if (!current || current.value() != original.value()) {
            return Error{"Steam's shortcuts changed during the update. The original was left unchanged; try again."};
        }
    }
#ifdef _WIN32
    const bool replaced = existed
                              ? ReplaceFileW(path.c_str(), temporary.path.c_str(), nullptr, 0, nullptr, nullptr) != 0
                              : MoveFileExW(temporary.path.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    if (!replaced) {
        return Error{"Could not replace Steam's shortcuts file. The original and its backup were left unchanged."};
    }
#else
    if (existed) {
        fs::rename(temporary.path, path, error);
    } else {
        // link() creates the destination exclusively: a newly appeared library must never be replaced.
        if (link(temporary.path.c_str(), path.c_str()) != 0) {
            return Error{"Could not install Steam's shortcuts file. Your library was left unchanged."};
        }
    }
    if (error) {
        return Error{"Could not replace Steam's shortcuts file. The original and its backup were left unchanged."};
    }
#endif
    return SteamShortcutResult{false, backup};
}

Result<SteamShortcutResult> addSteamShortcut(const SteamAccount& account, const SteamShortcut& shortcut) {
    return steam_shortcut::add(account, shortcut, steamRunning);
}

Result<std::vector<SteamAccount>> findSteamAccounts(const std::vector<fs::path>& roots) {
    std::vector<SteamAccount> accounts;
    std::set<fs::path> visited;
    for (const auto& candidate : roots) {
        std::error_code error;
        if (!fs::is_directory(candidate, error) || error) {
            continue;
        }
        // Steam deliberately uses symlink aliases such as ~/.steam/root. Resolve that known installation root
        // once, then reject links anywhere below it before reading accounts or writing shortcuts.
        const fs::path root = fs::canonical(candidate, error);
        if (error || !visited.insert(root).second || !safePath(root)) {
            continue;
        }
        const auto names = accountNames(root);
        const fs::path userdata = root / "userdata";
        if (!safePath(userdata)) {
            continue;
        }
        fs::directory_iterator directories(userdata, error);
        if (error) {
            return Error{"Could not read Steam's accounts. Sign in to Steam once, exit it, then try again."};
        }
        const fs::directory_iterator end;
        for (; directories != end; directories.increment(error)) {
            if (error) {
                return Error{"Could not finish reading Steam's accounts. Try again after exiting Steam."};
            }
            const auto& directory = *directories;
            const std::string id = directory.path().filename().string();
            if (!number<std::uint32_t>(id) || id == "0") {
                continue;
            }
            const fs::path config = directory.path() / "config";
            if (!safePath(config) || !fs::is_directory(config, error) || error) {
                continue;
            }
            const auto name = names.find(id);
            accounts.push_back({config, name == names.end() ? "Steam account " + id : name->second + " (" + id + ")"});
        }
        if (error) {
            return Error{"Could not finish reading Steam's accounts. Try again after exiting Steam."};
        }
    }
    std::ranges::sort(
        accounts, [](const SteamAccount& a, const SteamAccount& b) { return a.configDirectory < b.configDirectory; });
    return accounts;
}

Result<std::vector<SteamAccount>> findSteamAccounts() {
    std::vector<fs::path> roots;
#ifdef _WIN32
    std::array<wchar_t, 32768> buffer{};
    auto size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buffer.data(),
                     &size) == ERROR_SUCCESS &&
        buffer[0] != L'\0') {
        roots.emplace_back(buffer.data());
    }
    for (const char* key : {"ProgramFiles(x86)", "ProgramFiles"}) {
        if (const auto folder = environmentPath(key)) {
            roots.push_back(*folder / "Steam");
        }
    }
#else
    if (const auto home = environmentPath("HOME")) {
        roots.push_back(*home / ".steam" / "steam");
        roots.push_back(*home / ".steam" / "root");
        roots.push_back(*home / ".local" / "share" / "Steam");
        roots.push_back(*home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
        roots.push_back(*home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam");
    }
    if (const auto xdg = environmentPath("XDG_DATA_HOME")) {
        roots.push_back(*xdg / "Steam");
    }
#endif
    return findSteamAccounts(roots);
}

}  // namespace black::core
