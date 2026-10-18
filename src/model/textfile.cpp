/**
 * @file textfile.cpp
 * @brief UTF-8 text-file reading for the Qt-free core. See textfile.h.
 */

#include "textfile.h"

#include <fstream>
#include <iterator>

namespace {

/// Appends one Unicode code point as UTF-8.
void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/// Decodes UTF-16 (after its BOM) into UTF-8. An unpaired surrogate becomes U+FFFD, as
/// Qt's decoder would make it.
std::string utf16ToUtf8(const std::string& bytes, std::size_t offset, bool big_endian)
{
    std::string out;
    out.reserve((bytes.size() - offset) / 2);
    auto unit = [&](std::size_t i) -> char16_t {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
        return static_cast<char16_t>(big_endian ? (b0 << 8) | b1 : (b1 << 8) | b0);
    };
    for (std::size_t i = offset; i + 1 < bytes.size(); i += 2)
    {
        const char16_t u = unit(i);
        if (u >= 0xD800 && u <= 0xDBFF && i + 3 < bytes.size())
        {
            const char16_t lo = unit(i + 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                appendUtf8(out, 0x10000 + ((static_cast<char32_t>(u) - 0xD800) << 10)
                                        + (static_cast<char32_t>(lo) - 0xDC00));
                i += 2;
                continue;
            }
        }
        if (u >= 0xD800 && u <= 0xDFFF)
        {
            appendUtf8(out, 0xFFFD);
            continue;
        }
        appendUtf8(out, u);
    }
    return out;
}

} // namespace

namespace textfile {

std::string decodeText(const std::string& bytes)
{
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF
        && static_cast<unsigned char>(bytes[1]) == 0xBB
        && static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        return bytes.substr(3);
    }
    if (bytes.size() >= 2)
    {
        const auto b0 = static_cast<unsigned char>(bytes[0]);
        const auto b1 = static_cast<unsigned char>(bytes[1]);
        if (b0 == 0xFF && b1 == 0xFE)
        {
            return utf16ToUtf8(bytes, 2, false);
        }
        if (b0 == 0xFE && b1 == 0xFF)
        {
            return utf16ToUtf8(bytes, 2, true);
        }
    }
    return bytes;
}

bool readText(const std::filesystem::path& path, std::string& out)
{
    // Binary, so the bytes arrive exactly as stored; decodeText() handles the BOM and
    // callers drop each line's CR with the rest of its whitespace.
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }
    out = decodeText(std::string(std::istreambuf_iterator<char>(file),
                                 std::istreambuf_iterator<char>()));
    return true;
}

/// The file name of @p path as UTF-8, for messages. u8string() is std::string in C++17
/// and std::u8string in C++20; copying through iterators reads either.
std::string utf8FileName(const std::filesystem::path& path)
{
    const auto name = path.filename().u8string();
    return std::string(name.begin(), name.end());
}

std::filesystem::path pathFromUtf8(const std::string& utf8)
{
    // The char8_t constructor: u8path() is deprecated in C++20, and the plain
    // std::string one would read these bytes in the ANSI code page on Windows.
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

} // namespace textfile
