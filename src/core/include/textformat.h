/**
 * @file textformat.h
 * @brief Fixed-notation number formatting that rounds an exact tie away from zero.
 *
 * The core builds its text with `std::format`, which rounds an exact tie to the even digit
 * (`2.5` with no decimals is "2"). The reports an operator reads have always rounded it away from
 * zero ("3"). Measured, not assumed: 0.5 -> "1" vs "0", 2.5 -> "3" vs "2",
 * 0.25 with one decimal -> "0.3" vs "0.2", 1234.5 -> "1235" vs "1234". Every other value agrees,
 * because it is decided by the exact binary value rather than a tie.
 *
 * So a step file with half-dB steps must keep printing "3 dB", not "2 dB". `formatFixed()` is that
 * output; tests/core/test_textformat.cpp pins it against what v2.14 printed.
 */

#ifndef TEXTFORMAT_H
#define TEXTFORMAT_H

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>

namespace textformat {

/// True when @p magnitude (non-negative, finite) lies exactly halfway between two decimals of
/// @p decimals places - the one case where the rounding rules differ. False for zero, a negative value,
/// an infinity and a not-a-number.
///
/// Exact, from the binary value: write @p magnitude as m * 2^f with m an odd integer. It is a tie at d places
/// exactly when 2 * 10^d * magnitude is an odd integer, and 2 * 10^d * m * 2^f = m * 5^d * 2^(f + d + 1), whose
/// odd part m * 5^d is odd: so it is an odd integer exactly when f + d + 1 == 0. (Until v3.5.1 this printed the
/// value to 1100 places to look, which was about 60 % of a large CSV export's time; test_textformat.cpp holds the
/// two answers equal.)
inline bool isExactTie(double magnitude, int decimals)
{
    if (!std::isfinite(magnitude) || magnitude <= 0.0 || decimals < 0)
    {
        return false;
    }
    int exponent = 0;
    const double fraction = std::frexp(magnitude, &exponent);   // magnitude = fraction * 2^exponent, fraction in [0.5, 1)
    std::uint64_t mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));   // exact: a double holds 53 bits
    int power = exponent - 53;
    const int trailing = std::countr_zero(mantissa);
    mantissa >>= trailing;
    power += trailing;   // magnitude = mantissa * 2^power, mantissa odd
    return power == -(decimals + 1);
}

/// @p value in fixed notation with @p decimals places: ties rounded away from zero, and the
/// sign kept on a negative value that rounds to zero ("-0"), though not on negative zero itself.
inline std::string formatFixed(double value, int decimals)
{
    // A not-a-number or an infinity needs no branch of its own: the magnitude has no sign bit left, so a
    // not-a-number prints as "nan" whatever its sign (std::format would say "-nan"), and an infinity
    // gets its sign from the comparison below.
    // Not the sign bit: negative zero prints as "0" but -0.4 rounded to nothing as "-0".
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
