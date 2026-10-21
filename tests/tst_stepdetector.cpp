#include "tst_stepdetector.h"

#include <bit>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

#include "containerutil.h"
#include "qtbridge.h"
#include "stepdetector.h"
#include "textfile.h"
#include "textparse.h"

namespace {

/// Appends @p count samples of @p level to @p series.
void appendRun(std::vector<double>& series, double level, int count)
{
    for (int i = 0; i < count; i++)
    {
        series.push_back(level);
    }
}

/// Writes @p content to a temporary .toml file and returns its path. The file
/// is owned by @p file (kept alive by the caller).
QString writeTempToml(QTemporaryFile& file, const QString& content)
{
    file.setFileTemplate(QDir::tempPath() + "/stepcfg_XXXXXX.toml");
    if (!file.open())
        qWarning("writeTempToml: could not open temporary TOML file");
    file.write(content.toUtf8());
    file.flush();
    return file.fileName();
}

const double kPeriod = 0.01; // 10 ms -> a 1.0s confirmation window is 100 samples.

// ---------------------------------------------------------------------------
// The original hand-written step-file parser, kept as an oracle.
//
// StepDetector::parseStepConfig was a second TOML parser of its own; it now reads through
// TomlDocument. This is the code it had before, copied here unchanged, so the two can be run side
// by side over files nobody wrote by hand. It is a test fixture: nothing else calls it.
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

bool referenceParseStepConfig(const std::filesystem::path& path, std::vector<StepDefinition>& out,
                              std::string& error)
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

void TestStepDetector::parseStepConfigValid()
{
    QTemporaryFile f;
    const QString path = writeTempToml(f,
        "[[Step]]\n"
        "db = 0.0\n"
        "\n"
        "[[Step]]\n"
        "db = 6.0\n");

    std::vector<StepDefinition> steps;
    std::string error;
    QVERIFY(StepDetector::parseStepConfig(toFsPath(path), steps, error));
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps[0].db, 0.0);
    QCOMPARE(steps[1].db, 6.0);
}

namespace {

// Everything below talks to StepDetector::parseStepConfig through these helpers, so the
// QString-to-std::string port changes them and nothing else: the assertions were
// written against the QString implementation and kept as they were.
bool parseSteps(const QString& path, std::vector<StepDefinition>& steps, QString& error)
{
    std::string err;
    const bool ok = StepDetector::parseStepConfig(toFsPath(path), steps, err);
    error = QString::fromStdString(err);
    return ok;
}

/// initialStepConfigPath() takes and returns UTF-8 strings; the assertions compare QStrings.
QString initialPath(const QString& root, const QString& preferred)
{
    return QString::fromStdString(
        StepDetector::initialStepConfigPath(root.toStdString(), preferred.toStdString()));
}

/// Writes @p content to a file under @p dir and returns its path (empty on failure).
QString writeStepFile(const QTemporaryDir& dir, const QString& name, const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return {};
    }
    f.write(content);
    return path;
}

/// The db values of a parsed list, for compact comparison.
QList<double> dbs(const std::vector<StepDefinition>& steps)
{
    QList<double> out;
    for (const StepDefinition& s : steps)
    {
        out << s.db;
    }
    return out;
}

} // namespace

void TestStepDetector::parseStepConfigMatchesAreCaseInsensitive()
{
    // The header and the key are matched without regard to case, and the whitespace
    // around '=' and at the ends of the line is ignored.
    QTemporaryDir dir;
    const QString path = writeStepFile(dir, "case.toml",
        "[[STEP]]\n"
        "  DB   =   3.5  \n"
        "[[step]]\n"
        "Db = 7\n");
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY2(parseSteps(path, steps, error), qPrintable(error));
    QCOMPARE(dbs(steps), QList<double>({3.5, 7.0}));
}

void TestStepDetector::parseStepConfigHeaderMustBeExactlyStepToCount()
{
    // The header comparison is against the whole trimmed line, so decoration on it
    // means it is NOT a step header: a trailing comment, inner spaces, a longer name.
    // Whatever follows then sits in no step and is ignored. Pinned as the current
    // behaviour - a header with a comment silently losing its step deserves its own
    // decision, not to be changed as a side effect of a port.
    QTemporaryDir dir;
    const QString path = writeStepFile(dir, "headers.toml",
        "[[Step]] # the first one\n"
        "db = 1\n"
        "[[ Step ]]\n"
        "db = 2\n"
        "[[Steps]]\n"
        "db = 3\n"
        "[[Step]]\n"
        "db = 4\n");
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY2(parseSteps(path, steps, error), qPrintable(error));
    QCOMPARE(dbs(steps), QList<double>({4.0}));
}

void TestStepDetector::parseStepConfigIgnoresKeysOutsideAStepAndOtherSections()
{
    QTemporaryDir dir;
    const QString path = writeStepFile(dir, "scope.toml",
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
        "db = 96\n");
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY2(parseSteps(path, steps, error), qPrintable(error));
    QCOMPARE(dbs(steps), QList<double>({5.0, 0.0}));
}

void TestStepDetector::parseStepConfigValueRules_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<double>("expected");

    // QString::toDouble's rules, on the text after '=' with a trailing '#' comment
    // removed. A value that does not parse is skipped, leaving the step at its default
    // of 0 - the step itself still exists.
    QTest::newRow("integer")       << "6"        << 6.0;
    QTest::newRow("negative")      << "-3.5"     << -3.5;
    QTest::newRow("plus")          << "+6"       << 6.0;
    QTest::newRow("exponent")      << "1e1"      << 10.0;
    QTest::newRow("leading-dot")   << ".5"       << 0.5;
    QTest::newRow("trailing-dot")  << "5."       << 5.0;
    QTest::newRow("comment")       << "6 # six"  << 6.0;
    QTest::newRow("comment-glued") << "6# six"   << 6.0;
    QTest::newRow("quoted")        << "\"6\""    << 0.0; // quotes are NOT stripped here
    QTest::newRow("words")         << "abc"      << 0.0;
    QTest::newRow("trailing-junk") << "6x"       << 0.0;
    QTest::newRow("comma")         << "1,5"      << 0.0;
    QTest::newRow("hex")           << "0x10"     << 0.0;
    QTest::newRow("overflow")      << "1e999"    << 0.0;
    QTest::newRow("double-sign")   << "+-6"      << 0.0;
    QTest::newRow("empty")         << ""         << 0.0;
}

void TestStepDetector::parseStepConfigValueRules()
{
    QFETCH(QString, text);
    QFETCH(double, expected);

    QTemporaryDir dir;
    const QString path = writeStepFile(dir, "value.toml",
        ("[[Step]]\ndb = " + text + "\n").toUtf8());
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY2(parseSteps(path, steps, error), qPrintable(error));
    QCOMPARE(steps.size(), std::size_t{1});
    QCOMPARE(steps[0].db, expected);
}

void TestStepDetector::parseStepConfigKeepsFileOrderAndLastDbWins()
{
    QTemporaryDir dir;
    const QString path = writeStepFile(dir, "order.toml",
        "[[Step]]\ndb = 30\n"
        "[[Step]]\ndb = 10\n"
        "[[Step]]\ndb = 20\ndb = 21\n");
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY2(parseSteps(path, steps, error), qPrintable(error));
    QCOMPARE(dbs(steps), QList<double>({30.0, 10.0, 21.0}));
}

void TestStepDetector::parseStepConfigClearsItsOutputFirst()
{
    // A failed parse must not leave the previous file's steps behind.
    QTemporaryDir dir;
    std::vector<StepDefinition> steps = {{1.0}, {2.0}};
    QString error;
    QVERIFY(!parseSteps(writeStepFile(dir, "none.toml", "# nothing\n"), steps, error));
    QVERIFY(steps.empty());

    steps = {{1.0}, {2.0}};
    QVERIFY(!parseSteps(dir.filePath("absent.toml"), steps, error));
    QVERIFY(steps.empty());
}

void TestStepDetector::parseStepConfigReadsBomCrlfAndUtf16_data()
{
    QTest::addColumn<QByteArray>("bytes");
    // Files as Notepad saves them. QTextStream detected all of these; a plain byte
    // reader would see the BOM as part of the first line and silently drop step 1.
    QTest::newRow("utf8-bom-crlf")
        << (QByteArray("\xEF\xBB\xBF") + "[[Step]]\r\ndb = 4\r\n[[Step]]\r\ndb = 8\r\n");
    auto utf16 = [](bool big) {
        const QString text = QStringLiteral("[[Step]]\r\ndb = 4\r\n[[Step]]\r\ndb = 8\r\n");
        QByteArray b = big ? QByteArray("\xFE\xFF", 2) : QByteArray("\xFF\xFE", 2);
        for (const QChar c : text)
        {
            const char hi = static_cast<char>(c.unicode() >> 8);
            const char lo = static_cast<char>(c.unicode() & 0xFF);
            b.append(big ? hi : lo);
            b.append(big ? lo : hi);
        }
        return b;
    };
    QTest::newRow("utf16le") << utf16(false);
    QTest::newRow("utf16be") << utf16(true);
}

void TestStepDetector::parseStepConfigReadsBomCrlfAndUtf16()
{
    QFETCH(QByteArray, bytes);
    QTemporaryDir dir;
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY2(parseSteps(writeStepFile(dir, "enc.toml", bytes), steps, error), qPrintable(error));
    QCOMPARE(dbs(steps), QList<double>({4.0, 8.0}));
}

void TestStepDetector::parseStepConfigOpensNonAsciiPaths()
{
    // Step files live wherever the operator keeps them, including user folders with
    // accented or non-Latin names.
    QTemporaryDir dir;
    const QString folder = QStringLiteral("omega-") + QChar(0x03A9);
    QVERIFY(QDir(dir.path()).mkdir(folder));
    const QString path = writeStepFile(dir, folder + "/caf" + QChar(0x00E9) + ".toml",
                                       "[[Step]]\ndb = 12\n");
    QVERIFY(!path.isEmpty());
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY2(parseSteps(path, steps, error), qPrintable(error));
    QCOMPARE(dbs(steps), QList<double>({12.0}));
}

void TestStepDetector::parseStepConfigErrorMessagesNameTheFile()
{
    // The message carries the file NAME (not the full path), so the log line reads the
    // same wherever the file lives.
    QTemporaryDir dir;
    std::vector<StepDefinition> steps;
    QString error;
    QVERIFY(!parseSteps(dir.filePath("missing_steps.toml"), steps, error));
    QCOMPARE(error, QStringLiteral("Could not open step-config file 'missing_steps.toml'."));

    QVERIFY(!parseSteps(writeStepFile(dir, "empty_steps.toml", "[Meta]\nx = 1\n"), steps, error));
    QCOMPARE(error, QStringLiteral("No [[Step]] entries found in 'empty_steps.toml'."));
}

// The hand-written parser and TomlDocument agree on files neither author imagined. Every line comes
// from a pool of the shapes a step file has, and the awkward ones near them - headers with
// comments, spaces, other names, a missing bracket; keys in every case, quoted, commented, empty,
// repeated, outside any step - shuffled into files of up to a dozen lines. Whatever the original
// made of a file, the parser must make the same: whether it succeeded, the steps and their dB to
// the bit, and the message when it did not.
void TestStepDetector::parseStepConfigAgreesWithTheOriginalHandWrittenParserOnGeneratedFiles()
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

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("generated.toml");

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
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(content.data(), static_cast<qint64>(content.size()));
        }

        std::vector<StepDefinition> want, got;
        std::string want_error, got_error;
        const bool want_ok = referenceParseStepConfig(toFsPath(path), want, want_error);
        const bool got_ok = StepDetector::parseStepConfig(toFsPath(path), got, got_error);

        const QString why = QStringLiteral("file #%1:\n%2").arg(n).arg(QString::fromStdString(content));
        QVERIFY2(want_ok == got_ok, qPrintable(why + " (success differs)"));
        QVERIFY2(want_error == got_error, qPrintable(why + " (message differs: '" +
                                                      QString::fromStdString(got_error) + "')"));
        QVERIFY2(want.size() == got.size(), qPrintable(why + QStringLiteral(" (%1 steps, wanted %2)")
                                                             .arg(got.size()).arg(want.size())));
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            QVERIFY2(std::bit_cast<std::uint64_t>(want[i].db) == std::bit_cast<std::uint64_t>(got[i].db),
                     qPrintable(why + QStringLiteral(" (step %1: %2, wanted %3)")
                                          .arg(i).arg(got[i].db).arg(want[i].db)));
        }
        (want_ok ? agreed_ok : agreed_fail)++;
    }
    // The generator must reach both outcomes in volume, or the comparison proves little.
    QVERIFY2(agreed_ok > 1000, qPrintable(QString::number(agreed_ok)));
    QVERIFY2(agreed_fail > 300, qPrintable(QString::number(agreed_fail)));
}

void TestStepDetector::parseStepConfigEmptyFails()
{
    QTemporaryFile f;
    const QString path = writeTempToml(f, "# just a comment, no steps\n");

    std::vector<StepDefinition> steps;
    std::string error;
    QVERIFY(!StepDetector::parseStepConfig(toFsPath(path), steps, error));
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

void TestStepDetector::detectOnATooShortSeriesFailsCleanly()
{
    // A series with fewer than two samples has no derivative and so no edge, and must
    // come back as "not calibrated". Sizing the derivative takes `size() - 1`: harmless
    // on a Qt container, but a huge unsigned number on a std::vector, where `reserve`
    // throws. detect() rejects the input first and sampleDerivative() guards its own
    // reserve; this pins the outcome whichever of the two a refactor weakens.
    const std::vector<StepDefinition> steps = {{0.0}, {6.0}};

    const StepDetector::Result none = StepDetector::detect({}, kPeriod, steps);
    QVERIFY(!none.profile.valid);
    QVERIFY(none.profile.points.empty());

    const StepDetector::Result one = StepDetector::detect({1000.0}, kPeriod, steps);
    QVERIFY(!one.profile.valid);
    QVERIFY(one.profile.points.empty());
}

void TestStepDetector::detectCleanSteps()
{
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 3);
    QCOMPARE(r.profile.points.size(), 3);
    // Sorted ascending by raw; dB carried from the paired steps.
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    QVERIFY(qFuzzyCompare(r.profile.points[2].rawAvg, 3000.0));
    QCOMPARE(r.profile.points[2].trueDb, 12.0);
}

void TestStepDetector::detectTooFewPlateausFails()
{
    // Three steps expected, but the data only contains two plateaus.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(!r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 2);
}

void TestStepDetector::detectExtraPlateausUsesLastOfMonotonicRun()
{
    // Two steps expected, three increasing plateaus present. Selection takes the
    // LAST `expected` plateaus of the first maximal monotonic run (here the run
    // is all three: 1000 < 2000 < 3000), so (2000, 3000) pair with the steps and
    // the lowest plateau (1000) is dropped as leading pre-roll / turn-on transient.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QVERIFY(r.extraPlateaus);
    QCOMPARE(r.profile.points.size(), 2);
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 2000.0));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 3000.0));
    QCOMPARE(r.profile.points[1].trueDb, 6.0);
}

void TestStepDetector::detectShortBlipDoesNotStealPairingSlot()
{
    // Reproduces a real failure seen on decoded hardware data: a brief startup
    // transient (55 samples = 0.55s, under the 1.0s confirmation window) ahead
    // of the genuine sweep. Without a confirmation requirement, this blip would
    // be counted as a real plateau and consume pairing slot 0, shifting every
    // subsequent plateau<->step pairing by one. It must be rejected so the
    // three genuine (confirmed) plateaus pair correctly with steps 0/6/12 dB.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 500.0, 55);   // spurious sub-confirmation blip
    appendRun(raw, 1000.0, 150); // genuine 0 dB plateau
    appendRun(raw, 2000.0, 150); // genuine 6 dB plateau
    appendRun(raw, 3000.0, 150); // genuine 12 dB plateau

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 3);
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 2000.0));
    QCOMPARE(r.profile.points[1].trueDb, 6.0);
    QVERIFY(qFuzzyCompare(r.profile.points[2].rawAvg, 3000.0));
    QCOMPARE(r.profile.points[2].trueDb, 12.0);
}

void TestStepDetector::detectLongLeadingTransientDoesNotShiftPairing()
{
    // Reproduces the RASA "time skew" field failure. The signal generator is
    // switched on a few seconds into the recording, producing a confirmed
    // leading plateau (here 1.5s, ABOVE the 1.0s confirmation window, so unlike
    // detectShortBlipDoesNotStealPairingSlot it is NOT rejected) below the real
    // 0 dB level. The injected sweep then climbs 0,3,6,12,18,24,30,36 and falls
    // back down (the operator's optional down-ramp).
    //
    // With a naive first-N-consecutive-increasing selection the leading transient
    // steals pairing slot 0: the real 0 dB plateau gets paired with the 3 dB step,
    // every subsequent pairing shifts up one, and the sweep stops one step short of
    // the 36 dB peak. Applied to the main run that off-by-one makes the channel
    // climb at the wrong rate — the staircase visibly drifts in time against
    // channels whose transient was too small to register. Selection instead takes
    // the LAST `expected` plateaus of the monotonic run, which drops the leading
    // transient (it extends the run's start) and keeps the pairing correct.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0},
                                     {18.0}, {24.0}, {30.0}, {36.0}};

    // Raw counts: a leading turn-on transient, the 8 injected levels climbing to
    // the peak, then the optional down-ramp.
    const std::vector<double> up = {1000.0, 1100.0, 1300.0, 1700.0,
                                2100.0, 2500.0, 2900.0, 3300.0};
    std::vector<double> raw;
    appendRun(raw, 600.0, 150); // generator-off / turn-on transient (1.5s, confirmed)
    for (double level : up)
    {
        appendRun(raw, level, 150);
    }
    // Down-ramp (a separate, opposite-direction run; excluded).
    for (int i = intSize(up) - 2; i >= 0; i--)
    {
        appendRun(raw, up[i], 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 8);
    // Real 0 dB level (1000), NOT the 600 transient, must pair with step 0.
    QVERIFY2(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0),
             qPrintable(QString("first point rawAvg=%1, expected 1000 (leading "
                                "transient stole the slot)").arg(r.profile.points[0].rawAvg)));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    // Peak (3300) must pair with the top step (36 dB), not be left off the sweep.
    QVERIFY(qFuzzyCompare(r.profile.points[7].rawAvg, 3300.0));
    QCOMPARE(r.profile.points[7].trueDb, 36.0);
}

void TestStepDetector::detectInvertedPolaritySweepNotReversed()
{
    // RASA receivers are inverted polarity: the raw count FALLS as the injected
    // signal climbs, so the peak-signal step (36 dB) is the MINIMUM raw plateau,
    // not the maximum. Anchoring naively on the maximum raw plateau would treat
    // the 0 dB end as the top step and pair the whole sweep backwards, flipping
    // the calibrated staircase (the field "0.48 dB at the peak" inversion). The
    // monotonic-run selection is direction-agnostic: it uses the run's own
    // direction (here decreasing), takes its last `expected` plateaus, and pairs
    // them with the steps in time order — so step 0 stays paired with the
    // highest-raw plateau and step 7 with the lowest, without locking onto a
    // raw max/min extreme (which would instead latch onto the return ramp).
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0},
                                     {18.0}, {24.0}, {30.0}, {36.0}};

    // Raw counts descend as dB ascends. Leading turn-on transient sits on the
    // low-signal (HIGH raw) side, ahead of the sweep, then a return ramp after.
    const std::vector<double> down = {3300.0, 2900.0, 2500.0, 2100.0,
                                  1700.0, 1300.0, 1100.0, 1000.0};
    std::vector<double> raw;
    appendRun(raw, 3500.0, 150); // turn-on transient (highest raw = no signal)
    for (double level : down)
    {
        appendRun(raw, level, 150);
    }
    // Return ramp back up in raw (signal falling away); a separate opposite-
    // direction run, excluded.
    for (int i = intSize(down) - 2; i >= 0; i--)
    {
        appendRun(raw, down[i], 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 8);
    // Points are sorted ascending by raw. Lowest raw (1000) is the 36 dB peak,
    // highest raw within the sweep (3300) is the 0 dB step — NOT reversed, and the
    // 3500 transient is excluded.
    QVERIFY2(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0),
             qPrintable(QString("lowest-raw point rawAvg=%1, expected 1000")
                            .arg(r.profile.points[0].rawAvg)));
    QCOMPARE(r.profile.points[0].trueDb, 36.0);
    QVERIFY(qFuzzyCompare(r.profile.points[7].rawAvg, 3300.0));
    QCOMPARE(r.profile.points[7].trueDb, 0.0);
    // The 3500 transient must not appear as a calibration point.
    for (const CalibrationPoint& p : r.profile.points)
    {
        QVERIFY(p.rawAvg < 3400.0);
    }
}

void TestStepDetector::detectNonMonotonicPairingRejected()
{
    // Chronological plateau order (2000, 1000, 3000) paired positionally with
    // steps (0, 6, 12) gives points (2000,0),(1000,6),(3000,12). Sorted
    // ascending by rawAvg that's (1000,6),(2000,0),(3000,12) -- dB goes
    // 6 -> 0 -> 12, which is neither non-decreasing nor non-increasing. This
    // mirrors a real failure mode: extra/missed plateaus shift the positional
    // step pairing, producing two near-adjacent raw values pinned to
    // contradictory dB values. The profile must be rejected (not handed to
    // interpolateCalibration with a near-vertical interpolation slope).
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> raw;
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(!r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 3); // plateaus were found; pairing was just rejected
}

void TestStepDetector::detectNoisyStepsStillDetected()
{
    std::vector<StepDefinition> steps = {{0.0}, {6.0}};

    // Small in-plateau ripple (amplitude 2) << the 1000-count step transitions.
    std::vector<double> raw;
    for (int i = 0; i < 150; i++) raw.push_back(1000.0 + (i % 3 == 0 ? 2.0 : 0.0));
    for (int i = 0; i < 150; i++) raw.push_back(2000.0 + (i % 3 == 0 ? 2.0 : 0.0));

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 2);
}

void TestStepDetector::detectSettlingAtPlateauStartExcluded()
{
    // The plateau's first samples ramp up gradually (settling, delta small
    // enough to stay "stable") before reaching the true core level. Averaging
    // the confirmation window from the END of the plateau (immediately before
    // the next transition) rather than its start must skip that settling
    // region entirely, so the average equals the clean core value.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}};

    std::vector<double> raw;
    appendRun(raw, 0.0, 150); // genuine 0 dB baseline plateau
    // Gradual settling ramp at the start of the second plateau.
    for (int v = 991; v <= 1000; v++) raw.push_back(static_cast<double>(v));
    appendRun(raw, 1000.0, 190); // stable core

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 2);
    // The second plateau's confirmation window (last 100 of its 200 samples)
    // falls entirely within the flat core, so the settling ramp is excluded.
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 1000.0));
}

void TestStepDetector::roundTripSameDataIsExact()
{
    // Round trip: calibrate from a clean stepped series, then "measure" the very
    // same raw levels back through the resulting profile. Because the data used
    // to build the profile is identical to the data fed through it, there is no
    // error between them — interpolating each step's own raw average must return
    // that step's exact dB (6, 12, 18, 24, 30). This is the zero-noise property
    // we get when the calibration file and the measured file are one and the same.
    std::vector<StepDefinition> steps = {{6.0}, {12.0}, {18.0}, {24.0}, {30.0}};

    const std::vector<double> levels = {1000.0, 2000.0, 3000.0, 4000.0, 5000.0};
    std::vector<double> raw;
    for (double level : levels)
    {
        appendRun(raw, level, 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 5);
    QCOMPARE(r.profile.points.size(), 5);

    // Each plateau's own raw average maps back to its paired step dB, exactly.
    for (int i = 0; i < levels.size(); i++)
    {
        const double db = interpolateCalibration(levels[i], r.profile);
        QVERIFY2(qFuzzyCompare(db, steps[i].db),
                 qPrintable(QString("raw %1 -> %2 dB, expected %3 dB")
                                .arg(levels[i]).arg(db).arg(steps[i].db)));
    }
}

void TestStepDetector::detectMultiChannelSweepStaysTimeAligned()
{
    // Regression for the "staircases drift in time across channels" report:
    // several receiver channels see the SAME injected dB sweep at the SAME times,
    // but with different gain, opposite polarity, or a per-channel turn-on
    // transient. Each channel is calibrated independently — yet every channel must
    // map the k-th sweep segment back to the k-th step's dB. If one channel's
    // plateau<->step pairing shifts, that channel crosses a given dB level at a
    // different segment than its peers, so when the profiles are applied on the
    // shared per-stream time axis the staircases visibly stagger. Identical dB per
    // segment across all channels == time-aligned transitions.
    const std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}};
    constexpr int kHold = 150; // > the 100-sample (1.0 s) confirmation window

    // Per-channel raw plateau level at each of the four sweep segments.
    const std::vector<double> clean    = {1000.0, 1500.0, 2000.0, 3000.0}; // nominal gain
    const std::vector<double> hotGain  = {1300.0, 1900.0, 2500.0, 3700.0}; // higher gain, same sweep
    const std::vector<double> inverted = {3000.0, 2500.0, 2000.0, 1000.0}; // inverted polarity (raw falls as dB climbs)

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

    for (int c = 0; c < channels.size(); c++)
    {
        const std::vector<double> raw = buildRaw(channels[c].levels, channels[c].transient);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY2(r.profile.valid, qPrintable(QString("channel %1 profile invalid").arg(c)));
        QCOMPARE(r.profile.points.size(), steps.size());

        // The crux: each channel's own raw level for sweep segment k must
        // calibrate back to step k's dB. Same dB per segment for every channel.
        for (int k = 0; k < steps.size(); k++)
        {
            const double db = interpolateCalibration(channels[c].levels[k], r.profile);
            QVERIFY2(qAbs(db - steps[k].db) < 1e-6,
                     qPrintable(QString("channel %1 segment %2: raw %3 -> %4 dB, expected %5 dB "
                                        "(pairing shifted -> staircase would stagger)")
                                    .arg(c).arg(k).arg(channels[c].levels[k]).arg(db).arg(steps[k].db)));
        }
    }
}

// ---------------------------------------------------------------------------
// interpolateCalibration
// ---------------------------------------------------------------------------

void TestStepDetector::interpolateMidpoint()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    QVERIFY(qFuzzyCompare(interpolateCalibration(1500.0, p), 9.0));
}

void TestStepDetector::interpolateBelowFirstClamps()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // Below the first point clamps to its dB (out of calibrated range), not
    // extrapolated: raw 500 -> 6.0 (the first point's dB), not 3.0.
    QVERIFY(qFuzzyCompare(interpolateCalibration(500.0, p), 6.0));
}

void TestStepDetector::interpolateAboveLastClamps()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // Above the last point clamps to its dB: raw 2500 -> 12.0, not 15.0.
    QVERIFY(qFuzzyCompare(interpolateCalibration(2500.0, p), 12.0));
}

void TestStepDetector::interpolateCoincidentRawNoCrash()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {1000.0, 12.0}};
    p.valid = true;
    // Divide-by-zero guard returns the first point's dB rather than crashing.
    QVERIFY(qFuzzyCompare(interpolateCalibration(1000.0, p), 6.0));
}

void TestStepDetector::detectOutcomeNamesTheGateThatRejected()
{
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    // Success carries Calibrated, and reports the threshold it judged edges by -
    // the quantity a merged step is measured against.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        appendRun(raw, 3000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY(r.profile.valid);
        QCOMPARE(r.outcome, CalibrationOutcome::Calibrated);
        QVERIFY(r.edgeThreshold > 0.0);
    }

    // Too few plateaus: only two levels for three expected steps. This is the
    // shape a compressed / near-saturation receiver produces, where adjacent
    // steps merge because their separation never clears the edge threshold.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY(!r.profile.valid);
        QCOMPARE(r.outcome, CalibrationOutcome::TooFewPlateaus);
    }

    // Enough plateaus, but they zig-zag instead of sweeping one way, so no run of
    // three consecutive levels moves in a single direction. Distinct from the
    // case above and pointing at a different cause, which is exactly why the
    // plateau count alone cannot explain a fallback.
    {
        std::vector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 3000.0, 150);
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 3000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY(!r.profile.valid);
        QVERIFY(r.detectedPlateaus >= steps.size()); // count alone looked fine
        QCOMPARE(r.outcome, CalibrationOutcome::NoMonotonicSweep);
    }
}

void TestStepDetector::detectCompressedOutOfToleranceReceiverStillCalibrates()
{
    // A receiver far out of tolerance does not move a uniform number of raw
    // counts per dB: it is compressed at one end and expansive at the other, so
    // one sweep can hold both 12-count and 1100-count steps. A single global edge
    // threshold sized for the large steps cannot see the small ones, which is how
    // these channels lose their profile and fall back to linear.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {24.0}, {36.0}};

    const std::vector<double> levels = {800.0, 812.0, 826.0, 900.0, 1500.0, 2600.0};

    // Pseudo-random (not periodic) dither, seeded for reproducibility. A periodic
    // pattern would be a poor stand-in for receiver noise: its median absolute
    // deviation collapses to zero even when the swings are large, which drives the
    // MAD-derived edge threshold to its floor and marks every sample an edge.
    quint32 seed = 12345u;
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

    QVERIFY2(r.profile.valid,
             qPrintable(QString("outcome=%1 plateaus=%2 threshold=%3")
                            .arg(calibrationOutcomeText(r.outcome))
                            .arg(r.detectedPlateaus)
                            .arg(r.edgeThreshold)));
    QCOMPARE(r.profile.points.size(), steps.size());

    // Every step must keep its own dB: a merged pair would shift the pairing and
    // silently mis-map the whole channel rather than fail loudly.
    QCOMPARE(r.profile.points.front().trueDb, 0.0);
    QCOMPARE(r.profile.points.back().trueDb, 36.0);

    // It only resolved because the threshold was loosened, and that fact is
    // reported rather than swallowed: a channel whose steps sit this close to its
    // own noise is the one drifting toward not calibrating at all.
    QVERIFY(r.thresholdRelaxed);

    // A receiver with well-separated steps must NOT be reported as relaxed - the
    // signal is only meaningful if the ordinary case stays quiet.
    std::vector<double> healthy;
    appendRun(healthy, 800.0, 150);
    appendRun(healthy, 1200.0, 150);
    appendRun(healthy, 1600.0, 150);
    appendRun(healthy, 2000.0, 150);
    appendRun(healthy, 2400.0, 150);
    appendRun(healthy, 2800.0, 150);
    StepDetector::Result h = StepDetector::detect(healthy, kPeriod, steps);
    QVERIFY(h.profile.valid);
    QVERIFY(!h.thresholdRelaxed);
}

void TestStepDetector::detectNoiseSplitPlateausDoNotSeverTheSweep()
{
    // Levels taken verbatim from a real out-of-tolerance receiver
    // (step_cal, example.ch10, R_RCVR1): a clean climb from no-signal to a
    // saturated ceiling, except noise split two dwells into pairs of plateaus
    // differing by 3 and -1 raw counts.
    //
    // Those sub-count wobbles are not transitions, but the sweep selector read
    // them as "no real change" and severed the monotonic run mid-sweep, so a
    // plainly good calibration was discarded and the channel fell back to linear.
    // Adjacent plateaus within the edge threshold are the SAME physical level and
    // must be coalesced before the run is sought.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0},
                                     {18.0}, {24.0}, {30.0}, {36.0}};

    const std::vector<double> levels = {0.0,     7255.0,  14746.0, 22314.0,
                                    27315.0, 34752.0, 49892.0, 49895.0,
                                    57310.0, 57309.0, 65472.0, 0.0};

    // Carry receiver noise too, because it is load-bearing: the noise is what
    // lifts the edge threshold above the 1-3 count split, and a noiseless copy of
    // these levels would sit at the 2.0-count floor where a 3-count split is a
    // "real" transition. The bug only exists in the presence of the noise that
    // created the splits in the first place.
    quint32 seed = 987u;
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
    QVERIFY2(r.profile.valid,
             qPrintable(QString("outcome=%1 plateaus=%2 threshold=%3")
                            .arg(calibrationOutcomeText(r.outcome))
                            .arg(r.detectedPlateaus)
                            .arg(r.edgeThreshold)));
    QCOMPARE(r.profile.points.size(), steps.size());

    // Nine levels for eight steps, so the run's last `expected` are kept: 7255 ->
    // 0 dB up to the ceiling -> 36 dB, each split pair contributing exactly one
    // point. (These levels are a coalescing fixture, not a correct calibration of
    // that receiver: its true sweep is 0-60 dB in eleven steps with the top two
    // on the rail - see TestCalibrationExtractor.) Compared with a tolerance
    // because the dither moves each settled average slightly.
    QVERIFY2(qAbs(r.profile.points.front().rawAvg - 7255.0) < 5.0,
             qPrintable(QString::number(r.profile.points.front().rawAvg)));
    QCOMPARE(r.profile.points.front().trueDb, 0.0);
    QVERIFY2(qAbs(r.profile.points.back().rawAvg - 65472.0) < 5.0,
             qPrintable(QString::number(r.profile.points.back().rawAvg)));
    QCOMPARE(r.profile.points.back().trueDb, 36.0);
}

void TestStepDetector::detectFlatChannelReportsNoSignalNotMergedSteps()
{
    // A dead input holds one value for the whole recording. Before, this reported
    // "too few plateaus" - the same verdict a compressed receiver gets - which
    // sends the operator looking for a detection problem when the sweep simply is
    // not in the recording. The two must read differently.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    std::vector<double> flat;
    appendRun(flat, 0.0, 600);
    StepDetector::Result r = StepDetector::detect(flat, kPeriod, steps);
    QVERIFY(!r.profile.valid);
    QCOMPARE(r.outcome, CalibrationOutcome::FlatNoSignal);
    // The stuck level is carried out so the report can name it: 0 is a dead
    // input, full scale is a railed one, and the operator needs to tell them apart.
    QCOMPARE(r.plateauLevels.size(), 1);
    QCOMPARE(r.plateauLevels.front(), 0.0);

    // A railed channel is equally flat, just at the other end of the range.
    std::vector<double> railed;
    appendRun(railed, 65535.0, 600);
    StepDetector::Result railedResult = StepDetector::detect(railed, kPeriod, steps);
    QCOMPARE(railedResult.outcome, CalibrationOutcome::FlatNoSignal);
    QCOMPARE(railedResult.plateauLevels.front(), 65535.0);

    // Two genuine levels is NOT "flat" - it is a real but incomplete sweep, and
    // still reports as merged steps rather than as a dead channel.
    std::vector<double> twoLevels;
    appendRun(twoLevels, 1000.0, 300);
    appendRun(twoLevels, 2000.0, 300);
    StepDetector::Result partial = StepDetector::detect(twoLevels, kPeriod, steps);
    QCOMPARE(partial.outcome, CalibrationOutcome::TooFewPlateaus);
}

void TestStepDetector::detectPinnedSamplesDoNotShrinkTheNoiseEstimate()
{
    // A receiver parked at the converter floor repeats 0 exactly: noise-free, and
    // on a real cal recording a large share of the series (the pre-sweep, and the
    // post-sweep once the generator drops). Counted into the noise estimate, those
    // perfect-zero derivatives drag it far under the live steps' jitter; the edge
    // threshold falls to its 2-count floor, every jittering sample reads as an
    // edge, and the dwells shatter into runs too short to confirm. The channel
    // then reads as flat with its sweep plainly present.
    //
    // The pinned share is exaggerated here (a majority) so the failure is
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
    QVERIFY2(r.profile.valid, qPrintable(QString("outcome=%1 threshold=%2")
                                             .arg(calibrationOutcomeText(r.outcome))
                                             .arg(r.edgeThreshold)));
    QCOMPARE(r.profile.points.size(), steps.size());
    QCOMPARE(r.profile.points.front().rawAvg, 0.0);
    QCOMPARE(r.profile.points.front().trueDb, 0.0);
    QVERIFY(qAbs(r.profile.points.back().rawAvg - 4000.0) < 6.0);
    QCOMPARE(r.profile.points.back().trueDb, 24.0);
}

void TestStepDetector::detectSplitDwellFarAboveNoiseIsStillOneStep()
{
    // Noise grows with signal on a real receiver, so a high dwell can split into
    // two plateaus further apart than any threshold the quiet low steps support:
    // the step_cal recording's L_RCVR3 split its 42 dB dwell 22 counts apart against a
    // 17-count threshold, on steps ~3800 counts tall. The split took a pairing
    // slot, the real 0 dB level was dropped as "pre-roll", and every step below the
    // split read 6 dB low. Levels a small fraction of a typical step apart are one
    // step, whatever the noise estimate says.
    //
    // Noiseless here, so the edge threshold sits at its 2-count floor and only the
    // step-relative merge can heal the 30-count split.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    std::vector<double> raw;
    appendRun(raw, 0.0, 150);    // 0 dB
    appendRun(raw, 1000.0, 150); // 6 dB
    appendRun(raw, 2000.0, 150); // 12 dB
    appendRun(raw, 3000.0, 150); // 18 dB, first half of the dwell...
    appendRun(raw, 3030.0, 150); // ...second half, split 30 counts high
    appendRun(raw, 4000.0, 150); // 24 dB

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), steps.size());
    // The pre-sweep level keeps its 0 dB slot instead of being dropped as pre-roll.
    QCOMPARE(r.profile.points.front().rawAvg, 0.0);
    QCOMPARE(r.profile.points.front().trueDb, 0.0);
    // The split dwell is ONE point at 18 dB, carried by its later, settled half.
    QCOMPARE(r.profile.points[3].rawAvg, 3030.0);
    QCOMPARE(r.profile.points[3].trueDb, 18.0);
    QCOMPARE(r.profile.points.back().trueDb, 24.0);
}

void TestStepDetector::detectRailedTopStepsCalibrateWhatStayedInRange()
{
    // A receiver whose gain is set too high for the injected levels runs off the
    // top of its converter partway up the sweep: the last dwells all read full
    // scale, so the recording cannot hold `expected` distinct levels and no
    // threshold will find them. Before, the whole channel fell back to linear and
    // lost the steps it HAD measured. Those railed dwells carry no information -
    // the receiver had no headroom - so they are dropped, the steps below them are
    // calibrated, and interpolateCalibration() clamps above the top measured step
    // rather than inventing values by extrapolating past the data.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    appendRun(raw, 65535.0, 300); // 18 and 24 dB both peg the converter

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY2(r.profile.valid, qPrintable(QString::fromLatin1(calibrationOutcomeText(r.outcome))));
    QVERIFY(r.saturated);
    QCOMPARE(r.outcome, CalibrationOutcome::Calibrated);

    // The three measured dwells keep their own dB - the sweep starts at step 0.
    QCOMPARE(r.profile.points.size(), 3);
    QCOMPARE(r.profile.points.front().rawAvg, 1000.0);
    QCOMPARE(r.profile.points.front().trueDb, 0.0);
    QCOMPARE(r.profile.points.back().rawAvg, 3000.0);
    QCOMPARE(r.profile.points.back().trueDb, 12.0);
    QCOMPARE(r.unresolvedDb, std::vector<double>({18.0, 24.0}));

    // A raw value at the rail reads the top step that was actually measured. It
    // is not 24 dB: the recording cannot say whether the signal was 18, 24, or
    // anything above, so the honest answer is the last one it could see.
    QCOMPARE(interpolateCalibration(65535.0, r.profile), 12.0);
}

void TestStepDetector::detectUnconfirmedDwellIsMeasuredFromTheGrid()
{
    // The 18 dB dwell is in the recording but its plateau never confirms - noise
    // shatters it into runs too short to hold. Two failures to guard against:
    //  - position alone would pair the 24 dB dwell with the 18 dB step and slide
    //    everything after it down one, the silent mis-calibration a split dwell
    //    used to cause;
    //  - leaving the hole and interpolating across it assumes the receiver is
    //    linear there, which is exactly what an out-of-tolerance receiver is not
    //    (L_RCVR1's 24 dB dwell read 23.09 dB that way on the step_cal recording).
    // The dwell grid says where the missing dwell is, so it is measured directly.
    std::vector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}};

    std::vector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    // 18 dB: jitters by a few counts for its whole dwell, so no run of it is ever
    // stable long enough to confirm - but the transitions either side still are.
    // Deliberately NOT the midpoint of its neighbours, so a linear bridge across
    // the hole would read the wrong dB.
    for (int i = 0; i < 150; i++)
    {
        raw.push_back(3600.0 + ((i % 2 == 0) ? 0.0 : 6.0));
    }
    appendRun(raw, 5000.0, 150);
    appendRun(raw, 65535.0, 150); // 30 dB pegs the converter

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY2(r.profile.valid, qPrintable(QString::fromLatin1(calibrationOutcomeText(r.outcome))));
    QVERIFY(r.saturated);
    QCOMPARE(r.profile.points.size(), 5);

    // The unconfirmed dwell is measured at its own level and keeps its own dB...
    QCOMPARE(r.profile.points[3].rawAvg, 3603.0);
    QCOMPARE(r.profile.points[3].trueDb, 18.0);
    QCOMPARE(interpolateCalibration(3603.0, r.profile), 18.0);
    // ...the step after it keeps ITS own dB rather than sliding down one...
    QCOMPARE(r.profile.points.back().rawAvg, 5000.0);
    QCOMPARE(r.profile.points.back().trueDb, 24.0);
    // ...and only the railed step is left unresolved.
    QCOMPARE(r.unresolvedDb, std::vector<double>({30.0}));
}

void TestStepDetector::detectReportsHowManyLevelsTheSweepHeld()
{
    // An 8-step file against an 11-level sweep still "calibrates": the detector keeps
    // the last eight levels and sets the lower three aside as lead-in, so every step
    // is paired with the wrong level and nothing fails. sweepLevels is what lets the
    // report notice - the sweep held more levels than the step file describes.
    std::vector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}, {36.0}};

    std::vector<double> eleven;
    for (int k = 1; k <= 11; k++)
    {
        appendRun(eleven, 1000.0 * k, 150);
    }
    StepDetector::Result r = StepDetector::detect(eleven, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.sweepLevels, 11);

    // A sweep that matches the step file exactly reports exactly that many.
    std::vector<double> eight;
    for (int k = 1; k <= 8; k++)
    {
        appendRun(eight, 1000.0 * k, 150);
    }
    StepDetector::Result exact = StepDetector::detect(eight, kPeriod, steps);
    QVERIFY(exact.profile.valid);
    QCOMPARE(exact.sweepLevels, 8);
}

void TestStepDetector::initialStepConfigPathIgnoresADirectory()
{
    // "Is a file", not "exists": a directory sitting where the step file should be (the
    // preferred path, or the shipped default's own location) must not be offered to the
    // dialog as a step file to parse.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString rcvr_cals = root.path() + "/settings/rcvr_cals";
    QVERIFY(QDir().mkpath(rcvr_cals + "/default.toml")); // a directory, named like the file
    QVERIFY(QDir().mkpath(root.path() + "/somedir"));

    QVERIFY(initialPath(root.path(), root.path() + "/somedir").isEmpty());
    QVERIFY(initialPath(root.path(), QString()).isEmpty());
}

void TestStepDetector::initialStepConfigPrefersTheStreamsFileThenTheDefault()
{
    // The Extract Calibration dialog opens with a step file already loaded, because
    // almost every calibration uses the same one. Which file is decided here.
    QDir root(QCoreApplication::applicationDirPath());
    root.cdUp(); // tests/
    root.cdUp(); // project root
    const QString appRoot     = root.absolutePath();
    const QString shipped     = appRoot + "/settings/rcvr_cals/default.toml";
    const QString rasa        = appRoot + "/settings/rcvr_cals/RASA.toml";
    QVERIFY2(QFileInfo(shipped).isFile(), qPrintable(shipped));

    // No previous choice: the shipped default.
    QCOMPARE(initialPath(appRoot, QString()), shipped);

    // A stream that already used a step file keeps it, even when it is not the
    // default - re-opening the dialog must not silently swap the operator's file.
    QCOMPARE(initialPath(appRoot, rasa), rasa);

    // A stored reference that no longer resolves (a template from another machine)
    // falls back to the default instead of opening on a missing file.
    QCOMPARE(initialPath(appRoot, "C:/nowhere/gone.toml"), shipped);

    // No settings tree at all (a bare build directory): nothing to preload, and the
    // dialog opens empty as it did before.
    QTemporaryDir bare;
    QVERIFY(bare.isValid());
    QVERIFY(initialPath(bare.path(), QString()).isEmpty());
    QVERIFY(initialPath(QString(), QString()).isEmpty());
}
