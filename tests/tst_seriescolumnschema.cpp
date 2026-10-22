/**
 * @file tst_seriescolumnschema.cpp
 * @brief Tests for SeriesColumnSchema: formatting, parsing, and their round-trip.
 */

#include "tst_seriescolumnschema.h"

#include <limits>
#include <random>

#include <QPair>
#include <QStringView>
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

/// What parseColumnHeader() recovered, as plain QStrings and ints whatever the schema's own string
/// type is. Together with formatHeader() and formatSnrName() this is the ONLY code in this file that
/// touches the schema's string types, so the tests below read the same before and after the schema
/// moved into the Qt-free core.
struct Parsed
{
    MetricType metricType;
    QString    name;
    QString    streamLabel;
    int        streamOrder;
    int        receiverIndex;
    int        sourceId;
};

Parsed parseHeader(const QString& header)
{
    const auto col = SeriesColumnSchema::parseColumnHeader(header.toStdString());
    return { col.metricType, QString::fromStdString(col.name), QString::fromStdString(col.streamLabel),
             col.streamOrder, col.receiverIndex, col.sourceId };
}

QString formatHeader(const PlotSeriesData& series)
{
    return QString::fromStdString(
        SeriesColumnSchema::columnHeader(series.metricType, series.name.toStdString(), series.sourceId));
}

QString formatHeader(MetricType type, const QString& name, int sourceId)
{
    return formatHeader(makeSeries(type, name, sourceId));
}

QString formatSnrName(int id, const QString& label, const QString& channel)
{
    return QString::fromStdString(
        SeriesColumnSchema::snrSeriesName(id, label.toStdString(), channel.toStdString()));
}

QString u(const char* utf8) { return QString::fromUtf8(utf8); }

bool sameParsed(const Parsed& a, const Parsed& b)
{
    return a.metricType == b.metricType && a.name == b.name && a.streamLabel == b.streamLabel
           && a.streamOrder == b.streamOrder && a.receiverIndex == b.receiverIndex && a.sourceId == b.sourceId;
}
} // namespace

void TestSeriesColumnSchema::snrSeriesNameFormat()
{
    QCOMPARE(formatSnrName(40, "RCVR Data", "L_RCVR1"),
             QString("40 - RCVR Data L_RCVR1"));
}

void TestSeriesColumnSchema::columnHeaderAddsMetricSuffix()
{
    // Left-axis metrics get a suffix so a stream's lock/missed columns don't collide.
    QCOMPARE(formatHeader(makeSeries(MetricType::FrameSyncLock, "Ch 05")),
             QString("Ch 05") + SeriesColumnSchema::kLockSuffix);
    QCOMPARE(formatHeader(makeSeries(MetricType::AccumulatedMissedFrames, "Ch 05")),
             QString("Ch 05") + SeriesColumnSchema::kMissedFramesSuffix);
}

void TestSeriesColumnSchema::columnHeaderSnrPassthrough()
{
    // SNR names already carry the channel id and are unique — passed through as-is.
    const QString name = "40 - RCVR Data L_RCVR1";
    QCOMPARE(formatHeader(makeSeries(MetricType::SNR, name)), name);
}

void TestSeriesColumnSchema::parseLockHeader()
{
    const auto col = parseHeader(
        QString("Ch 05") + SeriesColumnSchema::kLockSuffix);
    QVERIFY(col.metricType == MetricType::FrameSyncLock);
    QCOMPARE(col.name, QString("Ch 05"));
    QCOMPARE(col.streamLabel, QString("Ch 05"));   // re-links the lock/missed pair
    QCOMPARE(col.receiverIndex, 0);
}

void TestSeriesColumnSchema::parseMissedHeader()
{
    const auto col = parseHeader(
        QString("Ch 05") + SeriesColumnSchema::kMissedFramesSuffix);
    QVERIFY(col.metricType == MetricType::AccumulatedMissedFrames);
    QCOMPARE(col.name, QString("Ch 05"));
    QCOMPARE(col.streamLabel, QString("Ch 05"));
}

void TestSeriesColumnSchema::parseSnrHeader()
{
    const auto col = parseHeader("40 - RCVR Data L_RCVR3");
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
    const auto col = parseHeader("7 - Band A 10Mbps R_RCVR2");
    QVERIFY(col.metricType == MetricType::SNR);
    QCOMPARE(col.streamOrder, 7);
    QCOMPARE(col.streamLabel, QString("Band A 10Mbps"));
    QCOMPARE(col.receiverIndex, 2);
}

void TestSeriesColumnSchema::parseSnrShapeWinsOverSuffix()
{
    // A header that both looks like SNR ("<digits> - ") and ends with a metric suffix
    // must resolve as SNR, not be misrouted to Lock/MissedFrames.
    const QString header = QString("12 - Weird L_RCVR1") + SeriesColumnSchema::kLockSuffix;
    const auto col = parseHeader(header);
    QVERIFY(col.metricType == MetricType::SNR);
    QCOMPARE(col.streamOrder, 12);
}

void TestSeriesColumnSchema::parseUnknownFallsBackToSnr()
{
    // No metric suffix and no SNR id-prefix → SNR with a bare name (defensive default).
    const auto col = parseHeader("PlainName");
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
        makeSeries(MetricType::SNR, formatSnrName(40, "RCVR Data", "C_RCVR2")),
    };
    for (const PlotSeriesData& in : inputs)
    {
        const auto col = parseHeader(formatHeader(in));
        QVERIFY(col.metricType == in.metricType);
        QCOMPARE(col.name, in.name);
    }

    // The SNR round trip also recovers stream order / label / receiver.
    const auto snr = parseHeader(formatHeader(inputs[2]));
    QCOMPARE(snr.streamOrder, 40);
    QCOMPARE(snr.streamLabel, QString("RCVR Data"));
    QCOMPARE(snr.receiverIndex, 2);
}

void TestSeriesColumnSchema::columnHeaderSourceZeroIsUnqualified()
{
    // Source 0 (the first/only source) must produce EXACTLY the pre-multi-file
    // format -- a single-source export stays byte-identical across releases.
    const QString name = "40 - RCVR Data L_RCVR1";
    QCOMPARE(formatHeader(makeSeries(MetricType::SNR, name, /*sourceId=*/0)), name);
    QCOMPARE(formatHeader(makeSeries(MetricType::FrameSyncLock, "Ch 05", /*sourceId=*/0)),
             QString("Ch 05") + SeriesColumnSchema::kLockSuffix);
}

void TestSeriesColumnSchema::columnHeaderNonZeroSourceAddsQualifier()
{
    const QString name = "40 - RCVR Data L_RCVR1";
    QCOMPARE(formatHeader(makeSeries(MetricType::SNR, name, /*sourceId=*/1)),
             QString("S1| ") + name);
    QCOMPARE(formatHeader(makeSeries(MetricType::FrameSyncLock, "Ch 05", /*sourceId=*/2)),
             QString("S2| Ch 05") + SeriesColumnSchema::kLockSuffix);
}

void TestSeriesColumnSchema::parseSourceQualifiedSnrHeader()
{
    const auto col = parseHeader("S1| 40 - RCVR Data L_RCVR1");
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
    const auto col = parseHeader(
        QString("S2| Ch 05") + SeriesColumnSchema::kLockSuffix);
    QVERIFY(col.metricType == MetricType::FrameSyncLock);
    QCOMPARE(col.sourceId, 2);
    QCOMPARE(col.name, QString("Ch 05"));
    QCOMPARE(col.streamLabel, QString("Ch 05"));
}

void TestSeriesColumnSchema::parseUnqualifiedHeaderDefaultsToSourceZero()
{
    QCOMPARE(parseHeader("40 - RCVR Data L_RCVR1").sourceId, 0);
    QCOMPARE(parseHeader(
                 QString("Ch 05") + SeriesColumnSchema::kLockSuffix).sourceId, 0);
}

void TestSeriesColumnSchema::headerStartingWithLetterSButNotAQualifierIsUnaffected()
{
    // "Something" starts with 'S' but has no digit-then-"| " qualifier shape --
    // must NOT be misparsed as a source-qualified header.
    const auto col = parseHeader("Something");
    QCOMPARE(col.sourceId, 0);
    QCOMPARE(col.name, QString("Something"));

    // "S7Percent" -- digits follow 'S' but aren't followed by "| ", so this must
    // also be left alone (falls back to the SNR/unknown default, not stripped).
    const auto col2 = parseHeader("S7Percent");
    QCOMPARE(col2.sourceId, 0);
    QCOMPARE(col2.name, QString("S7Percent"));
}

void TestSeriesColumnSchema::roundTripPreservesSourceId()
{
    const QVector<PlotSeriesData> inputs = {
        makeSeries(MetricType::FrameSyncLock, "Ch 12", /*sourceId=*/3),
        makeSeries(MetricType::AccumulatedMissedFrames, "Ch 12", /*sourceId=*/3),
        makeSeries(MetricType::SNR, formatSnrName(40, "RCVR Data", "C_RCVR2"),
                   /*sourceId=*/5),
    };
    for (const PlotSeriesData& in : inputs)
    {
        const auto col = parseHeader(formatHeader(in));
        QVERIFY(col.metricType == in.metricType);
        QCOMPARE(col.name, in.name);        // qualifier never leaks into the name
        QCOMPARE(col.sourceId, in.sourceId);
    }
}

// ---------------------------------------------------------------------------------------------
// The differential test. The schema used to be written on QString; its implementation is kept below,
// verbatim apart from its names, as an ORACLE, and the Qt-free one is compared with it over tens of
// thousands of generated headers and field values. A table of cases someone thought of cannot show
// that a rewrite of a parser reads every header the same; this can, and it says so when the
// generator stops reaching an outcome.
//
// Not generated, by design: Unicode digits and Unicode whitespace. Qt treated both as part of a
// number; the Qt-free code is ASCII only (see textparse.h), and no export of this application
// contains either.
// ---------------------------------------------------------------------------------------------


namespace oracle {

constexpr QLatin1String kIdLabelSep(" - ");
constexpr QChar         kLabelChannelSep(u' ');
constexpr QLatin1String kReceiverToken("_RCVR");
constexpr QChar         kSourceQualifierLead(u'S');
constexpr QLatin1String kSourceQualifierSep("| ");

bool hasSnrIdPrefix(const QString& h)
{
    int digits = 0;
    while (digits < h.size() && h.at(digits).isDigit())
        digits++;
    return digits > 0 && QStringView(h).mid(digits).startsWith(kIdLabelSep);
}

QPair<int, QString> stripSourceQualifier(const QString& header)
{
    if (header.isEmpty() || header.at(0) != kSourceQualifierLead)
        return { 0, header };

    int digits = 1;
    while (digits < header.size() && header.at(digits).isDigit())
        digits++;
    if (digits == 1)
        return { 0, header };
    if (!QStringView(header).mid(digits).startsWith(kSourceQualifierSep))
        return { 0, header };

    bool ok = false;
    const int source_id = header.mid(1, digits - 1).toInt(&ok);
    if (!ok)
        return { 0, header };

    return { source_id, header.mid(digits + kSourceQualifierSep.size()) };
}

QString snrSeriesName(int pcmChannelId, const QString& streamLabel, const QString& channelName)
{
    return QString::number(pcmChannelId) + kIdLabelSep + streamLabel + kLabelChannelSep + channelName;
}

QString columnHeader(MetricType metricType, const QString& name, int sourceId)
{
    QString base;
    switch (metricType)
    {
    case MetricType::FrameSyncLock:
        base = name + SeriesColumnSchema::kLockSuffix;
        break;
    case MetricType::AccumulatedMissedFrames:
        base = name + SeriesColumnSchema::kMissedFramesSuffix;
        break;
    default:
        base = name;
        break;
    }
    if (sourceId == 0)
        return base;

    QString qualifier;
    qualifier += kSourceQualifierLead;
    qualifier += QString::number(sourceId);
    qualifier += kSourceQualifierSep;
    return qualifier + base;
}

Parsed parse(const QString& headerIn)
{
    Parsed out{ MetricType::SNR, QString(), QString(), 0, 0, 0 };

    const auto [source_id, header] = stripSourceQualifier(headerIn);
    out.sourceId = source_id;

    const QString lock_suffix   = QLatin1String(SeriesColumnSchema::kLockSuffix);
    const QString missed_suffix = QLatin1String(SeriesColumnSchema::kMissedFramesSuffix);
    const bool    looks_like_snr = hasSnrIdPrefix(header);

    if (!looks_like_snr && header.endsWith(lock_suffix))
    {
        out.metricType  = MetricType::FrameSyncLock;
        out.name        = header.left(header.size() - lock_suffix.size());
        out.streamLabel = out.name;
        return out;
    }
    if (!looks_like_snr && header.endsWith(missed_suffix))
    {
        out.metricType  = MetricType::AccumulatedMissedFrames;
        out.name        = header.left(header.size() - missed_suffix.size());
        out.streamLabel = out.name;
        return out;
    }

    out.metricType = MetricType::SNR;
    out.name       = header;

    const int dash = static_cast<int>(header.indexOf(kIdLabelSep));
    if (dash > 0)
    {
        bool id_ok = false;
        const int id = header.left(dash).toInt(&id_ok);
        if (id_ok)
            out.streamOrder = id;

        const QString rest = header.mid(dash + kIdLabelSep.size());
        const int last_space = static_cast<int>(rest.lastIndexOf(kLabelChannelSep));
        out.streamLabel = (last_space > 0) ? rest.left(last_space) : rest;
    }

    const int rcvr_pos = static_cast<int>(header.lastIndexOf(kReceiverToken));
    if (rcvr_pos >= 0)
    {
        bool ok = false;
        const int rcvr_num = header.mid(rcvr_pos + kReceiverToken.size()).toInt(&ok);
        out.receiverIndex = ok ? rcvr_num : 0;
    }

    return out;
}

} // namespace oracle

// ---------------------------------------------------------------------------------------------
// Edge cases. Written against the QString implementation before the schema moved into the Qt-free
// core, so they record what a header resolves to today rather than what the std::string rewrite
// happens to do. They go through the helpers at the top of the file, like every other case here.
// ---------------------------------------------------------------------------------------------

void TestSeriesColumnSchema::parseEdgeCasesKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<QString>("header");
    QTest::addColumn<int>("metric");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("streamLabel");
    QTest::addColumn<int>("streamOrder");
    QTest::addColumn<int>("receiverIndex");
    QTest::addColumn<int>("sourceId");

    auto row = [](const char* tag, const QString& header, int metric, const QString& name,
                  const QString& label, int order, int rcvr, int source) {
        QTest::newRow(tag) << header << metric << name << label << order << rcvr << source;
    };

    // --- empty and degenerate ------------------------------------------------------------------
    row("empty",                      "",                 0, "",             "",     0, 0, 0);
    row("suffix-only-lock",           " Lock (%)",        1, "",             "",     0, 0, 0);
    row("lock-without-leading-space", "Lock (%)",         0, "Lock (%)",     "",     0, 0, 0);
    row("suffix-then-trailing-space", "Ch Lock (%) ",     0, "Ch Lock (%) ", "",     0, 0, 0);

    // --- the SNR "<id> - <label> <channel>" shape ----------------------------------------------
    row("label-without-space",        "7 - Solo",         0, "7 - Solo",     "Solo", 7, 0, 0);
    row("empty-label",                "7 - ",             0, "7 - ",         "",     7, 0, 0);
    row("leading-zeros",              "007 - A B",        0, "007 - A B",    "A",    7, 0, 0);
    row("id-zero",                    "0 - A B",          0, "0 - A B",      "A",    0, 0, 0);
    row("id-overflows",               "99999999999 - A B", 0, "99999999999 - A B", "A", 0, 0, 0);
    row("negative-id",                "-5 - A B",         0, "-5 - A B",     "A",   -5, 0, 0);
    row("plus-id",                    "+5 - A B",         0, "+5 - A B",     "A",    5, 0, 0);
    row("leading-space-id",           " 5 - A B",         0, " 5 - A B",     "A",    5, 0, 0);
    row("no-spaces-around-dash",      "40-A B",           0, "40-A B",       "",     0, 0, 0);
    row("second-dash-stays-in-label", "5 - 6 - A B",      0, "5 - 6 - A B",  "6 - A", 5, 0, 0);
    row("two-spaces-after-dash",      "5 -  A B",         0, "5 -  A B",     " A",   5, 0, 0);

    // --- the receiver number "_RCVR<N>" --------------------------------------------------------
    row("rcvr-plain",                 "5 - A L_RCVR3",    0, "5 - A L_RCVR3", "A",   5, 3, 0);
    row("rcvr-space-before-number",   "5 - A L_RCVR 3",   0, "5 - A L_RCVR 3", "A L_RCVR", 5, 3, 0);
    row("rcvr-plus-number",           "5 - A L_RCVR+3",   0, "5 - A L_RCVR+3", "A",  5, 3, 0);
    row("rcvr-trailing-junk",         "5 - A L_RCVR3x",   0, "5 - A L_RCVR3x", "A",  5, 0, 0);
    row("rcvr-overflows",             "5 - A L_RCVR99999999999", 0, "5 - A L_RCVR99999999999", "A", 5, 0, 0);
    row("rcvr-without-number",        "5 - A L_RCVR",     0, "5 - A L_RCVR", "A",    5, 0, 0);
    row("last-rcvr-token-wins",       "5 - A L_RCVR1_RCVR2", 0, "5 - A L_RCVR1_RCVR2", "A", 5, 2, 0);
    row("rcvr-without-id-prefix",     "L_RCVR4",          0, "L_RCVR4",      "",     0, 4, 0);

    // --- the source qualifier "S<n>| " ---------------------------------------------------------
    row("qualifier-only",             "S1| ",             0, "",             "",     0, 0, 1);
    row("qualifier-needs-space",      "S1|x",             0, "S1|x",         "",     0, 0, 0);
    row("qualifier-overflows",        "S99999999999| a",  0, "S99999999999| a", "",  0, 0, 0);
    row("qualifier-zero",             "S0| a",            0, "a",            "",     0, 0, 0);
    row("qualifier-lock",             "S12| Ch 05 Lock (%)", 1, "Ch 05",     "Ch 05", 0, 0, 12);
    row("qualifier-missed",           "S3| Ch 05 Accumulated Missed Frames", 2, "Ch 05", "Ch 05", 0, 0, 3);
    row("qualifier-snr",              "S2| 40 - X L_RCVR1", 0, "40 - X L_RCVR1", "X", 40, 1, 2);
    row("qualifier-lower-case-s",     "s1| a",            0, "s1| a",        "",     0, 0, 0);
    row("qualifier-negative",         "S-1| a",           0, "S-1| a",       "",     0, 0, 0);

    // --- text outside ASCII (UTF-16 in Qt, UTF-8 in the core) ----------------------------------
    row("non-ascii-lock",             u("R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)"), 1, u("R\xC3\xA9" "cepteur \xCE\xA9"),
        u("R\xC3\xA9" "cepteur \xCE\xA9"), 0, 0, 0);
    row("non-ascii-snr",              u("40 - R\xC3\xA9" "cepteur \xCE\xA9 L_RCVR2"), 0,
        u("40 - R\xC3\xA9" "cepteur \xCE\xA9 L_RCVR2"), u("R\xC3\xA9" "cepteur \xCE\xA9"), 40, 2, 0);
    row("non-ascii-qualified",        u("S4| \xC3\x89t\xC3\xA9 Accumulated Missed Frames"), 2, u("\xC3\x89t\xC3\xA9"),
        u("\xC3\x89t\xC3\xA9"), 0, 0, 4);
}

void TestSeriesColumnSchema::parseEdgeCasesKeepTheirRecordedBehaviour()
{
    QFETCH(QString, header);
    QFETCH(int, metric);
    QFETCH(QString, name);
    QFETCH(QString, streamLabel);
    QFETCH(int, streamOrder);
    QFETCH(int, receiverIndex);
    QFETCH(int, sourceId);

    const Parsed got = parseHeader(header);
    QVERIFY2(sameParsed(oracle::parse(header), got), "the oracle and the Qt-free parser disagree on a pinned row");
    QCOMPARE(static_cast<int>(got.metricType), metric);
    QCOMPARE(got.name, name);
    QCOMPARE(got.streamLabel, streamLabel);
    QCOMPARE(got.streamOrder, streamOrder);
    QCOMPARE(got.receiverIndex, receiverIndex);
    QCOMPARE(got.sourceId, sourceId);
}

void TestSeriesColumnSchema::formatEdgeCasesKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("metric");
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("sourceId");
    QTest::addColumn<QString>("header");

    auto row = [](const char* tag, MetricType type, const QString& name, int source, const QString& header) {
        QTest::newRow(tag) << static_cast<int>(type) << name << source << header;
    };

    row("lock-empty-name",       MetricType::FrameSyncLock,           "",           0, " Lock (%)");
    row("missed-empty-name",     MetricType::AccumulatedMissedFrames, "",           0, " Accumulated Missed Frames");
    row("snr-empty-name",        MetricType::SNR,                     "",           0, "");
    row("snr-empty-qualified",   MetricType::SNR,                     "",           1, "S1| ");
    row("max-source-id",         MetricType::FrameSyncLock,           "X",          2147483647, "S2147483647| X Lock (%)");
    row("negative-source-id",    MetricType::SNR,                     "X",         -1, "S-1| X");
    row("non-ascii-name",        MetricType::FrameSyncLock,           u("\xC3\x89t\xC3\xA9 \xCE\xA9"), 2,
        u("S2| \xC3\x89t\xC3\xA9 \xCE\xA9 Lock (%)"));
    row("name-already-suffixed", MetricType::FrameSyncLock,           "X Lock (%)", 0, "X Lock (%) Lock (%)");
}

void TestSeriesColumnSchema::formatEdgeCasesKeepTheirRecordedBehaviour()
{
    QFETCH(int, metric);
    QFETCH(QString, name);
    QFETCH(int, sourceId);
    QFETCH(QString, header);
    QCOMPARE(formatHeader(static_cast<MetricType>(metric), name, sourceId), header);

    // snrSeriesName() is the same kind of formatter: the id, the label and the channel, joined.
    QCOMPARE(formatSnrName(0, "", ""), QString("0 -  "));
    QCOMPARE(formatSnrName(-3, "L C", "R"), QString("-3 - L C R"));
    QCOMPARE(formatSnrName(2147483647, u("\xC3\x89t\xC3\xA9"), u("\xCE\xA9_RCVR1")), u("2147483647 - \xC3\x89t\xC3\xA9 \xCE\xA9_RCVR1"));
}

namespace {

/// Picks one entry of a list with the test's fixed-seed generator.
const QString& pickOf(std::mt19937& rng, const QStringList& list)
{
    return list.at(static_cast<int>(rng() % static_cast<unsigned>(list.size())));
}

/// One generated column header. Half are built from the schema's own grammar (qualifier, id, separator,
/// label, channel, suffix) with the awkward variants of each part; the rest are random strings of the
/// tokens the parser looks for, so shapes nobody wrote down get tried too.
QString generateHeader(std::mt19937& rng)
{
    static const QStringList qualifiers = {
        "", "", "", "S1| ", "S2| ", "S12| ", "S0| ", "S007| ", "S1|", "S1 | ", "s1| ", "S| ", "S-1| ",
        "S+1| ", "S99999999999| ", "S2147483647| ", "S2147483648| ", "SS1| ", "S1| S2| "
    };
    static const QStringList ids = {
        "", "0", "1", "7", "40", "007", "12", "99999999999", "2147483647", "2147483648", "-5", "+5",
        " 5", "5 ", "1a", "a1", "5.5", "-", "+", "+-5"
    };
    static const QStringList separators = {
        " - ", " - ", " - ", "-", " -", "- ", " -  ", "  - ", " --", " - - ", " \xE2\x80\x93 "
    };
    static const QStringList labels = {
        "", "A", "A B", "RCVR Data", " A", "A ", "6 - A", "Lock (%)", "A  B", "S1| A",
        "R\xC3\xA9" "cepteur \xCE\xA9", "\xC3\x89t\xC3\xA9"
    };
    static const QStringList channels = {
        "", " L_RCVR1", " R_RCVR2", " C_RCVR3", " L_RCVR 3", " L_RCVR+4", " L_RCVR-1", " L_RCVR3x", " L_RCVR",
        "_RCVR2_RCVR9", " L_RCVR99999999999", " L_RCVR2147483648", " L_rcvr3", " X", " X Y", "\t_RCVR5"
    };
    static const QStringList suffixes = {
        "", "", " Lock (%)", " Accumulated Missed Frames", "Lock (%)", " Lock (%) ", " Lock (%) Lock (%)",
        " Accumulated Missed Frames Lock (%)", " lock (%)", " Lock(%)"
    };
    static const QStringList tokens = {
        " ", "  ", " - ", "-", "_RCVR", "_RCVR1", "S", "S1| ", "|", "| ", "0", "1", "42", "-7", "+3",
        " Lock (%)", " Accumulated Missed Frames", "Lock", "(%)", "A", "B C", "R\xC3\xA9", "\xCE\xA9", "\t"
    };

    if (rng() % 2 == 0)
    {
        return pickOf(rng, qualifiers) + pickOf(rng, ids) + pickOf(rng, separators) + pickOf(rng, labels)
               + pickOf(rng, channels) + pickOf(rng, suffixes);
    }
    QString out;
    const int parts = 1 + static_cast<int>(rng() % 7);
    for (int i = 0; i < parts; ++i)
        out += pickOf(rng, tokens);
    return out;
}

QString describe(const Parsed& p)
{
    return QString("metric=%1 name='%2' label='%3' order=%4 rcvr=%5 source=%6")
        .arg(static_cast<int>(p.metricType)).arg(p.name, p.streamLabel).arg(p.streamOrder)
        .arg(p.receiverIndex).arg(p.sourceId);
}

} // namespace

void TestSeriesColumnSchema::theStdImplementationReadsEveryGeneratedHeaderLikeTheQStringOne()
{
    std::mt19937 rng(20261021u);
    constexpr int kHeaders = 40000;

    int mismatches = 0;
    QStringList details;
    int metric_seen[3] = { 0, 0, 0 };
    int with_source = 0, with_order = 0, with_receiver = 0, with_label = 0, qualifier_left_in_name = 0;

    for (int n = 0; n < kHeaders; ++n)
    {
        const QString header = generateHeader(rng);
        const Parsed want = oracle::parse(header);
        const Parsed got  = parseHeader(header);

        if (!sameParsed(want, got))
        {
            ++mismatches;
            if (details.size() < 6)
                details << QString("header '%1'\n    qstring: %2\n    std:     %3").arg(header, describe(want), describe(got));
        }

        ++metric_seen[static_cast<int>(want.metricType)];
        with_source   += want.sourceId != 0;
        with_order    += want.streamOrder != 0;
        with_receiver += want.receiverIndex != 0;
        with_label    += !want.streamLabel.isEmpty();
        qualifier_left_in_name += want.sourceId == 0 && header.startsWith(QLatin1Char('S')) && want.name == header;
    }

    QVERIFY2(mismatches == 0, qPrintable(QString("%1 of %2 headers read differently:\n%3")
                                             .arg(mismatches).arg(kHeaders).arg(details.join('\n'))));

    // The generator must reach every outcome in volume, or "no mismatches" proves nothing.
    QVERIFY2(metric_seen[0] > 5000 && metric_seen[1] > 2000 && metric_seen[2] > 2000,
             qPrintable(QString("metrics seen: SNR %1, lock %2, missed %3")
                            .arg(metric_seen[0]).arg(metric_seen[1]).arg(metric_seen[2])));
    QVERIFY2(with_source > 2000 && with_order > 1500 && with_receiver > 3000 && with_label > 3000,
             qPrintable(QString("source %1, order %2, receiver %3, label %4")
                            .arg(with_source).arg(with_order).arg(with_receiver).arg(with_label)));
    QVERIFY2(qualifier_left_in_name > 500, qPrintable(QString("S-led headers left whole: %1").arg(qualifier_left_in_name)));
}

void TestSeriesColumnSchema::theStdFormattersAgreeWithTheQStringOnes()
{
    static const QStringList names = {
        "", "A", "Ch 05", "40 - RCVR Data L_RCVR1", " Lock (%)", "X Lock (%)", "S1| A", "R\xC3\xA9" "cepteur \xCE\xA9",
        "\xC3\x89t\xC3\xA9 - 7"
    };
    static const int sourceIds[] = { 0, 1, 2, 12, 99, -1, 2147483647, std::numeric_limits<int>::min() };
    static const int channelIds[] = { 0, 1, 7, 40, -3, 2147483647, std::numeric_limits<int>::min() };
    static const MetricType metrics[] = { MetricType::SNR, MetricType::FrameSyncLock,
                                          MetricType::AccumulatedMissedFrames };

    int checked = 0;
    for (const QString& name : names)
    {
        for (MetricType metric : metrics)
        {
            for (int source : sourceIds)
            {
                QCOMPARE(formatHeader(metric, name, source), oracle::columnHeader(metric, name, source));
                ++checked;
            }
        }
        for (int id : channelIds)
        {
            for (const QString& channel : names)
            {
                QCOMPARE(formatSnrName(id, name, channel), oracle::snrSeriesName(id, name, channel));
                ++checked;
            }
        }
    }
    // Every combination ran: 9 names x (3 metrics x 8 source ids + 7 channel ids x 9 channel names).
    QCOMPARE(checked, static_cast<int>(names.size()) * (3 * 8 + 7 * static_cast<int>(names.size())));

    // And the pair stays an inverse for what the application itself writes (names are written by
    // the oracle's formatter here, so this also ties the two implementations together on the way back).
    for (const QString& name : names)
    {
        if (name.startsWith(QLatin1String("S1| ")))
            continue;   // the format's one blind spot: see the next test
        for (MetricType metric : metrics)
        {
            for (int source : { 0, 1, 12 })
            {
                const Parsed got = parseHeader(oracle::columnHeader(metric, name, source));
                QCOMPARE(got.sourceId, source);
                // Not asserted: that the NAME comes back. An SNR-shaped name written with a lock metric
                // is read as SNR on purpose (parseSnrShapeWinsOverSuffix); agreement with the oracle is
                // what this loop is for, and the real combinations are the round-trip tests above.
                QVERIFY(sameParsed(got, oracle::parse(oracle::columnHeader(metric, name, source))));
            }
        }
    }
}

void TestSeriesColumnSchema::aNameThatLooksLikeASourceQualifierDoesNotRoundTrip()
{
    // A limitation of the FORMAT, present before the schema moved into the Qt-free core and kept as it
    // was: source 0 is written with no qualifier, so a series whose own NAME begins with "S<n>| " is read
    // back as source <n> with the rest as its name. Series names come from TMATS stream labels, so this
    // needs a recording labelled like that; pinned so a change to the format is a decision, not an accident.
    const QString header = formatHeader(MetricType::SNR, "S1| A", /*sourceId=*/0);
    QCOMPARE(header, QString("S1| A"));

    const Parsed got = parseHeader(header);
    QCOMPARE(got.sourceId, 1);
    QCOMPARE(got.name, QString("A"));
    QVERIFY(sameParsed(got, oracle::parse(header)));

    // Qualified on purpose it does round-trip, whatever the name (the qualifier is stripped once).
    const Parsed qualified = parseHeader(formatHeader(MetricType::SNR, "S1| A", /*sourceId=*/2));
    QCOMPARE(qualified.sourceId, 2);
    QCOMPARE(qualified.name, QString("S1| A"));
}
