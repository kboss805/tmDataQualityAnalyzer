#include "tst_framesetup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextStream>
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
