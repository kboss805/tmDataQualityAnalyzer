#include "tst_framesync.h"

#include <bit>
#include <cstdint>
#include <string>

#include <QtTest>

#include "framesync.h"
#include "processingparams.h"
#include "textparse.h"

namespace {

/// What Qt makes of @p text as an unsigned hex number.
bool qtHex(const QString& text, qulonglong& value)
{
    bool ok = false;
    value = text.toULongLong(&ok, 16);
    return ok;
}

/// What the Qt-free parser makes of the same text.
bool ourHex(const QString& text, qulonglong& value)
{
    std::uint64_t parsed = 0;
    if (!textparse::parseUnsignedHex(text.toStdString(), parsed))
    {
        return false;
    }
    value = parsed;
    return true;
}

} // namespace

void TestFrameSync::parseUnsignedHexAgreesWithQtOnAwkwardInputs()
{
    const QStringList inputs = {
        "FE6B2840", "fe6b2840", " FE6B ", "FE6B ", " FE6B", "0xFE6B", "0XFE6B", "+FE6B", "-FE6B", "-0", "0",
        "FE 6B", "FE_6B", "FE,6B", "FFFFFFFFFFFFFFFF", "10000000000000000", "G", "", " ", "00FE", "1e5",
        "\t7\n", "\v1\f", "\r\n9", "0x", "0X", "x1", "FE6B.", "+", "-", "++1", "+-1", "-+1", "0b1",
        "8000000000000000", "+0x1", "0x+1", "0x-1", "0x 1", " 0x1 ", "0x0x1", "00000000000000000000FF",
        "0xFFFFFFFFFFFFFFFF", "0x10000000000000000", "+FFFFFFFFFFFFFFFF", "1 1", "1\t1", "0x\t1",
        QString(QChar(0x0661)), QString("1") + QChar(0x0661), "FE6B\n", "\nFE6B", "0xg", "0x1g",
    };
    for (const QString& s : inputs)
    {
        qulonglong qt_value = 0;
        qulonglong our_value = 0;
        const bool qt_ok  = qtHex(s, qt_value);
        const bool our_ok = ourHex(s, our_value);
        QVERIFY2(qt_ok == our_ok, qPrintable(QString("[%1]: Qt %2, helper %3").arg(s).arg(qt_ok).arg(our_ok)));
        if (qt_ok)
        {
            QVERIFY2(qt_value == our_value, qPrintable(QString("[%1]: Qt %2, helper %3").arg(s).arg(qt_value).arg(our_value)));
        }
    }
}

void TestFrameSync::parseUnsignedHexAgreesWithQtOnArbitraryText()
{
    // A fixed pseudo-random sweep (so a failure reproduces) over the characters that matter:
    // digits, both cases of the hex letters, a prefix letter, signs and whitespace.
    const QString alphabet = QStringLiteral("0123456789abcdefABCDEFxX+- \t");
    std::uint64_t state = 0x2545F4914F6CDD1Dull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int i = 0; i < 40000; i++)
    {
        const int length = static_cast<int>(next() % 22);          // 0..21, reaching past 16 digits
        QString s;
        for (int k = 0; k < length; k++)
        {
            s += alphabet[static_cast<int>(next() % static_cast<std::uint64_t>(alphabet.size()))];
        }
        qulonglong qt_value = 0;
        qulonglong our_value = 0;
        const bool qt_ok  = qtHex(s, qt_value);
        const bool our_ok = ourHex(s, our_value);
        if (qt_ok != our_ok || (qt_ok && qt_value != our_value))
        {
            QFAIL(qPrintable(QString("[%1]: Qt %2/%3, helper %4/%5")
                                 .arg(s).arg(qt_ok).arg(qt_value).arg(our_ok).arg(our_value)));
        }
    }
}

// The one definition of "every bit of the pattern", shared by the sync decode, the packet reader and
// the failure report. The edges are the point: 64 bits must not shift by 64 (undefined), and a pattern
// with no length gets a full mask rather than an empty one that would match everything.
void TestFrameSync::fullSyncMaskIsEveryBitOfThePatternAndNeverShiftsBySixtyFour()
{
    QCOMPARE(qulonglong(fullSyncMask(1)), 0x1ull);
    QCOMPARE(qulonglong(fullSyncMask(4)), 0xFull);
    QCOMPARE(qulonglong(fullSyncMask(32)), 0xFFFFFFFFull);
    QCOMPARE(qulonglong(fullSyncMask(63)), 0x7FFFFFFFFFFFFFFFull);
    QCOMPARE(qulonglong(fullSyncMask(64)), 0xFFFFFFFFFFFFFFFFull);
    QCOMPARE(qulonglong(fullSyncMask(65)), 0xFFFFFFFFFFFFFFFFull);
    QCOMPARE(qulonglong(fullSyncMask(0)), 0xFFFFFFFFFFFFFFFFull);
    QCOMPARE(qulonglong(fullSyncMask(-3)), 0xFFFFFFFFFFFFFFFFull);

    // Every length from 1 to 63 sets exactly that many bits.
    for (int bits = 1; bits < 64; ++bits)
    {
        const std::uint64_t mask = fullSyncMask(bits);
        QCOMPARE(std::popcount(mask), bits);
        QVERIFY2((mask & (mask + 1)) == 0, "a full mask is a run of ones from bit 0");
    }
}

void TestFrameSync::resolveFrameSyncDecodesPatternMaskAndLength()
{
    ResolvedFrameSync r;

    // An empty mask is "every bit of the pattern", whose length is four bits per CHARACTER.
    QCOMPARE(resolveFrameSync("FE6B2840", "", r), FrameSyncError::None);
    QCOMPARE(qulonglong(r.pattern), 0xFE6B2840ull);
    QCOMPARE(qulonglong(r.mask), 0xFFFFFFFFull);
    QCOMPARE(r.patternLength, 32);

    QCOMPARE(resolveFrameSync("1", "", r), FrameSyncError::None);
    QCOMPARE(qulonglong(r.mask), 0xFull);
    QCOMPARE(r.patternLength, 4);

    // Sixteen digits fill the word: no shift by 64, every bit set.
    QCOMPARE(resolveFrameSync("0123456789ABCDEF", "", r), FrameSyncError::None);
    QCOMPARE(r.patternLength, 64);
    QCOMPARE(qulonglong(r.mask), 0xFFFFFFFFFFFFFFFFull);

    // The characters count, not the digits: spaces and a prefix are length too.
    QCOMPARE(resolveFrameSync(" FE6B ", "", r), FrameSyncError::None);
    QCOMPARE(r.patternLength, 24);
    QCOMPARE(resolveFrameSync("0xFE6B", "", r), FrameSyncError::None);
    QCOMPARE(r.patternLength, 24);

    // A given mask is used as it stands, whatever its length.
    QCOMPARE(resolveFrameSync("FE6B2840", "00FF", r), FrameSyncError::None);
    QCOMPARE(qulonglong(r.mask), 0xFFull);
    QCOMPARE(resolveFrameSync("FE6B2840", "0", r), FrameSyncError::None);
    QCOMPARE(qulonglong(r.mask), 0ull);
}

void TestFrameSync::resolveFrameSyncReportsTheFirstProblemAndLeavesTheResultAlone()
{
    ResolvedFrameSync untouched;
    untouched.pattern       = 0x1234;
    untouched.mask          = 0x5678;
    untouched.patternLength = 99;

    auto expect = [&](const char* pattern, const char* mask, FrameSyncError error) {
        ResolvedFrameSync r = untouched;
        QCOMPARE(resolveFrameSync(pattern, mask, r), error);
        QCOMPARE(qulonglong(r.pattern), qulonglong(0x1234));          // never half-written
        QCOMPARE(qulonglong(r.mask), qulonglong(0x5678));
        QCOMPARE(r.patternLength, 99);
    };
    expect("", "", FrameSyncError::PatternEmpty);
    expect("", "ZZ", FrameSyncError::PatternEmpty);            // the pattern is checked first
    expect(" ", "", FrameSyncError::PatternInvalid);           // blank is not "empty"
    expect("ZZ", "", FrameSyncError::PatternInvalid);
    expect("ZZ", "XYZ", FrameSyncError::PatternInvalid);
    expect("-1", "", FrameSyncError::PatternInvalid);
    expect("10000000000000000", "", FrameSyncError::PatternInvalid);
    expect("FE6B2840", "XYZ", FrameSyncError::MaskInvalid);
    expect("FE6B2840", " ", FrameSyncError::MaskInvalid);      // a blank mask is not "no mask"
    expect("FE6B2840", "-1", FrameSyncError::MaskInvalid);
}

void TestFrameSync::applyAcquisitionSetsEveryRawAffectingField()
{
    // The main run and the calibration extraction both build their parameters here, so a field
    // missing from this function is missing from both - and this is the test that names each one.
    Acquisition a;
    a.sync.pattern       = 0xFE6B2840;
    a.sync.mask          = 0x00FF00FF;
    a.sync.patternLength = 32;
    a.bitsInMinorFrame   = 8000;
    a.randomized         = true;
    a.inverted           = true;
    a.dataRateMbps       = 0.8;
    a.swapBytes          = false;

    ProcessingParams p;
    applyAcquisition(p, a);

    QCOMPARE(qulonglong(p.frameSync), 0xFE6B2840ull);
    QCOMPARE(qulonglong(p.frameSyncMask), 0x00FF00FFull);
    QCOMPARE(p.syncPatternLength, 32);
    QCOMPARE(p.bitsInMinorFrame, 8000);
    QCOMPARE(p.wordsInMinorFrame, 500);          // rounded up to 16-bit words
    QVERIFY(p.isRandomized);
    QVERIFY(p.isInverted);
    QVERIFY(!p.swapBytes);
    QCOMPARE(p.dataRateBps, 800000.0);

    // And the other way round, so no field is "set" only because it defaulted to that value.
    a.randomized   = false;
    a.inverted     = false;
    a.swapBytes    = true;
    a.dataRateMbps = 0.0;
    a.bitsInMinorFrame = 801;                    // a partly filled last word still takes a slot
    applyAcquisition(p, a);
    QVERIFY(!p.isRandomized);
    QVERIFY(!p.isInverted);
    QVERIFY(p.swapBytes);
    QCOMPARE(p.dataRateBps, 0.0);                // "use the TMATS rate", not a rate
    QCOMPARE(p.wordsInMinorFrame, 51);
}

void TestFrameSync::applyAcquisitionLeavesEverythingElseAlone()
{
    ProcessingParams p;
    p.timeChannelId  = 3;
    p.pcmChannelId   = 7;
    p.streamLabel    = "label";
    p.samplePeriodSec = 0.25;
    p.mode           = StreamMode::ReceiverChannelInfo;
    p.startSeconds   = 11;
    p.stopSeconds    = 22;

    Acquisition a;
    a.sync.pattern = 1;
    a.sync.mask    = 1;
    a.sync.patternLength = 4;
    a.bitsInMinorFrame   = 800;
    applyAcquisition(p, a);

    QCOMPARE(p.timeChannelId, 3);
    QCOMPARE(p.pcmChannelId, 7);
    QCOMPARE(p.streamLabel, std::string("label"));
    QCOMPARE(p.samplePeriodSec, 0.25);
    QCOMPARE(p.mode, StreamMode::ReceiverChannelInfo);
    QCOMPARE(qulonglong(p.startSeconds), 11ull);
    QCOMPARE(qulonglong(p.stopSeconds), 22ull);
}
