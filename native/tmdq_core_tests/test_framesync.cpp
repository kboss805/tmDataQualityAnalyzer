// framesync: the shared frame-sync decode (framesync.h) and the hex parser under it. The Qt suite used
// QString::toULongLong(16) as the oracle for the parser; this one has no Qt to ask, so the expected answers are
//   * what Qt answered, recorded once with a throwaway diagnostic (the table of awkward inputs, and the 40,000-string
//     sweep as one digest), and
//   * a round trip through std::format("{:x}") for values the parser must read back exactly, in every spelling it
//     accepts - an oracle that shares no code with the parser.

#include <bit>
#include <cstdint>
#include <format>
#include <string>

#include "doctest.h"
#include "framesync.h"
#include "processingparams.h"
#include "textparse.h"

namespace {

// What Qt answered (QString::toULongLong(&ok, 16)), recorded with a throwaway diagnostic in the Qt suite.
struct HexCase { const char* text; bool ok; unsigned long long value; };
const HexCase kHexCases[] = {
    {"FE6B2840", true, 4268435520ull},
    {"fe6b2840", true, 4268435520ull},
    {" FE6B ", true, 65131ull},
    {"FE6B ", true, 65131ull},
    {" FE6B", true, 65131ull},
    {"0xFE6B", true, 65131ull},
    {"0XFE6B", true, 65131ull},
    {"+FE6B", true, 65131ull},
    {"-FE6B", false, 0ull},
    {"-0", false, 0ull},
    {"0", true, 0ull},
    {"FE 6B", false, 0ull},
    {"FE_6B", false, 0ull},
    {"FE,6B", false, 0ull},
    {"FFFFFFFFFFFFFFFF", true, 18446744073709551615ull},
    {"10000000000000000", false, 0ull},
    {"G", false, 0ull},
    {"", false, 0ull},
    {" ", false, 0ull},
    {"00FE", true, 254ull},
    {"1e5", true, 485ull},
    {"\0117\012", true, 7ull},
    {"\0131\014", true, 1ull},
    {"\015\0129", true, 9ull},
    {"0x", false, 0ull},
    {"0X", false, 0ull},
    {"x1", false, 0ull},
    {"FE6B.", false, 0ull},
    {"+", false, 0ull},
    {"-", false, 0ull},
    {"++1", false, 0ull},
    {"+-1", false, 0ull},
    {"-+1", false, 0ull},
    {"0b1", true, 177ull},
    {"8000000000000000", true, 9223372036854775808ull},
    {"+0x1", true, 1ull},
    {"0x+1", false, 0ull},
    {"0x-1", false, 0ull},
    {"0x 1", false, 0ull},
    {" 0x1 ", true, 1ull},
    {"0x0x1", false, 0ull},
    {"00000000000000000000FF", true, 255ull},
    {"0xFFFFFFFFFFFFFFFF", true, 18446744073709551615ull},
    {"0x10000000000000000", false, 0ull},
    {"+FFFFFFFFFFFFFFFF", true, 18446744073709551615ull},
    {"1 1", false, 0ull},
    {"1\0111", false, 0ull},
    {"0x\0111", false, 0ull},
    {"\331\241", false, 0ull},
    {"1\331\241", false, 0ull},
    {"FE6B\012", true, 65131ull},
    {"\012FE6B", true, 65131ull},
    {"0xg", false, 0ull},
    {"0x1g", false, 0ull},
    {"7FFFFFFFFFFFFFFF", true, 9223372036854775807ull},
    {"FFFFFFFFFFFFFFFE", true, 18446744073709551614ull},
    {"1FFFFFFFFFFFFFFFF", false, 0ull},
    {"0x0", true, 0ull},
    {"0x00", true, 0ull},
    {"+0", true, 0ull},
    {"+0x0", true, 0ull},
    {" +1", true, 1ull},
    {"+ 1", false, 0ull},
    {"0x 0x1", false, 0ull},
    {"\0371", false, 0ull},
    {"1\037", false, 0ull},
    {"\302\2401", false, 0ull},
    {"1\302\240", false, 0ull},
    {"\013", false, 0ull},
    {"\0141", true, 1ull},
    {"9", true, 9ull},
    {"A", true, 10ull},
    {"a", true, 10ull},
    {"F", true, 15ull},
    {"f", true, 15ull},
    {"g", false, 0ull},
    {"10", true, 16ull},
    {"0010", true, 16ull},
};
const unsigned long long kSweepDigest = 0x940c88d02e0892bull;
const int kSweepAccepted = 7753;

struct Xorshift
{
    std::uint64_t state;
    std::uint64_t operator()()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
};

bool ourHex(const std::string& text, std::uint64_t& value) { return textparse::parseUnsignedHex(text, value); }

} // namespace

TEST_CASE("framesync: parseUnsignedHex agrees with Qt on awkward inputs")
{
    for (const HexCase& c : kHexCases)
    {
        INFO("[" << c.text << "]");
        std::uint64_t value = 0x5A5A5A5A5A5A5A5Aull;   // a sentinel: a failed parse must leave it alone
        const bool ok = ourHex(c.text, value);
        CHECK(ok == c.ok);
        if (c.ok)
        {
            CHECK(value == c.value);
        }
        else
        {
            CHECK(value == 0x5A5A5A5A5A5A5A5Aull);
        }
    }
}

TEST_CASE("framesync: parseUnsignedHex agrees with Qt on arbitrary text")
{
    // A fixed pseudo-random sweep (so a failure reproduces) over the characters that matter: digits, both cases of
    // the hex letters, a prefix letter, signs and whitespace. Qt's 40,000 answers were folded into one FNV-1a digest
    // when the sweep was recorded.
    const std::string alphabet = "0123456789abcdefABCDEFxX+- \t";
    Xorshift next{0x2545F4914F6CDD1Dull};
    std::uint64_t digest = 1469598103934665603ull;
    int accepted = 0;
    for (int i = 0; i < 40000; i++)
    {
        const int length = static_cast<int>(next() % 22);          // 0..21, reaching past 16 digits
        std::string s;
        for (int k = 0; k < length; k++)
        {
            s += alphabet[static_cast<std::size_t>(next() % alphabet.size())];
        }
        std::uint64_t value = 0;
        const bool ok = ourHex(s, value);
        accepted += ok ? 1 : 0;
        const std::string text = (ok ? "1:" + std::to_string(value) : std::string("0")) + "\n";
        for (const unsigned char c : text)
        {
            digest ^= c;
            digest *= 1099511628211ull;
        }
    }
    CHECK(digest == kSweepDigest);
    CHECK(accepted == kSweepAccepted);   // the sweep reached both outcomes in volume
}

TEST_CASE("framesync: parseUnsignedHex reads back what std::format wrote, in every spelling it accepts")
{
    Xorshift next{0x9E3779B97F4A7C15ull};
    const std::string pads[] = {"", " ", "\t", "\r\n", " \t\v\f ", "\n"};
    for (int i = 0; i < 4000; i++)
    {
        // Values of every width, so the 64-bit edge and the short ones are all reached.
        const int bits = 1 + static_cast<int>(next() % 64);
        const std::uint64_t v = (bits == 64) ? next() : (next() & ((std::uint64_t{1} << bits) - 1));
        const bool upper = (next() & 1) != 0;
        const std::string digits = upper ? std::format("{:X}", v) : std::format("{:x}", v);
        const std::string zeros(next() % 3 == 0 ? next() % 25 : 0, '0');     // leading zeros do not count against 64 bits
        const std::string prefix = (next() & 1) ? ((next() & 1) ? "0x" : "0X") : "";
        const std::string sign   = (next() % 3 == 0) ? "+" : "";
        const std::string text   = pads[next() % 6] + sign + prefix + zeros + digits + pads[next() % 6];
        std::uint64_t value = 0;
        INFO("[" << text << "]");
        REQUIRE(ourHex(text, value));
        CHECK(value == v);
    }
}

TEST_CASE("framesync: parseUnsignedHex refuses what is not an unsigned hex number")
{
    std::uint64_t value = 7;
    CHECK(ourHex("FFFFFFFFFFFFFFFF", value));
    CHECK(value == 0xFFFFFFFFFFFFFFFFull);                       // the widest value is a number
    value = 7;
    CHECK(!ourHex("10000000000000000", value));                  // one more bit is not (and does not wrap)
    CHECK(value == 7);
    CHECK(!ourHex("1FFFFFFFFFFFFFFFF", value));
    CHECK(ourHex("0000000000000000FFFFFFFFFFFFFFFF", value));    // leading zeros are free
    CHECK(value == 0xFFFFFFFFFFFFFFFFull);
    for (const char* bad : {"-1", "-0", "- 1", "+-1", "-+1", "++1", "1-", "1+", "G", "1G", "0x", "0xg", "x1", "1 1", "1\t1", "0x 1", "0x0x1", ""})
    {
        value = 7;
        INFO("[" << bad << "]");
        CHECK(!ourHex(bad, value));
        CHECK(value == 7);
    }
}

// The one definition of "every bit of the pattern", shared by the sync decode, the packet reader and the failure
// report. The edges are the point: 64 bits must not shift by 64 (undefined), and a pattern with no length gets a
// full mask rather than an empty one that would match everything.
TEST_CASE("framesync: fullSyncMask is every bit of the pattern and never shifts by sixty-four")
{
    CHECK(fullSyncMask(1) == 0x1ull);
    CHECK(fullSyncMask(4) == 0xFull);
    CHECK(fullSyncMask(32) == 0xFFFFFFFFull);
    CHECK(fullSyncMask(63) == 0x7FFFFFFFFFFFFFFFull);
    CHECK(fullSyncMask(64) == 0xFFFFFFFFFFFFFFFFull);
    CHECK(fullSyncMask(65) == 0xFFFFFFFFFFFFFFFFull);
    CHECK(fullSyncMask(0) == 0xFFFFFFFFFFFFFFFFull);
    CHECK(fullSyncMask(-3) == 0xFFFFFFFFFFFFFFFFull);

    // Every length from 1 to 63 sets exactly that many bits.
    for (int bits = 1; bits < 64; ++bits)
    {
        const std::uint64_t mask = fullSyncMask(bits);
        CHECK(std::popcount(mask) == bits);
        CHECK((mask & (mask + 1)) == 0);   // a full mask is a run of ones from bit 0
    }
}

TEST_CASE("framesync: resolveFrameSync decodes pattern, mask and length")
{
    ResolvedFrameSync r;

    // An empty mask is "every bit of the pattern", whose length is four bits per CHARACTER.
    CHECK(resolveFrameSync("FE6B2840", "", r) == FrameSyncError::None);
    CHECK(r.pattern == 0xFE6B2840ull);
    CHECK(r.mask == 0xFFFFFFFFull);
    CHECK(r.patternLength == 32);

    CHECK(resolveFrameSync("1", "", r) == FrameSyncError::None);
    CHECK(r.mask == 0xFull);
    CHECK(r.patternLength == 4);

    // Sixteen digits fill the word: no shift by 64, every bit set.
    CHECK(resolveFrameSync("0123456789ABCDEF", "", r) == FrameSyncError::None);
    CHECK(r.patternLength == 64);
    CHECK(r.mask == 0xFFFFFFFFFFFFFFFFull);

    // The characters count, not the digits: spaces and a prefix are length too.
    CHECK(resolveFrameSync(" FE6B ", "", r) == FrameSyncError::None);
    CHECK(r.patternLength == 24);
    CHECK(resolveFrameSync("0xFE6B", "", r) == FrameSyncError::None);
    CHECK(r.patternLength == 24);

    // A given mask is used as it stands, whatever its length.
    CHECK(resolveFrameSync("FE6B2840", "00FF", r) == FrameSyncError::None);
    CHECK(r.mask == 0xFFull);
    CHECK(resolveFrameSync("FE6B2840", "0", r) == FrameSyncError::None);
    CHECK(r.mask == 0ull);
    // ...and the pattern's own value is untouched by it.
    CHECK(r.pattern == 0xFE6B2840ull);
}

TEST_CASE("framesync: resolveFrameSync reports the first problem and leaves the result alone")
{
    ResolvedFrameSync untouched;
    untouched.pattern       = 0x1234;
    untouched.mask          = 0x5678;
    untouched.patternLength = 99;

    auto expect = [&](const char* pattern, const char* mask, FrameSyncError error) {
        INFO("pattern [" << pattern << "] mask [" << mask << "]");
        ResolvedFrameSync r = untouched;
        CHECK(resolveFrameSync(pattern, mask, r) == error);
        CHECK(r.pattern == 0x1234ull);          // never half-written
        CHECK(r.mask == 0x5678ull);
        CHECK(r.patternLength == 99);
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
    expect("FE6B2840", "10000000000000000", FrameSyncError::MaskInvalid);
}

TEST_CASE("framesync: applyAcquisition sets every raw-affecting field")
{
    // The main run and the calibration extraction both build their parameters here, so a field missing from this
    // function is missing from both - and this is the test that names each one.
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

    CHECK(p.frameSync == 0xFE6B2840ull);
    CHECK(p.frameSyncMask == 0x00FF00FFull);
    CHECK(p.syncPatternLength == 32);
    CHECK(p.bitsInMinorFrame == 8000);
    CHECK(p.wordsInMinorFrame == 500);          // rounded up to 16-bit words
    CHECK(p.isRandomized);
    CHECK(p.isInverted);
    CHECK(!p.swapBytes);
    CHECK(p.dataRateBps == 800000.0);

    // And the other way round, so no field is "set" only because it defaulted to that value.
    a.randomized   = false;
    a.inverted     = false;
    a.swapBytes    = true;
    a.dataRateMbps = 0.0;
    a.bitsInMinorFrame = 801;                    // a partly filled last word still takes a slot
    applyAcquisition(p, a);
    CHECK(!p.isRandomized);
    CHECK(!p.isInverted);
    CHECK(p.swapBytes);
    CHECK(p.dataRateBps == 0.0);                 // "use the TMATS rate", not a rate
    CHECK(p.wordsInMinorFrame == 51);
    CHECK(p.bitsInMinorFrame == 801);

    // Each flag on its own, so one cannot be read from another.
    for (int mask = 0; mask < 8; ++mask)
    {
        Acquisition b;
        b.sync.patternLength = 8;
        b.randomized = (mask & 1) != 0;
        b.inverted   = (mask & 2) != 0;
        b.swapBytes  = (mask & 4) != 0;
        ProcessingParams q;
        applyAcquisition(q, b);
        CHECK(q.isRandomized == b.randomized);
        CHECK(q.isInverted == b.inverted);
        CHECK(q.swapBytes == b.swapBytes);
    }
}

TEST_CASE("framesync: applyAcquisition turns megabits into bits per second and a missing rate into zero")
{
    Acquisition a;
    a.sync.patternLength = 8;
    ProcessingParams p;
    for (const double mbps : {0.001, 0.8, 1.0, 10.0, 12.5})
    {
        a.dataRateMbps = mbps;
        applyAcquisition(p, a);
        CHECK(p.dataRateBps == doctest::Approx(mbps * 1e6));
    }
    a.dataRateMbps = 0.0;
    p.dataRateBps = 123.0;
    applyAcquisition(p, a);
    CHECK(p.dataRateBps == 0.0);
    a.dataRateMbps = -2.0;                       // a negative rate is no rate either
    p.dataRateBps = 123.0;
    applyAcquisition(p, a);
    CHECK(p.dataRateBps == 0.0);
}

TEST_CASE("framesync: applyAcquisition rounds the frame up to whole 16-bit words")
{
    Acquisition a;
    a.sync.patternLength = 8;
    ProcessingParams p;
    const struct { int bits; int words; } cases[] = {{0, 0}, {1, 1}, {15, 1}, {16, 1}, {17, 2}, {32, 2}, {33, 3}, {800, 50}, {801, 51}, {65536, 4096}};
    for (const auto& c : cases)
    {
        a.bitsInMinorFrame = c.bits;
        applyAcquisition(p, a);
        INFO(c.bits << " bits");
        CHECK(p.wordsInMinorFrame == c.words);
        CHECK(p.bitsInMinorFrame == c.bits);
    }
}

TEST_CASE("framesync: applyAcquisition leaves everything else alone")
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

    CHECK(p.timeChannelId == 3);
    CHECK(p.pcmChannelId == 7);
    CHECK(p.streamLabel == "label");
    CHECK(p.samplePeriodSec == 0.25);
    CHECK(p.mode == StreamMode::ReceiverChannelInfo);
    CHECK(p.startSeconds == 11ull);
    CHECK(p.stopSeconds == 22ull);
}
