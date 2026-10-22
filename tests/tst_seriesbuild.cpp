/**
 * @file tst_seriesbuild.cpp
 * @brief Implementation of the series-construction and time-base tests.
 */

#include "tst_seriesbuild.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#include <QDir>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

#include "plotviewmodel.h"
#include "processedstreamdata.h"
#include "seriesbuilder.h"
#include "seriescolumnschema.h"

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
    static const QList<Case> list = {
        // --- what one stream becomes
        { "a-lock-only-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 90, 95, 100 }, {})); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=90..100 x=0,1,2 y=90,95,100" },
        { "lock-and-missed-frames", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1 }, { 90, 95 }, { 0, 3 })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=1 warn=[] n=2"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=90..95 x=0,1 y=90,95"
            " | [1] id=2 'Ch 5' label='Ch 5' M2 ord=5 seq=0 src=0 rcv=0 ch=0 y=0..3 x=0,1 y=0,3" },
        { "missed-frames-only", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1 }, {}, { 0, 3 })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=1 warn=[] n=1"
            " | [0] id=1 'Ch 5' label='Ch 5' M2 ord=5 seq=0 src=0 rcv=0 ch=0 y=0..3 x=0,1 y=0,3" },
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
        { "a-receiver-stream-without-lock-series", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Rcvr", 40, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { -80, -79 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=1"
            " | [0] id=1 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=-80..-79 x=0,1 y=-80,-79" },
        { "a-stream-with-no-samples-is-ignored", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, {}, { 1 }, { 2 })); }), "base=0/0 x=0..0 view=0..0 lock=0 missed=0 warn=[] n=0" },
        { "a-stream-with-no-samples-leaves-the-others", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1 }, { 1, 2 }, {}));
              vm.addStreamData(stream("Ch 6", 6, 0, 1, {}, { 1 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2" },
        { "the-unlabelled-and-the-non-ascii-label", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("", 5, 0, 0, { kT0, kT0 + 1 }, { 1, 2 }, {}));
              vm.addStreamData(stream("R\xC3\xA9" "cepteur \xCE\xA9", 6, 0, 1, { kT0, kT0 + 1 }, { 3, 4 }, {},
                                      { { "L_RCVR1", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=3"
            " | [0] id=1 '' label='' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 'R<U+00E9>cepteur <U+03A9>' label='R<U+00E9>cepteur <U+03A9>' M1 ord=6 seq=1 src=0 rcv=0 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '6 - R<U+00E9>cepteur <U+03A9> L_RCVR1' label='R<U+00E9>cepteur <U+03A9>' M0 ord=6 seq=1 src=0 rcv=1 ch=0 y=5..6 x=0,1 y=5,6" },

        // --- the receiver number and channel index of an SNR series
        { "receiver-number-from-the-last-token", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "X_RCVR2_RCVR3", { 1, 2 } }, { "_RCVR5", { 3, 4 } }, { "L_RCVR12", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S X_RCVR2_RCVR3' label='S' M0 ord=1 seq=0 src=0 rcv=3 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S _RCVR5' label='S' M0 ord=1 seq=0 src=0 rcv=5 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR12' label='S' M0 ord=1 seq=0 src=0 rcv=12 ch=0 y=5..6 x=0,1 y=5,6" },
        { "no-receiver-token", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "NAME", { 1, 2 } }, { "rcvr3", { 3, 4 } }, { "", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S NAME' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S rcvr3' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S ' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=2 y=5..6 x=0,1 y=5,6" },
        { "receiver-token-without-a-number", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR", { 1, 2 } }, { "L_RCVRx", { 3, 4 } }, { "L_RCVR2x", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S L_RCVR' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S L_RCVRx' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR2x' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=2 y=5..6 x=0,1 y=5,6" },
        { "receiver-number-spelled-oddly", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR 7", { 1, 2 } }, { "L_RCVR+4", { 3, 4 } }, { "L_RCVR-2", { 5, 6 } },
                                        { "L_RCVR0", { 7, 8 } }, { "L_RCVR+-3", { 9, 10 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=5"
            " | [0] id=1 '1 - S L_RCVR 7' label='S' M0 ord=1 seq=0 src=0 rcv=7 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S L_RCVR+4' label='S' M0 ord=1 seq=0 src=0 rcv=4 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR-2' label='S' M0 ord=1 seq=0 src=0 rcv=-2 ch=0 y=5..6 x=0,1 y=5,6"
            " | [3] id=4 '1 - S L_RCVR0' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=7..8 x=0,1 y=7,8"
            " | [4] id=5 '1 - S L_RCVR+-3' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=9..10 x=0,1 y=9,10" },
        { "receiver-number-out-of-range", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR2147483647", { 1, 2 } }, { "L_RCVR2147483648", { 3, 4 } }, { "L_RCVR99999999999", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S L_RCVR2147483647' label='S' M0 ord=1 seq=0 src=0 rcv=2147483647 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S L_RCVR2147483648' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR99999999999' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=5..6 x=0,1 y=5,6" },
        { "receiver-token-with-non-ascii-text", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "\xCE\xA9_RCVR2", { 1, 2 } }, { "\xCE\xA9\xC3\xA9", { 3, 4 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 '1 - S <U+03A9>_RCVR2' label='S' M0 ord=1 seq=0 src=0 rcv=2 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S <U+03A9><U+00E9>' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=3..4 x=0,1 y=3,4" },
        { "channel-index-counts-within-a-receiver", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR1", { 1, 2 } }, { "R_RCVR1", { 3, 4 } }, { "L_RCVR2", { 5, 6 } },
                                        { "C_RCVR1", { 7, 8 } }, { "R_RCVR2", { 9, 10 } }, { "NAME", { 11, 12 } }, { "OTHER", { 13, 14 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=7"
            " | [0] id=1 '1 - S L_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S R_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR2' label='S' M0 ord=1 seq=0 src=0 rcv=2 ch=0 y=5..6 x=0,1 y=5,6"
            " | [3] id=4 '1 - S C_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=2 y=7..8 x=0,1 y=7,8"
            " | [4] id=5 '1 - S R_RCVR2' label='S' M0 ord=1 seq=0 src=0 rcv=2 ch=1 y=9..10 x=0,1 y=9,10"
            " | [5] id=6 '1 - S NAME' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=11..12 x=0,1 y=11,12"
            " | [6] id=7 '1 - S OTHER' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=13..14 x=0,1 y=13,14" },
        { "channel-index-restarts-for-each-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 1, 2 } }, { "R_RCVR1", { 3, 4 } } }));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - A L_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - A R_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '2 - B L_RCVR1' label='B' M0 ord=2 seq=1 src=0 rcv=1 ch=0 y=5..6 x=0,1 y=5,6" },
        { "the-snr-name-carries-the-channel-and-label", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("Safran 2", 40, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 1, 2 } } }));
              vm.addStreamData(stream("Safran 2", 41, 1, 1, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 3, 4 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 '40 - Safran 2 L_RCVR1' label='Safran 2' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '41 - Safran 2 L_RCVR1' label='Safran 2' M0 ord=41 seq=1 src=1 rcv=1 ch=0 y=3..4 x=0,1 y=3,4" },

        // --- sample arrays that do not line up
        { "lock-shorter-than-the-times", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 1, 2 }, { 5, 6, 7 })); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=1 warn=[] n=2"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1,2 y=1,2"
            " | [1] id=2 'S' label='S' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=5..7 x=0,1,2 y=5,6,7" },
        { "lock-longer-than-the-times", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 2, 3 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,1 y=1,2,3" },
        { "a-channel-with-no-values", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", {} }, { "R_RCVR1", { 1, 2 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 '1 - S L_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1.7976931348623157e+308..-1.7976931348623157e+308 x=0,1 y="
            " | [1] id=2 '1 - S R_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=1 y=1..2 x=0,1 y=1,2" },
        { "values-with-not-a-number-and-infinity", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2, kT0 + 3 },
                                      { std::numeric_limits<double>::quiet_NaN(), 5, std::numeric_limits<double>::infinity(), -1 }, {},
                                      { { "L_RCVR1", { -std::numeric_limits<double>::infinity(), 2, std::numeric_limits<double>::quiet_NaN(), 1 } } })); }), "base=318/80000 x=0..3 view=0..3 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=-1..inf x=0,1,2,3 y=nan,5,inf,-1"
            " | [1] id=2 '1 - S L_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1,2,3 y=-inf,2,nan,1" },
        { "times-out-of-order", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0 + 5, kT0, kT0 + 2 }, { 1, 2, 3 }, {})); }), "base=318/80005 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,-5,-3 y=1,2,3" },
        { "repeated-times", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0, kT0 }, { 1, 2, 3 }, {})); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,0,0 y=1,2,3" },
        { "a-single-sample", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0 }, { 50 }, { 0 })); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=1 warn=[] n=2"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=50..50 x=0 y=50"
            " | [1] id=2 'S' label='S' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=0..0 x=0 y=0" },

        // --- the order series are kept in
        { "streams-are-kept-in-job-order", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("C", 3, 0, 2, { kT0, kT0 + 1 }, { 3, 3 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=3"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=3 'B' label='B' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2"
            " | [2] id=1 'C' label='C' M1 ord=3 seq=2 src=0 rcv=0 ch=0 y=3..3 x=0,1 y=3,3" },
        { "equal-job-indices-keep-their-arrival-order", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("B", 2, 0, 0, { kT0, kT0 + 1 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("C", 3, 1, 0, { kT0, kT0 + 1 }, { 3, 3 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=3"
            " | [0] id=1 'B' label='B' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [2] id=3 'C' label='C' M1 ord=3 seq=0 src=1 rcv=0 ch=0 y=3..3 x=0,1 y=3,3" },
        { "negative-job-index", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 1, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, -1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=2 'B' label='B' M1 ord=2 seq=-1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2"
            " | [1] id=1 'A' label='A' M1 ord=1 seq=1 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },

        // --- running a stream again
        { "reprocessing-replaces-the-same-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, { 0, 0 }));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 9, 9, 9 }, {})); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=9..9 x=0,1,2 y=9,9,9" },
        { "a-different-label-is-another-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 1, 0, 1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=2 'B' label='B' M1 ord=1 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "a-different-channel-is-another-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 2, 0, 1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "a-different-source-is-another-stream", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "a-stream-changes-from-lock-only-to-receiver", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, { 0, 0 }));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}, { { "L_RCVR1", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=4 '1 - A L_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=5..6 x=0,1 y=5,6" },
        { "reprocessing-the-first-stream-can-move-the-base-later", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0 + 5, kT0 + 6 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 20, kT0 + 21 }, { 3, 3 }, {})); }), "base=318/80000 x=0..21 view=0..21 lock=1 missed=0 warn=[] n=2"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=3..3 x=20,21 y=3,3"
            " | [1] id=2 'B' label='B' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=5,6 y=2,2" },

        // --- the shared time axis across sources
        { "a-later-source-starts-after", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {})); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=4,6 y=2,2" },
        { "a-later-source-starts-before-so-the-others-shift", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 10, kT0 + 20 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {})); }), "base=318/80004 x=0..16 view=0..16 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=6,16 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,2 y=2,2" },
        { "a-source-starting-at-the-same-instant", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 3 }, { 2, 2 }, {})); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,3 y=2,2" },
        { "three-sources-each-earlier-than-the-last", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 30, kT0 + 40 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 20, kT0 + 25 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 5, kT0 + 6 }, { 3, 3 }, {})); }), "base=318/80005 x=0..35 view=0..35 lock=1 missed=0 warn=[1,2] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=25,35 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=15,20 y=2,2"
            " | [2] id=3 'A' label='A' M1 ord=1 seq=0 src=2 rcv=0 ch=0 y=3..3 x=0,1 y=3,3" },
        { "a-second-stream-of-a-source-that-starts-earlier", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 10, kT0 + 20 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0 + 2, kT0 + 3 }, { 2, 2 }, {})); }), "base=318/80002 x=0..18 view=0..18 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=8,18 y=1,1"
            " | [1] id=2 'B' label='B' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "shifting-by-fractions-of-a-second", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 0.3, kT0 + 0.7 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 0.1, kT0 + 0.2 }, { 2, 2 }, {})); }), "base=318/80000.099999904633 x=0..0.60000014305114746 view=0..0.60000014305114746 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0.20000004768371582,0.60000014305114746 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,0.10000014305114746 y=2,2" },
        { "one-source-with-several-channels-shared-axis", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 1, 2, 3 }, { 0, 0, 1 },
                                      { { "L_RCVR1", { 4, 5, 6 } } })); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=1 warn=[] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,1,2 y=1,2,3"
            " | [1] id=2 'A' label='A' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=0..1 x=0,1,2 y=0,0,1"
            " | [2] id=3 '1 - A L_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=4..6 x=0,1,2 y=4,5,6" },

        // --- sources that do not overlap
        { "overlapping-sources-raise-no-warning", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 5, kT0 + 15 }, { 2, 2 }, {})); }), "base=318/80000 x=0..15 view=0..15 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=5,15 y=2,2" },
        { "a-source-that-starts-where-the-last-ends", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 10, kT0 + 12 }, { 2, 2 }, {})); }), "base=318/80000 x=0..12 view=0..12 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=10,12 y=2,2" },
        { "a-source-that-starts-just-after", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 10.5, kT0 + 12 }, { 2, 2 }, {})); }), "base=318/80000 x=0..12 view=0..12 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=10.5,12 y=2,2" },
        { "a-source-that-ends-just-before", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 20, kT0 + 30 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 19.5 }, { 2, 2 }, {})); }), "base=318/80000 x=0..30 view=0..30 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=20,30 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,19.5 y=2,2" },
        { "a-source-that-ends-exactly-where-the-first-starts", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 20, kT0 + 30 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 20 }, { 2, 2 }, {})); }), "base=318/80000 x=0..30 view=0..30 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=20,30 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,20 y=2,2" },
        { "a-source-entirely-inside-another", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 100 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 40, kT0 + 50 }, { 2, 2 }, {})); }), "base=318/80000 x=0..100 view=0..100 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,100 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=40,50 y=2,2" },
        { "the-warning-is-raised-once-per-source", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 1000, kT0 + 1010 }, { 2, 2 }, {}));
              vm.addStreamData(stream("B", 2, 1, 1, { kT0 + 2000, kT0 + 2010 }, { 3, 3 }, {})); }), "base=318/80000 x=0..2010 view=0..2010 lock=1 missed=0 warn=[1] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=1000,1010 y=2,2"
            " | [2] id=3 'B' label='B' M1 ord=2 seq=1 src=1 rcv=0 ch=0 y=3..3 x=2000,2010 y=3,3" },
        { "a-third-source-is-judged-against-everything-so-far", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 1000, kT0 + 1010 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 500, kT0 + 505 }, { 3, 3 }, {})); }), "base=318/80000 x=0..1010 view=0..1010 lock=1 missed=0 warn=[1] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=1000,1010 y=2,2"
            " | [2] id=3 'A' label='A' M1 ord=1 seq=0 src=2 rcv=0 ch=0 y=3..3 x=500,505 y=3,3" },
        { "the-first-stream-of-a-reprocessed-source-is-not-new", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 5000, kT0 + 5010 }, { 2, 2 }, {})); }), "base=318/85000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },

        // --- the base day and time of day
        { "base-at-the-epoch", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 0.0, 1.0 }, { 1, 1 }, {})); }), "base=1/0 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },
        { "base-with-a-fraction", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 86399.999, 86401.0 }, { 1, 1 }, {})); }), "base=1/86399.998999999996 x=0..1.0010000000038417 view=0..1.0010000000038417 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1.0010000000038417 y=1,1" },
        { "base-on-the-last-day-of-a-year", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 1704067199.5, 1704067201.0 }, { 1, 1 }, {})); }), "base=365/86399.5 x=0..1.5 view=0..1.5 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1.5 y=1,1" },
        { "base-in-a-leap-year", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 1709164800.0, 1709164801.0 }, { 1, 1 }, {})); }), "base=60/0 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },
        { "base-far-in-the-future", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 4102444800.0, 4102444801.0 }, { 1, 1 }, {})); }), "base=1/0 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },
        { "base-before-the-epoch", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { -100.0, 50.0 }, { 1, 1 }, {})); }), "base=365/86300 x=0..150 view=0..150 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,150 y=1,1" },
        { "base-moves-to-an-earlier-day", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 86400 * 3, kT0 + 86400 * 3 + 5 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 5 }, { 2, 2 }, {})); }), "base=318/80000 x=0..259205 view=0..259205 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=259200,259205 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,5 y=2,2" },
        { "base-seconds-and-fraction", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 0.25, kT0 + 1.0 }, { 1, 1 }, {})); }), "base=318/80000.25 x=0..0.75 view=0..0.75 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,0.75 y=1,1" },

        // --- taking a source away
        { "remove-the-earliest-source", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {}));
              vm.removeSource(0); }), "base=318/80004 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,2 y=2,2" },
        { "remove-a-later-source", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {}));
              vm.removeSource(1); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-the-source-with-the-latest-end", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 60 }, { 2, 2 }, {}));
              vm.removeSource(1); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-the-only-source", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.removeSource(0); }), "base=0/0 x=0..0 view=0..0 lock=0 missed=0 warn=[] n=0" },
        { "remove-a-source-that-is-not-there", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.removeSource(7); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-one-of-three-sources", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 100, kT0 + 110 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 50, kT0 + 60 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 10, kT0 + 20 }, { 3, 3 }, {}));
              vm.removeSource(2); }), "base=318/80050 x=0..60 view=0..60 lock=1 missed=0 warn=[1,2] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=50,60 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },
        { "remove-two-sources-in-turn", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 100, kT0 + 110 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 50, kT0 + 60 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 10, kT0 + 20 }, { 3, 3 }, {}));
              vm.removeSource(2); vm.removeSource(1); }), "base=318/80100 x=0..10 view=0..10 lock=1 missed=0 warn=[1,2] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-a-source-with-several-streams", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 1, 1, { kT0 + 2, kT0 + 5 }, { 2, 2 }, {}, { { "L_RCVR1", { 3, 4 } } }));
              vm.addStreamData(stream("C", 3, 1, 2, { kT0 + 3, kT0 + 9 }, { 5, 5 }, {}));
              vm.removeSource(1); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-then-add-again", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.removeSource(0);
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 500, kT0 + 510 }, { 2, 2 }, {})); }), "base=318/80500 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },
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

        // --- an imported CSV: the order given to its streams
        { "import-lock-only-streams", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",Zed Lock (%),A Lock (%),A Accumulated Missed Frames,Zed Accumulated Missed Frames\n"
                                      "318:22:13:20.000,1,2,3,4\n318:22:13:21.000,5,6,7,8\n"); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=1 warn=[] n=4"
            " | [0] id=1 'Zed' label='Zed' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..5 x=0,1 y=1,5"
            " | [1] id=2 'A' label='A' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..6 x=0,1 y=2,6"
            " | [2] id=3 'A' label='A' M2 ord=2 seq=0 src=0 rcv=0 ch=0 y=3..7 x=0,1 y=3,7"
            " | [3] id=4 'Zed' label='Zed' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=4..8 x=0,1 y=4,8" },
        { "import-lock-streams-after-an-snr-one", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",Zed Lock (%),40 - Rcvr L_RCVR1,A Lock (%),40 - Rcvr R_RCVR1\n"
                                      "318:22:13:20.000,1,2,3,4\n318:22:13:21.000,5,6,7,8\n"); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=4"
            " | [0] id=1 'Zed' label='Zed' M1 ord=41 seq=0 src=0 rcv=0 ch=0 y=1..5 x=0,1 y=1,5"
            " | [1] id=2 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=2..6 x=0,1 y=2,6"
            " | [2] id=3 'A' label='A' M1 ord=42 seq=0 src=0 rcv=0 ch=0 y=3..7 x=0,1 y=3,7"
            " | [3] id=4 '40 - Rcvr R_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=1 y=4..8 x=0,1 y=4,8" },
        { "import-a-lock-stream-named-like-an-snr-stream", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",Rcvr Lock (%),40 - Rcvr L_RCVR1,Other Lock (%)\n"
                                      "318:22:13:20.000,1,2,3\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=3"
            " | [0] id=1 'Rcvr' label='Rcvr' M1 ord=40 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'Other' label='Other' M1 ord=41 seq=0 src=0 rcv=0 ch=0 y=3..3 x=0 y=3" },
        { "import-two-sources-with-one-label", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",A Lock (%),S1| A Lock (%),S1| A Accumulated Missed Frames,A Accumulated Missed Frames\n"
                                      "318:22:13:20.000,1,2,3,4\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=1 warn=[] n=4"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 'A' label='A' M1 ord=2 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'A' label='A' M2 ord=2 seq=0 src=1 rcv=0 ch=0 y=3..3 x=0 y=3"
            " | [3] id=4 'A' label='A' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=4..4 x=0 y=4" },
        { "import-snr-orders-are-kept-and-skipped", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",40 - A L_RCVR1,45 - B L_RCVR1,X Lock (%),Y Lock (%)\n"
                                      "318:22:13:20.000,1,2,3,4\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=4"
            " | [0] id=1 '40 - A L_RCVR1' label='A' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 '45 - B L_RCVR1' label='B' M0 ord=45 seq=0 src=0 rcv=1 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'X' label='X' M1 ord=46 seq=0 src=0 rcv=0 ch=0 y=3..3 x=0 y=3"
            " | [3] id=4 'Y' label='Y' M1 ord=47 seq=0 src=0 rcv=0 ch=0 y=4..4 x=0 y=4" },
        { "import-lock-and-missed-of-one-stream-share-an-order", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",A Lock (%),B Lock (%),B Accumulated Missed Frames,A Accumulated Missed Frames\n"
                                      "318:22:13:20.000,1,2,3,4\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=1 warn=[] n=4"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 'B' label='B' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'B' label='B' M2 ord=2 seq=0 src=0 rcv=0 ch=0 y=3..3 x=0 y=3"
            " | [3] id=4 'A' label='A' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=4..4 x=0 y=4" },
        { "import-an-empty-label-lock-stream", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ", Lock (%),40 - Rcvr L_RCVR1\n"
                                      "318:22:13:20.000,1,2\n"); }), "base=318/80000 x=0..0 view=0..0 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 'Lock (%)' label='' M0 ord=0 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=2..2 x=0 y=2" },
        { "import-keeps-the-files-own-clock", Scenario([](PlotViewModel& vm) {
              importCsv(vm, kHeader + ",A Lock (%)\n"
                                      "003:07:08:09.250,1\n003:07:08:11.500,2\n"); }), "base=3/25689.25 x=0..2.25 view=0..2.25 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,2.25 y=1,2" },
        { "import-after-streams-replaces-them", Scenario([](PlotViewModel& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              importCsv(vm, kHeader + ",B Lock (%)\n318:22:13:20.000,1\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'B' label='B' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1" },
    };
    return list;
}

}  // namespace

// ---------------------------------------------------------------------------
// The implementation the core replaced, kept here as an oracle: the data half of PlotViewModel's
// addStreamData / removeSource / clearData and the CSV commit's stream ordering, verbatim apart from
// the members it needs being gathered into one small class and the signals being recorded in a list. A
// difference with the code that replaced it is a change of behaviour, not of what was copied.
// ---------------------------------------------------------------------------

namespace oracle {

QVector<double> toPlotVector(const std::vector<double>& values)
{
    return QVector<double>(values.begin(), values.end());
}

class OldPlot
{
public:
    QVector<PlotSeriesData> m_series;
    QList<int> warnings;
    double m_x_min = 0.0, m_x_max = 0.0, m_x_view_min = 0.0, m_x_view_max = 0.0;
    bool m_has_lock_series = false, m_has_missed_frames_series = false;
    int m_base_day = 0;
    double m_base_time_offset = 0.0;
    double m_base_abs_seconds = 0.0;
    int m_next_series_id = 1;

    void recomputeBaseDayAndOffset()
    {
        std::time_t epoch = static_cast<std::time_t>(m_base_abs_seconds);
        std::tm tm_buf{};
        const bool ok = (gmtime_s(&tm_buf, &epoch) == 0);
        if (ok)
        {
            m_base_day = tm_buf.tm_yday + 1;
            m_base_time_offset = (tm_buf.tm_hour * UIConstants::kSecondsPerHour)
                               + (tm_buf.tm_min * UIConstants::kSecondsPerMinute)
                               + tm_buf.tm_sec
                               + (m_base_abs_seconds - static_cast<double>(epoch));
        }
    }

    void clearData()
    {
        m_series.clear();
        m_x_min = m_x_max = 0.0;
        m_x_view_min = m_x_view_max = 0.0;
        m_has_lock_series = false;
        m_has_missed_frames_series = false;
        m_base_day = 0;
        m_base_time_offset = 0.0;
        m_base_abs_seconds = 0.0;
    }

    void addStreamData(const ProcessedStreamData& data)
    {
        if (!data.hasSamples())
        {
            return;
        }

        const QString stream_label = QString::fromStdString(data.streamLabel);

        m_series.erase(std::remove_if(m_series.begin(), m_series.end(),
                                       [&](const PlotSeriesData& s) {
                                           return s.sourceId == data.sourceId &&
                                                  s.streamLabel == stream_label &&
                                                  s.streamOrder == data.pcmChannelId;
                                       }),
                       m_series.end());

        if (!m_series.isEmpty())
        {
            const bool source_already_present = std::any_of(m_series.cbegin(), m_series.cend(),
                [&](const PlotSeriesData& s) { return s.sourceId == data.sourceId; });
            if (!source_already_present)
            {
                const double existing_min_abs = m_base_abs_seconds;
                const double existing_max_abs = m_base_abs_seconds + m_x_max;
                const double new_min_abs = data.timesSec.front();
                const double new_max_abs = data.timesSec.back();
                const bool overlaps = new_min_abs <= existing_max_abs && existing_min_abs <= new_max_abs;
                if (!overlaps)
                {
                    warnings << data.sourceId;
                }
            }
        }

        if (m_series.isEmpty())
        {
            m_base_abs_seconds = data.timesSec.front();
            recomputeBaseDayAndOffset();
        }
        else if (data.timesSec.front() < m_base_abs_seconds)
        {
            const double delta = m_base_abs_seconds - data.timesSec.front();
            for (PlotSeriesData& s : m_series)
            {
                for (double& x : s.xValues)
                {
                    x += delta;
                }
            }
            m_base_abs_seconds = data.timesSec.front();
            recomputeBaseDayAndOffset();
        }

        const int sample_count = static_cast<int>(data.timesSec.size());

        QVector<double> elapsed(sample_count);
        for (int i = 0; i < sample_count; i++)
        {
            elapsed[i] = data.timesSec[i] - m_base_abs_seconds;
        }

        auto fillCaches = [](PlotSeriesData& s) {
            for (double v : s.yValues)
            {
                s.yMinCached = qMin(s.yMinCached, v);
                s.yMaxCached = qMax(s.yMaxCached, v);
            }
        };

        if (!data.lockPercent.empty())
        {
            PlotSeriesData lock;
            lock.name = stream_label;
            lock.streamLabel = stream_label;
            lock.metricType = PlotSeriesData::MetricType::FrameSyncLock;
            lock.receiverIndex = 0;
            lock.channelIndex = 0;
            lock.streamOrder = data.pcmChannelId;
            lock.streamSequence = data.jobIndex;
            lock.sourceId = data.sourceId;
            lock.id = m_next_series_id++;
            lock.xValues = elapsed;
            lock.yValues = toPlotVector(data.lockPercent);
            fillCaches(lock);
            m_series.push_back(lock);
        }

        if (!data.accumulatedMissedFrames.empty())
        {
            PlotSeriesData errors;
            errors.name = stream_label;
            errors.streamLabel = stream_label;
            errors.metricType = PlotSeriesData::MetricType::AccumulatedMissedFrames;
            errors.receiverIndex = 0;
            errors.channelIndex = 0;
            errors.streamOrder = data.pcmChannelId;
            errors.streamSequence = data.jobIndex;
            errors.sourceId = data.sourceId;
            errors.id = m_next_series_id++;
            errors.xValues = elapsed;
            errors.yValues = toPlotVector(data.accumulatedMissedFrames);
            fillCaches(errors);
            m_series.push_back(errors);
        }

        QMap<int, int> receiver_channel_count;
        for (const auto& ch : data.channels)
        {
            PlotSeriesData s;
            const QString ch_name = QString::fromStdString(ch.name);
            s.name = QString::fromStdString(SeriesColumnSchema::snrSeriesName(
                data.pcmChannelId, stream_label.toStdString(), ch.name));
            s.streamLabel = stream_label;
            s.metricType = PlotSeriesData::MetricType::SNR;
            int rcvr_pos = static_cast<int>(ch_name.lastIndexOf("_RCVR"));
            if (rcvr_pos >= 0)
            {
                bool ok = false;
                int rcvr_num = ch_name.mid(rcvr_pos + 5).toInt(&ok);
                s.receiverIndex = ok ? rcvr_num : 0;
            }
            s.channelIndex = receiver_channel_count.value(s.receiverIndex, 0);
            receiver_channel_count[s.receiverIndex]++;
            s.streamOrder = data.pcmChannelId;
            s.streamSequence = data.jobIndex;
            s.sourceId = data.sourceId;
            s.id = m_next_series_id++;
            s.xValues = elapsed;
            s.yValues = toPlotVector(ch.values);
            fillCaches(s);
            m_series.push_back(s);
        }

        std::stable_sort(m_series.begin(), m_series.end(),
                         [](const PlotSeriesData& a, const PlotSeriesData& b) {
                             return a.streamSequence < b.streamSequence;
                         });

        refreshRange();
    }

    void refreshRange()
    {
        m_x_min = 0.0;
        for (const auto& s : m_series)
        {
            if (!s.xValues.isEmpty())
            {
                m_x_max = qMax(m_x_max, s.xValues.last());
            }
        }
        m_x_view_min = m_x_min;
        m_x_view_max = m_x_max;

        m_has_lock_series = false;
        m_has_missed_frames_series = false;
        for (const auto& s : m_series)
        {
            if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            {
                m_has_lock_series = true;
            }
            else if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
            {
                m_has_missed_frames_series = true;
            }
        }
    }

    void removeSource(int sourceId)
    {
        const bool had_match = std::any_of(m_series.cbegin(), m_series.cend(),
            [sourceId](const PlotSeriesData& s) { return s.sourceId == sourceId; });
        if (!had_match)
        {
            return;
        }

        m_series.erase(std::remove_if(m_series.begin(), m_series.end(),
                                       [sourceId](const PlotSeriesData& s) {
                                           return s.sourceId == sourceId;
                                       }),
                       m_series.end());

        if (m_series.isEmpty())
        {
            clearData();
            return;
        }

        double new_base_abs = m_base_abs_seconds + m_series.first().xValues.first();
        for (const PlotSeriesData& s : m_series)
        {
            if (!s.xValues.isEmpty())
            {
                new_base_abs = qMin(new_base_abs, m_base_abs_seconds + s.xValues.first());
            }
        }
        const double delta = new_base_abs - m_base_abs_seconds; // >= 0
        if (delta > 0.0)
        {
            for (PlotSeriesData& s : m_series)
            {
                for (double& x : s.xValues)
                {
                    x -= delta;
                }
            }
            m_base_abs_seconds = new_base_abs;
            recomputeBaseDayAndOffset();
        }

        m_x_min = 0.0;
        m_x_max = 0.0;
        refreshRange();
    }

    QString snapshot() const
    {
        return format(m_base_day, m_base_time_offset, m_x_min, m_x_max, m_x_view_min, m_x_view_max,
                      m_has_lock_series, m_has_missed_frames_series, warnings, m_series);
    }
};

/// The CSV commit's stream ordering, as it was.
void assignImportedOrders(QVector<PlotSeriesData>& series)
{
    QMap<int, QMap<QString, int>> label_to_stream_order;
    int next_stream_order = 0;
    for (const auto& s : series)
    {
        if (s.metricType == PlotSeriesData::MetricType::SNR && !s.streamLabel.isEmpty())
        {
            label_to_stream_order[s.sourceId].insert(s.streamLabel, s.streamOrder);
        }
        next_stream_order = qMax(next_stream_order, s.streamOrder + 1);
    }

    for (auto& s : series)
    {
        switch (s.metricType)
        {
        case PlotSeriesData::MetricType::FrameSyncLock:
        case PlotSeriesData::MetricType::AccumulatedMissedFrames:
        {
            QMap<QString, int>& source_stream_order = label_to_stream_order[s.sourceId];
            auto it = source_stream_order.find(s.streamLabel);
            if (it == source_stream_order.end())
            {
                it = source_stream_order.insert(s.streamLabel, next_stream_order++);
            }
            s.streamOrder = it.value();
            break;
        }
        default:
            break;
        }
    }
}

}  // namespace oracle

namespace {

struct Rng
{
    quint32 state;
    explicit Rng(quint32 seed) : state(seed) {}
    quint32 next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int below(int n) { return int(next() % quint32(n)); }
    bool chance(int percent) { return below(100) < percent; }
    template <typename List>
    auto pick(const List& list) { return list[below(int(list.size()))]; }
};

std::vector<double> generateValues(Rng& rng, int count, bool finite_only = false)
{
    static const std::vector<double> specials = { 0.0, -0.0, 1e300, -1e300, 4.9e-324, 99.5, -80.25,
                                                  std::numeric_limits<double>::infinity(),
                                                  -std::numeric_limits<double>::infinity(),
                                                  std::numeric_limits<double>::quiet_NaN() };
    std::vector<double> v;
    for (int i = 0; i < count; ++i)
        v.push_back(!finite_only && rng.chance(15) ? specials[size_t(rng.below(int(specials.size())))] : rng.below(20000) / 100.0 - 50.0);
    return v;
}

ProcessedStreamData generateStream(Rng& rng)
{
    static const char* labels[] = { "A", "B", "Ch 5", "", "R\xC3\xA9" "cepteur \xCE\xA9", "Safran 2", "x y" };
    static const char* channel_names[] = { "L_RCVR1", "R_RCVR1", "C_RCVR1", "L_RCVR2", "R_RCVR2", "NAME", "", "L_RCVR", "L_RCVRx",
                                           "X_RCVR2_RCVR3", "L_RCVR 7", "L_RCVR+4", "L_RCVR-2", "L_RCVR2147483648", "\xCE\xA9_RCVR2",
                                           "L_RCVR\xD9\xA5" };
    ProcessedStreamData d;
    d.streamLabel = labels[rng.below(7)];
    d.pcmChannelId = rng.chance(10) ? -rng.below(3) : rng.below(6);
    d.sourceId = rng.below(4);
    d.jobIndex = rng.chance(10) ? -rng.below(3) : rng.below(4);

    // a clock: epoch-ish, a day boundary, a leap day, a far-future date, a fraction of a second
    static const double bases[] = { 1700000000.0, 0.0, 86399.5, 1709164800.0, 4102444800.0, 1704067199.75, -50.0, 1700000000.3 };
    double t = bases[rng.below(8)] + (rng.chance(50) ? 0.0 : rng.below(200000) / 10.0);
    const int n = rng.chance(5) ? 0 : 1 + rng.below(6);
    for (int i = 0; i < n; ++i)
    {
        d.timesSec.push_back(t);
        const int step = rng.below(10);
        t += step < 2 ? 0.0 : step < 8 ? rng.below(500) / 100.0 : -rng.below(300) / 100.0;
    }
    auto len = [&]() { return rng.chance(85) ? n : rng.below(n + 3); };
    if (rng.chance(75)) d.lockPercent = generateValues(rng, len());
    if (rng.chance(50)) d.accumulatedMissedFrames = generateValues(rng, len());
    if (rng.chance(45))
    {
        d.mode = StreamMode::ReceiverChannelInfo;
        for (int c = rng.below(5); c >= 0; --c)
        {
            ProcessedChannelSeries ch;
            ch.name = channel_names[rng.below(16)];
            // finite and moderate: the plot rounds an SNR range to whole decibels, which a calibrated value
            // (from 16-bit counts) always allows, and Qt's qFloor asserts on anything else in a debug build
            ch.values = generateValues(rng, len(), true);
            d.channels.push_back(std::move(ch));
        }
    }
    return d;
}

}  // namespace

void TestSeriesBuild::theCoreBuildsEveryGeneratedSequenceLikeTheOldViewModelCode()
{
    constexpr int kSequences = 700;
    Rng rng(20261029u);
    int adds = 0, removes = 0, clears = 0, rebased = 0, warned = 0, multi_source = 0, replaced = 0, snr = 0, nonfinite = 0;
    QStringList problems;
    for (int n = 0; n < kSequences; ++n)
    {
        PlotViewModel vm;
        QList<int> warnings;
        QObject::connect(&vm, &PlotViewModel::nonOverlappingSourceWarning, &vm, [&warnings](int s) { warnings << s; });
        oracle::OldPlot old_plot;

        const int ops = 1 + rng.below(8);
        for (int k = 0; k < ops && problems.isEmpty(); ++k)
        {
            const int kind = rng.below(100);
            QString what;
            if (kind < 76)
            {
                const ProcessedStreamData d = generateStream(rng);
                const int before = old_plot.m_series.size();
                const double base_before = old_plot.m_base_abs_seconds;
                old_plot.addStreamData(d);
                vm.addStreamData(d);
                ++adds;
                what = QString("add %1 src%2 ch%3").arg(QString::fromStdString(d.streamLabel)).arg(d.sourceId).arg(d.pcmChannelId);
                rebased += d.hasSamples() && before > 0 && old_plot.m_base_abs_seconds < base_before;
                replaced += d.hasSamples() && before > 0 && old_plot.m_series.size() <= before;
                snr += int(d.channels.size());
                for (double v : d.lockPercent) nonfinite += !std::isfinite(v);
            }
            else if (kind < 94)
            {
                const int source = rng.below(5);
                old_plot.removeSource(source);
                vm.removeSource(source);
                ++removes;
                what = QString("remove %1").arg(source);
            }
            else
            {
                old_plot.clearData();
                vm.clearData();
                ++clears;
                what = "clear";
            }
            QSet<int> sources;
            for (const auto& s : old_plot.m_series) sources.insert(s.sourceId);
            multi_source += sources.size() > 1;
            warned += old_plot.warnings.size();

            // PlotViewModel assigns ids from its own counter and clearData() does not reset it; the old
            // class starts at 1 too, so ids must agree.
            const QString want = old_plot.snapshot();
            const QString got = digest(vm, warnings);
            if (want != got)
                problems << QString("sequence %1 step %2 (%3):\n  old %4\n  new %5").arg(n).arg(k).arg(what, want, got);
        }
        if (!problems.isEmpty())
            break;
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n")));
    // The generator must reach each outcome in volume, or "no difference" proves nothing.
    QVERIFY2(adds > 2000, qPrintable(QString::number(adds)));
    QVERIFY2(removes > 400, qPrintable(QString::number(removes)));
    QVERIFY2(clears > 50, qPrintable(QString::number(clears)));
    QVERIFY2(rebased > 100, qPrintable(QString::number(rebased)));
    QVERIFY2(warned > 100, qPrintable(QString::number(warned)));
    QVERIFY2(multi_source > 1000, qPrintable(QString::number(multi_source)));
    QVERIFY2(replaced > 50, qPrintable(QString::number(replaced)));
    QVERIFY2(snr > 1000, qPrintable(QString::number(snr)));
    QVERIFY2(nonfinite > 20, qPrintable(QString::number(nonfinite)));
}

void TestSeriesBuild::theCoreOrdersImportedStreamsLikeTheOldViewModelCode()
{
    Rng rng(20261030u);
    static const char* labels[] = { "A", "B", "C", "", "R\xC3\xA9", "Zed" };
    int fresh = 0, shared = 0, multi_source = 0;
    for (int n = 0; n < 4000; ++n)
    {
        QVector<PlotSeriesData> old_series;
        std::vector<ImportedSeries> core;
        const int count = rng.below(10);
        for (int i = 0; i < count; ++i)
        {
            PlotSeriesData s;
            const int m = rng.below(3);
            s.metricType = PlotSeriesData::MetricType(m);
            s.sourceId = rng.below(3);
            s.streamLabel = QString::fromUtf8(labels[rng.below(6)]);
            s.streamOrder = m == 0 ? rng.below(8) * 5 : 0;   // SNR columns carry a channel id, the others 0
            old_series.append(s);
            core.push_back({ s.metricType, s.sourceId, s.streamLabel.toStdString(), s.streamOrder });
        }
        oracle::assignImportedOrders(old_series);
        assignImportedStreamOrders(core);
        for (int i = 0; i < count; ++i)
            QVERIFY2(old_series[i].streamOrder == core[size_t(i)].streamOrder,
                     qPrintable(QString("case %1 series %2: old %3, core %4").arg(n).arg(i).arg(old_series[i].streamOrder).arg(core[size_t(i)].streamOrder)));
        QSet<int> sources;
        for (const auto& s : old_series)
        {
            sources.insert(s.sourceId);
            if (s.metricType != PlotSeriesData::MetricType::SNR)
            {
                fresh += s.streamOrder >= 1 && s.streamOrder % 5 != 0;
                shared += s.streamOrder % 5 == 0 && s.streamOrder > 0;
            }
        }
        multi_source += sources.size() > 1;
    }
    QVERIFY2(fresh > 3000, qPrintable(QString::number(fresh)));
    QVERIFY2(shared > 500, qPrintable(QString::number(shared)));
    QVERIFY2(multi_source > 2000, qPrintable(QString::number(multi_source)));
}

void TestSeriesBuild::theCoreSpecsViewTheProcessedDataWithoutCopyingIt()
{
    // The y values of a spec are spans over the processed stream's own vectors: the plot copies them once,
    // into its QVectors, and the core makes no second copy.
    ProcessedStreamData d = stream("Rcvr", 40, 0, 0, { kT0, kT0 + 1 }, { 90, 95 }, { 0, 3 }, { { "L_RCVR1", { -80, -79 } } });
    const StreamSeries built = buildStreamSeries(d, kT0);
    QCOMPARE(int(built.series.size()), 3);
    QVERIFY(built.series[0].yValues.data() == d.lockPercent.data());
    QVERIFY(built.series[1].yValues.data() == d.accumulatedMissedFrames.data());
    QVERIFY(built.series[2].yValues.data() == d.channels[0].values.data());
    QCOMPARE(built.elapsed, (std::vector<double>{ 0.0, 1.0 }));
    QCOMPARE(built.series[2].name, std::string("40 - Rcvr L_RCVR1"));
}

void TestSeriesBuild::theTimeBaseReportsHowFarTheSeriesMustShift()
{
    SeriesTimeBase base;
    QCOMPARE(base.adopt(1000.0, false), 0.0);             // the first stream: the base is its first sample
    QCOMPARE(base.baseAbsSeconds(), 1000.0);
    QCOMPARE(base.adopt(1200.0, true), 0.0);              // a later one does not move it
    QCOMPARE(base.baseAbsSeconds(), 1000.0);
    QCOMPARE(base.adopt(940.0, true), 60.0);              // an earlier one does, and existing series shift right by the gap
    QCOMPARE(base.baseAbsSeconds(), 940.0);
    QCOMPARE(base.adopt(2000.0, false), 0.0);             // with nothing left the base may move LATER
    QCOMPARE(base.baseAbsSeconds(), 2000.0);

    QVERIFY(base.overlaps(2000.0, 2010.0, 5.0));          // touching at one instant counts
    QVERIFY(base.overlaps(2005.0, 2100.0, 5.0));
    QVERIFY(!base.overlaps(2005.5, 2100.0, 5.0));
    QVERIFY(!base.overlaps(1000.0, 1999.5, 5.0));
    QVERIFY(base.overlaps(1000.0, 2000.0, 5.0));

    QCOMPARE(base.advance(std::nullopt), 0.0);            // nothing remaining has a sample
    QCOMPARE(base.advance(0.0), 0.0);                     // the earliest is still the base
    QCOMPARE(base.advance(7.5), 7.5);                     // the earliest remaining starts later: the base follows, the rest shift left
    QCOMPARE(base.baseAbsSeconds(), 2007.5);

    base.setClock(3, 100.0);                              // an import states its own clock and leaves the absolute base alone
    QCOMPARE(base.baseDay(), 3);
    QCOMPARE(base.baseTimeOffset(), 100.0);
    QCOMPARE(base.baseAbsSeconds(), 2007.5);
    base.reset();
    QCOMPARE(base.baseDay(), 0);
    QCOMPARE(base.baseAbsSeconds(), 0.0);
}

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
