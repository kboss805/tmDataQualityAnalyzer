/**
 * @file textparse.h
 * @brief Text helpers with the forgiving number rules the application's files are read with.
 *
 * These rules are more forgiving than the C++ standard's `std::from_chars`: surrounding
 * whitespace is ignored and a leading '+' is accepted. Settings files and parameter names
 * written by hand rely on that. The rules live here, once, so the TOML reader, the CSV
 * importer and the receiver-name parser cannot drift apart (native/tmdq_core_tests/
 * test_tomldocument.cpp, test_framesetup.cpp and test_csvseries.cpp pin them).
 *
 * Deliberately ASCII-only: whitespace is ASCII whitespace and digits are ASCII digits.
 * v2.14 also trimmed Unicode spaces (U+00A0 and friends) and parsed Unicode digits; no
 * file this application reads carries either. Its answers are the oracle the tests hold
 * these rules to.
 */

#ifndef TEXTPARSE_H
#define TEXTPARSE_H

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

namespace textparse {

inline bool isAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/// @p s without its leading and trailing ASCII whitespace (space, \t, \n, \v, \f, \r).
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

/// An integer in base 10: surrounding whitespace ignored, one optional leading '+' or
/// '-', the whole remainder must be digits, and out of range for @p Int fails rather
/// than saturating. @return false (leaving @p out untouched) on failure.
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
            return false; // one sign only: "+-3" is not a number
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

/// An unsigned integer in base 16: surrounding whitespace ignored, an optional leading '+', an
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

/// A double: surrounding whitespace ignored, one optional leading '+' or '-', decimal or exponent
/// form, and the special values "nan", "inf", "+inf" and "-inf", in any case. `std::from_chars` also
/// takes "infinity", "nan(...)" and a signed "nan"; those were never numbers to this application, so they
/// are refused here (test_csvseries.cpp checks tens of thousands of strings against the recorded answers).
inline bool parseDouble(const std::string& text, double& out)
{
    const std::string s = trimmed(text);
    if (!s.empty())
    {
        const std::size_t sign = (s[0] == '+' || s[0] == '-') ? 1 : 0;
        const char c = s[sign];
        if (c == 'n' || c == 'N' || c == 'i' || c == 'I')
        {
            std::string word;
            for (char ch : s)
            {
                word += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
            }
            if (word == "nan")
            {
                out = std::numeric_limits<double>::quiet_NaN();
                return true;
            }
            if (word == "inf" || word == "+inf" || word == "-inf")
            {
                out = word[0] == '-' ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
                return true;
            }
            return false;
        }
    }
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
