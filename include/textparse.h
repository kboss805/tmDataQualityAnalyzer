/**
 * @file textparse.h
 * @brief Qt-free text helpers with `QString`'s parsing rules, for the de-Qt'd core.
 *
 * `QString::toInt()` / `toDouble()` / `trimmed()` are more forgiving than the C++
 * standard's `std::from_chars`: they ignore surrounding whitespace and accept a leading
 * '+'. Settings files and parameter names written by hand rely on that, so code that
 * moved off QString keeps the rules. They live here, once, so the TOML reader and the
 * receiver-name parser cannot drift apart (`TestTomlDocument` and
 * `TestFrameSetup::receiverNameParsingMatchesQStringToInt` pin them).
 *
 * Deliberately ASCII-only: Qt also trimmed Unicode spaces (U+00A0 and friends) and
 * parsed Unicode digits; no file this application reads carries either.
 */

#ifndef TEXTPARSE_H
#define TEXTPARSE_H

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>

namespace textparse {

inline bool isAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/// `QString::trimmed()` for ASCII whitespace.
inline std::string trimmed(const std::string& s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isAsciiSpace(s[b]))
    {
        ++b;
    }
    while (e > b && isAsciiSpace(s[e - 1]))
    {
        --e;
    }
    return s.substr(b, e - b);
}

/// `QString::toInt()` / `toLongLong()`: surrounding whitespace ignored, one optional
/// leading '+' or '-', base 10, the whole remainder must be digits, and out of range
/// fails rather than saturating. @return false (leaving @p out untouched) on failure.
template <typename Int>
bool parseInteger(const std::string& text, Int& out)
{
    const std::string s = trimmed(text);
    const char* first = s.data();
    const char* last  = s.data() + s.size();
    if (first != last && *first == '+')
    {
        ++first;
        if (first != last && (*first == '+' || *first == '-'))
        {
            return false; // "+-3" is not a number to Qt either
        }
    }
    if (first == last)
    {
        return false;
    }
    Int value{};
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc() || ptr != last)
    {
        return false;
    }
    out = value;
    return true;
}

/// `QString::toULongLong(&ok, 16)`: surrounding whitespace ignored, an optional leading '+', an
/// optional "0x" / "0X" prefix, then at least one hex digit and nothing else; a '-' is refused
/// (even "-0"), and more than 64 bits is refused rather than wrapped. @return false (leaving
/// @p out untouched) on failure.
inline bool parseUnsignedHex(const std::string& text, std::uint64_t& out)
{
    const std::string s = trimmed(text);
    const char* first = s.data();
    const char* last  = s.data() + s.size();
    if (first != last && *first == '+')
    {
        ++first;
    }
    if (last - first >= 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X'))
    {
        first += 2;
    }
    if (first == last)
    {
        return false;
    }
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(first, last, value, 16);
    if (ec != std::errc() || ptr != last)
    {
        return false;
    }
    out = value;
    return true;
}

/// `QString::toDouble()`, as far as a settings file can reach: surrounding whitespace
/// ignored, an optional leading '+', decimal or exponent form, "inf" / "nan".
inline bool parseDouble(const std::string& text, double& out)
{
    const std::string s = trimmed(text);
    const char* first = s.data();
    const char* last  = s.data() + s.size();
    if (first != last && *first == '+')
    {
        ++first;
        if (first != last && (*first == '+' || *first == '-'))
        {
            return false;
        }
    }
    if (first == last)
    {
        return false;
    }
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(first, last, value, std::chars_format::general);
    if (ec != std::errc() || ptr != last)
    {
        return false;
    }
    out = value;
    return true;
}

} // namespace textparse

#endif // TEXTPARSE_H
