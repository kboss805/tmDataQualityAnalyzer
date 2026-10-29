/**
 * @file tst_csvseries.cpp
 * @brief Implementation of the CSV series tests: what the plot does with an import and an export.
 */

#include "tst_csvseries.h"

#include <cmath>
#include <functional>
#include <vector>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest>

#include "plotviewmodel.h"
#include "processedstreamdata.h"

namespace {

// ---------------------------------------------------------------------------
// Helpers. Everything the tests do with the format goes through these, so a move of the
// code behind them changes these bodies and no assertion.
// ---------------------------------------------------------------------------

const QByteArray kTimeHeader = "Time (DOY:HH:MM:SS.mmm)";

/// Writes @p bytes to a fresh temp file and returns its path (the caller removes it).
QString writeBytes(const QByteArray& bytes)
{
    QTemporaryFile file;
    file.setAutoRemove(false);
    file.setFileTemplate(QDir::tempPath() + "/tst_csvseries_XXXXXX.csv");
    if (!file.open())
        return {};
    file.write(bytes);
    file.close();
    return file.fileName();
}

QByteArray readBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// A byte string with its control and non-ASCII bytes spelled out, so a mismatch reads in a log.
QString escaped(const QByteArray& bytes)
{
    QString out;
    for (const char c : bytes)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '\r')
            out += "\\r";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\\')
            out += "\\\\";
        else if (u < 0x20 || u >= 0x7F)
            out += QString("\\x%1").arg(u, 2, 16, QChar('0'));
        else
            out += QChar(u);
    }
    return out;
}

/// Text with its non-ASCII characters spelled out as <U+XXXX>.
QString asciiOnly(const QString& s)
{
    QString out;
    for (const QChar c : s)
        out += (c.unicode() < 0x80) ? QString(c) : QString("<U+%1>").arg(uint(c.unicode()), 4, 16, QChar('0')).toUpper();
    return out;
}

QString num(double v)
{
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0 ? "inf" : "-inf";
    return QString::number(v, 'g', 12);
}

/// Everything an import produces, as one line per fact: whether it loaded, the base time, the
/// span, and every series' identity and samples.
QString importDigest(const QByteArray& bytes)
{
    const QString path = writeBytes(bytes);
    PlotViewModel vm;
    QSignalSpy warnings(&vm, &PlotViewModel::loadWarning);
    const bool ok = vm.loadCsvFile(path);
    QFile::remove(path);
    if (!ok)
        return "fail";

    QString out = QString("ok skipped=%1 base=%2/%3 xmax=%4 n=%5")
                      .arg(warnings.isEmpty() ? 0
                                              : warnings.first().first().toString().section(' ', 1, 1).toInt())
                      .arg(vm.baseDay())
                      .arg(num(vm.baseTimeOffset()))
                      .arg(num(vm.xMax()))
                      .arg(vm.seriesCount());
    for (int i = 0; i < vm.seriesCount(); ++i)
    {
        const PlotSeriesData& s = vm.seriesAt(i);
        const char metric = s.metricType == PlotSeriesData::MetricType::SNR            ? 'S'
                            : s.metricType == PlotSeriesData::MetricType::FrameSyncLock ? 'L'
                                                                                        : 'M';
        out += QString(" | [%1] %2 label=%3 %4 order=%5 rcv=%6 ch=%7 src=%8 :")
                   .arg(i)
                   .arg(asciiOnly(s.name))
                   .arg(asciiOnly(s.streamLabel))
                   .arg(metric)
                   .arg(s.streamOrder)
                   .arg(s.receiverIndex)
                   .arg(s.channelIndex)
                   .arg(s.sourceId);
        for (int j = 0; j < s.xValues.size(); ++j)
            out += QString(" %1,%2").arg(num(s.xValues[j]), num(s.yValues[j]));
    }
    return out;
}

/// The exact bytes the exporter writes for the view model, or "none" if it declined.
QString exportDigest(const PlotViewModel& vm, int source_id = -1)
{
    const QString path = QDir::tempPath() + "/tst_csvseries_export.csv";
    QFile::remove(path);
    if (!vm.exportCsv(path, source_id))
        return QFile::exists(path) ? "none (but a file was left)" : "none";
    const QString out = escaped(readBytes(path));
    QFile::remove(path);
    return out;
}

ProcessedStreamData lockStream(const QString& label, int channel, const std::vector<double>& times,
                               const std::vector<double>& lock, const std::vector<double>& missed,
                               int source_id = 0)
{
    ProcessedStreamData d;
    d.streamLabel = label.toStdString();
    d.pcmChannelId = channel;
    d.sourceId = source_id;
    d.mode = StreamMode::FrameSyncLockStats;
    d.timesSec = times;
    d.lockPercent = lock;
    d.accumulatedMissedFrames = missed;
    return d;
}

// A base time of day 3, 07:08:09, in absolute seconds; every figure below is exactly representable.
constexpr double kBase = 3 * 86400.0 + 7 * 3600.0 + 8 * 60.0 + 9.0;

const QByteArray kOneLock = kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\n1:00:00:01.000,60\n";

// ---------------------------------------------------------------------------
// Import cases: the rows whose answer shows what the plot adds to the parser's (the warning's count, the clock it
// takes from the first row, the order each stream is given). The format itself is tested in
// native/tmdq_core_tests/test_csvseries.cpp.
// ---------------------------------------------------------------------------

struct ImportCase
{
    const char* name;
    QByteArray bytes;
    const char* expect;
};

const QList<ImportCase>& importCases()
{
    static const QList<ImportCase> cases = {
        { "basic-lock", kOneLock, "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "empty-file", QByteArray(), "fail" },
        { "every-row-bad", kTimeHeader + ",A Lock (%)\nnot a time,1\nalso not,2\n", "fail" },
        { "non-ascii-names", kTimeHeader + ",R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] R<U+00E9>cepteur <U+03A9> label=R<U+00E9>cepteur <U+03A9> L order=1 rcv=0 ch=0 src=0 : 0,50" },
        { "all-three-kinds", kTimeHeader + ",A Lock (%),A Accumulated Missed Frames,40 - Ch A L_RCVR2\n"
                                              "1:00:00:00.000,90,0,-80.5\n1:00:00:01.000,91,1,-79.5\n", "ok skipped=0 base=1/0 xmax=1 n=3"
            " | [0] A label=A L order=41 rcv=0 ch=0 src=0 : 0,90 1,91"
            " | [1] A label=A M order=41 rcv=0 ch=0 src=0 : 0,0 1,1"
            " | [2] 40 - Ch A L_RCVR2 label=Ch A S order=40 rcv=2 ch=0 src=0 : 0,-80.5 1,-79.5" },
        { "sources-keep-their-own-channel-counts",
          kTimeHeader + ",40 - A L_RCVR1,40 - A R_RCVR1,S1| 40 - A L_RCVR1,S1| 40 - A R_RCVR1\n"
                        "1:00:00:00.000,1,2,3,4\n", "ok skipped=0 base=1/0 xmax=0 n=4"
            " | [0] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=0 : 0,1"
            " | [1] 40 - A R_RCVR1 label=A S order=40 rcv=1 ch=1 src=0 : 0,2"
            " | [2] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=1 : 0,3"
            " | [3] 40 - A R_RCVR1 label=A S order=40 rcv=1 ch=1 src=1 : 0,4" },
        { "lock-only-streams-get-orders-after-the-snr-ones",
          kTimeHeader + ",Zed Lock (%),40 - A L_RCVR1,A Lock (%),Zed Accumulated Missed Frames\n"
                        "1:00:00:00.000,1,2,3,4\n", "ok skipped=0 base=1/0 xmax=0 n=4"
            " | [0] Zed label=Zed L order=41 rcv=0 ch=0 src=0 : 0,1"
            " | [1] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=0 : 0,2"
            " | [2] A label=A L order=40 rcv=0 ch=0 src=0 : 0,3"
            " | [3] Zed label=Zed M order=41 rcv=0 ch=0 src=0 : 0,4" },
        { "two-sources-with-one-label",
          kTimeHeader + ",A Lock (%),S2| A Lock (%),S2| A Accumulated Missed Frames\n"
                        "1:00:00:00.000,1,2,3\n", "ok skipped=0 base=1/0 xmax=0 n=3"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1"
            " | [1] A label=A L order=2 rcv=0 ch=0 src=2 : 0,2"
            " | [2] A label=A M order=2 rcv=0 ch=0 src=2 : 0,3" },

        // --- the data rows
        { "short-row-skipped", kTimeHeader + ",A Lock (%),B Lock (%)\n1:00:00:00.000,1,2\n1:00:00:01.000,3\n1:00:00:02.000,5,6\n", "ok skipped=1 base=1/0 xmax=2 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 2,5"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 0,2 2,6" },
        { "first-good-row-sets-the-base", kTimeHeader + ",A Lock (%)\nbad,1\n2:03:04:05.250,2\n2:03:04:06.250,3\n", "ok skipped=1 base=2/11045.25 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2 1,3" },
    };
    return cases;
}

}  // namespace

void TestCsvSeries::importEdgeCasesKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < importCases().size(); ++i)
        QTest::newRow(importCases()[i].name) << i;
}

void TestCsvSeries::importEdgeCasesKeepTheirRecordedBehaviour()
{
    QFETCH(int, index);
    const ImportCase& c = importCases()[index];
    const QString got = importDigest(c.bytes);
    QCOMPARE(got, QString::fromLatin1(c.expect));
}


// ---------------------------------------------------------------------------
// Export cases: the rows that show how the plot's series become columns (every series, one source's, none).
// The bytes the writer makes of a column are tested in native/tmdq_core_tests/test_csvseries.cpp.
// ---------------------------------------------------------------------------

namespace {

struct ExportCase
{
    const char* name;
    std::function<void(PlotViewModel&)> build;
    int source_id;
    const char* expect;
};

const QList<ExportCase>& exportCases()
{
    static const QList<ExportCase> cases = {
        { "one-stream",
          [](PlotViewModel& vm) { vm.addStreamData(lockStream("Ch 5", 5, { kBase, kBase + 1, kBase + 2 }, { 90, 95, 100 }, { 0, 1, 1 })); },
          -1, "Time (DOY:HH:MM:SS.mmm),Ch 5 Lock (%),Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,90.000000,0.000000\\r\\n"
            "004:07:08:10.000,95.000000,1.000000\\r\\n"
            "004:07:08:11.000,100.000000,1.000000\\r\\n"
            "" },
        { "two-streams-on-different-clocks-leave-cells-empty",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1 }, { 10, 20 }, { 0, 1 }));
              vm.addStreamData(lockStream("B", 2, { kBase + 0.5, kBase + 1 }, { 30, 40 }, { 2, 3 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames,B Lock (%),B Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,10.000000,0.000000,,\\r\\n"
            "004:07:08:09.500,,,30.000000,2.000000\\r\\n"
            "004:07:08:10.000,20.000000,1.000000,40.000000,3.000000\\r\\n"
            "" },
        { "a-stream-with-no-missed-frames-series",
          [](PlotViewModel& vm) { vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1 }, { 1, 2 }, {})); },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
            "004:07:08:09.000,1.000000\\r\\n"
            "004:07:08:10.000,2.000000\\r\\n"
            "" },
        { "an-empty-and-a-non-ascii-label",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("", 1, { kBase, kBase + 1 }, { 1, 2 }, { 0, 0 }));
              vm.addStreamData(lockStream(QString::fromUtf8("R\xC3\xA9" "cepteur \xCE\xA9"), 2, { kBase, kBase + 1 }, { 3, 4 }, { 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm), Lock (%), Accumulated Missed Frames,R\\xc3\\xa9cepteur \\xce\\xa9 Lock (%),R\\xc3\\xa9cepteur \\xce\\xa9 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000,3.000000,0.000000\\r\\n"
            "004:07:08:10.000,2.000000,0.000000,4.000000,0.000000\\r\\n"
            "" },
        { "two-sources-all",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("Ch 5", 5, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }, 0));
              vm.addStreamData(lockStream("Ch 5", 5, { kBase + 1, kBase + 2 }, { 3, 4 }, { 0, 2 }, 2));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),Ch 5 Lock (%),Ch 5 Accumulated Missed Frames,S2| Ch 5 Lock (%),S2| Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000,,\\r\\n"
            "004:07:08:10.000,2.000000,1.000000,3.000000,0.000000\\r\\n"
            "004:07:08:11.000,,,4.000000,2.000000\\r\\n"
            "" },
        { "two-sources-first-only",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("Ch 5", 5, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }, 0));
              vm.addStreamData(lockStream("Ch 5", 5, { kBase + 1, kBase + 2 }, { 3, 4 }, { 0, 2 }, 2));
          },
          0, "Time (DOY:HH:MM:SS.mmm),Ch 5 Lock (%),Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000\\r\\n"
            "004:07:08:10.000,2.000000,1.000000\\r\\n"
            "" },
        { "two-sources-second-only",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("Ch 5", 5, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }, 0));
              vm.addStreamData(lockStream("Ch 5", 5, { kBase + 1, kBase + 2 }, { 3, 4 }, { 0, 2 }, 2));
          },
          2, "Time (DOY:HH:MM:SS.mmm),S2| Ch 5 Lock (%),S2| Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:10.000,3.000000,0.000000\\r\\n"
            "004:07:08:11.000,4.000000,2.000000\\r\\n"
            "" },
        { "a-source-nothing-belongs-to",
          [](PlotViewModel& vm) { vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 })); },
          7, "none" },
        { "no-series-at-all", [](PlotViewModel&) {}, -1, "none" },
    };
    return cases;
}

}  // namespace

void TestCsvSeries::exportEdgeCasesKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < exportCases().size(); ++i)
        QTest::newRow(exportCases()[i].name) << i;
}

void TestCsvSeries::exportEdgeCasesKeepTheirRecordedBehaviour()
{
    QFETCH(int, index);
    const ExportCase& c = exportCases()[index];
    PlotViewModel vm;
    c.build(vm);
    const QString got = exportDigest(vm, c.source_id);
    QCOMPARE(got, QString::fromLatin1(c.expect));
}

void TestCsvSeries::csvPathsMayBeNonAscii()
{
    // A folder whose name no Western code page spells: written and read back through the core's paths.
    const QString dir = QDir::tempPath() + QString::fromUtf8("/caf\xC3\xA9-\xCE\xA9_tmdq_csv");
    QDir().mkpath(dir);
    const QString path = dir + QString::fromUtf8("/r\xC3\xA9sum\xC3\xA9.csv");

    PlotViewModel vm;
    vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }));
    QVERIFY(vm.exportCsv(path));
    QVERIFY(QFile::exists(path));

    PlotViewModel reloaded;
    QVERIFY(reloaded.loadCsvFile(path));
    QCOMPARE(reloaded.seriesCount(), 2);
    QDir(dir).removeRecursively();
}

void TestCsvSeries::exportOfNothingWritesNothing()
{
    // Declined before the file is opened: no series, or none for the asked-for source, leaves no file.
    PlotViewModel empty;
    const QString path = QDir::tempPath() + "/tst_csvseries_nothing.csv";
    QFile::remove(path);
    QVERIFY(!empty.exportCsv(path));
    QVERIFY(!QFile::exists(path));

    PlotViewModel vm;
    vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }));
    QVERIFY(!vm.exportCsv(path, 9));
    QVERIFY(!QFile::exists(path));
}

void TestCsvSeries::exportToAnUnwritablePathFails()
{
    PlotViewModel vm;
    vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }));
    QVERIFY(!vm.exportCsv(QDir::tempPath() + "/no_such_folder_for_tmdq/out.csv"));
}

void TestCsvSeries::exportThenImportThenExportIsByteIdentical()
{
    // The format's two directions are inverses: what the exporter writes the importer reads back
    // into series that export to the same bytes. Receiver SNR columns, two sources and a sparse
    // column are all in it.
    const QByteArray canonical =
        kTimeHeader + ",Ch 5 Lock (%),Ch 5 Accumulated Missed Frames,40 - Ch 5 L_RCVR1,40 - Ch 5 R_RCVR1,"
                      "S2| Ch 5 Lock (%),S2| 40 - Ch 5 L_RCVR2\r\n"
                      "003:07:08:09.000,90.000000,0.000000,-80.500000,-70.250000,,\r\n"
                      "003:07:08:10.000,91.000000,1.000000,,-71.000000,80.000000,-60.125000\r\n"
                      "003:07:08:11.500,,,-79.500000,,81.000000,\r\n";
    const QString in = writeBytes(canonical);
    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(in));
    QFile::remove(in);
    QCOMPARE(escaped(canonical), exportDigest(vm));
}
