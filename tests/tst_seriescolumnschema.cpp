/**
 * @file tst_seriescolumnschema.cpp
 * @brief Tests for SeriesColumnSchema: formatting, parsing, and their round-trip.
 */

#include "tst_seriescolumnschema.h"

#include <QtTest>

#include "constants.h"
#include "plotseriesdata.h"
#include "seriescolumnschema.h"

using MetricType = PlotSeriesData::MetricType;

namespace {
PlotSeriesData makeSeries(MetricType type, const QString& name, int sourceId = 0)
{
    PlotSeriesData s;
    s.metricType = type;
    s.name       = name;
    s.sourceId   = sourceId;
    return s;
}
} // namespace

void TestSeriesColumnSchema::snrSeriesNameFormat()
{
    QCOMPARE(SeriesColumnSchema::snrSeriesName(40, "RCVR Data", "L_RCVR1"),
             QString("40 - RCVR Data L_RCVR1"));
}

void TestSeriesColumnSchema::columnHeaderAddsMetricSuffix()
{
    // Left-axis metrics get a suffix so a stream's lock/missed columns don't collide.
    QCOMPARE(SeriesColumnSchema::columnHeader(makeSeries(MetricType::FrameSyncLock, "Ch 05")),
             QString("Ch 05") + PlotConstants::kCsvLockSuffix);
    QCOMPARE(SeriesColumnSchema::columnHeader(makeSeries(MetricType::AccumulatedMissedFrames, "Ch 05")),
             QString("Ch 05") + PlotConstants::kCsvMissedFramesSuffix);
}

void TestSeriesColumnSchema::columnHeaderSnrPassthrough()
{
    // SNR names already carry the channel id and are unique — passed through as-is.
    const QString name = "40 - RCVR Data L_RCVR1";
    QCOMPARE(SeriesColumnSchema::columnHeader(makeSeries(MetricType::SNR, name)), name);
}

void TestSeriesColumnSchema::parseLockHeader()
{
    const auto col = SeriesColumnSchema::parseColumnHeader(
        QString("Ch 05") + PlotConstants::kCsvLockSuffix);
    QVERIFY(col.metricType == MetricType::FrameSyncLock);
    QCOMPARE(col.name, QString("Ch 05"));
    QCOMPARE(col.streamLabel, QString("Ch 05"));   // re-links the lock/missed pair
    QCOMPARE(col.receiverIndex, 0);
}

void TestSeriesColumnSchema::parseMissedHeader()
{
    const auto col = SeriesColumnSchema::parseColumnHeader(
        QString("Ch 05") + PlotConstants::kCsvMissedFramesSuffix);
    QVERIFY(col.metricType == MetricType::AccumulatedMissedFrames);
    QCOMPARE(col.name, QString("Ch 05"));
    QCOMPARE(col.streamLabel, QString("Ch 05"));
}

void TestSeriesColumnSchema::parseSnrHeader()
{
    const auto col = SeriesColumnSchema::parseColumnHeader("40 - RCVR Data L_RCVR3");
    QVERIFY(col.metricType == MetricType::SNR);
    QCOMPARE(col.streamOrder, 40);
    QCOMPARE(col.streamLabel, QString("RCVR Data"));
    QCOMPARE(col.receiverIndex, 3);
    QCOMPARE(col.name, QString("40 - RCVR Data L_RCVR3"));
}

void TestSeriesColumnSchema::parseSnrLabelWithSpaces()
{
    // A multi-word stream label must split at the LAST space (before the channel
    // name), not the first — this was the crux of an earlier false-positive review.
    const auto col = SeriesColumnSchema::parseColumnHeader("7 - Band A 10Mbps R_RCVR2");
    QVERIFY(col.metricType == MetricType::SNR);
    QCOMPARE(col.streamOrder, 7);
    QCOMPARE(col.streamLabel, QString("Band A 10Mbps"));
    QCOMPARE(col.receiverIndex, 2);
}

void TestSeriesColumnSchema::parseSnrShapeWinsOverSuffix()
{
    // A header that both looks like SNR ("<digits> - ") and ends with a metric suffix
    // must resolve as SNR, not be misrouted to Lock/MissedFrames.
    const QString header = QString("12 - Weird L_RCVR1") + PlotConstants::kCsvLockSuffix;
    const auto col = SeriesColumnSchema::parseColumnHeader(header);
    QVERIFY(col.metricType == MetricType::SNR);
    QCOMPARE(col.streamOrder, 12);
}

void TestSeriesColumnSchema::parseUnknownFallsBackToSnr()
{
    // No metric suffix and no SNR id-prefix → SNR with a bare name (defensive default).
    const auto col = SeriesColumnSchema::parseColumnHeader("PlainName");
    QVERIFY(col.metricType == MetricType::SNR);
    QCOMPARE(col.name, QString("PlainName"));
    QCOMPARE(col.streamOrder, 0);
}

void TestSeriesColumnSchema::roundTripLockMissedSnr()
{
    // columnHeader() -> parseColumnHeader() recovers the identity for every metric.
    const QVector<PlotSeriesData> inputs = {
        makeSeries(MetricType::FrameSyncLock, "Ch 12"),
        makeSeries(MetricType::AccumulatedMissedFrames, "Ch 12"),
        makeSeries(MetricType::SNR, SeriesColumnSchema::snrSeriesName(40, "RCVR Data", "C_RCVR2")),
    };
    for (const PlotSeriesData& in : inputs)
    {
        const auto col = SeriesColumnSchema::parseColumnHeader(SeriesColumnSchema::columnHeader(in));
        QVERIFY(col.metricType == in.metricType);
        QCOMPARE(col.name, in.name);
    }

    // The SNR round trip also recovers stream order / label / receiver.
    const auto snr = SeriesColumnSchema::parseColumnHeader(SeriesColumnSchema::columnHeader(inputs[2]));
    QCOMPARE(snr.streamOrder, 40);
    QCOMPARE(snr.streamLabel, QString("RCVR Data"));
    QCOMPARE(snr.receiverIndex, 2);
}

void TestSeriesColumnSchema::columnHeaderSourceZeroIsUnqualified()
{
    // Source 0 (the first/only source) must produce EXACTLY the pre-multi-file
    // format -- a single-source export stays byte-identical across releases.
    const QString name = "40 - RCVR Data L_RCVR1";
    QCOMPARE(SeriesColumnSchema::columnHeader(makeSeries(MetricType::SNR, name, /*sourceId=*/0)), name);
    QCOMPARE(SeriesColumnSchema::columnHeader(makeSeries(MetricType::FrameSyncLock, "Ch 05", /*sourceId=*/0)),
             QString("Ch 05") + PlotConstants::kCsvLockSuffix);
}

void TestSeriesColumnSchema::columnHeaderNonZeroSourceAddsQualifier()
{
    const QString name = "40 - RCVR Data L_RCVR1";
    QCOMPARE(SeriesColumnSchema::columnHeader(makeSeries(MetricType::SNR, name, /*sourceId=*/1)),
             QString("S1| ") + name);
    QCOMPARE(SeriesColumnSchema::columnHeader(makeSeries(MetricType::FrameSyncLock, "Ch 05", /*sourceId=*/2)),
             QString("S2| Ch 05") + PlotConstants::kCsvLockSuffix);
}

void TestSeriesColumnSchema::parseSourceQualifiedSnrHeader()
{
    const auto col = SeriesColumnSchema::parseColumnHeader("S1| 40 - RCVR Data L_RCVR1");
    QVERIFY(col.metricType == MetricType::SNR);
    QCOMPARE(col.sourceId, 1);
    QCOMPARE(col.streamOrder, 40);
    QCOMPARE(col.streamLabel, QString("RCVR Data"));
    QCOMPARE(col.receiverIndex, 1);
    // The recovered name matches what source 0 would produce -- the qualifier is
    // metadata, not part of the series' own name.
    QCOMPARE(col.name, QString("40 - RCVR Data L_RCVR1"));
}

void TestSeriesColumnSchema::parseSourceQualifiedLockHeader()
{
    const auto col = SeriesColumnSchema::parseColumnHeader(
        QString("S2| Ch 05") + PlotConstants::kCsvLockSuffix);
    QVERIFY(col.metricType == MetricType::FrameSyncLock);
    QCOMPARE(col.sourceId, 2);
    QCOMPARE(col.name, QString("Ch 05"));
    QCOMPARE(col.streamLabel, QString("Ch 05"));
}

void TestSeriesColumnSchema::parseUnqualifiedHeaderDefaultsToSourceZero()
{
    QCOMPARE(SeriesColumnSchema::parseColumnHeader("40 - RCVR Data L_RCVR1").sourceId, 0);
    QCOMPARE(SeriesColumnSchema::parseColumnHeader(
                 QString("Ch 05") + PlotConstants::kCsvLockSuffix).sourceId, 0);
}

void TestSeriesColumnSchema::headerStartingWithLetterSButNotAQualifierIsUnaffected()
{
    // "Something" starts with 'S' but has no digit-then-"| " qualifier shape --
    // must NOT be misparsed as a source-qualified header.
    const auto col = SeriesColumnSchema::parseColumnHeader("Something");
    QCOMPARE(col.sourceId, 0);
    QCOMPARE(col.name, QString("Something"));

    // "S7Percent" -- digits follow 'S' but aren't followed by "| ", so this must
    // also be left alone (falls back to the SNR/unknown default, not stripped).
    const auto col2 = SeriesColumnSchema::parseColumnHeader("S7Percent");
    QCOMPARE(col2.sourceId, 0);
    QCOMPARE(col2.name, QString("S7Percent"));
}

void TestSeriesColumnSchema::roundTripPreservesSourceId()
{
    const QVector<PlotSeriesData> inputs = {
        makeSeries(MetricType::FrameSyncLock, "Ch 12", /*sourceId=*/3),
        makeSeries(MetricType::AccumulatedMissedFrames, "Ch 12", /*sourceId=*/3),
        makeSeries(MetricType::SNR, SeriesColumnSchema::snrSeriesName(40, "RCVR Data", "C_RCVR2"),
                   /*sourceId=*/5),
    };
    for (const PlotSeriesData& in : inputs)
    {
        const auto col = SeriesColumnSchema::parseColumnHeader(SeriesColumnSchema::columnHeader(in));
        QVERIFY(col.metricType == in.metricType);
        QCOMPARE(col.name, in.name);        // qualifier never leaks into the name
        QCOMPARE(col.sourceId, in.sourceId);
    }
}
