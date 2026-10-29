/**
 * @file tst_seriesbuild.cpp
 * @brief Implementation of the tests of what the plot does with processed streams and imported files.
 */

#include "tst_seriesbuild.h"

#include <cmath>
#include <functional>
#include <vector>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include "plotviewmodel.h"
#include "processedstreamdata.h"

namespace {

// ---------------------------------------------------------------------------
// Helpers. Everything the tests do with the plot's data goes through these, so a move of the code behind
// them changes these bodies and no assertion.
// ---------------------------------------------------------------------------

QString num(double v)
{
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0 ? "inf" : "-inf";
    return QString::number(v, 'g', 17);
}

QString asciiOnly(const QString& s)
{
    QString out;
    for (const QChar c : s)
        out += (c.unicode() < 0x80) ? QString(c) : QString("<U+%1>").arg(uint(c.unicode()), 4, 16, QChar('0')).toUpper();
    return out;
}

QString joined(const QVector<double>& values)
{
    QStringList parts;
    for (const double v : values)
        parts << num(v);
    return parts.join(',');
}

/// Everything the plot's data layer decides, as one line: the time base, the span, the flags, the warnings
/// raised, and every series' identity, indices, cached range and samples. Presentation (colour,
/// visibility) is deliberately not in it.
QString format(int day, double offset, double xmin, double xmax, double view_min, double view_max, bool lock,
               bool missed, const QList<int>& warnings, const QVector<PlotSeriesData>& series)
{
    QStringList warn;
    for (const int w : warnings)
        warn << QString::number(w);
    QString out = QString("base=%1/%2 x=%3..%4 view=%5..%6 lock=%7 missed=%8 warn=[%9] n=%10")
                      .arg(day).arg(num(offset)).arg(num(xmin), num(xmax)).arg(num(view_min), num(view_max))
                      .arg(lock).arg(missed).arg(warn.join(",")).arg(series.size());
    for (int i = 0; i < series.size(); ++i)
    {
        const PlotSeriesData& s = series[i];
        out += QString(" | [%1] id=%2 '%3' label='%4' M%5 ord=%6 seq=%7 src=%8 rcv=%9 ch=%10 y=%11..%12 x=%13 y=%14")
                   .arg(i).arg(s.id).arg(asciiOnly(s.name), asciiOnly(s.streamLabel)).arg(int(s.metricType))
                   .arg(s.streamOrder).arg(s.streamSequence).arg(s.sourceId).arg(s.receiverIndex).arg(s.channelIndex)
                   .arg(num(s.yMinCached), num(s.yMaxCached)).arg(joined(s.xValues), joined(s.yValues));
    }
    return out;
}

QString digest(const PlotViewModel& vm, const QList<int>& warnings)
{
    return format(vm.baseDay(), vm.baseTimeOffset(), vm.xMin(), vm.xMax(), vm.xViewMin(), vm.xViewMax(),
                  vm.hasLockSeries(), vm.hasMissedFramesSeries(), warnings, vm.allSeries());
}

struct Ch
{
    const char* name;
    std::vector<double> values;
};

ProcessedStreamData stream(const std::string& label, int channel, int source, int job, std::vector<double> times,
                           std::vector<double> lock, std::vector<double> missed, std::vector<Ch> channels = {})
{
    ProcessedStreamData d;
    d.streamLabel = label;
    d.pcmChannelId = channel;
    d.sourceId = source;
    d.jobIndex = job;
    d.mode = channels.empty() ? StreamMode::FrameSyncLockStats : StreamMode::ReceiverChannelInfo;
    d.timesSec = std::move(times);
    d.lockPercent = std::move(lock);
    d.accumulatedMissedFrames = std::move(missed);
    for (const Ch& c : channels)
    {
        ProcessedChannelSeries s;
        s.name = c.name;
        s.values = c.values;
        d.channels.push_back(std::move(s));
    }
    return d;
}

constexpr double kT0 = 1700000000.0;   // 2023-11-14 22:13:20 UTC, day 318

/// Writes @p csv to a temp file and imports it.
void importCsv(PlotViewModel& vm, const QByteArray& csv)
{
    const QString path = QDir::tempPath() + "/tst_seriesbuild_import.csv";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(csv);
    f.close();
    vm.loadCsvFile(path);
    QFile::remove(path);
}

using Scenario = std::function<void(PlotViewModel&)>;

struct Case
{
    const char* name;
    Scenario run;
    const char* expect;
};

const QByteArray kHeader = "Time (DOY:HH:MM:SS.mmm)";
const QList<Case>& cases()
{
    // The rows whose answer shows something the plot does: it drops a stream's earlier series, judges overlap, moves the
    // base, orders its legend, removes a source and moves the base back, keeps counting ids and takes an import in.
    // Every scenario, and the generated sequences, are run on the core's functions in
    // native/tmdq_core_tests/test_seriesbuild.cpp.
    static const QList<Case> list = {
        { "a-lock-only-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 90, 95, 100 }, {})); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=90..100 x=0,1,2 y=90,95,100" },
        { "lock-and-missed-frames", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1 }, { 90, 95 }, { 0, 3 })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=1 warn=[] n=2"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=90..95 x=0,1 y=90,95"
            " | [1] id=2 'Ch 5' label='Ch 5' M2 ord=5 seq=0 src=0 rcv=0 ch=0 y=0..3 x=0,1 y=0,3" },
        { "a-receiver-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Rcvr", 40, 0, 0, { kT0, kT0 + 1 }, { 99, 98 }, { 0, 1 },
                                      { { "L_RCVR1", { -80, -79 } }, { "R_RCVR1", { -81, -78 } }, { "C_RCVR1", { -82, -77 } },
                                        { "L_RCVR2", { -70, -71 } }, { "R_RCVR2", { -72, -73 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=1 warn=[] n=7"
            " | [0] id=1 'Rcvr' label='Rcvr' M1 ord=40 seq=0 src=0 rcv=0 ch=0 y=98..99 x=0,1 y=99,98"
            " | [1] id=2 'Rcvr' label='Rcvr' M2 ord=40 seq=0 src=0 rcv=0 ch=0 y=0..1 x=0,1 y=0,1"
            " | [2] id=3 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=-80..-79 x=0,1 y=-80,-79"
            " | [3] id=4 '40 - Rcvr R_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=1 y=-81..-78 x=0,1 y=-81,-78"
            " | [4] id=5 '40 - Rcvr C_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=2 y=-82..-77 x=0,1 y=-82,-77"
            " | [5] id=6 '40 - Rcvr L_RCVR2' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=2 ch=0 y=-71..-70 x=0,1 y=-70,-71"
            " | [6] id=7 '40 - Rcvr R_RCVR2' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=2 ch=1 y=-73..-72 x=0,1 y=-72,-73" },
        { "reprocessing-replaces-the-same-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, { 0, 0 }));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 9, 9, 9 }, {})); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=9..9 x=0,1,2 y=9,9,9" },
        { "streams-are-kept-in-job-order", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("C", 3, 0, 2, { kT0, kT0 + 1 }, { 3, 3 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=3"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=3 'B' label='B' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2"
            " | [2] id=1 'C' label='C' M1 ord=3 seq=2 src=0 rcv=0 ch=0 y=3..3 x=0,1 y=3,3" },
        { "a-later-source-starts-before-so-the-others-shift", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 10, kT0 + 20 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {})); }), "base=318/80004 x=0..16 view=0..16 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=6,16 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,2 y=2,2" },
        { "overlapping-sources-raise-no-warning", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 5, kT0 + 15 }, { 2, 2 }, {})); }), "base=318/80000 x=0..15 view=0..15 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=5,15 y=2,2" },
        { "the-warning-is-raised-once-per-source", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 1000, kT0 + 1010 }, { 2, 2 }, {}));
              vm.addStreamData(stream("B", 2, 1, 1, { kT0 + 2000, kT0 + 2010 }, { 3, 3 }, {})); }), "base=318/80000 x=0..2010 view=0..2010 lock=1 missed=0 warn=[1] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=1000,1010 y=2,2"
            " | [2] id=3 'B' label='B' M1 ord=2 seq=1 src=1 rcv=0 ch=0 y=3..3 x=2000,2010 y=3,3" },
        { "remove-the-earliest-source", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {}));
              vm.removeSource(0); }), "base=318/80004 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,2 y=2,2" },
        { "remove-the-only-source", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.removeSource(0); }), "base=0/0 x=0..0 view=0..0 lock=0 missed=0 warn=[] n=0" },
        { "remove-one-of-three-sources", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 100, kT0 + 110 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 50, kT0 + 60 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 10, kT0 + 20 }, { 3, 3 }, {}));
              vm.removeSource(2); }), "base=318/80050 x=0..60 view=0..60 lock=1 missed=0 warn=[1,2] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=50,60 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },
        { "clear-then-add-again", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.clearData();
              vm.addStreamData(stream("B", 2, 0, 0, { kT0 + 500, kT0 + 510 }, { 2, 2 }, {})); }), "base=318/80500 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'B' label='B' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },
        { "ids-keep-counting-after-a-removal", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, { 0, 0 }));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 10 }, { 2, 2 }, { 0, 0 }));
              vm.removeSource(0);
              vm.addStreamData(stream("B", 2, 2, 1, { kT0, kT0 + 10 }, { 3, 3 }, {})); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=1 warn=[] n=3"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,10 y=2,2"
            " | [1] id=4 'A' label='A' M2 ord=1 seq=0 src=1 rcv=0 ch=0 y=0..0 x=0,10 y=0,0"
            " | [2] id=5 'B' label='B' M1 ord=2 seq=1 src=2 rcv=0 ch=0 y=3..3 x=0,10 y=3,3" },
        { "import-lock-and-missed-of-one-stream-share-an-order", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",A Lock (%),B Lock (%),B Accumulated Missed Frames,A Accumulated Missed Frames\n"
                                      "318:22:13:20.000,1,2,3,4\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=1 warn=[] n=4"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 'B' label='B' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'B' label='B' M2 ord=2 seq=0 src=0 rcv=0 ch=0 y=3..3 x=0 y=3"
            " | [3] id=4 'A' label='A' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=4..4 x=0 y=4" },
        { "import-after-streams-replaces-them", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              importCsv(vm, kHeader + ",B Lock (%)\n318:22:13:20.000,1\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'B' label='B' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1" },
        { "import-keeps-the-files-own-clock", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",A Lock (%)\n"
                                      "003:07:08:09.250,1\n003:07:08:11.500,2\n"); }), "base=3/25689.25 x=0..2.25 view=0..2.25 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,2.25 y=1,2" },
    };
    return list;
}

}  // namespace

void TestSeriesBuild::scenariosKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < cases().size(); ++i)
        QTest::newRow(cases()[i].name) << i;
}

void TestSeriesBuild::scenariosKeepTheirRecordedBehaviour()
{
    QFETCH(int, index);
    const Case& c = cases()[index];

    PlotViewModel vm;
    QList<int> warnings;
    QObject::connect(&vm, &PlotViewModel::nonOverlappingSourceWarning, &vm, [&warnings](int source) { warnings << source; });
    c.run(vm);
    const QString got = digest(vm, warnings);

    QCOMPARE(got, QString::fromLatin1(c.expect));
}
