// FrameSyncFile: the frame-sync pattern file (US1.0 / US2.0 / US5.0), read and written by the core. No Qt; the core
// half of the Qt TestFrameSyncFile. The Qt suite pinned the file format through the dialog's load and save
// functions, so each of its expectations is what the dialog does with the file: a pattern upper-cased, a frame
// length clamped to the spin box, a missing Randomized key leaving the box alone. None of that is the core's. The
// cases here ask the core directly what a file SAYS (the pattern and mask as written, the length only if usable,
// where the length came from, Invert Data, and Randomized only when the file has the key) and what writing puts in
// the file. The dialog's half, and the Receiver SNR defaults, stay in TestFrameSyncFile.
//
// The oracle is the load and save code the core replaced, restated over TomlDocument and std types (the Qt suite
// kept it over QString and widgets), run beside the core over generated files.

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "constants.h"
#include "doctest.h"
#include "framesyncfile.h"
#include "test_support.h"
#include "tomldocument.h"

using namespace tmdq_test;

namespace {

using Strings = std::vector<std::string>;

std::string describe(const std::string& pattern, const std::string& mask, std::optional<int> bits, bool legacy,
                     bool inverted, std::optional<bool> randomized)
{
    return std::format("pattern=[{}] mask=[{}] bits={} legacy={} inverted={} randomized={}", pattern, mask,
                       bits ? std::to_string(*bits) : std::string("none"), legacy, inverted,
                       randomized ? (*randomized ? "true" : "false") : "none");
}

std::string describe(const FrameSyncFile& f)
{
    return describe(f.pattern, f.mask, f.bitsPerFrame, f.bitsFromLegacyKey, f.inverted, f.randomized);
}

/// What a file is expected to say: everything a file does not carry stays at its "says nothing" value.
struct Want
{
    std::string pattern;
    std::string mask;
    std::optional<int> bits;
    bool legacy = false;
    bool inverted = false;
    std::optional<bool> randomized;

    std::string text() const { return describe(pattern, mask, bits, legacy, inverted, randomized); }
};

std::string frame(const std::string& body)
{
    return "[Frame]\n" + body;
}

std::string utf16le(const std::string& ascii)
{
    std::string out("\xFF\xFE", 2);
    for (const char c : ascii)
    {
        out += c;
        out += '\0';
    }
    return out;
}

FrameSyncFile readBytesAs(const TempDir& dir, const std::string& bytes)
{
    const fs::path path = dir.file("pattern.toml");
    writeBytes(path, bytes);
    return readFrameSyncFile(path);
}

// ---------------------------------------------------------------------------
// What each file says
// ---------------------------------------------------------------------------

struct ReadCase
{
    const char* name;
    std::string bytes;
    bool present;
    Want want;
};

const std::vector<ReadCase>& readCases()
{
    static const std::vector<ReadCase> cases = {
        { "file-missing", {}, false, {} },
        { "file-empty", {}, true, {} },
        { "a-complete-file",
          frame("FrameSync = \"FE6B2840\"\nFrameSyncMask = \"FFFF0000\"\nBitsPerFrame = 800\nInverted = true\nRandomized = true\n"),
          true, { .pattern = "FE6B2840", .mask = "FFFF0000", .bits = 800, .inverted = true, .randomized = true } },
        { "a-complete-file-with-both-flags-off",
          frame("FrameSync = \"FE6B2840\"\nFrameSyncMask = \"FFFF0000\"\nBitsPerFrame = 800\nInverted = false\nRandomized = false\n"),
          true, { .pattern = "FE6B2840", .mask = "FFFF0000", .bits = 800, .randomized = false } },
        { "crlf-line-ends", frame("FrameSync = \"FE6B2840\"\r\nBitsPerFrame = 800\r\nRandomized = true\r\n"), true,
          { .pattern = "FE6B2840", .bits = 800, .randomized = true } },
        { "utf8-bom", "\xEF\xBB\xBF" + frame("FrameSync = \"FE6B2840\"\nBitsPerFrame = 800\n"), true,
          { .pattern = "FE6B2840", .bits = 800 } },
        { "utf16le-bom", utf16le(frame("FrameSync = \"FE6B2840\"\nBitsPerFrame = 800\nRandomized = true\n")), true,
          { .pattern = "FE6B2840", .bits = 800, .randomized = true } },

        // --- pattern and mask, as written (the dialog upper-cases; the file does not)
        { "pattern-only", frame("FrameSync = \"334AABBF\"\n"), true, { .pattern = "334AABBF" } },
        { "mask-only", frame("FrameSyncMask = \"FF00FF00\"\n"), true, { .mask = "FF00FF00" } },
        { "lowercase-is-kept-as-written", frame("FrameSync = \"fe6b2840\"\nFrameSyncMask = \"ffff0000\"\n"), true,
          { .pattern = "fe6b2840", .mask = "ffff0000" } },
        { "not-hex-is-kept-as-it-is", frame("FrameSync = \"zz-1\"\nFrameSyncMask = \"x\"\n"), true,
          { .pattern = "zz-1", .mask = "x" } },
        { "non-ascii-is-kept-as-written", frame("FrameSync = \"stra\xC3\x9F" "e \xCE\xBB\"\n"), true,
          { .pattern = "stra\xC3\x9F" "e \xCE\xBB" } },
        { "empty-values-say-nothing", frame("FrameSync = \"\"\nFrameSyncMask = \"\"\n"), true, {} },
        { "padded-value", frame("FrameSync = \"  AB  \"\n"), true, { .pattern = "  AB  " } },
        { "unquoted-value", frame("FrameSync = FE6B2840\n"), true, { .pattern = "FE6B2840" } },
        { "trailing-comment", frame("FrameSync = \"FE6B2840\" # the pattern\nBitsPerFrame = 800 # bits\n"), true,
          { .pattern = "FE6B2840", .bits = 800 } },
        { "duplicate-keys-last-wins", frame("FrameSync = \"AAAA\"\nFrameSync = \"BBBB\"\n"), true, { .pattern = "BBBB" } },
        { "keys-in-another-section-are-ignored", "[Other]\nFrameSync = \"FE6B2840\"\nBitsPerFrame = 800\n", true, {} },
        { "keys-with-no-section-are-ignored", "FrameSync = \"FE6B2840\"\nBitsPerFrame = 800\n", true, {} },
        { "key-case-matters", frame("framesync = \"FE6B2840\"\nbitsperframe = 800\n"), true, {} },

        // --- the frame length (the dialog clamps it to its spin box; the file does not)
        { "bits-at-the-minimum", frame("BitsPerFrame = 64\n"), true, { .bits = 64 } },
        { "bits-below-the-minimum-says-nothing", frame("BitsPerFrame = 63\n"), true, {} },
        { "bits-zero-says-nothing", frame("BitsPerFrame = 0\n"), true, {} },
        { "bits-negative-says-nothing", frame("BitsPerFrame = -800\n"), true, {} },
        { "bits-at-the-spin-box-maximum", frame("BitsPerFrame = 65536\n"), true, { .bits = 65536 } },
        { "bits-above-the-spin-box-maximum-is-not-clamped", frame("BitsPerFrame = 70000\n"), true, { .bits = 70000 } },
        { "bits-as-text-is-still-a-number", frame("BitsPerFrame = \"800\"\n"), true, { .bits = 800 } },
        { "bits-fraction-is-not-an-integer", frame("BitsPerFrame = 800.5\n"), true, {} },
        { "bits-padded", frame("BitsPerFrame =   800   \n"), true, { .bits = 800 } },
        { "bits-with-a-plus", frame("BitsPerFrame = +800\n"), true, { .bits = 800 } },
        { "words-in-minor-frame-is-sixteen-bits-a-word", frame("WordsInMinorFrame = 64\n"), true,
          { .bits = 1024, .legacy = true } },
        { "one-word-says-nothing", frame("WordsInMinorFrame = 1\n"), true, {} },
        { "two-words-is-below-the-minimum", frame("WordsInMinorFrame = 2\n"), true, {} },
        { "four-words-is-exactly-the-minimum", frame("WordsInMinorFrame = 4\n"), true, { .bits = 64, .legacy = true } },
        { "words-negative-says-nothing", frame("WordsInMinorFrame = -64\n"), true, {} },
        { "words-large-is-not-clamped", frame("WordsInMinorFrame = 100000\n"), true, { .bits = 1600000, .legacy = true } },
        { "bits-too-small-falls-back-to-words", frame("BitsPerFrame = 32\nWordsInMinorFrame = 64\n"), true,
          { .bits = 1024, .legacy = true } },
        { "valid-bits-beat-words", frame("BitsPerFrame = 800\nWordsInMinorFrame = 64\n"), true, { .bits = 800 } },
        { "bits-at-the-minimum-still-beat-words", frame("BitsPerFrame = 64\nWordsInMinorFrame = 100\n"), true,
          { .bits = 64 } },
        { "bits-too-small-and-words-too-small", frame("BitsPerFrame = 32\nWordsInMinorFrame = 3\n"), true, {} },

        // --- the two stream-format flags
        { "inverted-true", frame("Inverted = true\n"), true, { .inverted = true } },
        { "inverted-false", frame("Inverted = false\n"), true, {} },
        { "inverted-absent-is-off", frame("FrameSync = \"AB\"\n"), true, { .pattern = "AB" } },
        { "inverted-one", frame("Inverted = 1\n"), true, { .inverted = true } },
        { "inverted-zero", frame("Inverted = 0\n"), true, {} },
        { "inverted-quoted-true", frame("Inverted = \"true\"\n"), true, { .inverted = true } },
        { "inverted-yes", frame("Inverted = yes\n"), true, { .inverted = true } },
        { "inverted-uppercase-true", frame("Inverted = TRUE\n"), true, { .inverted = true } },
        { "inverted-empty", frame("Inverted =\n"), true, {} },
        { "randomized-true", frame("Randomized = true\n"), true, { .randomized = true } },
        { "randomized-false", frame("Randomized = false\n"), true, { .randomized = false } },
        { "randomized-absent-says-nothing", frame("FrameSync = \"AB\"\nInverted = true\n"), true,
          { .pattern = "AB", .inverted = true } },
        { "randomized-one", frame("Randomized = 1\n"), true, { .randomized = true } },
        { "randomized-zero", frame("Randomized = 0\n"), true, { .randomized = false } },
        { "randomized-yes", frame("Randomized = yes\n"), true, { .randomized = true } },
        { "randomized-empty-value-is-still-a-key", frame("Randomized =\n"), true, { .randomized = false } },
        { "randomized-quoted-false", frame("Randomized = \"false\"\n"), true, { .randomized = false } },
    };
    return cases;
}

} // namespace

TEST_CASE("framesyncfile: each file says what it says, case by case")
{
    TempDir dir;
    for (const ReadCase& c : readCases())
    {
        INFO("case " << c.name);
        const fs::path path = dir.file("pattern.toml");
        std::error_code ec;
        fs::remove(path, ec);
        if (c.present)
        {
            writeBytes(path, c.bytes);
        }
        CHECK(describe(readFrameSyncFile(path)) == c.want.text());
    }
}

TEST_CASE("framesyncfile: the reader reports what a file says and nothing else")
{
    TempDir dir;

    // Nothing in the file, or no file: nothing to say, and Invert Data off.
    const FrameSyncFile none = readFrameSyncFile(dir.file("nonexistent.toml"));
    CHECK(none.pattern.empty());
    CHECK(none.mask.empty());
    CHECK_FALSE(none.bitsPerFrame.has_value());
    CHECK_FALSE(none.randomized.has_value());
    CHECK_FALSE(none.inverted);
    CHECK_FALSE(none.bitsFromLegacyKey);

    // Everything as written: the pattern is not case-folded (that is the dialog's business).
    const FrameSyncFile all = readBytesAs(dir, frame("FrameSync = \"fe6b2840\"\nFrameSyncMask = \"ffff0000\"\n"
                                                     "BitsPerFrame = 800\nInverted = true\nRandomized = false\n"));
    CHECK(all.pattern == "fe6b2840");
    CHECK(all.mask == "ffff0000");
    REQUIRE(all.bitsPerFrame.has_value());
    CHECK(*all.bitsPerFrame == 800);
    CHECK_FALSE(all.bitsFromLegacyKey);
    CHECK(all.inverted);
    REQUIRE(all.randomized.has_value());
    CHECK_FALSE(*all.randomized);   // present and false is not the same as absent

    // The older key, sixteen bits a word, and the marker that says so.
    const FrameSyncFile legacy = readBytesAs(dir, frame("WordsInMinorFrame = 64\n"));
    REQUIRE(legacy.bitsPerFrame.has_value());
    CHECK(*legacy.bitsPerFrame == 1024);
    CHECK(legacy.bitsFromLegacyKey);
    CHECK_FALSE(legacy.randomized.has_value());

    // A usable BitsPerFrame wins, and a legacy value that is still too small leaves no marker behind.
    CHECK_FALSE(readBytesAs(dir, frame("BitsPerFrame = 800\nWordsInMinorFrame = 64\n")).bitsFromLegacyKey);
    const FrameSyncFile too_small = readBytesAs(dir, frame("BitsPerFrame = 8\nWordsInMinorFrame = 3\n"));
    CHECK_FALSE(too_small.bitsPerFrame.has_value());
    CHECK_FALSE(too_small.bitsFromLegacyKey);
}

// ---------------------------------------------------------------------------
// What writing puts in the file
// ---------------------------------------------------------------------------

namespace {

struct WriteCase
{
    const char* name;
    std::string pattern;
    std::string mask;
    int bits;
    bool inverted;
    bool randomized;
    std::string expect;   // the exact bytes
};

std::string fileOf(const std::string& pattern_value, const std::string& mask_value, const std::string& bits,
                   bool inverted, bool randomized)
{
    return "[Frame]\r\nFrameSync = \"" + pattern_value + "\"\r\nFrameSyncMask = \"" + mask_value +
           "\"\r\nBitsPerFrame = " + bits + "\r\nInverted = " + (inverted ? "true" : "false") +
           "\r\nRandomized = " + (randomized ? "true" : "false") + "\r\n";
}

const std::vector<WriteCase>& writeCases()
{
    static const std::vector<WriteCase> cases = {
        { "everything-on", "FE6B2840", "FFFFFFFF", 800, true, true, fileOf("FE6B2840", "FFFFFFFF", "800", true, true) },
        { "everything-off", "FE6B2840", "FFFFFFFF", 800, false, false, fileOf("FE6B2840", "FFFFFFFF", "800", false, false) },
        { "inverted-only", "334AABBF", "FFFFFFFF", 32767, true, false, fileOf("334AABBF", "FFFFFFFF", "32767", true, false) },
        { "randomized-only", "334AABBF", "FFFFFFFF", 32767, false, true, fileOf("334AABBF", "FFFFFFFF", "32767", false, true) },
        { "lowercase-is-written-as-typed", "fe6b2840", "ffff0000", 800, false, false,
          fileOf("fe6b2840", "ffff0000", "800", false, false) },
        { "empty-strings", "", "", 800, false, false, fileOf("", "", "800", false, false) },
        { "bits-zero", "AB", "CD", 0, false, false, fileOf("AB", "CD", "0", false, false) },
        { "bits-negative", "AB", "CD", -5, false, false, fileOf("AB", "CD", "-5", false, false) },
        { "bits-at-the-maximum", "AB", "CD", 65536, false, false, fileOf("AB", "CD", "65536", false, false) },
        { "bits-very-large", "AB", "CD", 2147483647, false, false, fileOf("AB", "CD", "2147483647", false, false) },
        { "quote-and-backslash", "A\"B\\C", "D", 800, false, false, fileOf("A\\\"B\\\\C", "D", "800", false, false) },
        { "non-ascii", "\xCE\xA9\xC3\xA9", "FF", 800, false, false, fileOf("\xCE\xA9\xC3\xA9", "FF", "800", false, false) },
        { "a-hash-and-an-equals", "A#B=C", "D", 800, false, false, fileOf("A#B=C", "D", "800", false, false) },
        { "padded", "  AB  ", " CD ", 800, false, false, fileOf("  AB  ", " CD ", "800", false, false) },
        // A newline in a value is written as the file's own line end, inside the quotes.
        { "a-newline", "A\nB", "C", 800, false, false, fileOf("A\r\nB", "C", "800", false, false) },
    };
    return cases;
}

} // namespace

TEST_CASE("framesyncfile: writing puts exactly these bytes in the file")
{
    TempDir dir;
    for (const WriteCase& c : writeCases())
    {
        INFO("case " << c.name);
        const fs::path path = dir.file("saved.toml");
        std::error_code ec;
        fs::remove(path, ec);
        REQUIRE(writeFrameSyncFile(path, c.pattern, c.mask, c.bits, c.inverted, c.randomized));
        CHECK(readBytes(path) == c.expect);
    }
}

TEST_CASE("framesyncfile: writing to a path that cannot be written fails, and one that can succeeds")
{
    TempDir dir;
    CHECK_FALSE(writeFrameSyncFile(dir.file("no_such_folder_for_tmdq") / "pattern.toml", "AB", "CD", 800, false, false));
    CHECK_FALSE(fs::exists(dir.file("no_such_folder_for_tmdq")));

    const fs::path path = dir.file("pattern.toml");
    CHECK(writeFrameSyncFile(path, "AB", "CD", 800, true, false));
    CHECK(fs::exists(path));
}

TEST_CASE("framesyncfile: writing replaces the whole file, and Data Rate and Sample Rate are not in it")
{
    TempDir dir;
    const fs::path path = dir.file("pattern.toml");
    writeBytes(path, frame("FrameSync = \"OLD\"\nDataRate = 12.5\nSampleRate = 4\nExtra = \"left over\"\n"));
    REQUIRE(writeFrameSyncFile(path, "NEW", "MASK", 1024, false, true));
    const std::string bytes = readBytes(path);
    CHECK(bytes == fileOf("NEW", "MASK", "1024", false, true));
    CHECK_FALSE(contains(bytes, "DataRate"));
    CHECK_FALSE(contains(bytes, "SampleRate"));
    CHECK_FALSE(contains(bytes, "OLD"));
    CHECK_FALSE(contains(bytes, "left over"));
}

TEST_CASE("framesyncfile: a pattern file in a folder with a non-ASCII name reads and writes")
{
    TempDir dir;
    const fs::path folder = dir.path() / fs::path(utf8(u8"pättern Ω"));
    fs::create_directories(folder);
    const fs::path path = folder / fs::path(utf8(u8"främe.toml"));
    REQUIRE(writeFrameSyncFile(path, "FE6B2840", "FFFF0000", 4096, true, true));
    const FrameSyncFile file = readFrameSyncFile(path);
    CHECK(describe(file) == Want{ .pattern = "FE6B2840", .mask = "FFFF0000", .bits = 4096, .inverted = true,
                                  .randomized = true }.text());
}

TEST_CASE("framesyncfile: what is written reads back, for every combination of the flags")
{
    TempDir dir;
    const fs::path path = dir.file("roundtrip.toml");
    const std::vector<int> lengths = { 64, 800, 4096, 65536 };
    const Strings texts = { "FE6B2840", "334AABBF", "" };
    for (const bool inverted : { false, true })
    {
        for (const bool randomized : { false, true })
        {
            for (const int bits : lengths)
            {
                for (const std::string& pattern : texts)
                {
                    INFO("inverted " << inverted << " randomized " << randomized << " bits " << bits << " pattern ["
                                     << pattern << "]");
                    REQUIRE(writeFrameSyncFile(path, pattern, "FFFF0000", bits, inverted, randomized));
                    const FrameSyncFile file = readFrameSyncFile(path);
                    CHECK(describe(file) == Want{ .pattern = pattern, .mask = "FFFF0000", .bits = bits,
                                                  .inverted = inverted, .randomized = randomized }.text());
                }
            }
        }
    }

    // A length the reader will not use is still written (the dialog's spin box keeps it in range) and reads as nothing.
    REQUIRE(writeFrameSyncFile(path, "AB", "CD", 63, false, false));
    CHECK_FALSE(readFrameSyncFile(path).bitsPerFrame.has_value());
    CHECK(contains(readBytes(path), "BitsPerFrame = 63\r\n"));
}

// ---------------------------------------------------------------------------
// The code the core replaced, restated over std types, beside the core over generated files.
// ---------------------------------------------------------------------------

namespace {

/// The load the dialog used to do inline, minus the widgets: the same keys, the same fallback, the same rules about
/// what a file leaves alone. The Qt suite kept this over QString and widgets; here it fills the struct the core returns.
FrameSyncFile oldRead(const fs::path& path)
{
    const TomlDocument cfg = TomlDocument::load(path);
    FrameSyncFile out;

    std::string sync = cfg.string("Frame/FrameSync");
    if (!sync.empty())
    {
        out.pattern = sync;
    }
    std::string mask = cfg.string("Frame/FrameSyncMask");
    if (!mask.empty())
    {
        out.mask = mask;
    }

    int bits = cfg.integer("Frame/BitsPerFrame", 0);
    bool legacy = false;
    if (bits < PCMConstants::kMinFrameLengthBits)
    {
        const int words = cfg.integer("Frame/WordsInMinorFrame", 0);
        if (words >= 2)
        {
            bits = words * PCMConstants::kCommonWordLen;
            legacy = true;
        }
    }
    if (bits >= PCMConstants::kMinFrameLengthBits)
    {
        out.bitsPerFrame = bits;
        out.bitsFromLegacyKey = legacy;
    }

    out.inverted = cfg.boolean("Frame/Inverted", false);
    if (cfg.contains("Frame/Randomized"))
    {
        out.randomized = cfg.boolean("Frame/Randomized");
    }
    return out;
}

bool oldWrite(const fs::path& path, const std::string& pattern, const std::string& mask, int bits, bool inverted,
              bool randomized)
{
    TomlDocument cfg;
    cfg.setValue("Frame/FrameSync", pattern);
    cfg.setValue("Frame/FrameSyncMask", mask);
    cfg.setValue("Frame/BitsPerFrame", bits);
    cfg.setValue("Frame/Inverted", inverted);
    cfg.setValue("Frame/Randomized", randomized);
    return cfg.save(path);
}

/// A deterministic generator (the Qt suite's, state for state), so a failure names the same case every run.
struct Rng
{
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int below(int n) { return int(next() % std::uint32_t(n)); }
    bool chance(int percent) { return below(100) < percent; }
    template <typename T>
    const T& pick(const std::vector<T>& list) { return list[static_cast<std::size_t>(below(int(list.size())))]; }
};

std::string lowered(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return s;
}

/// A pattern file as a hand-edited or older one might be: keys present or not, in the right section or another,
/// with values from pools of the ordinary and the awkward, repeated, commented, with either line ending and
/// sometimes a byte-order mark.
std::string generateFile(Rng& rng)
{
    static const Strings strings = {
        "\"FE6B2840\"", "\"fe6b2840\"", "\"\"", "FE6B2840", "\"  AB  \"", "\"stra\xC3\x9F" "e\"", "\"zz\"", "\"A#B\"",
        "123", "\"334AABBF\"", "\"FFFFFFFF\"", "\"ffff0000\"", "'single'",
    };
    static const Strings bits = { "800", "64", "63", "0", "-5", "65536", "70000", "\"800\"", "800.5", "+800", "abc",
                                  "", "32", "2147483647", "4294967296", "32767", "1024" };
    static const Strings words = { "64", "1", "2", "3", "4", "-64", "100000", "abc", "0", "\"64\"", "32767", "8", "" };
    static const Strings flags = { "true", "false", "1", "0", "yes", "no", "TRUE", "\"true\"", "\"false\"", "", "maybe", "2", "False" };

    std::string text;
    const int section = rng.below(100);
    if (section < 88)
    {
        text += "[Frame]\n";
    }
    else if (section < 94)
    {
        text += "[Other]\n";
    }
    // else: no section at all

    auto line = [&](const char* name, const std::string& value) {
        std::string k = name;
        if (rng.chance(4))
        {
            k = lowered(k);   // key case matters
        }
        std::string l = k + " = " + value;
        if (rng.chance(8))
        {
            l += " # a comment";
        }
        text += l + "\n";
    };

    struct Field { const char* name; const Strings* pool; int presence; };
    const Field fields[] = {
        { "FrameSync", &strings, 70 }, { "FrameSyncMask", &strings, 55 }, { "BitsPerFrame", &bits, 65 },
        { "WordsInMinorFrame", &words, 30 }, { "Inverted", &flags, 50 }, { "Randomized", &flags, 50 },
    };
    for (const Field& f : fields)
    {
        if (!rng.chance(f.presence))
        {
            continue;
        }
        line(f.name, rng.pick(*f.pool));
        if (rng.chance(6))
        {
            line(f.name, rng.pick(*f.pool));   // a repeated key: the last one wins
        }
    }

    if (rng.chance(30))
    {
        std::string crlf;
        for (const char c : text)
        {
            crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
        }
        text = crlf;
    }
    if (rng.chance(5))
    {
        text = "\xEF\xBB\xBF" + text;
    }
    return text;
}

} // namespace

TEST_CASE("framesyncfile: the core reads every generated file like the code it replaced")
{
    constexpr int kFiles = 300;
    Rng rng(20261026u);
    TempDir dir;
    const fs::path path = dir.file("generated.toml");
    int with_bits = 0, with_legacy_bits = 0, randomized_true = 0, randomized_false = 0, randomized_absent = 0,
        inverted = 0, with_pattern = 0, ignored_bits = 0;
    for (int n = 0; n < kFiles; ++n)
    {
        const std::string bytes = generateFile(rng);
        writeBytes(path, bytes);
        const FrameSyncFile want = oldRead(path);
        const FrameSyncFile got = readFrameSyncFile(path);
        INFO("case " << n << ": " << bytes.substr(0, 260));
        CHECK(describe(got) == describe(want));

        with_bits += got.bitsPerFrame.has_value();
        with_legacy_bits += got.bitsFromLegacyKey && got.bitsPerFrame.has_value();
        ignored_bits += !got.bitsPerFrame.has_value();
        with_pattern += !got.pattern.empty();
        inverted += got.inverted;
        randomized_true += got.randomized.has_value() && *got.randomized;
        randomized_false += got.randomized.has_value() && !*got.randomized;
        randomized_absent += !got.randomized.has_value();
    }
    // The generator must reach every branch in volume, or "no difference" proves nothing.
    CHECK(with_bits > 60);
    CHECK(with_legacy_bits > 6);
    CHECK(ignored_bits > 15);
    CHECK(with_pattern > 80);
    CHECK(inverted > 20);
    CHECK(randomized_true > 20);
    CHECK(randomized_false > 20);
    CHECK(randomized_absent > 20);
}

TEST_CASE("framesyncfile: the core writes every generated file like the code it replaced")
{
    Rng rng(20261027u);
    const Strings texts = { "FE6B2840", "fe6b2840", "", "  AB  ", "A\"B\\C", "A#B=C", "A\nB", "334AABBF", "FFFFFFFF",
                            "\xCE\xA9\xC3\xA9", "x y", "'q'" };
    const std::vector<int> bits = { 0, 1, 63, 64, 800, 4096, 32767, 65536, -1, -800, 2147483647,
                                    std::numeric_limits<int>::min() };
    TempDir dir;
    const fs::path path_old = dir.file("old.toml");
    const fs::path path_new = dir.file("new.toml");
    int different_flags = 0, odd_text = 0;
    for (int n = 0; n < 300; ++n)
    {
        const std::string pattern = rng.pick(texts), mask = rng.pick(texts);
        const int b = rng.pick(bits);
        const bool inv = rng.chance(50), rnd = rng.chance(50);
        std::error_code ec;
        fs::remove(path_old, ec);
        fs::remove(path_new, ec);
        REQUIRE(oldWrite(path_old, pattern, mask, b, inv, rnd));
        REQUIRE(writeFrameSyncFile(path_new, pattern, mask, b, inv, rnd));
        const std::string old_bytes = readBytes(path_old);
        INFO("case " << n);
        CHECK(readBytes(path_new) == old_bytes);
        CHECK_FALSE(old_bytes.empty());
        different_flags += inv != rnd;
        odd_text += pattern.find('"') != std::string::npos || pattern.find('\n') != std::string::npos || pattern.empty();
    }
    CHECK(different_flags > 60);
    CHECK(odd_text > 40);
}
