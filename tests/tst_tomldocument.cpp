#include "tst_tomldocument.h"

#include <cmath>
#include <string>
#include <vector>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "qtbridge.h"
#include "tomldocument.h"

namespace {

// The four helpers below are the only place this suite touches TomlDocument's string
// and path types. That is deliberate: the suite was written against the QString
// implementation and then kept, assertion for assertion, across the port to std::string,
// so every expectation below was first proven against the code it replaced.

TomlDocument loadDoc(const QString& path, QString* error = nullptr)
{
    std::string err;
    TomlDocument doc = TomlDocument::load(toFsPath(path), error ? &err : nullptr);
    if (error)
    {
        *error = QString::fromStdString(err);
    }
    return doc;
}

bool saveDoc(const TomlDocument& doc, const QString& path, QString* error = nullptr)
{
    std::string err;
    const bool ok = doc.save(toFsPath(path), error ? &err : nullptr);
    if (error)
    {
        *error = QString::fromStdString(err);
    }
    return ok;
}

QString q(const std::string& value)
{
    return QString::fromStdString(value);
}

QStringList q(const std::vector<std::string>& values)
{
    QStringList out;
    for (const std::string& v : values)
    {
        out << QString::fromStdString(v);
    }
    return out;
}

/// A value going IN to setValue(), in whatever string type TomlDocument takes.
std::string v(const QString& value)
{
    return value.toStdString();
}

/// Writes raw bytes, so a test controls the encoding, BOM and line endings exactly.
void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(bytes), bytes.size());
}

QByteArray readBytes(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return f.readAll();
}

} // namespace

void TestTomlDocument::readsSectionsAndKeysInFileOrder()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("order.toml");
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
    QVERIFY(doc.ok());
    // Top-level keys are not a section; an empty section is still declared; a
    // repeated section is listed once.
    QCOMPARE(q(doc.sectionsInFileOrder()),
             QStringList({"Zeta", "EMPTY", "Alpha"}));
    QCOMPARE(doc.integer("Top"), 1);
    QCOMPARE(doc.integer("Zeta/Word"), 3);
    QCOMPARE(doc.integer("Alpha/Word"), 1);
    QVERIFY(!doc.contains("EMPTY/Word"));
}

// `[[Step]]` used to read as a section literally named "[Step]": the class did not implement
// arrays of tables, and that was pinned so nobody would rely on it. It does now (step files are read
// through it), so the same file reads as an array of tables and has no sections at all.
void TestTomlDocument::doubleBracketHeaderOpensAnArrayTableNotASection()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("steps.toml");
    writeBytes(path, "[[Step]]\ndb = 6\n");

    const TomlDocument doc = loadDoc(path);
    QVERIFY(doc.sectionsInFileOrder().empty());
    QVERIFY(!doc.contains("[Step]/db"));
    QVERIFY(!doc.contains("Step/db"));
    QCOMPARE(doc.arrayTables().size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(doc.arrayTables()[0].name), QStringLiteral("Step"));
    QCOMPARE(doc.arrayTables()[0].entries.size(), std::size_t{1});
}

void TestTomlDocument::arrayTablesKeepFileOrderKeysAndValuesAsWritten()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("tables.toml");
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
    QCOMPARE(items.size(), std::size_t{2});

    // Keys in file order, repeats included, trimmed; values as written - quotes kept, an unquoted
    // trailing comment and the whitespace around the value removed, a '#' inside quotes left alone.
    const auto& first = items[0]->entries;
    QCOMPARE(first.size(), std::size_t{4});
    QCOMPARE(QString::fromStdString(first[0].first), QStringLiteral("Name"));
    QCOMPARE(QString::fromStdString(first[0].second), QStringLiteral("\"first one\""));
    QCOMPARE(QString::fromStdString(first[1].first), QStringLiteral("Count"));
    QCOMPARE(QString::fromStdString(first[1].second), QStringLiteral("3"));
    QCOMPARE(QString::fromStdString(first[2].second), QStringLiteral("4"));
    QCOMPARE(QString::fromStdString(first[3].second), QStringLiteral("\"a # b\""));

    QCOMPARE(items[1]->entries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(items[1]->entries[0].second), QStringLiteral("second"));
}

void TestTomlDocument::arrayTableNamesMatchWithoutCaseButOtherwiseExactly()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("names.toml");
    writeBytes(path, "[[Step]]\nk = 1\n"
                     "[[STEP]]\nk = 2\n"
                     "[[step]]\nk = 3\n"
                     "[[ Step ]]\nk = 4\n"      // inner spaces are part of the name
                     "[[Steps]]\nk = 5\n"       // so is a longer name
                     "[[Other]]\nk = 6\n");

    const TomlDocument doc = loadDoc(path);
    QCOMPARE(doc.arrayTables().size(), std::size_t{6});

    QStringList got;
    for (const TomlDocument::Table* t : doc.tables("StEp"))
        got << QString::fromStdString(t->entries.at(0).second);
    QCOMPARE(got, QStringList({"1", "2", "3"}));
    QCOMPARE(doc.tables("steps").size(), std::size_t{1});
    QVERIFY(doc.tables("missing").empty());
}

// A `[[` line that is not just `[[Name]]` still opens an entry, so what follows is not mistaken for
// the entry before it - but no name matches it. This is the rule the step-file parser always had.
void TestTomlDocument::aMalformedArrayHeaderOpensAnEntryNoNameMatches()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("malformed.toml");
    writeBytes(path, "[[Step]] # the first one\nk = 1\n"
                     "[[Step\nk = 2\n"
                     "[[\nk = 3\n"
                     "[[Step]]\nk = 4\n");

    const TomlDocument doc = loadDoc(path);
    QCOMPARE(doc.arrayTables().size(), std::size_t{4});
    QCOMPARE(QString::fromStdString(doc.arrayTables()[0].name), QString());
    QCOMPARE(QString::fromStdString(doc.arrayTables()[1].name), QString());
    QCOMPARE(QString::fromStdString(doc.arrayTables()[2].name), QString());

    const auto steps = doc.tables("step");
    QCOMPARE(steps.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(steps[0]->entries.at(0).second), QStringLiteral("4"));
}

void TestTomlDocument::aSectionHeaderEndsAnEntryAndSectionsAreUnaffectedByEntries()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("mixed.toml");
    writeBytes(path, "Top = 1\n"
                     "[Before]\nA = 10\n"
                     "[[Item]]\nin_item = 20\n"
                     "[After]\nB = 30\n"                 // a section header ends the entry
                     "[[Item]]\nin_second = 40\n"
                     "[Meta] # not a well-formed header\n"
                     "stray = 50\n");                    // still ends the entry: not the second item's

    const TomlDocument doc = loadDoc(path);

    // The sections read as they always did, and no key from an entry leaks into one.
    QCOMPARE(q(doc.sectionsInFileOrder()), QStringList({"Before", "After"}));
    QCOMPARE(doc.integer("Top"), 1);
    QCOMPARE(doc.integer("Before/A"), 10);
    QCOMPARE(doc.integer("After/B"), 30);
    QVERIFY(!doc.contains("in_item"));
    QVERIFY(!doc.contains("Item/in_item"));
    QVERIFY(!doc.contains("After/in_second"));

    // Each entry holds only what was under its own header.
    const auto items = doc.tables("item");
    QCOMPARE(items.size(), std::size_t{2});
    QCOMPARE(items[0]->entries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(items[0]->entries[0].first), QStringLiteral("in_item"));
    QCOMPARE(items[1]->entries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(items[1]->entries[0].first), QStringLiteral("in_second"));
}

void TestTomlDocument::commentsAreQuoteAwareAndEscapesUnescape()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("quotes.toml");
    writeBytes(path, "# a full-line comment\n"
                     "[F]\n"
                     "Hash = \"a # b\"   # trailing comment\n"
                     "Bare = 5 # trailing comment\n"
                     "Esc = \"say \\\"hi\\\" \\\\ done\"\n"
                     "Spaced   =   \"  kept  \"  \n"
                     "NoEquals\n");

    const TomlDocument doc = loadDoc(path);
    QCOMPARE(q(doc.string("F/Hash")), QStringLiteral("a # b"));
    QCOMPARE(q(doc.string("F/Bare")), QStringLiteral("5"));
    QCOMPARE(q(doc.string("F/Esc")), QStringLiteral("say \"hi\" \\ done"));
    // Inside the quotes is the value, untrimmed.
    QCOMPARE(q(doc.string("F/Spaced")), QStringLiteral("  kept  "));
    // A line with no '=' is skipped rather than failing the file.
    QVERIFY(!doc.contains("F/NoEquals"));
    QVERIFY(doc.ok());
}

void TestTomlDocument::repeatedKeyOverwrites()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("repeat.toml");
    writeBytes(path, "[A]\nK = 1\nK = 2\n");
    QCOMPARE(loadDoc(path).integer("A/K"), 2);
}

void TestTomlDocument::readsUtf8BomAndCrlf()
{
    // A file saved by Notepad: a UTF-8 BOM and CRLF line endings. Without BOM handling
    // the first key would be read as "\xEF\xBB\xBFTop" and silently go missing.
    QTemporaryDir tmp;
    const QString path = tmp.filePath("bom.toml");
    writeBytes(path, QByteArray("\xEF\xBB\xBF") + "Top = 7\r\n[S]\r\nK = \"v\"\r\n");

    const TomlDocument doc = loadDoc(path);
    QCOMPARE(doc.integer("Top", -1), 7);
    QCOMPARE(q(doc.string("S/K")), QStringLiteral("v"));
    QCOMPARE(q(doc.sectionsInFileOrder()), QStringList({"S"}));
}

void TestTomlDocument::readsUtf16WithBom_data()
{
    QTest::addColumn<bool>("bigEndian");
    QTest::newRow("UTF-16LE") << false;
    QTest::newRow("UTF-16BE") << true;
}

void TestTomlDocument::readsUtf16WithBom()
{
    // Older Notepad's "Unicode" encoding. Qt's text stream detected the BOM and decoded
    // it, so a settings file saved that way worked, and the port must not stop it.
    QFETCH(bool, bigEndian);
    const QString text = QStringLiteral("[S]\r\nName = \"caf") + QChar(0x00E9)
                         + QStringLiteral(" ") + QChar(0x03A9) + QStringLiteral("\"\r\nN = 12\r\n");
    QByteArray bytes = bigEndian ? QByteArray("\xFE\xFF", 2) : QByteArray("\xFF\xFE", 2);
    for (const QChar c : text)
    {
        const char hi = static_cast<char>(c.unicode() >> 8);
        const char lo = static_cast<char>(c.unicode() & 0xFF);
        bytes.append(bigEndian ? hi : lo);
        bytes.append(bigEndian ? lo : hi);
    }

    QTemporaryDir tmp;
    const QString path = tmp.filePath("utf16.toml");
    writeBytes(path, bytes);

    const TomlDocument doc = loadDoc(path);
    QCOMPARE(doc.integer("S/N", -1), 12);
    QCOMPARE(q(doc.string("S/Name")),
             QStringLiteral("caf") + QChar(0x00E9) + QStringLiteral(" ") + QChar(0x03A9));
}

void TestTomlDocument::nonAsciiValuesAndPathsRoundTrip()
{
    // A value outside ASCII, saved to and loaded from a folder outside every Western
    // code page: the value must come back intact and the file must be UTF-8.
    QTemporaryDir tmp;
    const QString folder = QStringLiteral("omega-") + QChar(0x03A9);
    QVERIFY(QDir(tmp.path()).mkdir(folder));
    const QString path = QDir(tmp.path()).filePath(folder + "/values.toml");
    const QString value = QStringLiteral("caf") + QChar(0x00E9) + QStringLiteral(" ") + QChar(0x03A9);

    TomlDocument out;
    out.setValue("S/Name", v(value));
    QString error;
    QVERIFY2(saveDoc(out, path, &error), qPrintable(error));

    QVERIFY(readBytes(path).contains(value.toUtf8()));
    const TomlDocument in = loadDoc(path, &error);
    QVERIFY2(in.ok(), qPrintable(error));
    QCOMPARE(q(in.string("S/Name")), value);
}

void TestTomlDocument::integerParsing()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("ints.toml");
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
    QCOMPARE(doc.integer("I/Plain", -1, &parsed), 42);
    QVERIFY(parsed);
    // QString::toInt ignores surrounding whitespace and accepts a leading '+'.
    QCOMPARE(doc.integer("I/Spaced", -1, &parsed), 42);
    QVERIFY(parsed);
    QCOMPARE(doc.integer("I/Plus", -1, &parsed), 7);
    QVERIFY(parsed);
    QCOMPARE(doc.integer("I/Minus", -1, &parsed), -3);
    QVERIFY(parsed);
    // Present but unparseable returns the caller's fallback (not 0), and says so.
    QCOMPARE(doc.integer("I/Junk", -1, &parsed), -1);
    QVERIFY(!parsed);
    QCOMPARE(doc.integer("I/Fraction", -1, &parsed), -1);
    QVERIFY(!parsed);
    QCOMPARE(doc.integer("I/Overflow", -1, &parsed), -1);
    QVERIFY(!parsed);
    QCOMPARE(doc.integer("I/Empty", -1, &parsed), -1);
    QVERIFY(!parsed);
    QCOMPARE(doc.integer("I/Missing", -9, &parsed), -9);
    QVERIFY(!parsed);
}

void TestTomlDocument::numberParsing()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("doubles.toml");
    writeBytes(path, "[D]\n"
                     "Plain = \"1.5\"\n"
                     "Plus = \"+2.5\"\n"
                     "Spaced = \" 3e2 \"\n"
                     "Int = 4\n"
                     "Junk = \"1.5x\"\n"
                     "Inf = \"inf\"\n");
    const TomlDocument doc = loadDoc(path);

    QCOMPARE(doc.number("D/Plain", -1.0), 1.5);
    QCOMPARE(doc.number("D/Plus", -1.0), 2.5);
    QCOMPARE(doc.number("D/Spaced", -1.0), 300.0);
    QCOMPARE(doc.number("D/Int", -1.0), 4.0);
    QCOMPARE(doc.number("D/Junk", -1.0), -1.0);
    QVERIFY(std::isinf(doc.number("D/Inf", -1.0)));
    QCOMPARE(doc.number("D/Missing", -2.0), -2.0);
}

void TestTomlDocument::booleanRule()
{
    // QVariant::toBool()'s rule, reproduced on purpose: false only for empty, "0" or
    // "false" (any case, surrounding space ignored) - anything else, even "no", is true.
    QTemporaryDir tmp;
    const QString path = tmp.filePath("bools.toml");
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
        QVERIFY2(!doc.boolean(key, true), key);
    }
    for (const char* key : {"B/T1", "B/T2", "B/T3", "B/T4"})
    {
        QVERIFY2(doc.boolean(key, false), key);
    }
    QVERIFY(doc.boolean("B/Missing", true));
    QVERIFY(!doc.boolean("B/Missing", false));
}

void TestTomlDocument::writesTheExactFileFormat()
{
    // The bytes on disk, not just a round trip: top-level keys first and unheaded, one
    // blank line between sections, booleans and integers bare, everything else quoted
    // and escaped, CRLF line endings, and no BOM.
    TomlDocument doc;
    doc.setValue("Top", 1);
    doc.setValue("Frame/FrameSync", v(QStringLiteral("FE6B2840")));
    doc.setValue("Frame/BitsPerFrame", 1024);
    doc.setValue("Frame/Inverted", true);
    doc.setValue("Frame/Note", v(QStringLiteral("a \"b\" \\ c")));
    doc.setValue("Word2/Word", 2);
    doc.setValue("Frame/BitsPerFrame", 2048); // a re-set key keeps its place

    QTemporaryDir tmp;
    const QString path = tmp.filePath("out.toml");
    QVERIFY(saveDoc(doc, path));

    const QByteArray expected =
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
    QCOMPARE(readBytes(path), expected);

    const TomlDocument back = loadDoc(path);
    QCOMPARE(q(back.string("Frame/Note")), QStringLiteral("a \"b\" \\ c"));
    QCOMPARE(back.integer("Frame/BitsPerFrame"), 2048);
    QVERIFY(back.boolean("Frame/Inverted"));
}

void TestTomlDocument::doublesAreWrittenAsSixSignificantDigits()
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

    QTemporaryDir tmp;
    const QString path = tmp.filePath("doubles_out.toml");
    QVERIFY(saveDoc(doc, path));
    const QByteArray bytes = readBytes(path);

    QVERIFY2(bytes.contains("Long = \"0.123457\"\r\n"), bytes.constData());
    QVERIFY2(bytes.contains("Small = \"1e-05\"\r\n"), bytes.constData());
    QVERIFY2(bytes.contains("Big = \"1e+21\"\r\n"), bytes.constData());
    QVERIFY2(bytes.contains("Whole = 3\r\n"), bytes.constData());
    QVERIFY2(bytes.contains("Neg = \"-12.5\"\r\n"), bytes.constData());
}

void TestTomlDocument::saveTruncatesAnExistingFile()
{
    // The QSettings version merged into an existing file, so an old word map survived
    // beside a new one unless the caller deleted the file first.
    QTemporaryDir tmp;
    const QString path = tmp.filePath("truncate.toml");
    writeBytes(path, "[Old]\nWord = 9\n[Older]\nWord = 8\n");

    TomlDocument doc;
    doc.setValue("New/Word", 1);
    QVERIFY(saveDoc(doc, path));

    const TomlDocument back = loadDoc(path);
    QCOMPARE(q(back.sectionsInFileOrder()), QStringList({"New"}));
    QVERIFY(!back.contains("Old/Word"));
}

void TestTomlDocument::missingFileAndUnwritablePathReportErrors()
{
    QTemporaryDir tmp;
    QString error;
    const TomlDocument missing = loadDoc(tmp.filePath("absent.toml"), &error);
    QVERIFY(!missing.ok());
    QVERIFY2(error.contains(QStringLiteral("absent.toml")), qPrintable(error));

    TomlDocument doc;
    doc.setValue("A/B", 1);
    error.clear();
    QVERIFY(!saveDoc(doc, tmp.filePath("no/such/dir/out.toml"), &error));
    QVERIFY2(error.contains(QStringLiteral("out.toml")), qPrintable(error));
}
