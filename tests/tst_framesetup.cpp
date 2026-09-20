#include "tst_framesetup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>
#include <QtTest>

#include "framesetup.h"
#include "tomlconfighelper.h"

/// Test frame size: 48 data words + 1 (matches default 16 receivers x 3 channels).
static constexpr int kTestWordsInFrame = 49;

static QString testFixturePath(const QString& filename)
{
    // The test executable is built in tests/debug/ or tests/release/.
    // One cdUp() reaches tests/, then fixtures/ is a sibling of data/.
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir.filePath("fixtures/" + filename);
}

/// Write a raw TOML string to a temporary file and return the file path.
/// The file is kept alive until process exit (intentional leak).
static QString writeTemporaryToml(const QByteArray& content)
{
    QTemporaryFile* tmp = new QTemporaryFile;
    tmp->setAutoRemove(true);
    if (!tmp->open())
        return {};
    tmp->write(content);
    tmp->flush();
    tmp->close();
    return tmp->fileName();
}

void TestFrameSetup::defaultLengthIsZero()
{
    FrameSetup fs;
    QCOMPARE(fs.length(), 0);
}

void TestFrameSetup::clearParametersResetsToEmpty()
{
    FrameSetup fs;
    QString path = testFixturePath("test_framesetup.toml");
    if (QFile::exists(path))
    {
        fs.tryLoadingFile(path, kTestWordsInFrame);
        QVERIFY(fs.length() > 0);
    }
    fs.clearParameters();
    QCOMPARE(fs.length(), 0);
}

void TestFrameSetup::getParameterInvalidIndexReturnsNull()
{
    FrameSetup fs;
    QVERIFY(fs.getParameter(0) == nullptr);
    QVERIFY(fs.getParameter(100) == nullptr);
}

void TestFrameSetup::getParameterNegativeIndexReturnsNull()
{
    FrameSetup fs;
    QVERIFY(fs.getParameter(-1) == nullptr);
}

void TestFrameSetup::tryLoadingFileValidFile()
{
    QString path = testFixturePath("test_framesetup.toml");
    if (!QFile::exists(path))
        QSKIP("Test fixture file not found");

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, kTestWordsInFrame);

    QVERIFY(result);
    QCOMPARE(fs.length(), 3);

    // Parameters preserve file order: L, R, C
    const ParameterInfo* p0 = fs.getParameter(0);
    QVERIFY(p0 != nullptr);
    QCOMPARE(p0->name, QString("L_RCVR1"));
    QCOMPARE(p0->word, 0);  // TOML Word=1, stored as 0
    QCOMPARE(p0->is_enabled, true);
    QCOMPARE(p0->slope, 0.0);
    QCOMPARE(p0->scale, 0.0);
    QCOMPARE(p0->sample_sum, 0.0);

    const ParameterInfo* p1 = fs.getParameter(1);
    QVERIFY(p1 != nullptr);
    QCOMPARE(p1->name, QString("R_RCVR1"));
    QCOMPARE(p1->word, 1);  // TOML Word=2, stored as 1
    QCOMPARE(p1->is_enabled, true);

    const ParameterInfo* p2 = fs.getParameter(2);
    QVERIFY(p2 != nullptr);
    QCOMPARE(p2->name, QString("C_RCVR1"));
    QCOMPARE(p2->word, 2);  // TOML Word=3, stored as 2
    QCOMPARE(p2->is_enabled, true);
}

void TestFrameSetup::tryLoadingFileMissingWordKey()
{
    QString path = testFixturePath("test_framesetup_missing_word.toml");
    if (!QFile::exists(path))
        QSKIP("Test fixture file not found");

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, kTestWordsInFrame);

    QVERIFY(!result);
}

void TestFrameSetup::tryLoadingFileOutOfBoundsWord()
{
    QString path = testFixturePath("test_framesetup_out_of_bounds.toml");
    if (!QFile::exists(path))
        QSKIP("Test fixture file not found");

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, kTestWordsInFrame);

    QVERIFY(!result);
}

void TestFrameSetup::tryLoadingFileWordZeroFails()
{
    QString path = writeTemporaryToml("[TestParam]\nWord = 0\n");
    QVERIFY(!path.isEmpty());

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, kTestWordsInFrame);
    QVERIFY(!result);
}

void TestFrameSetup::tryLoadingFileWordEqualsFrameSize()
{
    QByteArray toml = "[TestParam]\nWord = " + QByteArray::number(kTestWordsInFrame) + "\n";
    QString path = writeTemporaryToml(toml);
    QVERIFY(!path.isEmpty());

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, kTestWordsInFrame);
    QVERIFY(!result);
}

void TestFrameSetup::saveToSettingsWritesCorrectData()
{
    QString path = testFixturePath("test_framesetup.toml");
    if (!QFile::exists(path))
        QSKIP("Test fixture file not found");

    FrameSetup fs;
    fs.tryLoadingFile(path, kTestWordsInFrame);

    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    if (!tmp.open())
        QSKIP("Could not create temporary file");
    tmp.close();

    QSettings out_settings(tmp.fileName(), TomlConfigHelper::format());
    fs.saveToSettings(out_settings);
    out_settings.sync();

    QSettings in_settings(tmp.fileName(), TomlConfigHelper::format());

    in_settings.beginGroup("L_RCVR1");
    QCOMPARE(in_settings.value("Word").toInt(), 1);
    QVERIFY(!in_settings.contains("Enabled"));
    in_settings.endGroup();

    in_settings.beginGroup("C_RCVR1");
    QCOMPARE(in_settings.value("Word").toInt(), 3);
    QVERIFY(!in_settings.contains("Enabled"));
    in_settings.endGroup();
}

void TestFrameSetup::tryLoadingFileSmallerFrameAccepts()
{
    // test_framesetup.toml has 3 params with Word=1,2,3.
    // A frame with 3 data words + 1 = 4 total words should accept all three.
    QString path = testFixturePath("test_framesetup.toml");
    if (!QFile::exists(path))
        QSKIP("Test fixture file not found");

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, 4); // 3 data words + 1
    QVERIFY(result);
    QCOMPARE(fs.length(), 3);
}

void TestFrameSetup::tryLoadingFileSmallerFrameRejectsBoundary()
{
    // test_framesetup.toml has Word=3 for C_RCVR1.
    // A frame with 2 data words + 1 = 3 total words should reject Word=3
    // because parameter_word = 2, and the boundary check is parameter_word >= (3 - 1).
    QString path = testFixturePath("test_framesetup.toml");
    if (!QFile::exists(path))
        QSKIP("Test fixture file not found");

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, 3); // 2 data words + 1
    QVERIFY(!result);
}

void TestFrameSetup::tryLoadingFileLargerFrameAccepts()
{
    // test_framesetup.toml has Word=1,2,3. A larger frame (e.g., 100 words)
    // should accept all parameters since they're well within range.
    QString path = testFixturePath("test_framesetup.toml");
    if (!QFile::exists(path))
        QSKIP("Test fixture file not found");

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, 100);
    QVERIFY(result);
    QCOMPARE(fs.length(), 3);
}

void TestFrameSetup::tryLoadingFileSingleChannelFrameSize()
{
    // Create a temp TOML with 1 parameter at Word=1.
    // Frame size = 1 data word + 1 = 2 should accept it.
    QString path = writeTemporaryToml("[L_RCVR1]\nWord = 1\n");
    QVERIFY(!path.isEmpty());

    FrameSetup fs;
    bool result = fs.tryLoadingFile(path, 2); // 1 data word + 1
    QVERIFY(result);
    QCOMPARE(fs.length(), 1);
    QCOMPARE(fs.getParameter(0)->word, 0);
}

void TestFrameSetup::receiverIndexFromNameInvertsParameterName()
{
    // Round-trips against the constructor it inverts, across the known channel
    // prefixes and past them (where the prefix becomes "CH<n+1>").
    for (int receiver = 0; receiver < 16; receiver++)
    {
        for (int channel = 0; channel < 5; channel++)
        {
            const QString name = FrameSetup::receiverParameterName(channel, receiver);
            QCOMPARE(FrameSetup::receiverIndexFromName(name), receiver + 1);
            QCOMPARE(FrameSetup::channelPartOfName(name),
                     FrameSetup::channelPrefix(channel));
        }
    }

    // A receiver-params TOML may name parameters anything at all. Those must read
    // as "no receiver number" (0) rather than a guess, so the calibration summary
    // falls back to listing names instead of inventing receivers.
    QCOMPARE(FrameSetup::receiverIndexFromName("AGC_LEFT"), 0);
    QCOMPARE(FrameSetup::receiverIndexFromName(""), 0);
    QCOMPARE(FrameSetup::receiverIndexFromName("L_RCVR"), 0);   // token, no number
    QCOMPARE(FrameSetup::receiverIndexFromName("L_RCVR0"), 0);  // receivers are 1-based
    QCOMPARE(FrameSetup::receiverIndexFromName("L_RCVRx2"), 0); // not a bare number
    // An unparseable name keeps its whole self as the "channel part".
    QCOMPARE(FrameSetup::channelPartOfName("AGC_LEFT"), QString("AGC_LEFT"));
}

// ---------------------------------------------------------------------------
// Receiver-parameters files: the word map and the scalars are one file, and
// saving must produce a file that loading accepts (v2.12.1).
// ---------------------------------------------------------------------------

namespace {

/// @return <repo root>/settings/receiver_params, alongside the built test exe.
QString shippedReceiverParamsDir()
{
    QDir root(QCoreApplication::applicationDirPath());
    root.cdUp();   // tests/
    root.cdUp();   // project root
    return root.absolutePath() + "/settings/receiver_params";
}

/// Writes a minimal word-map TOML: one group per entry, Word being 1-based.
void writeWordMap(const QString& path, const QList<QPair<QString, int>>& words)
{
    QFile::remove(path);
    QSettings cfg(path, TomlConfigHelper::format());
    for (const auto& entry : words)
    {
        cfg.beginGroup(entry.first);
        cfg.setValue("Word", entry.second);
        cfg.endGroup();
    }
    cfg.sync();
}

} // namespace

void TestFrameSetup::wordsInMinorFrameRoundsUp()
{
    // A partially filled last word still occupies a word slot, so the division
    // rounds up — the word map's bounds check depends on this.
    QCOMPARE(FrameSetup::wordsInMinorFrame(800), 50);
    QCOMPARE(FrameSetup::wordsInMinorFrame(801), 51);
    QCOMPARE(FrameSetup::wordsInMinorFrame(16),  1);
    QCOMPARE(FrameSetup::wordsInMinorFrame(1),   1);
}

void TestFrameSetup::tryLoadingFileMetadataOnlyFileYieldsNoParameters()
{
    // A file with metadata sections but no word-map groups parses, and leaves no
    // parameters behind. Callers must check length(); this pins the contract they
    // rely on, and is exactly the file the dialog's Save used to produce.
    const QString path = QDir::tempPath() + "/tst_rcvr_meta_only.toml";
    QFile::remove(path);
    {
        QSettings cfg(path, TomlConfigHelper::format());
        cfg.beginGroup("Parameters");
        cfg.setValue("Polarity", 1);
        cfg.setValue("Slope", 2);
        cfg.endGroup();
        cfg.sync();
    }
    QVERIFY(QFileInfo(path).size() > 0);

    FrameSetup fs;
    QVERIFY(fs.tryLoadingFile(path, kTestWordsInFrame));
    QCOMPARE(fs.length(), 0);

    QFile::remove(path);
}

void TestFrameSetup::readReceiverParamsReadsTheShippedReceiversBlock()
{
    // The shipped files carry their receiver counts under [Receivers], which the
    // dialog never read — loading RASA left the counts showing the defaults.
    const QString rasa = shippedReceiverParamsDir() + "/RASA.toml";
    QVERIFY2(QFileInfo(rasa).isFile(), qPrintable(rasa));

    const ReceiverParams params = FrameSetup::readReceiverParams(rasa);
    QCOMPARE(params.numReceivers, 12);
    QCOMPARE(params.receiverChannels, 3);
    QCOMPARE(params.polarityIndex, 1);
    QCOMPARE(params.slopeIndex, 2);
    QCOMPARE(params.scaleDdBPerV, 10.0);
}

void TestFrameSetup::readReceiverParamsAcceptsTheOlderParametersKeys()
{
    // Files the dialog wrote before this change put the counts under [Parameters].
    const QString path = QDir::tempPath() + "/tst_rcvr_legacy_keys.toml";
    QFile::remove(path);
    {
        QSettings cfg(path, TomlConfigHelper::format());
        cfg.beginGroup("Parameters");
        cfg.setValue("NumReceivers", 4);
        cfg.setValue("ReceiverChannels", 2);
        cfg.endGroup();
        cfg.sync();
    }
    QVERIFY(QFileInfo(path).size() > 0);

    const ReceiverParams params = FrameSetup::readReceiverParams(path);
    QCOMPARE(params.numReceivers, 4);
    QCOMPARE(params.receiverChannels, 2);

    // A key the file does not carry keeps the value the caller passed in — the
    // dialog passes what it is showing, so a partial file changes only what it
    // actually specifies instead of resetting the rest to the app defaults.
    ReceiverParams on_screen;
    on_screen.polarityIndex    = 1;
    on_screen.scaleDdBPerV     = 33.0;
    on_screen.numReceivers     = 9;
    on_screen.receiverChannels = 5;
    const ReceiverParams merged = FrameSetup::readReceiverParams(path, on_screen);
    QCOMPARE(merged.polarityIndex, 1);      // absent from the file
    QCOMPARE(merged.scaleDdBPerV, 33.0);    // absent from the file
    QCOMPARE(merged.numReceivers, 4);       // the file wins where it speaks
    QCOMPARE(merged.receiverChannels, 2);

    QFile::remove(path);
}

void TestFrameSetup::receiverParamsFileRoundTripsScalarsAndWordMap()
{
    // Save then load: the scalars come back, and so does the word map that was
    // carried over from the file the stream had loaded. Without the map the
    // saved file loads with zero parameters and the stream cannot process.
    const QString source = QDir::tempPath() + "/tst_rcvr_source.toml";
    writeWordMap(source, {{"L_RCVR1", 1}, {"R_RCVR1", 2}, {"C_RCVR1", 3}});

    ReceiverParams params;
    params.polarityIndex    = 1;
    params.slopeIndex       = 2;
    params.scaleDdBPerV     = 12.5;
    params.numReceivers     = 1;
    params.receiverChannels = 3;

    const QString saved = QDir::tempPath() + "/tst_rcvr_saved.toml";
    QFile::remove(saved);
    QString error;
    QVERIFY2(FrameSetup::saveReceiverParamsFile(saved, params, source, kTestWordsInFrame, error),
             qPrintable(error));

    const ReceiverParams back = FrameSetup::readReceiverParams(saved);
    QCOMPARE(back.polarityIndex, 1);
    QCOMPARE(back.slopeIndex, 2);
    QCOMPARE(back.scaleDdBPerV, 12.5);
    QCOMPARE(back.numReceivers, 1);
    QCOMPARE(back.receiverChannels, 3);

    FrameSetup fs;
    QVERIFY(fs.tryLoadingFile(saved, kTestWordsInFrame));
    QCOMPARE(fs.length(), 3);
    QHash<QString, int> by_name;
    for (int i = 0; i < fs.length(); i++)
    {
        by_name.insert(fs.getParameter(i)->name, fs.getParameter(i)->word);
    }
    QCOMPARE(by_name.value("L_RCVR1", -1), 0);
    QCOMPARE(by_name.value("R_RCVR1", -1), 1);
    QCOMPARE(by_name.value("C_RCVR1", -1), 2);

    QFile::remove(source);
    QFile::remove(saved);
}

void TestFrameSetup::receiverParamsFileFallsBackToTheDefaultWordMap()
{
    // No file was loaded, so the saved file carries the default map for the
    // counts on screen — the same map the run would have built for them.
    ReceiverParams params;
    params.numReceivers     = 2;
    params.receiverChannels = 3;

    const QString saved = QDir::tempPath() + "/tst_rcvr_default_map.toml";
    QFile::remove(saved);
    QString error;
    QVERIFY2(FrameSetup::saveReceiverParamsFile(saved, params, QString(), kTestWordsInFrame, error),
             qPrintable(error));

    FrameSetup fs;
    QVERIFY(fs.tryLoadingFile(saved, kTestWordsInFrame));
    QCOMPARE(fs.length(), 6);

    FrameSetup expected;
    QString build_error;
    QVERIFY(expected.buildDefaultReceiverMap(2, 3, kTestWordsInFrame, build_error));
    QHash<QString, int> saved_words;
    for (int i = 0; i < fs.length(); i++)
    {
        saved_words.insert(fs.getParameter(i)->name, fs.getParameter(i)->word);
    }
    for (int i = 0; i < expected.length(); i++)
    {
        const ParameterInfo* p = expected.getParameter(i);
        QCOMPARE(saved_words.value(p->name, -1), p->word);
    }

    QFile::remove(saved);
}

void TestFrameSetup::receiverParamsFileIsNeverWrittenWithoutAWordMap()
{
    // Counts that cannot fit the frame leave no map to write, and a scalars-only
    // file is one the application rejects — so nothing is written at all.
    ReceiverParams params;
    params.numReceivers     = 40;
    params.receiverChannels = 3;   // 120 words into a 49-word frame

    const QString saved = QDir::tempPath() + "/tst_rcvr_nomap.toml";
    QFile::remove(saved);
    QString error;
    QVERIFY(!FrameSetup::saveReceiverParamsFile(saved, params, QString(), kTestWordsInFrame, error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(saved));

    // The instance-level writer refuses an empty map for the same reason.
    FrameSetup empty;
    QVERIFY(!empty.writeReceiverParams(saved, params));
    QVERIFY(!QFileInfo::exists(saved));
}
