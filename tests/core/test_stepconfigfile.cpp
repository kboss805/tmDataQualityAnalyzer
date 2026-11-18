// The step-config file: the `[[Step]]` levels a calibration recording is held to, and which step file the Extract
// Calibration dialog opens with (ported in plan step 2.4 with the StepDetector suite they came out of). The parser once
// was a second TOML parser of its own; `referenceParseStepConfig` below is what it was, kept as an oracle.

#include <bit>
#include <cstdint>
#include <filesystem>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "stepconfigfile.h"
#include "test_support.h"
#include "textfile.h"
#include "textparse.h"

using namespace tmdq_test;

namespace {

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
    return parseStepConfig(dir.file(name), steps, error);
}

// ---------------------------------------------------------------------------
// The original hand-written step-file parser, kept as an oracle.
//
// parseStepConfig was a second TOML parser of its own; it now reads through TomlDocument. This is the code
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

TEST_CASE("StepConfigFile: parseStepConfig reads a valid file")
{
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE(parseBytes(dir, "valid.toml", "[[Step]]\ndb = 0.0\n\n[[Step]]\ndb = 6.0\n", steps, error));
    CHECK(steps.size() == 2);
    CHECK(dbs(steps) == std::vector<double>{0.0, 6.0});
}

TEST_CASE("StepConfigFile: parseStepConfig matches are case-insensitive")
{
    // The header and the key are matched without regard to case, and the whitespace around '=' and at the ends of the
    // line is ignored.
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(parseBytes(dir, "case.toml", "[[STEP]]\n  DB   =   3.5  \n[[step]]\nDb = 7\n", steps, error), error);
    CHECK(dbs(steps) == std::vector<double>{3.5, 7.0});
}

TEST_CASE("StepConfigFile: parseStepConfig header must be exactly [[Step]] to count")
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

TEST_CASE("StepConfigFile: parseStepConfig ignores keys outside a step and other sections")
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

TEST_CASE("StepConfigFile: parseStepConfig value rules")
{
    // textparse::parseDouble's rules (surrounding whitespace ignored, one optional sign, decimal or exponent form, the
    // whole text a number), on the text after '=' with a trailing '#' comment removed. A value that does not parse
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

TEST_CASE("StepConfigFile: parseStepConfig keeps file order and the last db wins")
{
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(parseBytes(dir, "order.toml", "[[Step]]\ndb = 30\n[[Step]]\ndb = 10\n[[Step]]\ndb = 20\ndb = 21\n", steps,
                               error),
                    error);
    CHECK(dbs(steps) == std::vector<double>{30.0, 10.0, 21.0});
}

TEST_CASE("StepConfigFile: parseStepConfig clears its output first")
{
    // A failed parse must not leave the previous file's steps behind.
    TempDir dir;
    std::vector<StepDefinition> steps = {{1.0}, {2.0}};
    std::string error;
    CHECK(!parseBytes(dir, "none.toml", "# nothing\n", steps, error));
    CHECK(steps.empty());

    steps = {{1.0}, {2.0}};
    CHECK(!parseStepConfig(dir.file("absent.toml"), steps, error));
    CHECK(steps.empty());

    // And a successful one replaces rather than appends.
    steps = {{1.0}, {2.0}};
    REQUIRE(parseBytes(dir, "one.toml", "[[Step]]\ndb = 9\n", steps, error));
    CHECK(dbs(steps) == std::vector<double>{9.0});
}

TEST_CASE("StepConfigFile: parseStepConfig reads a BOM, CRLF and UTF-16")
{
    // Files as Notepad saves them. v2.14.2 read all of these; a plain byte reader would see the BOM as part of
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

TEST_CASE("StepConfigFile: parseStepConfig opens non-ASCII paths")
{
    // Step files live wherever the operator keeps them, including user folders with accented or non-Latin names.
    TempDir dir;
    const fs::path folder = dir.path() / std::u8string(u8"omega-\u03A9");
    REQUIRE(fs::create_directory(folder));
    const fs::path path = folder / std::u8string(u8"caf\u00E9.toml");
    writeBytes(path, "[[Step]]\ndb = 12\n");
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(parseStepConfig(path, steps, error), error);
    CHECK(dbs(steps) == std::vector<double>{12.0});
}

TEST_CASE("StepConfigFile: parseStepConfig error messages name the file")
{
    // The message carries the file NAME (not the full path), so the log line reads the same wherever the file lives.
    TempDir dir;
    std::vector<StepDefinition> steps;
    std::string error;
    CHECK(!parseStepConfig(dir.file("missing_steps.toml"), steps, error));
    CHECK(error == "Could not open step-config file 'missing_steps.toml'.");

    CHECK(!parseBytes(dir, "empty_steps.toml", "[Meta]\nx = 1\n", steps, error));
    CHECK(error == "No [[Step]] entries found in 'empty_steps.toml'.");
}

TEST_CASE("StepConfigFile: parseStepConfig with no steps fails")
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
TEST_CASE("StepConfigFile: parseStepConfig agrees with the original hand-written parser on generated files")
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
        const bool got_ok = parseStepConfig(path, got, got_error);

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
// initialStepConfigPath
// ---------------------------------------------------------------------------

TEST_CASE("StepConfigFile: initialStepConfigPath prefers the stream's file, then the default")
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
    CHECK(initialStepConfigPath(appRoot, "") == shipped);

    // A stream that already used a step file keeps it, even when it is not the default.
    CHECK(initialStepConfigPath(appRoot, rasa) == rasa);

    // A stored reference that no longer resolves (a template from another machine) falls back to the default.
    CHECK(initialStepConfigPath(appRoot, "C:/nowhere/gone.toml") == shipped);

    // No settings tree at all (a bare build directory): nothing to preload.
    TempDir bare;
    const std::u8string bare_u8 = bare.path().generic_u8string();
    CHECK(initialStepConfigPath(std::string(bare_u8.begin(), bare_u8.end()), "").empty());
    CHECK(initialStepConfigPath("", "").empty());
}

TEST_CASE("StepConfigFile: initialStepConfigPath ignores a directory")
{
    // "Is a file", not "exists": a directory sitting where the step file should be (the preferred path, or the shipped
    // default's own location) must not be offered to the dialog as a step file to parse.
    TempDir root;
    REQUIRE(fs::create_directories(root.path() / "settings" / "rcvr_cals" / "default.toml"));   // a directory
    REQUIRE(fs::create_directories(root.path() / "somedir"));
    const std::u8string root_u8 = root.path().generic_u8string();
    const std::string r(root_u8.begin(), root_u8.end());

    CHECK(initialStepConfigPath(r, r + "/somedir").empty());
    CHECK(initialStepConfigPath(r, "").empty());
}

TEST_CASE("StepConfigFile: initialStepConfigPath works under a non-ASCII application root")
{
    // The root arrives as UTF-8; read in the ANSI code page, an accented folder name would not be found.
    TempDir dir;
    const fs::path root = dir.path() / std::u8string(u8"caf\u00E9-\u03A9");
    REQUIRE(fs::create_directories(root / "settings" / "rcvr_cals"));
    writeBytes(root / "settings" / "rcvr_cals" / "default.toml", "[[Step]]\ndb = 0\n");
    const std::u8string root_u8 = root.generic_u8string();
    const std::string r(root_u8.begin(), root_u8.end());
    CHECK(initialStepConfigPath(r, "") == r + "/settings/rcvr_cals/default.toml");
}
