// TomlDocument, the reader/writer behind every settings file. No Qt: this file is built by
// native/tmdq_core_tests/tmdq_core_tests.vcxproj with MSBuild and links no Qt.
//
// It is the doctest port of the Qt Test suite TestTomlDocument (plan step 2.4), case for case and
// assertion for assertion. Those cases pin the behaviour directly, including the parts Qt supplied for free
// (byte-order marks and UTF-16, whitespace and '+' tolerance in numbers, the %g double format, CRLF on
// write), because those are exactly what a port off QString would change without any caller noticing.

#include <cmath>
#include <limits>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_support.h"
#include "tomldocument.h"

using namespace tmdq_test;

namespace {

TomlDocument loadDoc(const fs::path& path, std::string* error = nullptr)
{
    return TomlDocument::load(path, error);
}

} // namespace

TEST_CASE("TomlDocument reads sections and keys in file order")
{
    TempDir tmp;
    const fs::path path = tmp.file("order.toml");
    // Deliberately not alphabetical: a receiver word map is meaningful in file order,
    // which is what the QSettings version lost and had to re-parse the file to recover.
    writeBytes(path, "Top = 1\n"
                     "\n"
                     "[Zeta]\n"
                     "Word = 3\n"
                     "[EMPTY]\n"
                     "[Alpha]\n"
                     "Word = 1\n"
                     "[EMPTY]\n");

    const TomlDocument doc = loadDoc(path);
    CHECK(doc.ok());
    // Top-level keys are not a section; an empty section is still declared; a
    // repeated section is listed once.
    CHECK(doc.sectionsInFileOrder() == Strings{"Zeta", "EMPTY", "Alpha"});
    CHECK(doc.integer("Top") == 1);
    CHECK(doc.integer("Zeta/Word") == 3);
    CHECK(doc.integer("Alpha/Word") == 1);
    CHECK(!doc.contains("EMPTY/Word"));
}

// `[[Step]]` used to read as a section literally named "[Step]": the class did not implement
// arrays of tables, and that was pinned so nobody would rely on it. It does now (step files are read
// through it), so the same file reads as an array of tables and has no sections at all.
TEST_CASE("TomlDocument: a double-bracket header opens an array table, not a section")
{
    TempDir tmp;
    const fs::path path = tmp.file("steps.toml");
    writeBytes(path, "[[Step]]\ndb = 6\n");

    const TomlDocument doc = loadDoc(path);
    CHECK(doc.sectionsInFileOrder().empty());
    CHECK(!doc.contains("[Step]/db"));
    CHECK(!doc.contains("Step/db"));
    REQUIRE(doc.arrayTables().size() == 1);
    CHECK(doc.arrayTables()[0].name == "Step");
    CHECK(doc.arrayTables()[0].entries.size() == 1);
}

TEST_CASE("TomlDocument: array tables keep file order, keys and values as written")
{
    TempDir tmp;
    const fs::path path = tmp.file("tables.toml");
    writeBytes(path, "[[Item]]\n"
                     "Name = \"first one\"   # kept with its quotes\n"
                     "  Count   =   3  # trailing comment\n"
                     "Count = 4\n"
                     "Hash = \"a # b\"\n"
                     "\n"
                     "[[Item]]\n"
                     "Name = second\n");

    const TomlDocument doc = loadDoc(path);
    const auto items = doc.tables("item");
    REQUIRE(items.size() == 2);

    // Keys in file order, repeats included, trimmed; values as written - quotes kept, an unquoted
    // trailing comment and the whitespace around the value removed, a '#' inside quotes left alone.
    const auto& first = items[0]->entries;
    REQUIRE(first.size() == 4);
    CHECK(first[0].first == "Name");
    CHECK(first[0].second == "\"first one\"");
    CHECK(first[1].first == "Count");
    CHECK(first[1].second == "3");
    CHECK(first[2].second == "4");
    CHECK(first[3].second == "\"a # b\"");

    REQUIRE(items[1]->entries.size() == 1);
    CHECK(items[1]->entries[0].second == "second");
}

TEST_CASE("TomlDocument: array table names match without case but otherwise exactly")
{
    TempDir tmp;
    const fs::path path = tmp.file("names.toml");
    writeBytes(path, "[[Step]]\nk = 1\n"
                     "[[STEP]]\nk = 2\n"
                     "[[step]]\nk = 3\n"
                     "[[ Step ]]\nk = 4\n"      // inner spaces are part of the name
                     "[[Steps]]\nk = 5\n"       // so is a longer name
                     "[[Other]]\nk = 6\n");

    const TomlDocument doc = loadDoc(path);
    CHECK(doc.arrayTables().size() == 6);

    Strings got;
    for (const TomlDocument::Table* t : doc.tables("StEp"))
    {
        got.push_back(t->entries.at(0).second);
    }
    CHECK(got == Strings{"1", "2", "3"});
    CHECK(doc.tables("steps").size() == 1);
    CHECK(doc.tables("missing").empty());
}

// A `[[` line that is not just `[[Name]]` still opens an entry, so what follows is not mistaken for
// the entry before it - but no name matches it. This is the rule the step-file parser always had.
TEST_CASE("TomlDocument: a malformed array header opens an entry no name matches")
{
    TempDir tmp;
    const fs::path path = tmp.file("malformed.toml");
    writeBytes(path, "[[Step]] # the first one\nk = 1\n"
                     "[[Step\nk = 2\n"
                     "[[\nk = 3\n"
                     "[[Step]]\nk = 4\n");

    const TomlDocument doc = loadDoc(path);
    REQUIRE(doc.arrayTables().size() == 4);
    CHECK(doc.arrayTables()[0].name.empty());
    CHECK(doc.arrayTables()[1].name.empty());
    CHECK(doc.arrayTables()[2].name.empty());

    const auto steps = doc.tables("step");
    REQUIRE(steps.size() == 1);
    CHECK(steps[0]->entries.at(0).second == "4");
}

TEST_CASE("TomlDocument: a section header ends an entry and sections are unaffected by entries")
{
    TempDir tmp;
    const fs::path path = tmp.file("mixed.toml");
    writeBytes(path, "Top = 1\n"
                     "[Before]\nA = 10\n"
                     "[[Item]]\nin_item = 20\n"
                     "[After]\nB = 30\n"                 // a section header ends the entry
                     "[[Item]]\nin_second = 40\n"
                     "[Meta] # not a well-formed header\n"
                     "stray = 50\n");                    // still ends the entry: not the second item's

    const TomlDocument doc = loadDoc(path);

    // The sections read as they always did, and no key from an entry leaks into one.
    CHECK(doc.sectionsInFileOrder() == Strings{"Before", "After"});
    CHECK(doc.integer("Top") == 1);
    CHECK(doc.integer("Before/A") == 10);
    CHECK(doc.integer("After/B") == 30);
    CHECK(!doc.contains("in_item"));
    CHECK(!doc.contains("Item/in_item"));
    CHECK(!doc.contains("After/in_second"));

    // Each entry holds only what was under its own header.
    const auto items = doc.tables("item");
    REQUIRE(items.size() == 2);
    REQUIRE(items[0]->entries.size() == 1);
    CHECK(items[0]->entries[0].first == "in_item");
    REQUIRE(items[1]->entries.size() == 1);
    CHECK(items[1]->entries[0].first == "in_second");
}

TEST_CASE("TomlDocument: comments are quote-aware and escapes unescape")
{
    TempDir tmp;
    const fs::path path = tmp.file("quotes.toml");
    writeBytes(path, "# a full-line comment\n"
                     "[F]\n"
                     "Hash = \"a # b\"   # trailing comment\n"
                     "Bare = 5 # trailing comment\n"
                     "Esc = \"say \\\"hi\\\" \\\\ done\"\n"
                     "Spaced   =   \"  kept  \"  \n"
                     "NoEquals\n");

    const TomlDocument doc = loadDoc(path);
    CHECK(doc.string("F/Hash") == "a # b");
    CHECK(doc.string("F/Bare") == "5");
    CHECK(doc.string("F/Esc") == "say \"hi\" \\ done");
    // Inside the quotes is the value, untrimmed.
    CHECK(doc.string("F/Spaced") == "  kept  ");
    // A line with no '=' is skipped rather than failing the file.
    CHECK(!doc.contains("F/NoEquals"));
    CHECK(doc.ok());
}

TEST_CASE("TomlDocument: a repeated key overwrites")
{
    TempDir tmp;
    const fs::path path = tmp.file("repeat.toml");
    writeBytes(path, "[A]\nK = 1\nK = 2\n");
    CHECK(loadDoc(path).integer("A/K") == 2);
}

TEST_CASE("TomlDocument reads a UTF-8 BOM and CRLF")
{
    // A file saved by Notepad: a UTF-8 BOM and CRLF line endings. Without BOM handling
    // the first key would be read as "\xEF\xBB\xBFTop" and silently go missing.
    TempDir tmp;
    const fs::path path = tmp.file("bom.toml");
    writeBytes(path, std::string("\xEF\xBB\xBF") + "Top = 7\r\n[S]\r\nK = \"v\"\r\n");

    const TomlDocument doc = loadDoc(path);
    CHECK(doc.integer("Top", -1) == 7);
    CHECK(doc.string("S/K") == "v");
    CHECK(doc.sectionsInFileOrder() == Strings{"S"});
}

TEST_CASE("TomlDocument reads UTF-16 with a BOM")
{
    // Older Notepad's "Unicode" encoding. Qt's text stream detected the BOM and decoded
    // it, so a settings file saved that way worked, and the port must not stop it.
    const std::u16string text = u"[S]\r\nName = \"café Ω\"\r\nN = 12\r\n";

    for (const bool bigEndian : {false, true})
    {
        CAPTURE(bigEndian);
        std::string bytes = bigEndian ? std::string("\xFE\xFF", 2) : std::string("\xFF\xFE", 2);
        for (const char16_t c : text)
        {
            const char hi = static_cast<char>(c >> 8);
            const char lo = static_cast<char>(c & 0xFF);
            bytes.push_back(bigEndian ? hi : lo);
            bytes.push_back(bigEndian ? lo : hi);
        }

        TempDir tmp;
        const fs::path path = tmp.file("utf16.toml");
        writeBytes(path, bytes);

        const TomlDocument doc = loadDoc(path);
        CHECK(doc.integer("S/N", -1) == 12);
        CHECK(doc.string("S/Name") == utf8(u8"café Ω"));
    }
}

TEST_CASE("TomlDocument: non-ASCII values and paths round-trip")
{
    // A value outside ASCII, saved to and loaded from a folder outside every Western
    // code page: the value must come back intact and the file must be UTF-8.
    TempDir tmp;
    const fs::path folder = tmp.path() / std::u8string(u8"omega-Ω");
    REQUIRE(fs::create_directory(folder));
    const fs::path path = folder / "values.toml";
    const std::string value = utf8(u8"café Ω");

    TomlDocument out;
    out.setValue("S/Name", value);
    std::string error;
    REQUIRE_MESSAGE(out.save(path, &error), error);

    CHECK(contains(readBytes(path), value));
    const TomlDocument in = loadDoc(path, &error);
    REQUIRE_MESSAGE(in.ok(), error);
    CHECK(in.string("S/Name") == value);
}

TEST_CASE("TomlDocument: integer parsing")
{
    TempDir tmp;
    const fs::path path = tmp.file("ints.toml");
    writeBytes(path, "[I]\n"
                     "Plain = 42\n"
                     "Spaced = \" 42 \"\n"
                     "Plus = +7\n"
                     "Minus = -3\n"
                     "Junk = 12abc\n"
                     "Fraction = 1.5\n"
                     "Overflow = 99999999999\n"
                     "Empty = \"\"\n");
    const TomlDocument doc = loadDoc(path);

    bool parsed = false;
    CHECK(doc.integer("I/Plain", -1, &parsed) == 42);
    CHECK(parsed);
    // QString::toInt ignores surrounding whitespace and accepts a leading '+'.
    CHECK(doc.integer("I/Spaced", -1, &parsed) == 42);
    CHECK(parsed);
    CHECK(doc.integer("I/Plus", -1, &parsed) == 7);
    CHECK(parsed);
    CHECK(doc.integer("I/Minus", -1, &parsed) == -3);
    CHECK(parsed);
    // Present but unparseable returns the caller's fallback (not 0), and says so.
    CHECK(doc.integer("I/Junk", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/Fraction", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/Overflow", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/Empty", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/Missing", -9, &parsed) == -9);
    CHECK(!parsed);
}

TEST_CASE("TomlDocument: number parsing")
{
    TempDir tmp;
    const fs::path path = tmp.file("doubles.toml");
    writeBytes(path, "[D]\n"
                     "Plain = \"1.5\"\n"
                     "Plus = \"+2.5\"\n"
                     "Spaced = \" 3e2 \"\n"
                     "Int = 4\n"
                     "Junk = \"1.5x\"\n"
                     "Inf = \"inf\"\n");
    const TomlDocument doc = loadDoc(path);

    CHECK(doc.number("D/Plain", -1.0) == 1.5);
    CHECK(doc.number("D/Plus", -1.0) == 2.5);
    CHECK(doc.number("D/Spaced", -1.0) == 300.0);
    CHECK(doc.number("D/Int", -1.0) == 4.0);
    CHECK(doc.number("D/Junk", -1.0) == -1.0);
    CHECK(std::isinf(doc.number("D/Inf", -1.0)));
    CHECK(doc.number("D/Missing", -2.0) == -2.0);
}

TEST_CASE("TomlDocument: the boolean rule")
{
    // QVariant::toBool()'s rule, reproduced on purpose: false only for empty, "0" or
    // "false" (any case, surrounding space ignored) - anything else, even "no", is true.
    TempDir tmp;
    const fs::path path = tmp.file("bools.toml");
    writeBytes(path, "[B]\n"
                     "F1 = false\n"
                     "F2 = FALSE\n"
                     "F3 = \" False \"\n"
                     "F4 = 0\n"
                     "F5 = \"\"\n"
                     "T1 = true\n"
                     "T2 = 1\n"
                     "T3 = yes\n"
                     "T4 = no\n");
    const TomlDocument doc = loadDoc(path);

    for (const char* key : {"B/F1", "B/F2", "B/F3", "B/F4", "B/F5"})
    {
        INFO(key);
        CHECK(!doc.boolean(key, true));
    }
    for (const char* key : {"B/T1", "B/T2", "B/T3", "B/T4"})
    {
        INFO(key);
        CHECK(doc.boolean(key, false));
    }
    CHECK(doc.boolean("B/Missing", true));
    CHECK(!doc.boolean("B/Missing", false));
}

TEST_CASE("TomlDocument writes the exact file format")
{
    // The bytes on disk, not just a round trip: top-level keys first and unheaded, one
    // blank line between sections, booleans and integers bare, everything else quoted
    // and escaped, CRLF line endings, and no BOM.
    TomlDocument doc;
    doc.setValue("Top", 1);
    doc.setValue("Frame/FrameSync", std::string("FE6B2840"));
    doc.setValue("Frame/BitsPerFrame", 1024);
    doc.setValue("Frame/Inverted", true);
    doc.setValue("Frame/Note", std::string("a \"b\" \\ c"));
    doc.setValue("Word2/Word", 2);
    doc.setValue("Frame/BitsPerFrame", 2048); // a re-set key keeps its place

    TempDir tmp;
    const fs::path path = tmp.file("out.toml");
    REQUIRE(doc.save(path));

    const std::string expected =
        "Top = 1\r\n"
        "\r\n"
        "[Frame]\r\n"
        "FrameSync = \"FE6B2840\"\r\n"
        "BitsPerFrame = 2048\r\n"
        "Inverted = true\r\n"
        "Note = \"a \\\"b\\\" \\\\ c\"\r\n"
        "\r\n"
        "[Word2]\r\n"
        "Word = 2\r\n";
    CHECK(readBytes(path) == expected);

    const TomlDocument back = loadDoc(path);
    CHECK(back.string("Frame/Note") == "a \"b\" \\ c");
    CHECK(back.integer("Frame/BitsPerFrame") == 2048);
    CHECK(back.boolean("Frame/Inverted"));
}

TEST_CASE("TomlDocument: doubles are written as six significant digits")
{
    // QString::number(double) is %g with six significant digits. A whole value prints
    // with no decimal point and is then written bare like an integer; anything else is
    // a quoted string. Existing scale files carry exactly these spellings.
    TomlDocument doc;
    doc.setValue("D/Long", 0.123456789);
    doc.setValue("D/Small", 0.00001);
    doc.setValue("D/Big", 1e21);
    doc.setValue("D/Whole", 3.0);
    doc.setValue("D/Neg", -12.5);

    TempDir tmp;
    const fs::path path = tmp.file("doubles_out.toml");
    REQUIRE(doc.save(path));
    const std::string bytes = readBytes(path);

    INFO(bytes);
    CHECK(contains(bytes, "Long = \"0.123457\"\r\n"));
    CHECK(contains(bytes, "Small = \"1e-05\"\r\n"));
    CHECK(contains(bytes, "Big = \"1e+21\"\r\n"));
    CHECK(contains(bytes, "Whole = 3\r\n"));
    CHECK(contains(bytes, "Neg = \"-12.5\"\r\n"));
}

TEST_CASE("TomlDocument: save truncates an existing file")
{
    // The QSettings version merged into an existing file, so an old word map survived
    // beside a new one unless the caller deleted the file first.
    TempDir tmp;
    const fs::path path = tmp.file("truncate.toml");
    writeBytes(path, "[Old]\nWord = 9\n[Older]\nWord = 8\n");

    TomlDocument doc;
    doc.setValue("New/Word", 1);
    REQUIRE(doc.save(path));

    const TomlDocument back = loadDoc(path);
    CHECK(back.sectionsInFileOrder() == Strings{"New"});
    CHECK(!back.contains("Old/Word"));
}

TEST_CASE("TomlDocument: a missing file and an unwritable path report errors")
{
    TempDir tmp;
    std::string error;
    const TomlDocument missing = loadDoc(tmp.file("absent.toml"), &error);
    CHECK(!missing.ok());
    CHECK_MESSAGE(contains(error, "absent.toml"), error);

    TomlDocument doc;
    doc.setValue("A/B", 1);
    error.clear();
    CHECK(!doc.save(tmp.file("no/such/dir/out.toml"), &error));
    CHECK_MESSAGE(contains(error, "out.toml"), error);
}

// --- cases added beyond the Qt suite: each closes a gap its mutation check found (plan step 2.4)

TEST_CASE("TomlDocument: quotes come off a value only when they surround it")
{
    TempDir tmp;
    const fs::path path = tmp.file("quotes2.toml");
    writeBytes(path, "[Q]\n"
                     "Lone = \"\n"
                     "OpenOnly = \"abc\n"
                     "CloseOnly = abc\"\n"
                     "Empty = \"\"\n"
                     "Windows = \"C:\\temp\\new\"\n"      // a backslash before anything but a quote or a backslash stays
                     "Trailing = \"abc\\\"\n");           // a backslash right before the closing quote has nothing to escape
    const TomlDocument doc = loadDoc(path);

    CHECK(doc.string("Q/Lone", "unset") == "\"");
    CHECK(doc.string("Q/OpenOnly") == "\"abc");
    CHECK(doc.string("Q/CloseOnly") == "abc\"");
    CHECK(doc.string("Q/Empty", "unset").empty());
    CHECK(doc.string("Q/Windows") == "C:\\temp\\new");
    CHECK(doc.string("Q/Trailing") == "abc\\");        // the backslash stays: it has no quote or backslash after it
}

TEST_CASE("TomlDocument: a commented-out key is not read, and a bracketed value is not a header")
{
    TempDir tmp;
    const fs::path path = tmp.file("brackets.toml");
    writeBytes(path, "[F]\n"
                     "# Hidden = 1\n"
                     "List = [1, 2]\n"                    // ends with a bracket, starts with a name: a key line
                     "[ Spaced ]\n"                       // the name is what is inside, trimmed
                     "K = 1\n"
                     "[]\n"                               // nothing between the brackets: no section
                     "Top = 2\n");
    const TomlDocument doc = loadDoc(path);

    CHECK(!doc.contains("F/# Hidden"));
    CHECK(!doc.contains("F/Hidden"));
    CHECK(doc.string("F/List") == "[1, 2]");
    CHECK(doc.integer("Spaced/K") == 1);
    CHECK(doc.sectionsInFileOrder() == Strings{"F", "Spaced"});
    CHECK(doc.integer("Top") == 2);                       // after "[]" the keys are top-level again
}

TEST_CASE("TomlDocument: a line that starts like a header but is not one is a key line outside an array table")
{
    TempDir tmp;
    const fs::path path = tmp.file("halfheader.toml");
    writeBytes(path, "[x] = 1\n");                        // read as the key "[x]"
    const TomlDocument doc = loadDoc(path);
    CHECK(doc.contains("[x]"));
    CHECK(doc.integer("[x]") == 1);
}

TEST_CASE("TomlDocument: tabs are whitespace")
{
    TempDir tmp;
    const fs::path path = tmp.file("tabs.toml");
    writeBytes(path, "\t[\tS\t]\t\n"
                     "\tK\t=\t5\t\n"
                     "\tN\t=\t\"v\"\t#\tnote\n");
    const TomlDocument doc = loadDoc(path);
    CHECK(doc.sectionsInFileOrder() == Strings{"S"});
    CHECK(doc.integer("S/K") == 5);
    CHECK(doc.string("S/N") == "v");
}

TEST_CASE("TomlDocument: a missing key gives the fallback, whatever its kind")
{
    const TomlDocument doc;
    CHECK(doc.string("A/B", "fallback") == "fallback");
    CHECK(doc.string("A/B").empty());
    CHECK(doc.integer("A/B", 7) == 7);
    CHECK(doc.number("A/B", 1.25) == 1.25);
    CHECK(doc.boolean("A/B", true));
    CHECK(!doc.contains("A/B"));
    CHECK(doc.ok());
}

TEST_CASE("TomlDocument: integers that are not integers")
{
    TempDir tmp;
    const fs::path path = tmp.file("badints.toml");
    writeBytes(path, "[I]\n"
                     "PlusMinus = +-3\n"
                     "PlusPlus = ++3\n"
                     "PlusOnly = +\n"
                     "Hex = 0x10\n"
                     "Tabbed = \"\t42\t\"\n");
    const TomlDocument doc = loadDoc(path);
    bool parsed = true;
    CHECK(doc.integer("I/PlusMinus", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/PlusPlus", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/PlusOnly", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/Hex", -1, &parsed) == -1);
    CHECK(!parsed);
    CHECK(doc.integer("I/Tabbed", -1, &parsed) == 42);
    CHECK(parsed);
}

TEST_CASE("TomlDocument: the words a number may be")
{
    TempDir tmp;
    const fs::path path = tmp.file("words.toml");
    writeBytes(path, "[D]\n"
                     "Nan = \"nan\"\n"
                     "NanUpper = \"NaN\"\n"
                     "NegInf = \"-inf\"\n"
                     "PosInf = \"+inf\"\n"
                     "Inf = \"INF\"\n"
                     "Infinity = \"infinity\"\n"          // not a number to Qt, though to the standard library
                     "None = \"none\"\n"
                     "NanWithText = \"nan(1)\"\n"
                     "Exp = \"1E-1\"\n"
                     "Tabbed = \"\t2.5\t\"\n");
    const TomlDocument doc = loadDoc(path);

    CHECK(std::isnan(doc.number("D/Nan", -1.0)));
    CHECK(std::isnan(doc.number("D/NanUpper", -1.0)));
    CHECK(doc.number("D/NegInf", 0.0) == -std::numeric_limits<double>::infinity());
    CHECK(doc.number("D/PosInf", 0.0) == std::numeric_limits<double>::infinity());
    CHECK(doc.number("D/Inf", 0.0) == std::numeric_limits<double>::infinity());
    CHECK(doc.number("D/Infinity", -1.0) == -1.0);
    CHECK(doc.number("D/None", -1.0) == -1.0);
    CHECK(doc.number("D/NanWithText", -1.0) == -1.0);
    CHECK(doc.number("D/Exp", -1.0) == 0.1);
    CHECK(doc.number("D/Tabbed", -1.0) == 2.5);
}

TEST_CASE("TomlDocument: a key path splits at its first slash")
{
    TomlDocument doc;
    doc.setValue("Plain", 1);
    doc.setValue("A/B/C", 2);

    TempDir tmp;
    const fs::path path = tmp.file("slashes.toml");
    REQUIRE(doc.save(path));
    CHECK(readBytes(path) == "Plain = 1\r\n\r\n[A]\r\nB/C = 2\r\n");
}

TEST_CASE("TomlDocument: booleans are written bare, true or false")
{
    TomlDocument doc;
    doc.setValue("S/On", true);
    doc.setValue("S/Off", false);
    doc.setValue("S/Text", std::string("false "));     // text that is only almost a boolean is quoted

    TempDir tmp;
    const fs::path path = tmp.file("bools_out.toml");
    REQUIRE(doc.save(path));
    CHECK(readBytes(path) == "[S]\r\nOn = true\r\nOff = false\r\nText = \"false \"\r\n");
}

TEST_CASE("TomlDocument: a file with only sections starts at the first header")
{
    TomlDocument doc;
    doc.setValue("A/K", 1);
    doc.setValue("B/L", 2);

    TempDir tmp;
    const fs::path path = tmp.file("sections_only.toml");
    REQUIRE(doc.save(path));
    CHECK(readBytes(path) == "[A]\r\nK = 1\r\n\r\n[B]\r\nL = 2\r\n");
}

TEST_CASE("TomlDocument: a loaded file saves back in its own order, empty sections included")
{
    TempDir tmp;
    const fs::path in = tmp.file("in.toml");
    writeBytes(in, "Z = 1\n"
                   "A = \"two\"\n"
                   "[Second]\n"
                   "K = 3\n"
                   "[EMPTY]\n"
                   "[First]\n"
                   "L = \"x y\"\n"
                   "M = true\n");
    const TomlDocument doc = loadDoc(in);

    const fs::path out = tmp.file("out.toml");
    REQUIRE(doc.save(out));
    CHECK(readBytes(out) == "Z = 1\r\n"
                            "A = \"two\"\r\n"
                            "\r\n"
                            "[Second]\r\n"
                            "K = 3\r\n"
                            "\r\n"
                            "[EMPTY]\r\n"
                            "\r\n"
                            "[First]\r\n"
                            "L = \"x y\"\r\n"
                            "M = true\r\n");
}

TEST_CASE("TomlDocument: the messages name the file and say what failed")
{
    TempDir tmp;
    std::string error;
    const TomlDocument missing = loadDoc(tmp.path() / "deep" / "absent.toml", &error);
    CHECK(!missing.ok());
    CHECK(error == "Could not open 'absent.toml'.");        // the name, not the whole path

    TomlDocument doc;
    doc.setValue("A/B", 1);
    error.clear();
    CHECK(!doc.save(tmp.path() / "no" / "such" / "dir" / "out.toml", &error));
    CHECK(error == "Could not write 'out.toml'.");

    // And with no place to put a message, nothing goes wrong.
    CHECK(!loadDoc(tmp.file("absent.toml")).ok());
    CHECK(!doc.save(tmp.path() / "no" / "such" / "dir" / "out.toml"));
}
