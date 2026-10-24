// textformat: formatFixed() reproduces QString::number(v, 'f', n) without Qt. The Qt suite used Qt itself as the
// oracle; this one has no Qt to ask, so the expected text comes from two independent sources:
//   * what Qt printed, recorded once with a throwaway diagnostic (the tables and the sweep's digest below), and
//   * exact integer arithmetic, for every value that is a binary fraction (n/64): the scaled value is an integer
//     quotient and a remainder, so "round half away from zero" is a comparison and not a formatting rule.

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>

#include "doctest.h"
#include "textformat.h"

namespace {

struct Recorded
{
    double value;
    int decimals;
    const char* expected;   // what QString::number(value, 'f', decimals) printed
};

// What Qt printed (recorded with a throwaway diagnostic in the Qt suite; 17 significant digits round-trip each value).
const Recorded kNotTies[] = {
    {0.34999999999999998, 0, "0"},
    {0.34999999999999998, 1, "0.3"},
    {0.34999999999999998, 2, "0.35"},
    {0.34999999999999998, 3, "0.350"},
    {0.34999999999999998, 4, "0.3500"},
    {0.34999999999999998, 5, "0.35000"},
    {0.34999999999999998, 6, "0.350000"},
    {0.34999999999999998, 7, "0.3500000"},
    {0.45000000000000001, 0, "0"},
    {0.45000000000000001, 1, "0.5"},
    {0.45000000000000001, 2, "0.45"},
    {0.45000000000000001, 3, "0.450"},
    {0.45000000000000001, 4, "0.4500"},
    {0.45000000000000001, 5, "0.45000"},
    {0.45000000000000001, 6, "0.450000"},
    {0.45000000000000001, 7, "0.4500000"},
    {2.6749999999999998, 0, "3"},
    {2.6749999999999998, 1, "2.7"},
    {2.6749999999999998, 2, "2.67"},
    {2.6749999999999998, 3, "2.675"},
    {2.6749999999999998, 4, "2.6750"},
    {2.6749999999999998, 5, "2.67500"},
    {2.6749999999999998, 6, "2.675000"},
    {2.6749999999999998, 7, "2.6750000"},
    {0.050000000000000003, 0, "0"},
    {0.050000000000000003, 1, "0.1"},
    {0.050000000000000003, 2, "0.05"},
    {0.050000000000000003, 3, "0.050"},
    {0.050000000000000003, 4, "0.0500"},
    {0.050000000000000003, 5, "0.05000"},
    {0.050000000000000003, 6, "0.050000"},
    {0.050000000000000003, 7, "0.0500000"},
    {0.14999999999999999, 0, "0"},
    {0.14999999999999999, 1, "0.1"},
    {0.14999999999999999, 2, "0.15"},
    {0.14999999999999999, 3, "0.150"},
    {0.14999999999999999, 4, "0.1500"},
    {0.14999999999999999, 5, "0.15000"},
    {0.14999999999999999, 6, "0.150000"},
    {0.14999999999999999, 7, "0.1500000"},
    {12.345000000000001, 0, "12"},
    {12.345000000000001, 1, "12.3"},
    {12.345000000000001, 2, "12.35"},
    {12.345000000000001, 3, "12.345"},
    {12.345000000000001, 4, "12.3450"},
    {12.345000000000001, 5, "12.34500"},
    {12.345000000000001, 6, "12.345000"},
    {12.345000000000001, 7, "12.3450000"},
    {99.950000000000003, 0, "100"},
    {99.950000000000003, 1, "100.0"},
    {99.950000000000003, 2, "99.95"},
    {99.950000000000003, 3, "99.950"},
    {99.950000000000003, 4, "99.9500"},
    {99.950000000000003, 5, "99.95000"},
    {99.950000000000003, 6, "99.950000"},
    {99.950000000000003, 7, "99.9500000"},
    {9.9999999999999995e-08, 0, "0"},
    {9.9999999999999995e-08, 1, "0.0"},
    {9.9999999999999995e-08, 2, "0.00"},
    {9.9999999999999995e-08, 3, "0.000"},
    {9.9999999999999995e-08, 4, "0.0000"},
    {9.9999999999999995e-08, 5, "0.00000"},
    {9.9999999999999995e-08, 6, "0.000000"},
    {9.9999999999999995e-08, 7, "0.0000001"},
    {6.0, 0, "6"},
    {6.0, 1, "6.0"},
    {6.0, 2, "6.00"},
    {6.0, 3, "6.000"},
    {6.0, 4, "6.0000"},
    {6.0, 5, "6.00000"},
    {6.0, 6, "6.000000"},
    {6.0, 7, "6.0000000"},
    {48.0, 0, "48"},
    {48.0, 1, "48.0"},
    {48.0, 2, "48.00"},
    {48.0, 3, "48.000"},
    {48.0, 4, "48.0000"},
    {48.0, 5, "48.00000"},
    {48.0, 6, "48.000000"},
    {48.0, 7, "48.0000000"},
    {0.10000000000000001, 0, "0"},
    {0.10000000000000001, 1, "0.1"},
    {0.10000000000000001, 2, "0.10"},
    {0.10000000000000001, 3, "0.100"},
    {0.10000000000000001, 4, "0.1000"},
    {0.10000000000000001, 5, "0.10000"},
    {0.10000000000000001, 6, "0.100000"},
    {0.10000000000000001, 7, "0.1000000"},
    {0.69999999999999996, 0, "1"},
    {0.69999999999999996, 1, "0.7"},
    {0.69999999999999996, 2, "0.70"},
    {0.69999999999999996, 3, "0.700"},
    {0.69999999999999996, 4, "0.7000"},
    {0.69999999999999996, 5, "0.70000"},
    {0.69999999999999996, 6, "0.700000"},
    {0.69999999999999996, 7, "0.7000000"},
    {3.1415899999999999, 0, "3"},
    {3.1415899999999999, 1, "3.1"},
    {3.1415899999999999, 2, "3.14"},
    {3.1415899999999999, 3, "3.142"},
    {3.1415899999999999, 4, "3.1416"},
    {3.1415899999999999, 5, "3.14159"},
    {3.1415899999999999, 6, "3.141590"},
    {3.1415899999999999, 7, "3.1415900"},
};
const Recorded kSigns[] = {
    {0.0, 0, "0"},
    {0.0, 1, "0.0"},
    {0.0, 2, "0.00"},
    {0.0, 0, "0"},
    {0.0, 1, "0.0"},
    {0.0, 2, "0.00"},
    {0.40000000000000002, 0, "0"},
    {0.40000000000000002, 1, "0.4"},
    {0.40000000000000002, 2, "0.40"},
    {-0.40000000000000002, 0, "-0"},
    {-0.40000000000000002, 1, "-0.4"},
    {-0.40000000000000002, 2, "-0.40"},
    {-0.040000000000000001, 0, "-0"},
    {-0.040000000000000001, 1, "-0.0"},
    {-0.040000000000000001, 2, "-0.04"},
    {-0.00040000000000000002, 0, "-0"},
    {-0.00040000000000000002, 1, "-0.0"},
    {-0.00040000000000000002, 2, "-0.00"},
    {0.5, 0, "1"},
    {0.5, 1, "0.5"},
    {0.5, 2, "0.50"},
    {-0.5, 0, "-1"},
    {-0.5, 1, "-0.5"},
    {-0.5, 2, "-0.50"},
};
const Recorded kMagnitudes[] = {
    {65472.0, 0, "65472"},
    {65472.0, 1, "65472.0"},
    {65472.0, 2, "65472.00"},
    {65472.0, 3, "65472.000"},
    {65471.5, 0, "65472"},
    {65471.5, 1, "65471.5"},
    {65471.5, 2, "65471.50"},
    {65471.5, 3, "65471.500"},
    {1000000000.0, 0, "1000000000"},
    {1000000000.0, 1, "1000000000.0"},
    {1000000000.0, 2, "1000000000.00"},
    {1000000000.0, 3, "1000000000.000"},
    {1000000000.5, 0, "1000000001"},
    {1000000000.5, 1, "1000000000.5"},
    {1000000000.5, 2, "1000000000.50"},
    {1000000000.5, 3, "1000000000.500"},
    {123456789.5, 0, "123456790"},
    {123456789.5, 1, "123456789.5"},
    {123456789.5, 2, "123456789.50"},
    {123456789.5, 3, "123456789.500"},
    {4503599627370496.0, 0, "4503599627370496"},
    {4503599627370496.0, 1, "4503599627370496.0"},
    {4503599627370496.0, 2, "4503599627370496.00"},
    {4503599627370496.0, 3, "4503599627370496.000"},
    {1000000000000000.0, 0, "1000000000000000"},
    {1000000000000000.0, 1, "1000000000000000.0"},
    {1000000000000000.0, 2, "1000000000000000.00"},
    {1000000000000000.0, 3, "1000000000000000.000"},
    {2.2250738585072014e-308, 0, "0"},
    {2.2250738585072014e-308, 1, "0.0"},
    {2.2250738585072014e-308, 2, "0.00"},
    {2.2250738585072014e-308, 3, "0.000"},
    {4.9406564584124654e-324, 0, "0"},
    {4.9406564584124654e-324, 1, "0.0"},
    {4.9406564584124654e-324, 2, "0.00"},
    {4.9406564584124654e-324, 3, "0.000"},
};
const uint64_t kSweepDigest = 0x28538b78a9783048ull;

std::string ours(double value, int decimals) { return textformat::formatFixed(value, decimals); }

int64_t powerOfTen(int n)
{
    int64_t p = 1;
    for (int i = 0; i < n; i++) { p *= 10; }
    return p;
}

/// The digits of @p scaled (an integer count of 10^-decimals) with the decimal point put back.
std::string withPoint(int64_t scaled, int decimals)
{
    std::string digits = std::to_string(scaled);
    if (decimals > 0)
    {
        if (static_cast<int>(digits.size()) <= decimals)
        {
            digits.insert(0, static_cast<std::size_t>(decimals) + 1 - digits.size(), '0');
        }
        digits.insert(digits.size() - static_cast<std::size_t>(decimals), 1, '.');
    }
    return digits;
}

/// n/64 printed to @p decimals places by integer arithmetic alone. @p nudge is -1 / 0 / +1 for a value one
/// representable step below / at / above n/64 (only meaningful where n/64 is itself a tie).
std::string exactFixed(int n, int decimals, int nudge = 0)
{
    const int64_t magnitude = (n < 0 ? -static_cast<int64_t>(n) : n) * powerOfTen(decimals);   // |n|/64 in units of 10^-d, times 64
    int64_t quotient = magnitude / 64;
    const int64_t twice_remainder = (magnitude % 64) * 2;
    if (nudge < 0)
    {
        if (twice_remainder > 64) { quotient++; }    // just below a tie rounds down
    }
    else if (twice_remainder >= 64)
    {
        quotient++;                                  // the tie itself, and just above it, go away from zero
    }
    return std::string(n < 0 ? "-" : "") + withPoint(quotient, decimals);
}

bool isTie(int n, int decimals)
{
    const int64_t magnitude = (n < 0 ? -static_cast<int64_t>(n) : n) * powerOfTen(decimals);
    return (magnitude % 64) * 2 == 64;
}

} // namespace

TEST_CASE("textformat: exact ties round away from zero like Qt")
{
    // The values the calibration report prints: half-dB steps and half-count levels. Each is an exact binary
    // fraction, so each is a true tie - and std::format would round half of them down.
    struct Case { double value; int decimals; const char* expected; };
    const Case cases[] = {
        {0.5, 0, "1"},   {1.5, 0, "2"},   {2.5, 0, "3"},   {3.5, 0, "4"},   {37.5, 0, "38"},
        {42.5, 0, "43"}, {1234.5, 0, "1235"}, {0.25, 1, "0.3"}, {1.25, 1, "1.3"}, {0.75, 1, "0.8"},
        {0.125, 2, "0.13"}, {0.375, 2, "0.38"}, {0.625, 2, "0.63"},
        {-0.5, 0, "-1"}, {-2.5, 0, "-3"}, {-0.25, 1, "-0.3"},
    };
    for (const Case& c : cases)
    {
        INFO(c.value << " with " << c.decimals << " decimals");
        CHECK(ours(c.value, c.decimals) == c.expected);
    }
    // And the reason the helper exists: the plain formatter disagrees on some of these.
    CHECK(std::format("{:.0f}", 2.5) != "3");
    CHECK(std::format("{:.1f}", 0.25) != "0.3");
}

TEST_CASE("textformat: values that are not ties agree with Qt")
{
    // Decimal halves that binary cannot hold exactly (0.35, 2.675 ...) are decided by their true value, not
    // by a tie, and both formatters agree. Recorded from Qt, at 0 to 7 decimals.
    for (const Recorded& r : kNotTies)
    {
        INFO(r.value << " with " << r.decimals << " decimals");
        CHECK(ours(r.value, r.decimals) == r.expected);
    }
}

TEST_CASE("textformat: signs and zeros agree with Qt")
{
    for (const Recorded& r : kSigns)
    {
        INFO(r.value << " with " << r.decimals << " decimals");
        CHECK(ours(r.value, r.decimals) == r.expected);
    }
    // A negative that rounds to zero keeps its sign, as Qt prints it.
    CHECK(ours(-0.4, 0) == "-0");
    CHECK(ours(-0.04, 1) == "-0.0");
    // ...but negative zero itself prints without one.
    CHECK(ours(-0.0, 0) == "0");
    CHECK(ours(-0.0, 2) == "0.00");
}

TEST_CASE("textformat: every binary fraction agrees with exact arithmetic at every precision")
{
    // n/64 is exact in binary, so this walks hundreds of true ties at many precisions - and a few thousand
    // near-ties - against integer arithmetic.
    int ties = 0;
    for (int n = -4200; n <= 4200; n++)
    {
        const double v = n / 64.0;
        for (int d = 0; d <= 6; d++)
        {
            const std::string expected = exactFixed(n, d);
            if (ours(v, d) != expected)
            {
                FAIL_CHECK("n/64 with n=" << n << ", " << d << " decimals: expected " << expected << ", got " << ours(v, d));
            }
            if (isTie(n, d)) { ties++; }
        }
    }
    CHECK(ties > 1000);   // the walk really did cover ties, not just easy values
}

TEST_CASE("textformat: one step either side of a tie goes the way the value lies")
{
    // The helper nudges a tie up one representable step, so the neighbours of every tie are where a mistake in
    // the nudge (wrong direction, too big, applied to a non-tie) shows.
    int checked = 0;
    for (int n = -4200; n <= 4200; n++)
    {
        const double v = n / 64.0;
        for (int d = 0; d <= 6; d++)
        {
            if (!isTie(n, d)) { continue; }
            const double away = std::nextafter(v, n < 0 ? -std::numeric_limits<double>::infinity()
                                                         : std::numeric_limits<double>::infinity());
            const double towards = std::nextafter(v, 0.0);
            const std::string up   = exactFixed(n, d, +1);
            const std::string down = exactFixed(n, d, -1);
            if (ours(away, d) != up)
            {
                FAIL_CHECK("just above " << v << " at " << d << " decimals: expected " << up << ", got " << ours(away, d));
            }
            if (ours(towards, d) != down)
            {
                FAIL_CHECK("just below " << v << " at " << d << " decimals: expected " << down << ", got " << ours(towards, d));
            }
            checked++;
        }
    }
    CHECK(checked > 1000);
}

TEST_CASE("textformat: a sweep of arbitrary doubles agrees with Qt")
{
    // A fixed pseudo-random sweep (so a failure reproduces) across several magnitudes. Qt's 20,000 answers were
    // folded into one FNV-1a digest when the sweep was recorded; the powers of ten are a table so the values do
    // not depend on std::pow.
    const double pow10[] = {1e-3, 1e-2, 1e-1, 1e0, 1e1, 1e2, 1e3, 1e4, 1e5};
    uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    uint64_t digest = 1469598103934665603ull;
    for (int i = 0; i < 20000; i++)
    {
        const double unit  = static_cast<double>(next() >> 11) / static_cast<double>(1ull << 53);   // [0, 1)
        const int exponent = static_cast<int>(next() % 9);                                           // 1e-3 .. 1e5
        const double v     = (unit - 0.3) * pow10[exponent];
        const int d        = static_cast<int>(next() % 4);
        const std::string text = ours(v, d) + "\n";
        for (const unsigned char c : text)
        {
            digest ^= c;
            digest *= 1099511628211ull;
        }
    }
    CHECK(digest == kSweepDigest);
}

TEST_CASE("textformat: large and small magnitudes agree with Qt")
{
    for (const Recorded& r : kMagnitudes)
    {
        INFO(r.value << " with " << r.decimals << " decimals");
        CHECK(ours(r.value, r.decimals) == r.expected);
    }
}

TEST_CASE("textformat: infinity and not-a-number print as words, with no sign on a not-a-number")
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const int d : {0, 2})
    {
        CHECK(ours(inf, d) == "inf");
        CHECK(ours(-inf, d) == "-inf");
        CHECK(ours(nan, d) == "nan");
        CHECK(ours(-nan, d) == "nan");   // Qt prints no sign whatever the sign bit says
    }
}

TEST_CASE("textformat: isExactTie sees only real ties")
{
    using textformat::isExactTie;
    CHECK(isExactTie(0.5, 0));
    CHECK(isExactTie(2.5, 0));
    CHECK(isExactTie(0.25, 1));
    CHECK(isExactTie(0.125, 2));
    CHECK(isExactTie(1234.5, 0));
    // Looks like a tie to the eye, is not one in binary.
    CHECK(!isExactTie(0.35, 1));
    CHECK(!isExactTie(2.675, 2));
    CHECK(!isExactTie(0.15, 1));
    // Not a tie at all.
    CHECK(!isExactTie(3.0, 0));
    CHECK(!isExactTie(3.4, 0));
    CHECK(!isExactTie(0.0, 2));
    // A tie at one precision is not a tie at another.
    CHECK(!isExactTie(0.5, 1));
    CHECK(!isExactTie(0.25, 2));
    // One representable step either side of a tie is not one: above it a 5 is still the next digit, and
    // only the nonzero digits after it say so; below it the digits are 4 and nines.
    for (const double tie : {0.25, 2.5, 1234.5, 0.125})
    {
        const int d = tie == 0.25 ? 1 : (tie == 0.125 ? 2 : 0);
        INFO("neighbours of " << tie << " at " << d << " decimals");
        CHECK(isExactTie(tie, d));
        CHECK(!isExactTie(std::nextafter(tie, std::numeric_limits<double>::infinity()), d));
        CHECK(!isExactTie(std::nextafter(tie, 0.0), d));
    }
}
