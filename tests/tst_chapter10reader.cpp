#include "tst_chapter10reader.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "chapter10reader.h"
#include "constants.h"

static QString testDataPath(const QString& filename)
{
    // tests/data/ relative to the test executable location
    QDir dir(QCoreApplication::applicationDirPath());
    // Navigate up from debug/ or release/ build dir
    dir.cdUp();
    // If inside build_tests, navigate up once more into tests/
    if (dir.dirName() == "build_tests") {
        dir.cdUp();
    }
    return dir.filePath("data/" + filename);
}

void TestChapter10Reader::init()
{
    // Every test in this suite loads a real .ch10 fixture. Those files are
    // gitignored (hundreds of MB each) so they aren't present on a fresh
    // checkout (e.g. CI). Skip each test cleanly rather than hard-failing;
    // they run in full locally where the fixtures live.
    //
    // This MUST be a per-test skip (init(), run before every test) — a
    // suite-level QSKIP in initTestCase() poisons the custom multi-qExec harness
    // in main.cpp and silently stops every suite that runs after this one.
    if (!QFileInfo::exists(testDataPath("agc_nzl_rasa_testfile.ch10")))
        QSKIP("Chapter 10 test fixture (agc_nzl_rasa_testfile.ch10) not present — "
              "large .ch10 fixtures are gitignored; this suite runs locally only");
}

void TestChapter10Reader::loadChannelsReturnsTrueForValidFile()
{
    Chapter10Reader reader;
    bool result = reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));
    QVERIFY2(result, "loadChannels should return true for a valid .ch10 file");
}

void TestChapter10Reader::loadChannelsPopulatesTimeChannels()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    QStringList time_list = reader.getTimeChannelComboBoxList();
    QVERIFY2(!time_list.isEmpty(), "Time channel list should not be empty after loading a valid file");
}

void TestChapter10Reader::loadChannelsPopulatesPcmChannels()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    QStringList pcm_list = reader.getPCMChannelComboBoxList();
    QVERIFY2(!pcm_list.isEmpty(), "PCM channel list should not be empty after loading a valid file");
}

void TestChapter10Reader::comboBoxListsContainChannelIdAndName()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    QStringList time_list = reader.getTimeChannelComboBoxList();
    QVERIFY(!time_list.isEmpty());
    // Each entry should be formatted as "ID - Name" where Name is non-empty
    for (const QString& entry : time_list)
    {
        QStringList parts = entry.split(" - ");
        QVERIFY2(parts.size() >= 2, qPrintable("Time entry missing ' - ' separator: " + entry));
        QVERIFY2(!parts.last().isEmpty(), qPrintable("Time channel has no name label: " + entry));
    }

    QStringList pcm_list = reader.getPCMChannelComboBoxList();
    QVERIFY(!pcm_list.isEmpty());
    for (const QString& entry : pcm_list)
    {
        QStringList parts = entry.split(" - ");
        QVERIFY2(parts.size() >= 2, qPrintable("PCM entry missing ' - ' separator: " + entry));
        QVERIFY2(!parts.last().isEmpty(), qPrintable("PCM channel has no name label: " + entry));
    }
}

void TestChapter10Reader::currentTimeChannelSetAfterLoad()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    // After loading, a default time channel should be selected (not -1)
    QVERIFY2(reader.getCurrentTimeChannelID() != -1,
             "Current time channel should be set after loading a file with time channels");
}

void TestChapter10Reader::currentPcmChannelSetAfterLoad()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    // After loading, a default PCM channel should be selected (not -1)
    QVERIFY2(reader.getCurrentPCMChannelID() != -1,
             "Current PCM channel should be set after loading a file with PCM channels");
}

void TestChapter10Reader::clearSettingsResetsChannels()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    // Verify channels were loaded
    QVERIFY(!reader.getTimeChannelComboBoxList().isEmpty());
    QVERIFY(!reader.getPCMChannelComboBoxList().isEmpty());

    reader.clearSettings();

    QVERIFY2(reader.getTimeChannelComboBoxList().isEmpty(),
             "Time channel list should be empty after clearSettings");
    QVERIFY2(reader.getPCMChannelComboBoxList().isEmpty(),
             "PCM channel list should be empty after clearSettings");
    QCOMPARE(reader.getCurrentTimeChannelID(), -1);
    QCOMPARE(reader.getCurrentPCMChannelID(), -1);
}

void TestChapter10Reader::timeChannelChangedUpdatesSelection()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    QStringList time_list = reader.getTimeChannelComboBoxList();
    if (time_list.size() < 1)
        QSKIP("Not enough time channels in test file to test selection change");

    // Index 0 in combo = "Select a Time Stream" placeholder, index 1 = first real channel
    reader.timeChannelChanged(1);
    int first_id = reader.getCurrentTimeChannelID();
    QVERIFY2(first_id != -1, "Selecting first time channel should set a valid ID");
}

void TestChapter10Reader::pcmChannelChangedUpdatesSelection()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    QStringList pcm_list = reader.getPCMChannelComboBoxList();
    if (pcm_list.size() < 1)
        QSKIP("Not enough PCM channels in test file to test selection change");

    // Index 0 in combo = placeholder, index 1 = first real channel
    reader.pcmChannelChanged(1);
    int first_id = reader.getCurrentPCMChannelID();
    QVERIFY2(first_id != -1, "Selecting first PCM channel should set a valid ID");
}

void TestChapter10Reader::loadChannelsReturnsFalseForInvalidFile()
{
    Chapter10Reader reader;
    bool result = reader.loadChannels("nonexistent_file.ch10");
    QVERIFY2(!result, "loadChannels should return false for a nonexistent file");
}

void TestChapter10Reader::getFirstPcmChannelIdReturnsValidId()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    int first_pcm = reader.getFirstPCMChannelID();
    QVERIFY2(first_pcm != -1, "getFirstPCMChannelID should return a valid ID after loading");
    QCOMPARE(first_pcm, reader.getCurrentPCMChannelID());
}


void TestChapter10Reader::getTimeChannelIndexReturnsValidIndex()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    int time_id = reader.getCurrentTimeChannelID();
    QVERIFY2(time_id != -1, "Precondition: a time channel must be selected after loading");

    int idx = reader.getTimeChannelIndex(time_id);
    QVERIFY2(idx >= 0, "getTimeChannelIndex should return a non-negative index for a loaded time channel");
}

void TestChapter10Reader::getTimeChannelIndexReturnsMinusOneForUnknown()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    QCOMPARE(reader.getTimeChannelIndex(-1),   -1);
    QCOMPARE(reader.getTimeChannelIndex(9999), -1);
}

void TestChapter10Reader::getPcmChannelIndexReturnsValidIndex()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    int pcm_id = reader.getCurrentPCMChannelID();
    QVERIFY2(pcm_id != -1, "Precondition: a PCM channel must be selected after loading");

    int idx = reader.getPCMChannelIndex(pcm_id);
    QVERIFY2(idx >= 0, "getPCMChannelIndex should return a non-negative index for a loaded PCM channel");
}

void TestChapter10Reader::getPcmChannelIndexReturnsMinusOneForUnknown()
{
    Chapter10Reader reader;
    reader.loadChannels(testDataPath("agc_nzl_rasa_testfile.ch10"));

    QCOMPARE(reader.getPCMChannelIndex(-1),   -1);
    QCOMPARE(reader.getPCMChannelIndex(9999), -1);
}

