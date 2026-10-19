
#include "tst_chapter10reader.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

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

namespace {

/// What loading a file produced: whether it succeeded and the messages it reported.
struct LoadResult
{
    bool        ok = false;
    QStringList errors;
};

/// Loads @p path into @p reader. The one place these tests open a file with the reader or
/// read its error report, so a change in how either is spelled touches this helper only.
LoadResult load(Chapter10Reader& reader, const QString& path)
{
    QSignalSpy spy(&reader, &Chapter10Reader::displayErrorMessage);
    LoadResult result;
    result.ok = reader.loadChannels(path);
    for (const QList<QVariant>& args : spy)
    {
        result.errors << args.at(0).toString();
    }
    return result;
}

/// Loads a fixture from tests/data/test files/ and requires it to open.
void loadFixture(Chapter10Reader& reader, const QString& name)
{
    const LoadResult result = load(reader, testDataPath("test files/" + name));
    QVERIFY2(result.ok, qPrintable(name + ": " + result.errors.join("; ")));
}

QString idNameRate(int id, const QString& name, double bits_per_second)
{
    return QString::number(id) + "|" + name + "|" + QString::number(bits_per_second, 'f', 0);
}

} // namespace

void TestChapter10Reader::init()
{
    // The fixtures this suite reads are committed, so this only guards a checkout that
    // left them out (a worktree without the tests/data junction).
    //
    // This MUST be a per-test skip (init(), run before every test) - a
    // suite-level QSKIP in initTestCase() poisons the custom multi-qExec harness
    // in main.cpp and silently stops every suite that runs after this one.
    if (!QFileInfo::exists(testDataPath("test files/agc_nzl_rasa_testfile.ch10")))
        QSKIP("Chapter 10 test fixture (test files/agc_nzl_rasa_testfile.ch10) not present - "
              "this suite needs the committed fixtures under tests/data");
}

void TestChapter10Reader::loadChannelsReturnsTrueForValidFile()
{
    Chapter10Reader reader;
    const LoadResult result = load(reader, testDataPath("test files/agc_nzl_rasa_testfile.ch10"));
    QVERIFY2(result.ok, "loadChannels should return true for a valid .ch10 file");
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
}

void TestChapter10Reader::loadChannelsPopulatesTimeChannels()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    QStringList time_list = reader.getTimeChannelComboBoxList();
    QVERIFY2(!time_list.isEmpty(), "Time channel list should not be empty after loading a valid file");
}

void TestChapter10Reader::loadChannelsPopulatesPcmChannels()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    QStringList pcm_list = reader.getPCMChannelComboBoxList();
    QVERIFY2(!pcm_list.isEmpty(), "PCM channel list should not be empty after loading a valid file");
}

void TestChapter10Reader::comboBoxListsContainChannelIdAndName()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

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

void TestChapter10Reader::eachFixtureListsItsChannelsExactly()
{
    QFETCH(QString, file);
    QFETCH(QStringList, time_list);
    QFETCH(QStringList, pcm_list);
    QFETCH(QStringList, pcm_channels);
    QFETCH(int, first_pcm);

    Chapter10Reader reader;
    loadFixture(reader, file);

    // The combo-box strings are "<id> - <name>"; the channel list carries the bare name,
    // which labels the stream in the dialog and on the plot.
    QCOMPARE(reader.getTimeChannelComboBoxList(), time_list);
    QCOMPARE(reader.getPCMChannelComboBoxList(), pcm_list);

    QStringList actual;
    for (const auto& channel : reader.getPCMChannelList())
    {
        actual << idNameRate(channel.first, channel.second, reader.getTmatsDataRateBps(channel.first));
    }
    QCOMPARE(actual, pcm_channels);

    // Defaults after a load: the first time channel, and the first PCM channel.
    QCOMPARE(reader.getCurrentTimeChannelID(), 1);
    QCOMPARE(reader.getFirstPCMChannelID(), first_pcm);
    QCOMPARE(reader.m_current_pcm_channel, first_pcm);
}

void TestChapter10Reader::eachFixtureListsItsChannelsExactly_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QStringList>("time_list");
    QTest::addColumn<QStringList>("pcm_list");
    QTest::addColumn<QStringList>("pcm_channels");
    QTest::addColumn<int>("first_pcm");

    QTest::newRow("rasa")
        << "agc_nzl_rasa_testfile.ch10"
        << QStringList{"1 - TimeChannel-1"}
        << QStringList{"19 - 2240.5A", "20 - 2257.5A", "21 - 2257.5B", "22 - 2257.5C",
                       "23 - 2240.5B", "24 - 2240.5C", "25 - 2240.5D", "26 - CH-01"}
        << QStringList{idNameRate(19, "2240.5A", 2.5e6), idNameRate(20, "2257.5A", 2.1e6),
                       idNameRate(21, "2257.5B", 2.1e6), idNameRate(22, "2257.5C", 2.1e6),
                       idNameRate(23, "2240.5B", 2.5e6), idNameRate(24, "2240.5C", 2.5e6),
                       idNameRate(25, "2240.5D", 2.5e6), idNameRate(26, "CH-01", 8.0e5)}
        << 19;
    QTest::newRow("trc")
        << "agc_rnrz-l_trc_testfile.ch10"
        << QStringList{"1 - IRIG-B"}
        << QStringList{"13 - CH-01 2250.5MHZ AGC 800Kbps RNRZ-L"}
        << QStringList{idNameRate(13, "CH-01 2250.5MHZ AGC 800Kbps RNRZ-L", 8.0e5)}
        << 13;
    QTest::newRow("prn")
        << "prn_testfile, sample.ch10"
        << QStringList{"1 - IRIG-B"}
        << QStringList{"13 - CH-01 PRN 15 5M", "14 - CH-02 PRN 15 20M",
                       "15 - CH-03 PRN 11 1M", "16 - CH-04 PRN 11 5M"}
        << QStringList{idNameRate(13, "CH-01 PRN 15 5M", 5.0e6), idNameRate(14, "CH-02 PRN 15 20M", 2.0e7),
                       idNameRate(15, "CH-03 PRN 11 1M", 1.0e6), idNameRate(16, "CH-04 PRN 11 5M", 5.0e6)}
        << 13;
    QTest::newRow("safran")
        << "safran_testfile, sample.ch10"
        << QStringList{"1 - Time_Pck"}
        << QStringList{"11 - PCM01", "12 - PCM02", "13 - PCM03", "14 - PCM04"}
        << QStringList{idNameRate(11, "PCM01", 7.0e5), idNameRate(12, "PCM02", 1.0e6),
                       idNameRate(13, "PCM03", 1.0e6), idNameRate(14, "PCM04", 7.0e5)}
        << 11;
    QTest::newRow("step_cal")
        << "step_cal, example.ch10"
        << QStringList{"1 - IRIG-B"}
        << QStringList{"26 - CH-16 AGC STEP CALS 800K"}
        << QStringList{idNameRate(26, "CH-16 AGC STEP CALS 800K", 8.0e5)}
        << 26;
}

void TestChapter10Reader::aChannelTmatsDoesNotDescribeHasNoDataRate()
{
    Chapter10Reader reader;
    loadFixture(reader, "prn_testfile, sample.ch10");

    QCOMPARE(reader.getTmatsDataRateBps(13), 5.0e6);   // described
    QCOMPARE(reader.getTmatsDataRateBps(999), 0.0);    // not in the file at all
    QCOMPARE(reader.getTmatsDataRateBps(1), 0.0);      // the time channel: no PCM record
    QCOMPARE(reader.getTmatsDataRateBps(-1), 0.0);
}

void TestChapter10Reader::currentTimeChannelSetAfterLoad()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    // After loading, a default time channel should be selected (not -1)
    QVERIFY2(reader.getCurrentTimeChannelID() != -1,
             "Current time channel should be set after loading a file with time channels");
}

void TestChapter10Reader::currentPcmChannelSetAfterLoad()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    // After loading, a default PCM channel should be selected (not -1). No production
    // caller reads it back through a getter, so the friend class reaches the field
    // directly - see TestChapter10Reader's friend declaration on Chapter10Reader.
    QVERIFY2(reader.m_current_pcm_channel != -1,
             "Current PCM channel should be set after loading a file with PCM channels");
}

void TestChapter10Reader::clearSettingsResetsChannels()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    // Verify channels were loaded
    QVERIFY(!reader.getTimeChannelComboBoxList().isEmpty());
    QVERIFY(!reader.getPCMChannelComboBoxList().isEmpty());

    reader.clearSettings();

    QVERIFY2(reader.getTimeChannelComboBoxList().isEmpty(),
             "Time channel list should be empty after clearSettings");
    QVERIFY2(reader.getPCMChannelComboBoxList().isEmpty(),
             "PCM channel list should be empty after clearSettings");
    QVERIFY(reader.getPCMChannelList().isEmpty());
    QCOMPARE(reader.getCurrentTimeChannelID(), -1);
    QCOMPARE(reader.getFirstPCMChannelID(), -1);
    QCOMPARE(reader.m_current_pcm_channel, -1);
}

void TestChapter10Reader::timeChannelChangedUpdatesSelection()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

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
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    QStringList pcm_list = reader.getPCMChannelComboBoxList();
    if (pcm_list.size() < 1)
        QSKIP("Not enough PCM channels in test file to test selection change");

    // Index 0 in combo = placeholder, index 1 = first real channel
    reader.pcmChannelChanged(1);
    QVERIFY2(reader.m_current_pcm_channel != -1, "Selecting first PCM channel should set a valid ID");
}

void TestChapter10Reader::comboBoxIndexSelectsTheChannelAtThatPosition()
{
    // Four PCM channels (13..16) and one time channel (1). The combo box has a placeholder
    // at index 0, so index N picks the (N-1)th channel; the placeholder and anything past
    // the end mean "none selected".
    Chapter10Reader reader;
    loadFixture(reader, "prn_testfile, sample.ch10");

    const int pcm_expected[][2] = {{0, -1}, {1, 13}, {2, 14}, {3, 15}, {4, 16}, {5, -1}, {-1, -1}, {100, -1}};
    for (const auto& row : pcm_expected)
    {
        reader.pcmChannelChanged(row[0]);
        QVERIFY2(reader.m_current_pcm_channel == row[1],
                 qPrintable(QString("pcm index %1 -> %2, expected %3")
                                .arg(row[0]).arg(reader.m_current_pcm_channel).arg(row[1])));
    }

    const int time_expected[][2] = {{0, -1}, {1, 1}, {2, -1}, {-1, -1}};
    for (const auto& row : time_expected)
    {
        reader.timeChannelChanged(row[0]);
        QVERIFY2(reader.getCurrentTimeChannelID() == row[1],
                 qPrintable(QString("time index %1 -> %2, expected %3")
                                .arg(row[0]).arg(reader.getCurrentTimeChannelID()).arg(row[1])));
    }

    // Selecting does not change what is listed, and the first-PCM accessor is about the
    // list, not the selection.
    QCOMPARE(reader.getPCMChannelComboBoxList().size(), 4);
    QCOMPARE(reader.getFirstPCMChannelID(), 13);
}

void TestChapter10Reader::loadChannelsReturnsFalseForInvalidFile()
{
    Chapter10Reader reader;
    const LoadResult result = load(reader, "nonexistent_file.ch10");
    QVERIFY2(!result.ok, "loadChannels should return false for a nonexistent file");
}

void TestChapter10Reader::aFailedLoadReportsItsReasonAndLeavesNoChannels()
{
    QFETCH(QString, scenario);
    QFETCH(QString, message_suffix);   // appended to the path for "Error opening file: "
    QFETCH(QString, exact_message);    // used when the message does not carry the path

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // A real recording to cut up: its first packet is the TMATS record.
    QFile source(testDataPath("test files/step_cal, example.ch10"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray all = source.readAll();
    quint32 first_packet_length = 0;
    memcpy(&first_packet_length, all.constData() + 4, 4);
    QVERIFY(first_packet_length > 200 && int(first_packet_length) < all.size());

    QByteArray content;
    if (scenario == "text")                content = "this is not a chapter 10 file\n";
    else if (scenario == "no-tmats-first") content = all.mid(int(first_packet_length));
    else if (scenario == "short-header")   content = all.left(10);
    else if (scenario == "only-tmats")     content = all.left(int(first_packet_length));
    else if (scenario == "cut-in-tmats")   content = all.left(int(first_packet_length) - 100);
    // "missing" writes nothing and "empty" writes an empty file.

    const QString path = dir.filePath(scenario + ".ch10");
    if (scenario != "missing")
    {
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        QVERIFY(out.write(content) == content.size());
    }

    Chapter10Reader reader;
    const LoadResult result = load(reader, path);

    QVERIFY(!result.ok);
    const QString expected = exact_message.isEmpty() ? "Error opening file: " + path + message_suffix
                                                      : exact_message;
    QCOMPARE(result.errors, QStringList{expected});
    QVERIFY(reader.getTimeChannelComboBoxList().isEmpty());
    QVERIFY(reader.getPCMChannelComboBoxList().isEmpty());
    QCOMPARE(reader.getFirstPCMChannelID(), -1);
    QCOMPARE(reader.getCurrentTimeChannelID(), -1);
}

void TestChapter10Reader::aFailedLoadReportsItsReasonAndLeavesNoChannels_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<QString>("message_suffix");
    QTest::addColumn<QString>("exact_message");

    // The library refuses to open these, and the reader says which file.
    QTest::newRow("missing")       << "missing"        << "" << "";
    QTest::newRow("empty")         << "empty"          << "" << "";
    QTest::newRow("text")          << "text"           << "" << "";
    QTest::newRow("short-header")  << "short-header"   << "" << "";
    // A recording whose first packet is not TMATS is refused at open as well. The reader's
    // own "Failed to find TMATS message" branch sits behind a check the library has already
    // made, so no file reaches it.
    QTest::newRow("no-tmats-first") << "no-tmats-first" << "" << "";
    // These open, and then cannot establish time from what is left of the file.
    QTest::newRow("only-tmats")    << "only-tmats"     << "" << "Error establishing time sync.";
    QTest::newRow("cut-in-tmats")  << "cut-in-tmats"   << "" << "Error establishing time sync.";
}

void TestChapter10Reader::getFirstPcmChannelIdReturnsValidId()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    int first_pcm = reader.getFirstPCMChannelID();
    QVERIFY2(first_pcm != -1, "getFirstPCMChannelID should return a valid ID after loading");
    QCOMPARE(first_pcm, reader.m_current_pcm_channel);
}
