#include "tst_textformat.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <string>

#include <QtTest>

#include "textformat.h"

namespace {

/// What Qt prints, and what the helper prints, as QStrings so a failure shows both.
QString qtFixed(double v, int decimals) { return QString::number(v, 'f', decimals); }
QString ourFixed(double v, int decimals) { return QString::fromStdString(textformat::formatFixed(v, decimals)); }

} // namespace

void TestTextFormat::exactTiesRoundAwayFromZeroLikeQt()
{
    // The values the calibration report prints: half-dB steps and half-count levels. Each is an
    // exact binary fraction, so each is a true tie - and std::format would round half of them down.
    struct Case { double value; int decimals; const char* expected; };
    const Case cases[] = {
        {0.5, 0, "1"},   {1.5, 0, "2"},   {2.5, 0, "3"},   {3.5, 0, "4"},   {37.5, 0, "38"},
        {42.5, 0, "43"}, {1234.5, 0, "1235"}, {0.25, 1, "0.3"}, {1.25, 1, "1.3"}, {0.75, 1, "0.8"},
        {0.125, 2, "0.13"}, {0.375, 2, "0.38"}, {0.625, 2, "0.63"},
        {-0.5, 0, "-1"}, {-2.5, 0, "-3"}, {-0.25, 1, "-0.3"},
    };
    for (const Case& c : cases)
    {
        QCOMPARE(ourFixed(c.value, c.decimals), QString(c.expected));
        QCOMPARE(ourFixed(c.value, c.decimals), qtFixed(c.value, c.decimals));
        // And the reason the helper exists: the plain formatter disagrees on some of these.
    }
    QVERIFY(QString::fromStdString(std::format("{:.0f}", 2.5)) != qtFixed(2.5, 0));
    QVERIFY(QString::fromStdString(std::format("{:.1f}", 0.25)) != qtFixed(0.25, 1));
}

void TestTextFormat::valuesThatAreNotTiesAgreeWithQt()
{
    // Decimal halves that binary cannot hold exactly (0.35, 2.675 ...) are decided by their true
    // value, not by a tie, and both formatters agree.
    for (double v : {0.35, 0.45, 2.675, 0.05, 0.15, 12.345, 99.95, 1e-7, 6.0, 48.0, 0.1, 0.7, 3.14159})
    {
        for (int d = 0; d <= 3; d++)
        {
            QCOMPARE(ourFixed(v, d), qtFixed(v, d));
        }
    }
}

void TestTextFormat::signsAndZerosAgreeWithQt()
{
    for (double v : {0.0, -0.0, 0.4, -0.4, -0.04, -0.0004, 0.5, -0.5})
    {
        for (int d = 0; d <= 2; d++)
        {
            QCOMPARE(ourFixed(v, d), qtFixed(v, d));
        }
    }
    // A negative that rounds to zero keeps its sign, as Qt prints it.
    QCOMPARE(ourFixed(-0.4, 0), QString("-0"));
    QCOMPARE(ourFixed(-0.04, 1), QString("-0.0"));
    // ...but negative zero itself prints without one.
    QCOMPARE(ourFixed(-0.0, 0), QString("0"));
    QCOMPARE(ourFixed(-0.0, 2), QString("0.00"));
}

void TestTextFormat::everyBinaryFractionAgreesWithQtAtEveryPrecision()
{
    // n/64 is exact in binary, so this walks hundreds of true ties at many precisions - and a
    // few thousand near-ties - against Qt.
    for (int n = -4200; n <= 4200; n++)
    {
        const double v = n / 64.0;
        for (int d = 0; d <= 6; d++)
        {
            const QString expected = qtFixed(v, d);
            if (ourFixed(v, d) != expected)
            {
                QFAIL(qPrintable(QString("n/64 with n=%1, %2 decimals: Qt gives %3, helper gives %4")
                                     .arg(n).arg(d).arg(expected, ourFixed(v, d))));
            }
        }
    }
}

void TestTextFormat::aSweepOfArbitraryDoublesAgreesWithQt()
{
    // A fixed pseudo-random sweep (so a failure reproduces) across several magnitudes.
    uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int i = 0; i < 20000; i++)
    {
        const double unit  = static_cast<double>(next() >> 11) / static_cast<double>(1ull << 53);   // [0, 1)
        const int exponent = static_cast<int>(next() % 9) - 3;                                       // 1e-3 .. 1e5
        const double v     = (unit - 0.3) * std::pow(10.0, exponent);
        const int d        = static_cast<int>(next() % 4);
        const QString expected = qtFixed(v, d);
        if (ourFixed(v, d) != expected)
        {
            QFAIL(qPrintable(QString("%1 with %2 decimals: Qt gives %3, helper gives %4")
                                 .arg(v, 0, 'g', 17).arg(d).arg(expected, ourFixed(v, d))));
        }
    }
}

void TestTextFormat::largeAndSmallMagnitudesAgreeWithQt()
{
    for (double v : {65472.0, 65471.5, 1e9, 1e9 + 0.5, 123456789.5, 4503599627370496.5, 1e15, 2.2250738585072014e-308, 5e-324})
    {
        for (int d = 0; d <= 3; d++)
        {
            QCOMPARE(ourFixed(v, d), qtFixed(v, d));
        }
    }
}

void TestTextFormat::isExactTieSeesOnlyRealTies()
{
    using textformat::isExactTie;
    QVERIFY(isExactTie(0.5, 0));
    QVERIFY(isExactTie(2.5, 0));
    QVERIFY(isExactTie(0.25, 1));
    QVERIFY(isExactTie(0.125, 2));
    QVERIFY(isExactTie(1234.5, 0));
    // Looks like a tie to the eye, is not one in binary.
    QVERIFY(!isExactTie(0.35, 1));
    QVERIFY(!isExactTie(2.675, 2));
    QVERIFY(!isExactTie(0.15, 1));
    // Not a tie at all.
    QVERIFY(!isExactTie(3.0, 0));
    QVERIFY(!isExactTie(3.4, 0));
    QVERIFY(!isExactTie(0.0, 2));
    // A tie at one precision is not a tie at another.
    QVERIFY(!isExactTie(0.5, 1));
    QVERIFY(!isExactTie(0.25, 2));
}
