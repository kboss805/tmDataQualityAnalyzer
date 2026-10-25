// StepDetector: the step-file parser, the plateau detection that turns a calibration recording into a profile (US5.3),
// and interpolateCalibration(). No Qt; ported from the Qt TestStepDetector, whose cases are all here under the same
// descriptions. The data-driven Qt cases are loops with INFO; qFuzzyCompare is reproduced by fuzzy() below.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "stepdetector.h"
#include "test_support.h"
#include "textfile.h"
#include "textparse.h"

using namespace tmdq_test;

namespace {

/// qFuzzyCompare(a, b) for doubles: equal to about twelve significant digits. Like Qt's, it never holds for zero
/// against anything but zero, so the cases that compare with zero use ==.
bool fuzzy(double a, double b)
{
    return std::abs(a - b) * 1000000000000.0 <= std::min(std::abs(a), std::abs(b));
}

/// Appends @p count samples of @p level to @p series.
void appendRun(std::vector<double>& series, double level, int count)
{
    for (int i = 0; i < count; i++)
    {
        series.push_back(level);
    }
}

const double kPeriod = 0.01; // 10 ms -> a 1.0s confirmation window is 100 samples.

/// The db values of a parsed list, for compact comparison.
std::vector<double> dbs(const std::vector<StepDefinition>& steps)
{
    std::vector<double> out;
    for (const StepDefinition& s : steps)
    {
        out.push_back(s.db);
    }
    return out;
}

/// Parses @p bytes written to a step file in @p dir.
bool parseBytes(const TempDir& dir, const std::string& name, const std::string& bytes,
                std::vector<StepDefinition>& steps, std::string& error)
{
    writeBytes(dir.file(name), bytes);
    return StepDetector::parseStepConfig(dir.file(name), steps, error);
}

// ---------------------------------------------------------------------------
// The original hand-written step-file parser, kept as an oracle.
//
// StepDetector::parseStepConfig was a second TOML parser of its own; it now reads through TomlDocument. This is the code
// it had before, copied here unchanged, so the two can be run side by side over files nobody wrote by hand. It is a
// test fixture: nothing else calls it.
// ---------------------------------------------------------------------------

std::string referenceStripComment(const std::string& value)
{
    const std::size_t hash = value.find('#');
    return textparse::trimmed(hash == std::string::npos ? value : value.substr(0, hash));
}

std::string referenceLower(std::string s)
{
    for (char& c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

bool referenceParseStepConfig(const std::filesystem::path& path, std::vector<StepDefinition>& out, std::string& error)
{
    out.clear();

    std::string text;
    if (!textfile::readText(path, text))
    {
        error = "Could not open step-config file '" + textfile::utf8FileName(path) + "'.";
        return false;
    }

    std::istringstream in(text);
    bool in_step = false;
    StepDefinition current;

    auto flush = [&]() {
        if (in_step)
        {
            out.push_back(current);
        }
        current = StepDefinition{};
    };

    std::string raw_line;
    while (std::getline(in, raw_line))
    {
        const std::string line = textparse::trimmed(raw_line);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (line.compare(0, 2, "[[") == 0)
        {
            flush();
            in_step = referenceLower(line) == "[[step]]";
            continue;
        }
        if (line.front() == '[')
        {
            flush();
            in_step = false;
            continue;
        }
        if (!in_step)
        {
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        const std::string key = referenceLower(textparse::trimmed(line.substr(0, eq)));
        const std::string val = referenceStripComment(line.substr(eq + 1));

        double num = 0.0;
        if (!textparse::parseDouble(val, num))
        {
            continue;
        }
        if (key == "db")
        {
            current.db = num;
        }
    }
    flush();

    if (out.empty())
    {
        error = "No [[Step]] entries found in '" + textfile::utf8FileName(path) + "'.";
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// parseStepConfig
// ---------------------------------------------------------------------------

TEST_CASE("StepDetector: parseStepConfig reads a valid file")
{
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE(parseBytes(dir, "valid.toml", "[[Step]]\ndb = 0.0\n\n[[Step]]\ndb = 6.0\n", steps, error));
    CHECK(steps.size() == 2);
    CHECK(dbs(steps) == std::vector<double>{0.0, 6.0});
}

TEST_CASE("StepDetector: parseStepConfig matches are case-insensitive")
{
    // The header and the key are matched without regard to case, and the whitespace around '=' and at the ends of the
    // line is ignored.
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(parseBytes(dir, "case.toml", "[[STEP]]\n  DB   =   3.5  \n[[step]]\nDb = 7\n", steps, error), error);
    CHECK(dbs(steps) == std::vector<double>{3.5, 7.0});
}

TEST_CASE("StepDetector: parseStepConfig header must be exactly [[Step]] to count")
{
    // The header comparison is against the whole trimmed line, so decoration on it means it is NOT a step header: a
    // trailing comment, inner spaces, a longer name. Whatever follows then sits in no step and is ignored. Pinned as the
    // current behaviour - a header with a comment silently losing its step deserves its own decision, not to be changed
    // as a side effect of a port.
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(parseBytes(dir, "headers.toml",
                               "[[Step]] # the first one\ndb = 1\n[[ Step ]]\ndb = 2\n[[Steps]]\ndb = 3\n[[Step]]\ndb = 4\n",
                               steps, error),
                    error);
    CHECK(dbs(steps) == std::vector<double>{4.0});
}

TEST_CASE("StepDetector: parseStepConfig ignores keys outside a step and other sections")
{
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(parseBytes(dir, "scope.toml",
                               "db = 99\n"                 // before any header: no step to belong to
                               "[Meta]\n"
                               "db = 98\n"                 // an ordinary section is not a step
                               "[[Step]]\n"
                               "db = 5\n"
                               "[Other]\n"                 // ends the step context...
                               "db = 97\n"                 // ...so this is not step 1's
                               "[[Step]]\n"
                               "label = nothing\n"         // a step with no db keeps the default 0
                               "[[Unrelated]]\n"           // another array-of-tables ends the step too
                               "db = 96\n",
                               steps, error),
                    error);
    CHECK(dbs(steps) == std::vector<double>{5.0, 0.0});
}

TEST_CASE("StepDetector: parseStepConfig value rules")
{
    // QString::toDouble's rules, on the text after '=' with a trailing '#' comment removed. A value that does not parse
    // is skipped, leaving the step at its default of 0 - the step itself still exists.
    struct Row { const char* name; const char* text; double expected; };
    const Row rows[] = {
        {"integer", "6", 6.0},          {"negative", "-3.5", -3.5},    {"plus", "+6", 6.0},
        {"exponent", "1e1", 10.0},      {"leading-dot", ".5", 0.5},    {"trailing-dot", "5.", 5.0},
        {"comment", "6 # six", 6.0},    {"comment-glued", "6# six", 6.0},
        {"quoted", "\"6\"", 0.0},       // quotes are NOT stripped here
        {"words", "abc", 0.0},          {"trailing-junk", "6x", 0.0},  {"comma", "1,5", 0.0},
        {"hex", "0x10", 0.0},           {"overflow", "1e999", 0.0},    {"double-sign", "+-6", 0.0},
        {"empty", "", 0.0},
    };
    TempDir dir;
    for (const Row& row : rows)
    {
        INFO(row.name);
        std::vector<StepDefinition> steps;
        std::string error;
        REQUIRE_MESSAGE(parseBytes(dir, "value.toml", std::string("[[Step]]\ndb = ") + row.text + "\n", steps, error), error);
        REQUIRE(steps.size() == std::size_t{1});
        CHECK(steps[0].db == row.expected);
    }
}

TEST_CASE("StepDetector: parseStepConfig keeps file order and the last db wins")
{
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(parseBytes(dir, "order.toml", "[[Step]]\ndb = 30\n[[Step]]\ndb = 10\n[[Step]]\ndb = 20\ndb = 21\n", steps,
                               error),
                    error);
    CHECK(dbs(steps) == std::vector<double>{30.0, 10.0, 21.0});
}

TEST_CASE("StepDetector: parseStepConfig clears its output first")
{
    // A failed parse must not leave the previous file's steps behind.
    TempDir dir;
    std::vector<StepDefinition> steps = {{1.0}, {2.0}};
    std::string error;
    CHECK(!parseBytes(dir, "none.toml", "# nothing\n", steps, error));
    CHECK(steps.empty());

    steps = {{1.0}, {2.0}};
    CHECK(!StepDetector::parseStepConfig(dir.file("absent.toml"), steps, error));
    CHECK(steps.empty());

    // And a successful one replaces rather than appends.
    steps = {{1.0}, {2.0}};
    REQUIRE(parseBytes(dir, "one.toml", "[[Step]]\ndb = 9\n", steps, error));
    CHECK(dbs(steps) == std::vector<double>{9.0});
}

TEST_CASE("StepDetector: parseStepConfig reads a BOM, CRLF and UTF-16")
{
    // Files as Notepad saves them. QTextStream detected all of these; a plain byte reader would see the BOM as part of
    // the first line and silently drop step 1.
    const std::string text = "[[Step]]\r\ndb = 4\r\n[[Step]]\r\ndb = 8\r\n";
    auto utf16 = [&](bool big) {
        std::string b = big ? std::string("\xFE\xFF", 2) : std::string("\xFF\xFE", 2);
        for (const char c : text)
        {
            b.push_back(big ? '\0' : c);
            b.push_back(big ? c : '\0');
        }
        return b;
    };
    struct Row { const char* name; std::string bytes; };
    const Row rows[] = {
        {"utf8-bom-crlf", std::string("\xEF\xBB\xBF") + text},
        {"utf16le", utf16(false)},
        {"utf16be", utf16(true)},
    };
    TempDir dir;
    for (const Row& row : rows)
    {
        INFO(row.name);
        std::vector<StepDefinition> steps;
        std::string error;
        REQUIRE_MESSAGE(parseBytes(dir, "enc.toml", row.bytes, steps, error), error);
        CHECK(dbs(steps) == std::vector<double>{4.0, 8.0});
    }
}

TEST_CASE("StepDetector: parseStepConfig opens non-ASCII paths")
{
    // Step files live wherever the operator keeps them, including user folders with accented or non-Latin names.
    TempDir dir;
    const fs::path folder = dir.path() / std::u8string(u8"omega-\u03A9");
    REQUIRE(fs::create_directory(folder));
    const fs::path path = folder / std::u8string(u8"caf\u00E9.toml");
    writeBytes(path, "[[Step]]\ndb = 12\n");
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(StepDetector::parseStepConfig(path, steps, error), error);
    CHECK(dbs(steps) == std::vector<double>{12.0});
}

TEST_CASE("StepDetector: parseStepConfig error messages name the file")
{
    // The message carries the file NAME (not the full path), so the log line reads the same wherever the file lives.
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    CHECK(!StepDetector::parseStepConfig(dir.file("missing_steps.toml"), steps, error));
    CHECK(error == "Could not open step-config file 'missing_steps.toml'.");

    CHECK(!parseBytes(dir, "empty_steps.toml", "[Meta]\nx = 1\n", steps, error));
    CHECK(error == "No [[Step]] entries found in 'empty_steps.toml'.");
}

TEST_CASE("StepDetector: parseStepConfig with no steps fails")
{
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    CHECK(!parseBytes(dir, "comment.toml", "# just a comment, no steps\n", steps, error));
    CHECK(!parseBytes(dir, "empty.toml", "", steps, error));
}

// The hand-written parser and TomlDocument agree on files neither author imagined. Every line comes from a pool of the
// shapes a step file has, and the awkward ones near them - headers with comments, spaces, other names, a missing
// bracket; keys in every case, quoted, commented, empty, repeated, outside any step - shuffled into files of up to a
// dozen lines. Whatever the original made of a file, the parser must make the same: whether it succeeded, the steps and
// their dB to the bit, and the message when it did not.
TEST_CASE("StepDetector: parseStepConfig agrees with the original hand-written parser on generated files")
{
    const std::vector<std::string> pool = {
        // headers
        "[[Step]]", "[[Step]]", "[[Step]]", "[[STEP]]", "[[step]]", "[[Step]]  ", "  [[Step]]",
        "[[Step]] # the first one", "[[ Step ]]", "[[Steps]]", "[[Unrelated]]", "[[Step", "[[", "[[]]",
        "[Meta]", "[Other]", "[Meta] # note", "[Meta", "[]", "]", "[[Step]] db = 3", "[Meta] x = 1",
        // keys
        "db = 6", "db = 6", "db=7", "DB = 8", "  db  =  9  ", "Db = 10", "db = \"6\"", "db = 6 # c",
        "db = 6# c", "db = abc", "db = ", "db", "db = +3", "db = -2.5", "db = 1e1", "db = 0x10",
        "db = .5", "db = 5.", "db = 1e999", "db = +-6", "db = nan", "db = inf", "db = 1,5",
        "db = \"5 # x\"", "db = 5 # \"x\"", "db = 6 \"", "db = 1 = 2", "label = nothing", "foo=bar",
        "= 3", "# db = 1", "", "   ", "\tdb = 4\t",
    };

    TempDir dir;
    const fs::path path = dir.file("generated.toml");

    std::mt19937 rng(20261020u);
    int agreed_ok = 0;
    int agreed_fail = 0;
    for (int n = 0; n < 4000; ++n)
    {
        std::string content;
        const int lines = static_cast<int>(rng() % 13);
        for (int i = 0; i < lines; ++i)
        {
            content += pool[rng() % pool.size()];
            content += (rng() % 4 == 0) ? "\r\n" : "\n";
        }
        writeBytes(path, content);

        std::vector<StepDefinition> want, got;
        std::string want_error, got_error;
        const bool want_ok = referenceParseStepConfig(path, want, want_error);
        const bool got_ok = StepDetector::parseStepConfig(path, got, got_error);

        INFO("file #" << n << ":\n" << content);
        REQUIRE(want_ok == got_ok);
        REQUIRE(want_error == got_error);
        REQUIRE(want.size() == got.size());
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            REQUIRE(std::bit_cast<std::uint64_t>(want[i].db) == std::bit_cast<std::uint64_t>(got[i].db));
        }
        (want_ok ? agreed_ok : agreed_fail)++;
    }
    // The generator must reach both outcomes in volume, or the comparison proves little.
    CHECK(agreed_ok > 1000);
    CHECK(agreed_fail > 300);
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

TEST_CASE("StepDetector: detect on a too-short series fails cleanly")
{
    // A series with fewer than two samples has no derivative and so no edge, and must come back as "not calibrated".
    // Sizing the derivative takes `size() - 1`, which on a std::vector is a huge unsigned number, where `reserve`
    // throws. detect() rejects the input first and sampleDerivative() guards its own reserve; this pins the outcome
    // whichever of the two a refactor weakens.
    const std::vector<StepDefinition> steps = {{0.0}, {6.0}};

    const StepDetector::Result none = StepDetector::detect({}, kPeriod, steps);
    CHECK(!none.profile.valid);
    CHECK(none.profile.points.empty());

    const StepDetector::Result one = StepDetector::detect({1000.0}, kPeriod, steps);
    CHECK(!one.profile.valid);
    CHECK(one.profile.points.empty());
}

TEST_CASE("StepDetector: detect finds clean steps")
{
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    CHECK(r.detectedPlateaus == 3);
    REQUIRE(r.profile.points.size() == 3);
    // Sorted ascending by raw; dB carried from the paired steps.
    CHECK(fuzzy(r.profile.points[0].rawAvg, 1000.0));
    CHECK(r.profile.points[0].trueDb == 0.0);
    CHECK(fuzzy(r.profile.points[1].rawAvg, 2000.0));
    CHECK(r.profile.points[1].trueDb == 6.0);
    CHECK(fuzzy(r.profile.points[2].rawAvg, 3000.0));
    CHECK(r.profile.points[2].trueDb == 12.0);
    CHECK(!r.extraPlateaus);
    CHECK(!r.saturated);
    CHECK(r.unresolvedDb.empty());
}

TEST_CASE("StepDetector: detect with too few plateaus fails")
{
    // Three steps expected, but the data only contains two plateaus.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    CHECK(!r.profile.valid);
    CHECK(r.detectedPlateaus == 2);
}

TEST_CASE("StepDetector: detect with extra plateaus uses the last of the monotonic run")
{
    // Two steps expected, three increasing plateaus present. Selection takes the LAST `expected` plateaus of the first
    // maximal monotonic run (here the run is all three: 1000 < 2000 < 3000), so (2000, 3000) pair with the steps and the
    // lowest plateau (1000) is dropped as leading pre-roll / turn-on transient.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    CHECK(r.extraPlateaus);
    REQUIRE(r.profile.points.size() == 2);
    CHECK(fuzzy(r.profile.points[0].rawAvg, 2000.0));
    CHECK(r.profile.points[0].trueDb == 0.0);
    CHECK(fuzzy(r.profile.points[1].rawAvg, 3000.0));
    CHECK(r.profile.points[1].trueDb == 6.0);
}

TEST_CASE("StepDetector: a short blip does not steal a pairing slot")
{
    // Reproduces a real failure seen on decoded hardware data: a brief startup transient (55 samples = 0.55s, under the
    // 1.0s confirmation window) ahead of the genuine sweep. Without a confirmation requirement, this blip would be
    // counted as a real plateau and consume pairing slot 0, shifting every subsequent plateau<->step pairing by one. It
    // must be rejected so the three genuine (confirmed) plateaus pair correctly with steps 0/6/12 dB.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 500.0, 55);   // spurious sub-confirmation blip
    appendRun(raw, 1000.0, 150); // genuine 0 dB plateau
    appendRun(raw, 2000.0, 150); // genuine 6 dB plateau
    appendRun(raw, 3000.0, 150); // genuine 12 dB plateau

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == 3);
    CHECK(fuzzy(r.profile.points[0].rawAvg, 1000.0));
    CHECK(r.profile.points[0].trueDb == 0.0);
    CHECK(fuzzy(r.profile.points[1].rawAvg, 2000.0));
    CHECK(r.profile.points[1].trueDb == 6.0);
    CHECK(fuzzy(r.profile.points[2].rawAvg, 3000.0));
    CHECK(r.profile.points[2].trueDb == 12.0);
}

TEST_CASE("StepDetector: a long leading transient does not shift the pairing")
{
    // Reproduces the RASA "time skew" field failure. The signal generator is switched on a few seconds into the
    // recording, producing a confirmed leading plateau (here 1.5s, ABOVE the 1.0s confirmation window, so unlike the
    // short-blip case it is NOT rejected) below the real 0 dB level. The injected sweep then climbs 0,3,6,12,18,24,30,36
    // and falls back down (the operator's optional down-ramp).
    //
    // With a naive first-N-consecutive-increasing selection the leading transient steals pairing slot 0: the real 0 dB
    // plateau gets paired with the 3 dB step, every subsequent pairing shifts up one, and the sweep stops one step short
    // of the 36 dB peak. Selection instead takes the LAST `expected` plateaus of the monotonic run, which drops the
    // leading transient (it extends the run's start) and keeps the pairing correct.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}, {36.0}};

    const std::vector<double> up = {1000.0, 1100.0, 1300.0, 1700.0, 2100.0, 2500.0, 2900.0, 3300.0};
    std::vector<double> raw;
    appendRun(raw, 600.0, 150); // generator-off / turn-on transient (1.5s, confirmed)
    for (double level : up)
    {
        appendRun(raw, level, 150);
    }
    // Down-ramp (a separate, opposite-direction run; excluded).
    for (int i = static_cast<int>(up.size()) - 2; i >= 0; i--)
    {
        appendRun(raw, up[static_cast<std::size_t>(i)], 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == 8);
    // Real 0 dB level (1000), NOT the 600 transient, must pair with step 0.
    CHECK_MESSAGE(fuzzy(r.profile.points[0].rawAvg, 1000.0),
                  "first point rawAvg=" << r.profile.points[0].rawAvg << ", expected 1000 (leading transient stole the slot)");
    CHECK(r.profile.points[0].trueDb == 0.0);
    // Peak (3300) must pair with the top step (36 dB), not be left off the sweep.
    CHECK(fuzzy(r.profile.points[7].rawAvg, 3300.0));
    CHECK(r.profile.points[7].trueDb == 36.0);
}

TEST_CASE("StepDetector: an inverted-polarity sweep is not reversed")
{
    // RASA receivers are inverted polarity: the raw count FALLS as the injected signal climbs, so the peak-signal step
    // (36 dB) is the MINIMUM raw plateau, not the maximum. Anchoring naively on the maximum raw plateau would pair the
    // whole sweep backwards. The monotonic-run selection is direction-agnostic: it uses the run's own direction (here
    // decreasing), takes its last `expected` plateaus, and pairs them with the steps in time order.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}, {36.0}};

    const std::vector<double> down = {3300.0, 2900.0, 2500.0, 2100.0, 1700.0, 1300.0, 1100.0, 1000.0};
    std::vector<double> raw;
    appendRun(raw, 3500.0, 150); // turn-on transient (highest raw = no signal)
    for (double level : down)
    {
        appendRun(raw, level, 150);
    }
    // Return ramp back up in raw (signal falling away); a separate opposite-direction run, excluded.
    for (int i = static_cast<int>(down.size()) - 2; i >= 0; i--)
    {
        appendRun(raw, down[static_cast<std::size_t>(i)], 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == 8);
    // Points are sorted ascending by raw. Lowest raw (1000) is the 36 dB peak, highest raw within the sweep (3300) is the
    // 0 dB step - NOT reversed, and the 3500 transient is excluded.
    CHECK_MESSAGE(fuzzy(r.profile.points[0].rawAvg, 1000.0), "lowest-raw point rawAvg=" << r.profile.points[0].rawAvg);
    CHECK(r.profile.points[0].trueDb == 36.0);
    CHECK(fuzzy(r.profile.points[7].rawAvg, 3300.0));
    CHECK(r.profile.points[7].trueDb == 0.0);
    for (const CalibrationPoint& p : r.profile.points)
    {
        CHECK(p.rawAvg < 3400.0);
    }
}

TEST_CASE("StepDetector: a non-monotonic pairing is rejected")
{
    // Chronological plateau order (2000, 1000, 3000) paired positionally with steps (0, 6, 12) gives points
    // (2000,0),(1000,6),(3000,12). Sorted ascending by rawAvg that's (1000,6),(2000,0),(3000,12) -- dB goes 6 -> 0 -> 12,
    // which is neither non-decreasing nor non-increasing. The profile must be rejected (not handed to
    // interpolateCalibration with a near-vertical interpolation slope).
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    CHECK(!r.profile.valid);
    CHECK(r.detectedPlateaus == 3); // plateaus were found; pairing was just rejected
}

TEST_CASE("StepDetector: noisy steps are still detected")
{
    std::vector<StepDefinition> steps = {{0.0}, {6.0}};

    // Small in-plateau ripple (amplitude 2) << the 1000-count step transitions.
    std::vector<double> raw;
    for (int i = 0; i < 150; i++) raw.push_back(1000.0 + (i % 3 == 0 ? 2.0 : 0.0));
    for (int i = 0; i < 150; i++) raw.push_back(2000.0 + (i % 3 == 0 ? 2.0 : 0.0));

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    CHECK(r.profile.valid);
    CHECK(r.detectedPlateaus == 2);
}

TEST_CASE("StepDetector: settling at the start of a plateau is excluded")
{
    // The plateau's first samples ramp up gradually (settling, delta small enough to stay "stable") before reaching the
    // true core level. Averaging the confirmation window from the END of the plateau (immediately before the next
    // transition) rather than its start must skip that settling region entirely, so the average equals the clean core.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}};

    std::vector<double> raw;
    appendRun(raw, 0.0, 150); // genuine 0 dB baseline plateau
    for (int v = 991; v <= 1000; v++) raw.push_back(static_cast<double>(v));
    appendRun(raw, 1000.0, 190); // stable core

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == 2);
    CHECK(fuzzy(r.profile.points[1].rawAvg, 1000.0));
}

TEST_CASE("StepDetector: a round trip through the same data is exact")
{
    // Calibrate from a clean stepped series, then "measure" the very same raw levels back through the resulting profile:
    // each step's own raw average must return that step's exact dB.
    std::vector<StepDefinition> steps = {{6.0}, {12.0}, {18.0}, {24.0}, {30.0}};

    const std::vector<double> levels = {1000.0, 2000.0, 3000.0, 4000.0, 5000.0};
    std::vector<double> raw;
    for (double level : levels)
    {
        appendRun(raw, level, 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    CHECK(r.detectedPlateaus == 5);
    CHECK(r.profile.points.size() == 5);

    for (std::size_t i = 0; i < levels.size(); i++)
    {
        const double db = interpolateCalibration(levels[i], r.profile);
        CHECK_MESSAGE(fuzzy(db, steps[i].db), "raw " << levels[i] << " -> " << db << " dB, expected " << steps[i].db);
    }
}

TEST_CASE("StepDetector: a multi-channel sweep stays time-aligned")
{
    // Regression for the "staircases drift in time across channels" report: several receiver channels see the SAME
    // injected dB sweep at the SAME times, but with different gain, opposite polarity, or a per-channel turn-on
    // transient. Each channel is calibrated independently - yet every channel must map the k-th sweep segment back to
    // the k-th step's dB.
    const std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}};
    constexpr int kHold = 150; // > the 100-sample (1.0 s) confirmation window

    const std::vector<double> clean    = {1000.0, 1500.0, 2000.0, 3000.0}; // nominal gain
    const std::vector<double> hotGain  = {1300.0, 1900.0, 2500.0, 3700.0}; // higher gain, same sweep
    const std::vector<double> inverted = {3000.0, 2500.0, 2000.0, 1000.0}; // inverted polarity

    auto buildRaw = [&](const std::vector<double>& levels, double leadingTransient) {
        std::vector<double> raw;
        if (leadingTransient >= 0.0)
            appendRun(raw, leadingTransient, kHold); // confirmed transient, must be dropped
        for (double level : levels)
            appendRun(raw, level, kHold);
        return raw;
    };

    struct Channel { std::vector<double> levels; double transient; };
    const std::vector<Channel> channels = {
        { clean,    -1.0 },    // nominal, no transient
        { hotGain,  -1.0 },    // different gain
        { inverted, -1.0 },    // opposite polarity
        { clean,   600.0 },    // a per-channel turn-on transient below the 0 dB level
    };

    for (std::size_t c = 0; c < channels.size(); c++)
    {
        INFO("channel " << c);
        const std::vector<double> raw = buildRaw(channels[c].levels, channels[c].transient);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        REQUIRE(r.profile.valid);
        REQUIRE(r.profile.points.size() == steps.size());

        for (std::size_t k = 0; k < steps.size(); k++)
        {
            const double db = interpolateCalibration(channels[c].levels[k], r.profile);
            CHECK_MESSAGE(std::abs(db - steps[k].db) < 1e-6,
                          "segment " << k << ": raw " << channels[c].levels[k] << " -> " << db << " dB, expected "
                                     << steps[k].db << " (pairing shifted -> staircase would stagger)");
        }
    }
}

TEST_CASE("StepDetector: the outcome names the gate that rejected")
{
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    // Success carries Calibrated, and reports the threshold it judged edges by - the quantity a merged step is measured
    // against.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        appendRun(raw, 3000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        CHECK(r.profile.valid);
        CHECK(r.outcome == CalibrationOutcome::Calibrated);
        CHECK(r.edgeThreshold > 0.0);
    }

    // Too few plateaus: only two levels for three expected steps. This is the shape a compressed / near-saturation
    // receiver produces, where adjacent steps merge because their separation never clears the edge threshold.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        CHECK(!r.profile.valid);
        CHECK(r.outcome == CalibrationOutcome::TooFewPlateaus);
    }

    // Enough plateaus, but they zig-zag instead of sweeping one way, so no run of three consecutive levels moves in a
    // single direction. Distinct from the case above and pointing at a different cause.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 3000.0, 150);
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 3000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        CHECK(!r.profile.valid);
        CHECK(r.detectedPlateaus >= static_cast<int>(steps.size())); // count alone looked fine
        CHECK(r.outcome == CalibrationOutcome::NoMonotonicSweep);
    }

    // No samples at all is "no data", not a detection shortfall.
    {
        const StepDetector::Result r = StepDetector::detect({}, kPeriod, steps);
        CHECK(r.outcome == CalibrationOutcome::NoData);
    }
}

TEST_CASE("StepDetector: a compressed, out-of-tolerance receiver still calibrates")
{
    // A receiver far out of tolerance does not move a uniform number of raw counts per dB: it is compressed at one end
    // and expansive at the other, so one sweep can hold both 12-count and 1100-count steps. A single global edge
    // threshold sized for the large steps cannot see the small ones.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {24.0}, {36.0}};

    const std::vector<double> levels = {800.0, 812.0, 826.0, 900.0, 1500.0, 2600.0};

    // Pseudo-random (not periodic) dither, seeded for reproducibility. A periodic pattern would be a poor stand-in for
    // receiver noise: its median absolute deviation collapses to zero even when the swings are large.
    std::uint32_t seed = 12345u;
    auto dither = [&seed]() {
        seed = seed * 1664525u + 1013904223u;      // Numerical Recipes LCG
        return (static_cast<double>((seed >> 16) & 0xFF) / 255.0 - 0.5) * 8.0; // +/- 4 counts
    };

    std::vector<double> raw;
    for (double level : levels)
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + dither());
        }
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);

    REQUIRE_MESSAGE(r.profile.valid, "outcome=" << calibrationOutcomeText(r.outcome) << " plateaus=" << r.detectedPlateaus
                                                << " threshold=" << r.edgeThreshold);
    REQUIRE(r.profile.points.size() == steps.size());

    // Every step must keep its own dB: a merged pair would shift the pairing and silently mis-map the whole channel.
    CHECK(r.profile.points.front().trueDb == 0.0);
    CHECK(r.profile.points.back().trueDb == 36.0);

    // It only resolved because the threshold was loosened, and that fact is reported rather than swallowed.
    CHECK(r.thresholdRelaxed);

    // A receiver with well-separated steps must NOT be reported as relaxed.
    std::vector<double> healthy;
    appendRun(healthy, 800.0, 150);
    appendRun(healthy, 1200.0, 150);
    appendRun(healthy, 1600.0, 150);
    appendRun(healthy, 2000.0, 150);
    appendRun(healthy, 2400.0, 150);
    appendRun(healthy, 2800.0, 150);
    StepDetector::Result h = StepDetector::detect(healthy, kPeriod, steps);
    CHECK(h.profile.valid);
    CHECK(!h.thresholdRelaxed);
}

TEST_CASE("StepDetector: noise-split plateaus do not sever the sweep")
{
    // Levels taken verbatim from a real out-of-tolerance receiver (step_cal, example.ch10, R_RCVR1): a clean climb from
    // no-signal to a saturated ceiling, except noise split two dwells into pairs of plateaus differing by 3 and -1 raw
    // counts. Adjacent plateaus within the edge threshold are the SAME physical level and must be coalesced before the
    // run is sought.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}, {36.0}};

    const std::vector<double> levels = {0.0,     7255.0,  14746.0, 22314.0, 27315.0, 34752.0,
                                        49892.0, 49895.0, 57310.0, 57309.0, 65472.0, 0.0};

    // The noise is load-bearing: it is what lifts the edge threshold above the 1-3 count split.
    std::uint32_t seed = 987u;
    auto dither = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<double>((seed >> 16) & 0xFF) / 255.0 - 0.5) * 4.0; // +/- 2 counts
    };

    std::vector<double> raw;
    for (double level : levels)
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + dither());
        }
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE_MESSAGE(r.profile.valid, "outcome=" << calibrationOutcomeText(r.outcome) << " plateaus=" << r.detectedPlateaus
                                                << " threshold=" << r.edgeThreshold);
    REQUIRE(r.profile.points.size() == steps.size());

    // Nine levels for eight steps, so the run's last `expected` are kept: 7255 -> 0 dB up to the ceiling -> 36 dB, each
    // split pair contributing exactly one point. Compared with a tolerance because the dither moves each settled average.
    CHECK_MESSAGE(std::abs(r.profile.points.front().rawAvg - 7255.0) < 5.0, r.profile.points.front().rawAvg);
    CHECK(r.profile.points.front().trueDb == 0.0);
    CHECK_MESSAGE(std::abs(r.profile.points.back().rawAvg - 65472.0) < 5.0, r.profile.points.back().rawAvg);
    CHECK(r.profile.points.back().trueDb == 36.0);
}

TEST_CASE("StepDetector: a flat channel reports no signal, not merged steps")
{
    // A dead input holds one value for the whole recording. It must not read like a compressed receiver's "too few
    // plateaus", which sends the operator looking for a detection problem when the sweep simply is not in the recording.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> flat;
    appendRun(flat, 0.0, 600);
    StepDetector::Result r = StepDetector::detect(flat, kPeriod, steps);
    CHECK(!r.profile.valid);
    CHECK(r.outcome == CalibrationOutcome::FlatNoSignal);
    // The stuck level is carried out so the report can name it: 0 is a dead input, full scale a railed one.
    REQUIRE(r.plateauLevels.size() == 1);
    CHECK(r.plateauLevels.front() == 0.0);

    // A railed channel is equally flat, just at the other end of the range.
    std::vector<double> railed;
    appendRun(railed, 65535.0, 600);
    StepDetector::Result railedResult = StepDetector::detect(railed, kPeriod, steps);
    CHECK(railedResult.outcome == CalibrationOutcome::FlatNoSignal);
    REQUIRE(!railedResult.plateauLevels.empty());
    CHECK(railedResult.plateauLevels.front() == 65535.0);

    // Two genuine levels is NOT "flat" - it is a real but incomplete sweep.
    std::vector<double> twoLevels;
    appendRun(twoLevels, 1000.0, 300);
    appendRun(twoLevels, 2000.0, 300);
    StepDetector::Result partial = StepDetector::detect(twoLevels, kPeriod, steps);
    CHECK(partial.outcome == CalibrationOutcome::TooFewPlateaus);
}

TEST_CASE("StepDetector: pinned samples do not shrink the noise estimate")
{
    // A receiver parked at the converter floor repeats 0 exactly: noise-free, and on a real cal recording a large share
    // of the series. Counted into the noise estimate, those perfect-zero derivatives drag it far under the live steps'
    // jitter; the edge threshold falls to its 2-count floor, every jittering sample reads as an edge, and the dwells
    // shatter into runs too short to confirm. The pinned share is exaggerated here (a majority) so the failure is
    // deterministic rather than dependent on where a median happens to land.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    std::vector<double> raw;
    appendRun(raw, 0.0, 1000); // pinned at the floor: the 0 dB level
    for (double level : {1000.0, 2000.0, 3000.0, 4000.0})
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + ((i % 2 == 0) ? 5.0 : -5.0)); // +/-5 count jitter
        }
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE_MESSAGE(r.profile.valid, "outcome=" << calibrationOutcomeText(r.outcome) << " threshold=" << r.edgeThreshold);
    REQUIRE(r.profile.points.size() == steps.size());
    CHECK(r.profile.points.front().rawAvg == 0.0);
    CHECK(r.profile.points.front().trueDb == 0.0);
    CHECK(std::abs(r.profile.points.back().rawAvg - 4000.0) < 6.0);
    CHECK(r.profile.points.back().trueDb == 24.0);
}

TEST_CASE("StepDetector: a split dwell far above the noise is still one step")
{
    // Noise grows with signal on a real receiver, so a high dwell can split into two plateaus further apart than any
    // threshold the quiet low steps support. Levels a small fraction of a typical step apart are one step, whatever the
    // noise estimate says. Noiseless here, so the edge threshold sits at its 2-count floor and only the step-relative
    // merge can heal the 30-count split.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    std::vector<double> raw;
    appendRun(raw, 0.0, 150);    // 0 dB
    appendRun(raw, 1000.0, 150); // 6 dB
    appendRun(raw, 2000.0, 150); // 12 dB
    appendRun(raw, 3000.0, 150); // 18 dB, first half of the dwell...
    appendRun(raw, 3030.0, 150); // ...second half, split 30 counts high
    appendRun(raw, 4000.0, 150); // 24 dB

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == steps.size());
    // The pre-sweep level keeps its 0 dB slot instead of being dropped as pre-roll.
    CHECK(r.profile.points.front().rawAvg == 0.0);
    CHECK(r.profile.points.front().trueDb == 0.0);
    // The split dwell is ONE point at 18 dB, carried by its later, settled half.
    CHECK(r.profile.points[3].rawAvg == 3030.0);
    CHECK(r.profile.points[3].trueDb == 18.0);
    CHECK(r.profile.points.back().trueDb == 24.0);
}

TEST_CASE("StepDetector: railed top steps calibrate what stayed in range")
{
    // A receiver whose gain is set too high for the injected levels runs off the top of its converter partway up the
    // sweep: the last dwells all read full scale. Those railed dwells carry no information, so they are dropped, the
    // steps below them are calibrated, and interpolateCalibration() clamps above the top measured step rather than
    // inventing values by extrapolating past the data.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    appendRun(raw, 65535.0, 300); // 18 and 24 dB both peg the converter

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    CHECK(r.saturated);
    CHECK(r.outcome == CalibrationOutcome::Calibrated);

    // The three measured dwells keep their own dB - the sweep starts at step 0.
    REQUIRE(r.profile.points.size() == 3);
    CHECK(r.profile.points.front().rawAvg == 1000.0);
    CHECK(r.profile.points.front().trueDb == 0.0);
    CHECK(r.profile.points.back().rawAvg == 3000.0);
    CHECK(r.profile.points.back().trueDb == 12.0);
    CHECK(r.unresolvedDb == std::vector<double>({18.0, 24.0}));

    // A raw value at the rail reads the top step that was actually measured.
    CHECK(interpolateCalibration(65535.0, r.profile) == 12.0);
    // The run held four levels, the railed one included: that is what the report compares with the step file.
    CHECK(r.sweepLevels == 4);
}

TEST_CASE("StepDetector: an unconfirmed dwell is measured from the grid")
{
    // The 18 dB dwell is in the recording but its plateau never confirms - noise shatters it into runs too short to
    // hold. Position alone would pair the 24 dB dwell with the 18 dB step and slide everything after it down one;
    // leaving the hole and interpolating across it assumes the receiver is linear there. The dwell grid says where the
    // missing dwell is, so it is measured directly.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    // 18 dB: jitters by a few counts for its whole dwell, so no run of it is ever stable long enough to confirm. Not the
    // midpoint of its neighbours, so a linear bridge across the hole would read the wrong dB.
    for (int i = 0; i < 150; i++)
    {
        raw.push_back(3600.0 + ((i % 2 == 0) ? 0.0 : 6.0));
    }
    appendRun(raw, 5000.0, 150);
    appendRun(raw, 65535.0, 150); // 30 dB pegs the converter

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    CHECK(r.saturated);
    REQUIRE(r.profile.points.size() == 5);

    // The unconfirmed dwell is measured at its own level and keeps its own dB...
    CHECK(r.profile.points[3].rawAvg == 3603.0);
    CHECK(r.profile.points[3].trueDb == 18.0);
    CHECK(interpolateCalibration(3603.0, r.profile) == 18.0);
    // ...the step after it keeps ITS own dB rather than sliding down one...
    CHECK(r.profile.points.back().rawAvg == 5000.0);
    CHECK(r.profile.points.back().trueDb == 24.0);
    // ...and only the railed step is left unresolved.
    CHECK(r.unresolvedDb == std::vector<double>({30.0}));
}

TEST_CASE("StepDetector: detect reports how many levels the sweep held")
{
    // An 8-step file against an 11-level sweep still "calibrates": the detector keeps the last eight levels and sets the
    // lower three aside as lead-in, so every step is paired with the wrong level and nothing fails. sweepLevels is what
    // lets the report notice.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}, {36.0}};

    std::vector<double> eleven;
    for (int k = 1; k <= 11; k++)
    {
        appendRun(eleven, 1000.0 * k, 150);
    }
    StepDetector::Result r = StepDetector::detect(eleven, kPeriod, steps);
    CHECK(r.profile.valid);
    CHECK(r.sweepLevels == 11);

    // A sweep that matches the step file exactly reports exactly that many.
    std::vector<double> eight;
    for (int k = 1; k <= 8; k++)
    {
        appendRun(eight, 1000.0 * k, 150);
    }
    StepDetector::Result exact = StepDetector::detect(eight, kPeriod, steps);
    CHECK(exact.profile.valid);
    CHECK(exact.sweepLevels == 8);
}

// ---------------------------------------------------------------------------
// detect: the paths the cases above do not reach on their own. Each was found by a mutant that survived them: the
// noise estimate's exact value, the confirmation window's boundary, the edge floor, a sweep that starts where a
// reversal ends, an inverted receiver's split dwell and its railed floor, and the saturated-top path's dwell grid and
// guards.
// ---------------------------------------------------------------------------

namespace {

/// The step list 0, 6, 12 ... for @p n steps.
std::vector<StepDefinition> sixDbSteps(int n)
{
    std::vector<StepDefinition> steps;
    for (int k = 0; k < n; k++)
    {
        steps.push_back({6.0 * k});
    }
    return steps;
}

} // namespace

TEST_CASE("StepDetector: the edge threshold is six robust sigmas of the live derivative")
{
    // Worked by hand. Each dwell repeats level + {0, 0, 8}, so inside a dwell the derivative is 0, +8 and -8 in equal
    // thirds: its median is 0 and the median absolute deviation 8, which a factor of 1.4826 turns into a Gaussian sigma.
    // The threshold is six of those. The floor run at 0 and the long railed run at full scale repeat one value exactly,
    // and are left out: counted in, their zero derivatives would make the deviation 0 and the threshold the 2-count
    // floor.
    std::vector<double> raw;
    appendRun(raw, 0.0, 300);
    for (double level : {1000.0, 2000.0, 3000.0, 4000.0})
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + (i % 3 == 2 ? 8.0 : 0.0));
        }
    }
    appendRun(raw, 65535.0, 2000);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(4));
    CHECK(r.edgeThreshold == doctest::Approx(6.0 * 1.4826 * 8.0));
}

TEST_CASE("StepDetector: a plateau needs exactly the confirmation window of stable samples")
{
    // At 10 ms a confirmation is 1 s, 100 samples. A dwell between two transitions loses its first and last samples to
    // them, so 102 samples hold exactly 100 stable ones and confirm; 101 hold 99 and do not.
    const std::vector<StepDefinition> steps = sixDbSteps(3);
    for (const auto [middle, confirms] : {std::pair{102, true}, std::pair{101, false}})
    {
        INFO("middle dwell of " << middle << " samples");
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, middle);
        appendRun(raw, 3000.0, 150);
        const StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        CHECK(r.profile.valid == confirms);
        CHECK(r.detectedPlateaus == (confirms ? 3 : 2));
    }
}

TEST_CASE("StepDetector: steps just above the 2-count floor are still steps, and steps below it are not")
{
    // Noiseless, so the threshold is its floor of 2 counts. Steps of 2.5 counts clear it and calibrate.
    std::vector<double> above;
    for (double level : {100.0, 102.5, 105.0, 107.5})
    {
        appendRun(above, level, 150);
    }
    const StepDetector::Result r = StepDetector::detect(above, kPeriod, sixDbSteps(4));
    REQUIRE(r.profile.valid);
    CHECK(r.profile.points.front().rawAvg == 100.0);
    CHECK(r.profile.points.back().trueDb == 18.0);

    // Steps of 1.5 counts do not, and relaxing the threshold below its floor - where every noisy sample would read as an
    // edge - is not allowed to rescue them.
    std::vector<double> below;
    for (double level : {100.0, 101.5, 103.0, 104.5})
    {
        appendRun(below, level, 150);
    }
    const StepDetector::Result b = StepDetector::detect(below, kPeriod, sixDbSteps(4));
    CHECK(!b.profile.valid);
    CHECK(b.outcome == CalibrationOutcome::FlatNoSignal);
}

TEST_CASE("StepDetector: a sweep that starts where a reversal ended is found")
{
    // A turn-on transient ABOVE the 0 dB level: the levels fall to step 0, then climb. The falling pair is too short to
    // be the sweep, and the sweep's first level is the one the reversal ended on.
    std::vector<double> raw;
    for (double level : {3000.0, 1000.0, 2000.0, 3000.0})
    {
        appendRun(raw, level, 150);
    }
    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(3));
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == 3);
    CHECK(r.profile.points[0].rawAvg == 1000.0);
    CHECK(r.profile.points[0].trueDb == 0.0);
    CHECK(r.profile.points[2].rawAvg == 3000.0);
    CHECK(r.profile.points[2].trueDb == 12.0);
}

TEST_CASE("StepDetector: a split as large as a tenth of a step is still one step")
{
    // The step-relative merge takes levels up to 10% of the median step apart as one. On 1000-count steps that is 100
    // counts, so an 80-count split - well past half of it - still heals.
    std::vector<double> raw;
    for (double level : {0.0, 1000.0, 2000.0, 3000.0, 3080.0, 4000.0})
    {
        appendRun(raw, level, 150);
    }
    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == 5);
    CHECK(r.profile.points.front().rawAvg == 0.0);
    CHECK(r.profile.points[3].rawAvg == 3080.0);
    CHECK(r.profile.points[3].trueDb == 18.0);
}

TEST_CASE("StepDetector: an irregular dwell anywhere in the grid keeps the fallback")
{
    // Four regular dwells, then one held too long before the rail: the grid fails on the LAST transition, after every
    // earlier level has already been numbered, and the whole saturated profile must still be refused.
    std::vector<double> raw;
    for (double level : {1000.0, 2000.0, 3000.0, 4000.0})
    {
        appendRun(raw, level, 150);
    }
    appendRun(raw, 5000.0, 260);
    appendRun(raw, 65535.0, 300);
    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(7));
    CHECK(!r.profile.valid);
    CHECK(!r.saturated);
}

TEST_CASE("StepDetector: an inverted receiver's split dwell is still one step")
{
    // The split-dwell case mirrored: raw falls as dB climbs. The step-relative merge measures the steps by their size,
    // whatever their sign, so the 30-count split between 1000 and 970 heals the same way.
    std::vector<double> raw;
    for (double level : {4000.0, 3000.0, 2000.0, 1000.0, 970.0, 0.0})
    {
        appendRun(raw, level, 150);
    }
    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
    REQUIRE(r.profile.valid);
    REQUIRE(r.profile.points.size() == 5);
    CHECK(r.profile.points.front().rawAvg == 0.0);      // lowest raw is the top step on an inverted receiver
    CHECK(r.profile.points.front().trueDb == 24.0);
    CHECK(r.profile.points[1].rawAvg == 970.0);         // the split dwell, by its later half
    CHECK(r.profile.points[1].trueDb == 18.0);
    CHECK(r.profile.points.back().rawAvg == 4000.0);
    CHECK(r.profile.points.back().trueDb == 0.0);
}

TEST_CASE("StepDetector: an inverted receiver that rails at the floor calibrates what stayed in range")
{
    // On an inverted receiver the strong steps drive the count DOWN, so it runs out of range at zero, not full scale.
    // "Railed" is within 2% of either end of the converter's range, so the lowest measured level here stays above 1311.
    std::vector<double> raw;
    appendRun(raw, 4000.0, 150);
    appendRun(raw, 3000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 0.0, 300);   // 18 and 24 dB both peg the converter's floor

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    CHECK(r.saturated);
    REQUIRE(r.profile.points.size() == 3);
    CHECK(r.profile.points.front().rawAvg == 2000.0);
    CHECK(r.profile.points.front().trueDb == 12.0);
    CHECK(r.profile.points.back().rawAvg == 4000.0);
    CHECK(r.profile.points.back().trueDb == 0.0);
    CHECK(r.unresolvedDb == std::vector<double>({18.0, 24.0}));
    CHECK(interpolateCalibration(0.0, r.profile) == 12.0);   // the floor reads the strongest step it measured

    // A level inside that 2% band at the top of the run is railed too: 1000 counts is no measurement either.
    std::vector<double> near_floor;
    appendRun(near_floor, 4000.0, 150);
    appendRun(near_floor, 3000.0, 150);
    appendRun(near_floor, 2000.0, 150);
    appendRun(near_floor, 1000.0, 150);
    appendRun(near_floor, 0.0, 150);
    const StepDetector::Result n = StepDetector::detect(near_floor, kPeriod, sixDbSteps(6));
    REQUIRE_MESSAGE(n.profile.valid, std::string(calibrationOutcomeText(n.outcome)));
    CHECK(n.profile.points.front().rawAvg == 2000.0);
    CHECK(n.unresolvedDb == std::vector<double>({18.0, 24.0, 30.0}));
}

TEST_CASE("StepDetector: a transition smeared over two samples is one boundary of the dwell grid")
{
    // Each step passes through one intermediate sample, so two derivative samples clear the major-edge threshold side by
    // side. They are one transition; counted as two, the grid's dwell would be a single sample.
    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    raw.push_back(1500.0);
    appendRun(raw, 2000.0, 149);
    raw.push_back(2500.0);
    appendRun(raw, 3000.0, 149);
    raw.push_back(33000.0);
    appendRun(raw, 65535.0, 299);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    CHECK(r.saturated);
    REQUIRE(r.profile.points.size() == 3);
    CHECK(r.profile.points.back().trueDb == 12.0);
}

TEST_CASE("StepDetector: short dwells at a coarse sample period keep their own transitions")
{
    // At 100 ms a confirmation is 10 samples, and dwells of 30 are long enough. Transitions 30 samples apart are separate
    // transitions, not one smeared across them.
    std::vector<double> raw;
    appendRun(raw, 1000.0, 30);
    appendRun(raw, 2000.0, 30);
    appendRun(raw, 3000.0, 30);
    appendRun(raw, 65535.0, 60);

    const StepDetector::Result r = StepDetector::detect(raw, 0.1, sixDbSteps(5));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    CHECK(r.saturated);
    REQUIRE(r.profile.points.size() == 3);
    CHECK(r.profile.points[1].rawAvg == 2000.0);
    CHECK(r.profile.points[1].trueDb == 6.0);
}

TEST_CASE("StepDetector: the dwell grid tolerates a little irregularity and refuses more")
{
    // The generator holds every step for the same time, so transitions land on a regular grid; a level's distance from
    // the first, in whole dwells, is its step. Dwells of 150, 140 and 155 samples are within a quarter-dwell of the
    // grid and calibrate. Dwells of 150 and 260 are not, and keep the linear fallback rather than a guessed numbering.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 140);
        appendRun(raw, 3000.0, 155);
        appendRun(raw, 65535.0, 300);
        const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
        REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
        CHECK(r.profile.points.back().trueDb == 12.0);
    }
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        appendRun(raw, 3000.0, 260);
        appendRun(raw, 65535.0, 300);
        const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
        CHECK(!r.profile.valid);
    }
}

TEST_CASE("StepDetector: a dwell with no transition of its own is skipped in the numbering, not shifted")
{
    // The receiver held one level through two dwells (12 and 18 dB read the same), so the grid sees a gap of two
    // dwells. The level after it is the 24 dB step, not the 18 dB one.
    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 300);
    appendRun(raw, 4000.0, 150);
    appendRun(raw, 5000.0, 150);
    appendRun(raw, 65535.0, 300);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(8));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    REQUIRE(r.profile.points.size() == 5);
    CHECK(r.profile.points[2].rawAvg == 3000.0);
    CHECK(r.profile.points[2].trueDb == 12.0);
    CHECK(r.profile.points[3].rawAvg == 4000.0);
    CHECK(r.profile.points[3].trueDb == 24.0);
    CHECK(r.profile.points[4].trueDb == 30.0);
    CHECK(r.unresolvedDb == std::vector<double>({18.0, 36.0, 42.0}));
}

TEST_CASE("StepDetector: an unconfirmed dwell is measured from its settled tail, by the median")
{
    // As the unconfirmed-dwell case, but the dwell spends its first 90 samples still settling at 3550 (too short a run
    // to confirm a plateau of its own), and its jitter is lopsided (two samples at 3600 for every one at 3612). The level
    // is the median of the last confirmation window, 100 samples: 40 settling and 60 settled, so 3600. Measured from the
    // start of the dwell the settling samples would be the majority and read 3550; averaged, the window would read 3586.4.
    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    appendRun(raw, 3550.0, 90);
    for (int i = 0; i < 60; i++)
    {
        raw.push_back(i % 3 == 2 ? 3612.0 : 3600.0);
    }
    appendRun(raw, 5000.0, 150);
    appendRun(raw, 65535.0, 150);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(6));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    REQUIRE(r.profile.points.size() == 5);
    CHECK(r.profile.points[3].rawAvg == 3600.0);
    CHECK(r.profile.points[3].trueDb == 18.0);
}

TEST_CASE("StepDetector: an unconfirmed dwell that is not between its neighbours is left unresolved")
{
    // A dwell that reads below the step before it is not a clean measurement of the step between; the hole stays.
    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    for (int i = 0; i < 150; i++)
    {
        raw.push_back(2500.0 + ((i % 2 == 0) ? 0.0 : 6.0));
    }
    appendRun(raw, 5000.0, 150);
    appendRun(raw, 65535.0, 150);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(6));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    REQUIRE(r.profile.points.size() == 4);
    CHECK(r.unresolvedDb == std::vector<double>({18.0, 30.0}));
    for (const CalibrationPoint& p : r.profile.points)
    {
        CHECK(p.trueDb != 18.0);
    }
}

TEST_CASE("StepDetector: the saturated-top path needs a railed top, and enough of the sweep below it")
{
    // Three clean levels for five steps, nothing railed: an incomplete sweep, not a saturated one.
    {
        std::vector<double> raw;
        for (double level : {1000.0, 2000.0, 3000.0})
        {
            appendRun(raw, level, 150);
        }
        const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
        CHECK(!r.profile.valid);
        CHECK(r.outcome == CalibrationOutcome::TooFewPlateaus);
        CHECK(!r.saturated);
    }
    // Two measured levels below the rail is too few to be worth a profile (at least three).
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        appendRun(raw, 65535.0, 300);
        const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(4));
        CHECK(!r.profile.valid);
    }
    // ...and so is less than half of the step file: three of eight.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        appendRun(raw, 3000.0, 150);
        appendRun(raw, 65535.0, 750);
        const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(8));
        CHECK(!r.profile.valid);
    }
}

TEST_CASE("StepDetector: two levels inside one dwell keep the linear fallback")
{
    // The 2000 dwell moves to 2150 halfway through - too small a move for the grid to call a transition, too large for
    // the levels to merge. Both levels sit in one dwell, so they cannot both be steps, and nothing is guessed.
    std::vector<double> raw;
    appendRun(raw, 1000.0, 300);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 2150.0, 150);
    appendRun(raw, 3000.0, 300);
    appendRun(raw, 65535.0, 600);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(6));
    CHECK(!r.profile.valid);
}

TEST_CASE("StepDetector: the plateau count describes the nominal pass, not the last retry")
{
    // The compressed receiver needs the threshold relaxed before its small steps separate. The count it reports is what
    // detection first saw - fewer distinct plateaus than steps, which is why it relaxed - not what the successful retry
    // found.
    const std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {24.0}, {36.0}};
    const std::vector<double> levels = {800.0, 812.0, 826.0, 900.0, 1500.0, 2600.0};
    std::uint32_t seed = 12345u;
    auto dither = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<double>((seed >> 16) & 0xFF) / 255.0 - 0.5) * 8.0;
    };
    std::vector<double> raw;
    for (double level : levels)
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + dither());
        }
    }
    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    REQUIRE(r.profile.valid);
    REQUIRE(r.thresholdRelaxed);
    CHECK(r.detectedPlateaus < static_cast<int>(steps.size()));
}

// ---------------------------------------------------------------------------
// initialStepConfigPath
// ---------------------------------------------------------------------------

TEST_CASE("StepDetector: initialStepConfigPath prefers the stream's file, then the default")
{
    // The Extract Calibration dialog opens with a step file already loaded, because almost every calibration uses the
    // same one. Which file is decided here.
    const fs::path root_path = repoRoot();
    REQUIRE(!root_path.empty());
    const std::u8string root_u8 = root_path.generic_u8string();
    const std::string appRoot(root_u8.begin(), root_u8.end());
    const std::string shipped = appRoot + "/settings/rcvr_cals/default.toml";
    const std::string rasa    = appRoot + "/settings/rcvr_cals/RASA.toml";
    REQUIRE_MESSAGE(fs::is_regular_file(textfile::pathFromUtf8(shipped)), shipped);

    // No previous choice: the shipped default.
    CHECK(StepDetector::initialStepConfigPath(appRoot, "") == shipped);

    // A stream that already used a step file keeps it, even when it is not the default.
    CHECK(StepDetector::initialStepConfigPath(appRoot, rasa) == rasa);

    // A stored reference that no longer resolves (a template from another machine) falls back to the default.
    CHECK(StepDetector::initialStepConfigPath(appRoot, "C:/nowhere/gone.toml") == shipped);

    // No settings tree at all (a bare build directory): nothing to preload.
    TempDir bare;
    const std::u8string bare_u8 = bare.path().generic_u8string();
    CHECK(StepDetector::initialStepConfigPath(std::string(bare_u8.begin(), bare_u8.end()), "").empty());
    CHECK(StepDetector::initialStepConfigPath("", "").empty());
}

TEST_CASE("StepDetector: initialStepConfigPath ignores a directory")
{
    // "Is a file", not "exists": a directory sitting where the step file should be (the preferred path, or the shipped
    // default's own location) must not be offered to the dialog as a step file to parse.
    TempDir root;
    REQUIRE(fs::create_directories(root.path() / "settings" / "rcvr_cals" / "default.toml"));   // a directory
    REQUIRE(fs::create_directories(root.path() / "somedir"));
    const std::u8string root_u8 = root.path().generic_u8string();
    const std::string r(root_u8.begin(), root_u8.end());

    CHECK(StepDetector::initialStepConfigPath(r, r + "/somedir").empty());
    CHECK(StepDetector::initialStepConfigPath(r, "").empty());
}

TEST_CASE("StepDetector: initialStepConfigPath works under a non-ASCII application root")
{
    // The root arrives as UTF-8; read in the ANSI code page, an accented folder name would not be found.
    TempDir dir;
    const fs::path root = dir.path() / std::u8string(u8"caf\u00E9-\u03A9");
    REQUIRE(fs::create_directories(root / "settings" / "rcvr_cals"));
    writeBytes(root / "settings" / "rcvr_cals" / "default.toml", "[[Step]]\ndb = 0\n");
    const std::u8string root_u8 = root.generic_u8string();
    const std::string r(root_u8.begin(), root_u8.end());
    CHECK(StepDetector::initialStepConfigPath(r, "") == r + "/settings/rcvr_cals/default.toml");
}

// ---------------------------------------------------------------------------
// interpolateCalibration
// ---------------------------------------------------------------------------

TEST_CASE("StepDetector: interpolate at the midpoint")
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    CHECK(fuzzy(interpolateCalibration(1500.0, p), 9.0));
}

TEST_CASE("StepDetector: interpolate below the first point clamps")
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // Below the first point clamps to its dB (out of calibrated range), not extrapolated: raw 500 -> 6.0, not 3.0.
    CHECK(fuzzy(interpolateCalibration(500.0, p), 6.0));
}

TEST_CASE("StepDetector: interpolate above the last point clamps")
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // Above the last point clamps to its dB: raw 2500 -> 12.0, not 15.0.
    CHECK(fuzzy(interpolateCalibration(2500.0, p), 12.0));
}

TEST_CASE("StepDetector: interpolate with a coincident raw value does not crash")
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {1000.0, 12.0}};
    p.valid = true;
    // Divide-by-zero guard returns the first point's dB rather than crashing.
    CHECK(fuzzy(interpolateCalibration(1000.0, p), 6.0));
}

TEST_CASE("StepDetector: interpolate across several segments uses the one the value falls in")
{
    // Three segments with different slopes; each value must use its own segment's two points, and the points
    // themselves map exactly.
    CalibrationProfile p;
    p.points = {{1000.0, 0.0}, {2000.0, 6.0}, {2500.0, 12.0}, {4500.0, 18.0}};
    p.valid = true;
    CHECK(interpolateCalibration(1000.0, p) == 0.0);
    CHECK(fuzzy(interpolateCalibration(1250.0, p), 1.5));
    CHECK(interpolateCalibration(2000.0, p) == 6.0);
    CHECK(fuzzy(interpolateCalibration(2250.0, p), 9.0));
    CHECK(interpolateCalibration(2500.0, p) == 12.0);
    CHECK(fuzzy(interpolateCalibration(3500.0, p), 15.0));
    CHECK(interpolateCalibration(4500.0, p) == 18.0);
}

TEST_CASE("StepDetector: a turn-on transient before a saturated sweep does not shift the numbering")
{
    // The transient sits above the 0 dB level and the sweep reverses into it, so the run the saturated path takes
    // starts at 1000 - its first level is step 0. The transient's own transition is in the dwell grid, and counting
    // from it numbered every step one too high (1000 read 6 dB and 0 dB was left unresolved).
    std::vector<double> raw;
    for (double level : {5000.0, 1000.0, 2000.0, 3000.0})
    {
        appendRun(raw, level, 150);
    }
    appendRun(raw, 65535.0, 300);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    CHECK(r.saturated);
    REQUIRE(r.profile.points.size() == 3);
    CHECK(r.profile.points[0].rawAvg == 1000.0);
    CHECK(r.profile.points[0].trueDb == 0.0);
    CHECK(r.profile.points[1].trueDb == 6.0);
    CHECK(r.profile.points[2].rawAvg == 3000.0);
    CHECK(r.profile.points[2].trueDb == 12.0);
    CHECK(r.unresolvedDb == std::vector<double>({18.0, 24.0}));
}

TEST_CASE("StepDetector: an inverted receiver's transient before a floor-railed sweep does not shift the numbering")
{
    // Mirrored: the raw count falls as dB climbs, the transient sits BELOW the 0 dB level, and the sweep rails at zero.
    std::vector<double> raw;
    for (double level : {1500.0, 4000.0, 3000.0, 2000.0})
    {
        appendRun(raw, level, 150);
    }
    appendRun(raw, 0.0, 300);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(5));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    CHECK(r.saturated);
    REQUIRE(r.profile.points.size() == 3);
    CHECK(r.profile.points.back().rawAvg == 4000.0);   // highest raw is the 0 dB step on an inverted receiver
    CHECK(r.profile.points.back().trueDb == 0.0);
    CHECK(r.profile.points.front().rawAvg == 2000.0);
    CHECK(r.profile.points.front().trueDb == 12.0);
    CHECK(r.unresolvedDb == std::vector<double>({18.0, 24.0}));
}

TEST_CASE("StepDetector: an unconfirmed dwell after a turn-on transient is still measured at its own step")
{
    // The transient's transition must not shift the slots the unconfirmed-dwell measurement works in either: the
    // jittering 18 dB dwell is measured at 18 dB, and the step after it keeps its own 24 dB.
    std::vector<double> raw;
    for (double level : {8000.0, 1000.0, 2000.0, 3000.0})
    {
        appendRun(raw, level, 150);
    }
    for (int i = 0; i < 150; i++)
    {
        raw.push_back(3600.0 + ((i % 2 == 0) ? 0.0 : 6.0));
    }
    appendRun(raw, 5000.0, 150);
    appendRun(raw, 65535.0, 150);

    const StepDetector::Result r = StepDetector::detect(raw, kPeriod, sixDbSteps(6));
    REQUIRE_MESSAGE(r.profile.valid, std::string(calibrationOutcomeText(r.outcome)));
    REQUIRE(r.profile.points.size() == 5);
    CHECK(r.profile.points[0].rawAvg == 1000.0);
    CHECK(r.profile.points[0].trueDb == 0.0);
    CHECK(r.profile.points[3].rawAvg == 3603.0);
    CHECK(r.profile.points[3].trueDb == 18.0);
    CHECK(r.profile.points[4].rawAvg == 5000.0);
    CHECK(r.profile.points[4].trueDb == 24.0);
    CHECK(r.unresolvedDb == std::vector<double>({30.0}));
}
