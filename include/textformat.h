/**
 * @file textformat.h
 * @brief Number formatting that reproduces Qt's `'f'` output without Qt.
 *
 * The de-Qt'd core builds its text with `std::format`, which rounds an exact tie to the even digit
 * (`2.5` with no decimals is "2"). `QString::number(v, 'f', n)` and `QString::arg(v, 0, 'f', n)`
 * round it away from zero ("3"). Measured, not assumed: 0.5 -> "1" vs "0", 2.5 -> "3" vs "2",
 * 0.25 with one decimal -> "0.3" vs "0.2", 1234.5 -> "1235" vs "1234". Every other value agrees,
 * because it is decided by the exact binary value rather than a tie.
 *
 * Reports an operator reads were written against Qt's output, so a step file with half-dB steps
 * must keep printing "3 dB", not "2 dB". `formatFixed()` is that output.
 */

#ifndef TEXTFORMAT_H
#define TEXTFORMAT_H

#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <string>

namespace textformat {

/// True when @p magnitude (non-negative, finite) lies exactly halfway between two decimals of
/// @p decimals places - the one case where the rounding rules differ.
inline bool isExactTie(double magnitude, int decimals)
{
    // A tie needs a 5 in the next place; the cheap check rules out almost every value.
    const std::string next_place = std::format("{:.{}f}", magnitude, decimals + 1);
    if (next_place.back() != '5')
    {
        return false;
    }
    // A double's exact decimal expansion is at most 1074 places long, so printing 1100 is exact.
    // It is a tie only if that 5 is followed by nothing but zeros.
    constexpr int kExactPlaces = 1100;
    const std::string exact = std::format("{:.{}f}", magnitude, decimals + 1 + kExactPlaces);
    const std::size_t tail = exact.size() - kExactPlaces;   // first place after the 5
    return exact[tail - 1] == '5' && exact.find_first_not_of('0', tail) == std::string::npos;
}

/// `QString::number(value, 'f', decimals)`: fixed notation, ties rounded away from zero, and the
/// sign kept on a negative value that rounds to zero ("-0"), though not on negative zero itself.
inline std::string formatFixed(double value, int decimals)
{
    // A not-a-number or an infinity needs no branch of its own: the magnitude has no sign bit left, so a
    // not-a-number prints as "nan" whatever its sign (as Qt prints it; std::format would say "-nan"), and
    // an infinity gets its sign from the comparison below.
    // Not the sign bit: Qt prints negative zero as "0" but -0.4 rounded to nothing as "-0".
    const bool negative = value < 0.0;
    const double magnitude = std::fabs(value);
    // An exact tie is nudged one step up, so the correctly-rounded formatter takes it away from zero.
    const double rounded_input = isExactTie(magnitude, decimals)
                                     ? std::nextafter(magnitude, std::numeric_limits<double>::infinity())
                                     : magnitude;
    std::string digits = std::format("{:.{}f}", rounded_input, decimals);
    return negative ? "-" + digits : digits;
}

} // namespace textformat

#endif // TEXTFORMAT_H
