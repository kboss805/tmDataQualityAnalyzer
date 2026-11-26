// Helpers the native tests share. Header-only.

#ifndef TMDQ_TEST_SUPPORT_H
#define TMDQ_TEST_SUPPORT_H

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "chapter10reader.h"
#include "doctest.h"

namespace tmdq_test {

namespace fs = std::filesystem;

using Strings = std::vector<std::string>;

/// A fresh folder under the system temp directory, removed with everything in it when it goes out of scope.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        std::ostringstream name;
        name << "tmdq-test-" << std::hex << rd() << rd();
        m_path = fs::temp_directory_path() / name.str();
        fs::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return m_path; }
    fs::path file(const std::string& name) const { return m_path / name; }

private:
    fs::path m_path;
};

/// Writes raw bytes, so a test controls the encoding, BOM and line endings exactly.
inline void writeBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    REQUIRE(f.is_open());
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(f.good());
}

inline std::string readBytes(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        return {};
    }
    std::ostringstream all;
    all << f.rdbuf();
    return all.str();
}

inline bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// The UTF-8 bytes of a char8_t literal.
inline std::string utf8(const char8_t* text)
{
    return std::string(reinterpret_cast<const char*>(text));
}

/// The repository root: the first folder above the working directory, or above this header's own location, that
/// holds `tmdq.sln`. The second is what makes the committed fixtures (`tests/fixtures`, `settings/`) reachable however
/// the executable is started; MSBuild hands the compiler absolute paths, so `__FILE__` is one. Empty if neither finds it.
inline fs::path repoRoot()
{
    std::error_code ec;
    for (fs::path start : {fs::current_path(ec), fs::path(__FILE__).parent_path()})
    {
        for (fs::path dir = fs::absolute(start, ec); !dir.empty(); dir = dir.parent_path())
        {
            if (fs::exists(dir / "tmdq.sln", ec))
            {
                return dir;
            }
            if (dir == dir.parent_path())
            {
                break;
            }
        }
    }
    return {};
}

/// A committed file under the repository, required to exist: a missing fixture is a broken checkout, not a skip.
inline fs::path repoFile(const fs::path& relative)
{
    const fs::path root = repoRoot();
    REQUIRE_MESSAGE(!root.empty(), "the repository root (the folder holding tmdq.sln) was not found");
    const fs::path file = root / relative;
    REQUIRE_MESSAGE(fs::exists(file), "missing committed file: " << file.string());
    return file;
}

/// A committed recording under `tests/data/test files`, required to exist (a missing one is a broken checkout, not a skip).
inline fs::path recordingFile(const std::string& name)
{
    return repoFile(fs::path("tests") / "data" / "test files" / name);
}

/// UTF-8 text with its non-ASCII characters spelled out as <U+XXXX>; a byte that does not start a valid sequence is
/// U+FFFD (as the conversion behind the recorded answers made it).
inline std::string asciiOnly(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size();)
    {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80)
        {
            out += static_cast<char>(b);
            ++i;
            continue;
        }
        int extra = 0;
        std::uint32_t cp = 0;
        if ((b & 0xE0) == 0xC0) { extra = 1; cp = b & 0x1F; }
        else if ((b & 0xF0) == 0xE0) { extra = 2; cp = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { extra = 3; cp = b & 0x07; }
        bool ok = extra > 0;
        for (int k = 1; ok && k <= extra; ++k)
        {
            ok = i + static_cast<std::size_t>(k) < s.size() && (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) == 0x80;
            if (ok)
            {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
            }
        }
        if (!ok)
        {
            cp = 0xFFFD;
            extra = 0;
        }
        out += std::format("<U+{:04X}>", cp);
        i += 1 + static_cast<std::size_t>(extra);
    }
    return out;
}

/// The UTF-16 code units of UTF-8 text (the basic plane only, which is all any file here uses).
inline std::u16string units(const std::string& utf8)
{
    std::u16string out;
    for (std::size_t i = 0; i < utf8.size();)
    {
        const auto b = static_cast<unsigned char>(utf8[i]);
        if (b < 0x80)
        {
            out += static_cast<char16_t>(b);
            ++i;
        }
        else if ((b & 0xE0) == 0xC0)
        {
            out += static_cast<char16_t>(((b & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F));
            i += 2;
        }
        else
        {
            out += static_cast<char16_t>(((b & 0x0F) << 12) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6) |
                                         (static_cast<unsigned char>(utf8[i + 2]) & 0x3F));
            i += 3;
        }
    }
    return out;
}

/// UTF-8 text as UTF-16LE bytes, with a byte-order mark unless `bom` is false.
inline std::string utf16le(const std::string& text, bool bom = true)
{
    std::string out = bom ? std::string("\xFF\xFE", 2) : std::string();
    for (const char16_t c : units(text))
    {
        out += static_cast<char>(c & 0xFF);
        out += static_cast<char>(c >> 8);
    }
    return out;
}

/// The small committed recording most native tests run: `step_cal, example.ch10`, one PCM channel with 800-bit frames and
/// sync FE6B2840, and the ids of its time channel and first PCM channel.
struct StepCalFixture
{
    fs::path path;
    int      time_id = -1;
    int      pcm_id  = -1;
};

inline StepCalFixture loadStepCalFixture()
{
    StepCalFixture f;
    f.path = recordingFile("step_cal, example.ch10");
    Chapter10Reader meta;
    REQUIRE_MESSAGE(meta.loadChannels(f.path), meta.lastError());
    f.time_id = meta.getCurrentTimeChannelID();
    f.pcm_id  = meta.getFirstPCMChannelID();
    REQUIRE(f.time_id >= 0);
    REQUIRE(f.pcm_id >= 0);
    return f;
}

/// The length of a recording's first packet (its TMATS record), from the header's packet-length field.
inline std::uint32_t firstPacketLength(const std::string& bytes)
{
    std::uint32_t length = 0;
    std::memcpy(&length, bytes.data() + 4, 4);
    return length;
}

/// Rewrites the data-length field (bytes 8 to 11) of the packet header at @p offset in @p bytes, and its header checksum
/// (bytes 22 and 23) to match, the way irig106 checks it: the 16-bit sum of the header's first eleven little-endian
/// words. The result is a header the library accepts that claims @p data_length bytes of data.
inline void setPacketDataLength(std::string& bytes, std::size_t offset, std::uint32_t data_length)
{
    REQUIRE(offset + 24 <= bytes.size());
    for (std::size_t i = 0; i < 4; ++i)
    {
        bytes[offset + 8 + i] = static_cast<char>((data_length >> (8 * i)) & 0xFF);
    }
    std::uint16_t sum = 0;
    for (std::size_t word = 0; word < 11; ++word)
    {
        const auto lo = static_cast<std::uint8_t>(bytes[offset + 2 * word]);
        const auto hi = static_cast<std::uint8_t>(bytes[offset + 2 * word + 1]);
        sum = static_cast<std::uint16_t>(sum + (lo | (hi << 8)));
    }
    bytes[offset + 22] = static_cast<char>(sum & 0xFF);
    bytes[offset + 23] = static_cast<char>(sum >> 8);
}

/// Copies step_cal's recording with edits made to its TMATS text. Each edit is a (needle, replacement) pair of the SAME
/// length, so no packet length or checksum changes (this recording carries no data checksum, and the header is
/// untouched). Fails the case when an edit does not apply exactly once.
inline fs::path writePatchedStepCal(const TempDir& dir, const std::vector<std::pair<std::string, std::string>>& edits)
{
    const std::string all = readBytes(recordingFile("step_cal, example.ch10"));
    const std::uint32_t first = firstPacketLength(all);
    REQUIRE(first > 200);
    std::string tmats = all.substr(0, first);
    for (const auto& [needle, replacement] : edits)
    {
        INFO("edit: " << needle);
        REQUIRE(needle.size() == replacement.size());
        const std::size_t at = tmats.find(needle);
        REQUIRE(at != std::string::npos);
        REQUIRE(tmats.find(needle, at + 1) == std::string::npos);
        tmats.replace(at, replacement.size(), replacement);
    }
    const fs::path path = dir.file("patched.ch10");
    writeBytes(path, tmats + all.substr(first));
    return path;
}

} // namespace tmdq_test

#endif // TMDQ_TEST_SUPPORT_H
