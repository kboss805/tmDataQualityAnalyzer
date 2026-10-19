
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

QStringList timeList(const Chapter10Reader& reader) { return reader.getTimeChannelComboBoxList(); }
QStringList pcmList(const Chapter10Reader& reader) { return reader.getPCMChannelComboBoxList(); }

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

namespace {

/// Copies step_cal's recording to a temp file with edits made to its TMATS text. Each edit is
/// a (needle, replacement) pair of the SAME length, so no packet length or checksum changes
/// (this recording carries no data checksum, and the header is untouched). Returns an
/// empty path and says why in @p why when an edit does not apply.
QString writePatchedStepCal(const QTemporaryDir& dir, const QStringList& edits, QString* why)
{
    QFile source(testDataPath("test files/step_cal, example.ch10"));
    if (!source.open(QIODevice::ReadOnly))
    {
        *why = "cannot read step_cal";
        return QString();
    }
    const QByteArray all = source.readAll();
    quint32 first_packet_length = 0;
    memcpy(&first_packet_length, all.constData() + 4, 4);
    QByteArray tmats = all.left(int(first_packet_length));

    for (int i = 0; i + 1 < edits.size(); i += 2)
    {
        const QByteArray needle      = edits[i].toUtf8();
        const QByteArray replacement = edits[i + 1].toUtf8();
        if (needle.size() != replacement.size())
        {
            *why = "edit changes the length: " + edits[i];
            return QString();
        }
        const int at = tmats.indexOf(needle);
        if (at < 0 || tmats.indexOf(needle, at + 1) >= 0)
        {
            *why = "needle not found exactly once: " + edits[i];
            return QString();
        }
        tmats.replace(at, replacement.size(), replacement);
    }

    const QString path = dir.filePath("patched.ch10");
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly))
    {
        *why = "cannot write the patched copy";
        return QString();
    }
    out.write(tmats);
    out.write(all.mid(int(first_packet_length)));
    return path;
}

} // namespace

void TestChapter10Reader::tmatsTextDecidesWhichChannelsAreListedAndWhatTheyAreCalled()
{
    QFETCH(QStringList, edits);
    QFETCH(QStringList, pcm_list);
    QFETCH(QStringList, time_list);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString why;
    const QString path = writePatchedStepCal(dir, edits, &why);
    QVERIFY2(!path.isEmpty(), qPrintable(why));

    Chapter10Reader reader;
    const LoadResult result = load(reader, path);
    QVERIFY2(result.ok, qPrintable(result.errors.join("; ")));

    QCOMPARE(pcmList(reader), pcm_list);
    QCOMPARE(timeList(reader), time_list);
    QCOMPARE(reader.getFirstPCMChannelID(), pcm_list.isEmpty() ? -1 : 26);
    QCOMPARE(reader.getCurrentTimeChannelID(), time_list.isEmpty() ? -1 : 1);
}

void TestChapter10Reader::tmatsTextDecidesWhichChannelsAreListedAndWhatTheyAreCalled_data()
{
    QTest::addColumn<QStringList>("edits");
    QTest::addColumn<QStringList>("pcm_list");
    QTest::addColumn<QStringList>("time_list");

    const QStringList shown   = {"26 - CH-16 AGC STEP CALS 800K"};
    const QStringList none    = {};
    const QStringList irig    = {"1 - IRIG-B"};
    const QString che = R"(R-1\CHE-27:)";
    const QString cdt = R"(R-1\CDT-27:)";

    // "Channel enabled" (CHE-n): T, Y and 1 enable, in either case; F, N, 0 and anything
    // else disable, so the channel never appears; no CHE-n at all is treated as enabled.
    QTest::newRow("che-F")       << QStringList{che + "T;", che + "F;"} << none  << irig;
    QTest::newRow("che-N")       << QStringList{che + "T;", che + "N;"} << none  << irig;
    QTest::newRow("che-0")       << QStringList{che + "T;", che + "0;"} << none  << irig;
    QTest::newRow("che-Q")       << QStringList{che + "T;", che + "Q;"} << none  << irig;
    QTest::newRow("che-Y")       << QStringList{che + "T;", che + "Y;"} << shown << irig;
    QTest::newRow("che-1")       << QStringList{che + "T;", che + "1;"} << shown << irig;
    QTest::newRow("che-t")       << QStringList{che + "T;", che + "t;"} << shown << irig;
    QTest::newRow("che-y")       << QStringList{che + "T;", che + "y;"} << shown << irig;
    QTest::newRow("che-missing") << QStringList{R"(R-1\CHE-27:T;)", R"(R-1\CHX-27:T;)"} << shown << irig;

    // Whitespace around the flag is ignored. There is no spare byte, so the unused TK4-27
    // value gives one up ("27" -> "2"), which keeps the packet's length.
    const QString tk4 = QStringLiteral(R"(R-1\TK4-27:27;)") + "\r\n";
    const QString tk4_short = QStringLiteral(R"(R-1\TK4-27:2;)") + "\r\n";
    QTest::newRow("che-space-before") << QStringList{tk4 + che + "T;", tk4_short + che + " T;"} << shown << irig;
    QTest::newRow("che-space-after")  << QStringList{tk4 + che + "T;", tk4_short + che + "T ;"} << shown << irig;
    QTest::newRow("che-space-before-F") << QStringList{tk4 + che + "T;", tk4_short + che + " F;"} << none << irig;

    // Channel type (CDT-n): PCMIN, 01 and 09 are PCM; TIMEIN and 11 are time; the match is
    // case-sensitive and anything else is some other kind of channel that is not listed.
    QTest::newRow("type-01")     << QStringList{cdt + "PCMIN;", cdt + "01   ;"} << shown << irig;
    QTest::newRow("type-09")     << QStringList{cdt + "PCMIN;", cdt + "09   ;"} << shown << irig;
    QTest::newRow("type-pcmin-lower") << QStringList{cdt + "PCMIN;", cdt + "pcmin;"} << none << irig;
    QTest::newRow("type-other")  << QStringList{cdt + "PCMIN;", cdt + "XYZ  ;"} << none << irig;
    QTest::newRow("time-11")     << QStringList{R"(R-1\CDT-1:TIMEIN;)", R"(R-1\CDT-1:11    ;)"} << shown << irig;
    QTest::newRow("time-lower")  << QStringList{R"(R-1\CDT-1:TIMEIN;)", R"(R-1\CDT-1:timein;)"} << shown << none;

    // The channel's name: the data-source id (DSI-n) first, then the channel data link name
    // (CDLN-n), then the PCM link name (PDLN-n). The recording has the first two; PDLN is
    // made by renaming a same-length key (PDTF-27 -> PDLN-27).
    const QString dsi_off   = R"(R-1\DSI-27:)";
    const QString dsi_broken = R"(R-1\DSX-27:)";
    const QString cdln_on   = R"(R-1\CDLN-27:)";
    const QString cdln_broken = R"(R-1\CDLX-27:)";
    const QStringList pdln = {R"(R-1\PDTF-27:1;)", R"(R-1\PDLN-27:Q;)"};
    QTest::newRow("name-cdln-when-no-dsi")
        << QStringList{dsi_off, dsi_broken}
        << QStringList{"26 - MrgStream-16"} << irig;
    QTest::newRow("name-empty-when-neither")
        << QStringList{dsi_off, dsi_broken, cdln_on, cdln_broken}
        << QStringList{"26 - "} << irig;
    QTest::newRow("name-pdln-when-neither")
        << QStringList{dsi_off, dsi_broken, cdln_on, cdln_broken, pdln[0], pdln[1]}
        << QStringList{"26 - Q"} << irig;
    QTest::newRow("name-dsi-beats-pdln")
        << pdln << shown << irig;
    QTest::newRow("name-cdln-beats-pdln")
        << QStringList{dsi_off, dsi_broken, pdln[0], pdln[1]}
        << QStringList{"26 - MrgStream-16"} << irig;

    // A name outside ASCII is UTF-8 in the file (an accented letter is two bytes, so the edit
    // trades "H-" for it and keeps the length) and has to come out as the same text.
    QTest::newRow("name-non-ascii")
        << QStringList{QStringLiteral(R"(R-1\DSI-27:CH-16 AGC)"),
                       QStringLiteral(R"(R-1\DSI-27:C)") + QChar(0x00E9) + QStringLiteral("-1 AGC")}
        << QStringList{QStringLiteral("26 - C") + QChar(0x00E9) + QStringLiteral("-1 AGC STEP CALS 800K")}
        << irig;
}

void TestChapter10Reader::aFileThatFailsToOpenLeavesThePreviousChannelsAlone()
{
    Chapter10Reader reader;
    loadFixture(reader, "prn_testfile, sample.ch10");
    const QStringList before = pcmList(reader);

    const LoadResult failed = load(reader, QDir::tempPath() + "/tst_reader_missing.ch10");
    QVERIFY(!failed.ok);
    QCOMPARE(failed.errors.size(), 1);
    QCOMPARE(pcmList(reader), before);
}

void TestChapter10Reader::aTmatsDataRateIsReadLikeQStringToDouble()
{
    QFETCH(QString, value);       // six characters, replacing "800000" in P-16\D2
    QFETCH(double, expected);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString why;
    const QString path = writePatchedStepCal(
        dir, QStringList{QStringLiteral(R"(P-16\D2:800000;)"), QStringLiteral(R"(P-16\D2:)") + value + ";"}, &why);
    QVERIFY2(!path.isEmpty(), qPrintable(why));

    Chapter10Reader reader;
    const LoadResult result = load(reader, path);
    QVERIFY2(result.ok, qPrintable(result.errors.join("; ")));
    QCOMPARE(reader.getTmatsDataRateBps(26), expected);
}

void TestChapter10Reader::aTmatsDataRateIsReadLikeQStringToDouble_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<double>("expected");

    QTest::newRow("unchanged")        << "800000" << 800000.0;
    QTest::newRow("exponent")         << "8e5   " << 800000.0;
    QTest::newRow("leading-spaces")   << "  4000" << 4000.0;
    QTest::newRow("trailing-spaces")  << "4000  " << 4000.0;
    QTest::newRow("leading-plus")     << "+80000" << 80000.0;
    QTest::newRow("fraction")         << "1234.5" << 1234.5;
    // A rate that is not a positive number is "not described", not a rate.
    QTest::newRow("negative")         << "-80000" << 0.0;
    QTest::newRow("zero")             << "0     " << 0.0;
    QTest::newRow("junk")             << "80000x" << 0.0;
    QTest::newRow("word")             << "abcdef" << 0.0;
    QTest::newRow("group-separator")  << "1,0000" << 0.0;
    QTest::newRow("nan")              << "nan   " << 0.0;
}

void TestChapter10Reader::getFirstPcmChannelIdReturnsValidId()
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");

    int first_pcm = reader.getFirstPCMChannelID();
    QVERIFY2(first_pcm != -1, "getFirstPCMChannelID should return a valid ID after loading");
    QCOMPARE(first_pcm, reader.m_current_pcm_channel);
}
