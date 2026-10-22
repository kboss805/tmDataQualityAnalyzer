/**
 * @file tst_csvseries.cpp
 * @brief Implementation of the CSV series format tests (importer and exporter).
 */

#include "tst_csvseries.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include <span>
#include <vector>

#include <QDir>
#include <QFile>
#include <QMap>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVarLengthArray>
#include <QtMath>
#include <QtTest>

#include "clocktime.h"
#include "constants.h"
#include "csvseriesparser.h"
#include "csvserieswriter.h"
#include "plotviewmodel.h"
#include "processedstreamdata.h"
#include "qtbridge.h"
#include "seriescolumnschema.h"
#include "textparse.h"

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

// ---------------------------------------------------------------------------
// Import cases
// ---------------------------------------------------------------------------

struct ImportCase
{
    const char* name;
    QByteArray bytes;
    const char* expect;
};

QByteArray utf16le(const QString& text, bool bom = true)
{
    QByteArray out;
    if (bom)
        out += QByteArray("\xFF\xFE", 2);
    for (const QChar c : text)
    {
        out += char(c.unicode() & 0xFF);
        out += char(c.unicode() >> 8);
    }
    return out;
}

QByteArray utf16be(const QString& text)
{
    QByteArray out("\xFE\xFF", 2);
    for (const QChar c : text)
    {
        out += char(c.unicode() >> 8);
        out += char(c.unicode() & 0xFF);
    }
    return out;
}

QByteArray utf32le(const QString& text)
{
    QByteArray out("\xFF\xFE\x00\x00", 4);
    for (const QChar c : text)
    {
        out += char(c.unicode() & 0xFF);
        out += char(c.unicode() >> 8);
        out += QByteArray(2, '\0');
    }
    return out;
}

const QByteArray kOneLock = kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\n1:00:00:01.000,60\n";

const QList<ImportCase>& importCases()
{
    static const QList<ImportCase> cases = {
        { "basic-lock", kOneLock, "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "no-trailing-newline", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\n1:00:00:01.000,60", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "crlf", kTimeHeader + ",A Lock (%)\r\n1:00:00:00.000,50\r\n1:00:00:01.000,60\r\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "lone-cr-line-ends", kTimeHeader + ",A Lock (%)\r1:00:00:00.000,50\r1:00:00:01.000,60\r", "fail" },
        { "mixed-line-ends", kTimeHeader + ",A Lock (%)\r\n1:00:00:00.000,50\n1:00:00:01.000,60\r\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "blank-lines-between-rows", kTimeHeader + ",A Lock (%)\n\n1:00:00:00.000,50\n\n\n1:00:00:01.000,60\n\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "whitespace-only-line-is-a-short-row", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\n   \n1:00:00:01.000,60\n", "ok skipped=1 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "utf8-bom", QByteArray("\xEF\xBB\xBF") + kOneLock, "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "utf16le-bom", utf16le(QString::fromLatin1(kOneLock)), "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "utf16be-bom", utf16be(QString::fromLatin1(kOneLock)), "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        // Changed on purpose when the importer left Qt (plan step 1.2); it loaded as "basic-lock" before.
        // QTextStream detected a UTF-32 byte-order mark. Nothing writes a CSV in UTF-32, and the core
        // reads UTF-8 and UTF-16 (like TomlDocument), so it is now refused as not this format.
        { "utf32le-bom", utf32le(QString::fromLatin1(kOneLock)), "fail" },
        { "utf16le-no-bom-is-garbage", utf16le(QString::fromLatin1(kOneLock), false), "fail" },

        // --- the header line
        { "empty-file", QByteArray(), "fail" },
        { "blank-first-line", QByteArray("\n") + kOneLock, "fail" },
        { "header-only", kTimeHeader + ",A Lock (%)\n", "fail" },
        { "header-only-no-newline", kTimeHeader + ",A Lock (%)", "fail" },
        { "time-column-alone", kTimeHeader + "\n1:00:00:00.000\n", "fail" },
        { "wrong-time-header", QByteArray("Day,Time,A Lock (%)\n1,00:00:00,50\n"), "fail" },
        { "time-header-is-case-sensitive", QByteArray("time (DOY:HH:MM:SS.mmm),A Lock (%)\n1:00:00:00.000,50\n"), "fail" },
        { "time-header-padded", QByteArray("  ") + kTimeHeader + " \t,A Lock (%)\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50" },
        // Changed on purpose (plan step 1.2); it read as the lock series "A" before. Trimming is ASCII-only
        // in the core, as in textparse.h everywhere else, so a no-break space stays part of the name and
        // the column no longer ends in " Lock (%)". The application never writes one.
        { "header-padded-with-a-unicode-space", kTimeHeader + ",\xC2\xA0" "A Lock (%)\xC2\xA0\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] <U+00A0>A Lock (%)<U+00A0> label= S order=0 rcv=0 ch=0 src=0 : 0,50" },
        { "series-header-padded", kTimeHeader + ",  A Lock (%) \n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50" },
        { "trailing-comma-in-header", kTimeHeader + ",A Lock (%),\n1:00:00:00.000,50,7\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50"
            " | [1]  label= S order=0 rcv=0 ch=0 src=0 : 0,7" },
        { "empty-series-header", kTimeHeader + ",,A Lock (%)\n1:00:00:00.000,1,2\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0]  label= S order=0 rcv=0 ch=0 src=0 : 0,1"
            " | [1] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "duplicate-series-headers", kTimeHeader + ",A Lock (%),A Lock (%)\n1:00:00:00.000,1,2\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1"
            " | [1] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "non-ascii-names", kTimeHeader + ",R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] R<U+00E9>cepteur <U+03A9> label=R<U+00E9>cepteur <U+03A9> L order=1 rcv=0 ch=0 src=0 : 0,50" },
        // Changed on purpose (plan step 1.2); the whole file failed to load before. A byte that is not
        // valid UTF-8 (a Latin-1 "e-acute" from a re-saved file) made the QTextStream read give up on
        // the file. The core keeps the bytes; the plot shows the replacement character in that one name.
        { "invalid-utf8-in-a-name", kTimeHeader + ",R\xE9" "cepteur Lock (%)\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] R<U+FFFD>cepteur label=R<U+FFFD>cepteur L order=1 rcv=0 ch=0 src=0 : 0,50" },
        { "no-csv-quoting", kTimeHeader + ",\"A,B Lock (%)\"\n1:00:00:00.000,\"5\",6\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0] \"A label= S order=0 rcv=0 ch=0 src=0 :"
            " | [1] B Lock (%)\" label= S order=0 rcv=0 ch=1 src=0 : 0,6" },

        // --- the series' identity
        { "all-three-kinds", kTimeHeader + ",A Lock (%),A Accumulated Missed Frames,40 - Ch A L_RCVR2\n"
                                              "1:00:00:00.000,90,0,-80.5\n1:00:00:01.000,91,1,-79.5\n", "ok skipped=0 base=1/0 xmax=1 n=3"
            " | [0] A label=A L order=41 rcv=0 ch=0 src=0 : 0,90 1,91"
            " | [1] A label=A M order=41 rcv=0 ch=0 src=0 : 0,0 1,1"
            " | [2] 40 - Ch A L_RCVR2 label=Ch A S order=40 rcv=2 ch=0 src=0 : 0,-80.5 1,-79.5" },
        { "channel-index-counts-per-receiver-and-stream",
          kTimeHeader + ",40 - A L_RCVR1,40 - A R_RCVR1,40 - A C_RCVR1,40 - A L_RCVR2,41 - B L_RCVR1,41 - B R_RCVR1\n"
                        "1:00:00:00.000,1,2,3,4,5,6\n", "ok skipped=0 base=1/0 xmax=0 n=6"
            " | [0] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=0 : 0,1"
            " | [1] 40 - A R_RCVR1 label=A S order=40 rcv=1 ch=1 src=0 : 0,2"
            " | [2] 40 - A C_RCVR1 label=A S order=40 rcv=1 ch=2 src=0 : 0,3"
            " | [3] 40 - A L_RCVR2 label=A S order=40 rcv=2 ch=0 src=0 : 0,4"
            " | [4] 41 - B L_RCVR1 label=B S order=41 rcv=1 ch=0 src=0 : 0,5"
            " | [5] 41 - B R_RCVR1 label=B S order=41 rcv=1 ch=1 src=0 : 0,6" },
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
        { "extra-fields-ignored", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1,2,3\n1:00:00:01.000,4,5\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1,4" },
        { "empty-cell-leaves-the-series-sparse", kTimeHeader + ",A Lock (%),B Lock (%)\n1:00:00:00.000,1,\n1:00:00:01.000,,4\n1:00:00:02.000,5,6\n", "ok skipped=0 base=1/0 xmax=2 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 2,5"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 1,4 2,6" },
        { "unparseable-cell-skipped", kTimeHeader + ",A Lock (%),B Lock (%)\n1:00:00:00.000,x,2\n1:00:00:01.000,3,4\n", "ok skipped=0 base=1/0 xmax=1 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,3"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 0,2 1,4" },
        { "cell-padding-and-signs", kTimeHeader + ",A Lock (%)\n1:00:00:00.000, 5 \n1:00:00:01.000,+6\n1:00:00:02.000,-7\n1:00:00:03.000,\t8\t\n", "ok skipped=0 base=1/0 xmax=3 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,5 1,6 2,-7 3,8" },
        // Changed on purpose (plan step 1.2); the cell read as 5 before. Qt's number parser ignored
        // Unicode spaces around a number; the core's (textparse.h) ignores ASCII ones only, so this cell
        // is not a number and is skipped like any other. The application never writes one.
        { "cell-padded-with-a-unicode-space", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,\xC2\xA0" "5\xC2\xA0\n1:00:00:01.000,6\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,6" },
        { "cell-number-forms", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1e2\n1:00:00:01.000,.5\n1:00:00:02.000,5.\n1:00:00:03.000,1E-1\n1:00:00:04.000,0x10\n1:00:00:05.000,1_0\n", "ok skipped=0 base=1/0 xmax=3 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,100 1,0.5 2,5 3,0.1" },
        { "cell-special-values", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,nan\n1:00:00:01.000,inf\n1:00:00:02.000,-inf\n1:00:00:03.000,NaN\n1:00:00:04.000,Infinity\n1:00:00:05.000,+inf\n", "ok skipped=0 base=1/0 xmax=5 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,nan 1,inf 2,-inf 3,nan 5,inf" },
        { "cell-overflow-and-denormal", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1e999\n1:00:00:01.000,1e-999\n1:00:00:02.000,4.9e-324\n", "ok skipped=0 base=1/0 xmax=2 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 2,4.94065645841e-324" },
        { "cell-with-a-decimal-comma", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1;5\n1:00:00:01.000,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,2" },
        { "cell-with-non-ascii-digit", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,\xD9\xA5\n1:00:00:01.000,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,2" },
        { "every-row-bad", kTimeHeader + ",A Lock (%)\nnot a time,1\nalso not,2\n", "fail" },
        { "every-cell-bad", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,x\n1:00:00:01.000,y\n", "fail" },

        // --- the timestamp
        { "time-fields-unpadded", kTimeHeader + ",A Lock (%)\n1:0:0:0,1\n1:0:0:1.5,2\n", "ok skipped=0 base=1/0 xmax=1.5 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1.5,2" },
        { "time-fields-padded-wide", kTimeHeader + ",A Lock (%)\n001:00:00:00.000,1\n0001:00:00:01.000,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1,2" },
        { "time-fields-spaced", kTimeHeader + ",A Lock (%)\n 1 : 0 : 0 : 0 ,1\n1:00:00: 1.5,2\n", "ok skipped=0 base=1/0 xmax=1.5 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1.5,2" },
        { "time-fields-signed", kTimeHeader + ",A Lock (%)\n+1:+0:+0:+0,1\n1:00:00:01,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1,2" },
        { "time-negative-day", kTimeHeader + ",A Lock (%)\n-1:00:00:00,1\n0:00:00:00,2\n", "ok skipped=0 base=-1/0 xmax=86400 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 86400,2" },
        { "time-out-of-range-fields", kTimeHeader + ",A Lock (%)\n1:25:61:75.5,1\n1:00:00:00,2\n", "ok skipped=0 base=1/93735.5 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 -93735.5,2" },
        { "time-three-fields", kTimeHeader + ",A Lock (%)\n1:00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-five-fields", kTimeHeader + ",A Lock (%)\n1:00:00:00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-fraction-in-a-whole-field", kTimeHeader + ",A Lock (%)\n1.5:00:00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-exponent-seconds", kTimeHeader + ",A Lock (%)\n1:00:00:1e1,1\n1:00:00:01,2\n", "ok skipped=0 base=1/10 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 -9,2" },
        { "time-empty-field", kTimeHeader + ",A Lock (%)\n1::00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-crossing-midnight", kTimeHeader + ",A Lock (%)\n1:23:59:59.500,1\n2:00:00:00.250,2\n", "ok skipped=0 base=1/86399.5 xmax=0.75 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 0.75,2" },
        { "time-going-backwards", kTimeHeader + ",A Lock (%)\n1:00:00:05,1\n1:00:00:01,2\n1:00:00:09,3\n", "ok skipped=0 base=1/5 xmax=4 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 -4,2 4,3" },
        { "time-repeated", kTimeHeader + ",A Lock (%)\n1:00:00:01,1\n1:00:00:01,2\n", "ok skipped=0 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 0,2" },
        { "first-good-row-sets-the-base", kTimeHeader + ",A Lock (%)\nbad,1\n2:03:04:05.250,2\n2:03:04:06.250,3\n", "ok skipped=1 base=2/11045.25 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2 1,3" },
        { "first-row-with-an-unparseable-value-still-sets-the-base",
          kTimeHeader + ",A Lock (%)\n2:03:04:05.250,x\n2:03:04:06.250,3\n", "ok skipped=0 base=2/11045.25 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,3" },
        { "a-first-row-with-too-few-fields-does-not-set-the-base",
          kTimeHeader + ",A Lock (%),B Lock (%)\n2:03:04:05.250,1\n2:03:04:06.250,2,3\n", "ok skipped=1 base=2/11046.25 xmax=0 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 0,3" },
        { "many-columns", kTimeHeader + ",c1 Lock (%),c2 Lock (%),c3 Lock (%),c4 Lock (%),c5 Lock (%),c6 Lock (%),c7 Lock (%),c8 Lock (%)\n"
                                         "1:00:00:00,1,2,3,4,5,6,7,8\n1:00:00:01,9,10,11,12,13,14,15,16\n", "ok skipped=0 base=1/0 xmax=1 n=8"
            " | [0] c1 label=c1 L order=1 rcv=0 ch=0 src=0 : 0,1 1,9"
            " | [1] c2 label=c2 L order=2 rcv=0 ch=0 src=0 : 0,2 1,10"
            " | [2] c3 label=c3 L order=3 rcv=0 ch=0 src=0 : 0,3 1,11"
            " | [3] c4 label=c4 L order=4 rcv=0 ch=0 src=0 : 0,4 1,12"
            " | [4] c5 label=c5 L order=5 rcv=0 ch=0 src=0 : 0,5 1,13"
            " | [5] c6 label=c6 L order=6 rcv=0 ch=0 src=0 : 0,6 1,14"
            " | [6] c7 label=c7 L order=7 rcv=0 ch=0 src=0 : 0,7 1,15"
            " | [7] c8 label=c8 L order=8 rcv=0 ch=0 src=0 : 0,8 1,16" },
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
// Export cases
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
        { "milliseconds-round-half-up",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { kBase, kBase + 0.0004, kBase + 0.0005, kBase + 0.0006, kBase + 0.0015, kBase + 0.0025 },
                                          { 1, 2, 3, 4, 5, 6 }, { 0, 0, 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,2.000000,0.000000\\r\\n"
            "004:07:08:09.001,4.000000,0.000000\\r\\n"
            "004:07:08:09.002,5.000000,0.000000\\r\\n"
            "004:07:08:09.003,6.000000,0.000000\\r\\n"
            "" },
        { "milliseconds-that-are-exact-ties-either-side-of-the-base",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { kBase + 1, kBase + 1 - 0.0625, kBase + 1 - 0.1875, kBase + 1 + 0.0625, kBase + 1 + 0.1875 },
                                          { 1, 2, 3, 4, 5 }, { 0, 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.812,3.000000,0.000000\\r\\n"
            "004:07:08:09.937,2.000000,0.000000\\r\\n"
            "004:07:08:10.000,1.000000,0.000000\\r\\n"
            "004:07:08:10.063,4.000000,0.000000\\r\\n"
            "004:07:08:10.188,5.000000,0.000000\\r\\n"
            "" },
        { "six-decimal-ties-and-extremes",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1, kBase + 2, kBase + 3, kBase + 4, kBase + 5, kBase + 6, kBase + 7 },
                                          { 0.0078125, 0.0234375, 99.9999995, 1e-7, -0.0000005, 1e10, 123456789.123456789, -0.0 },
                                          { 0, 0, 0, 0, 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,0.007813,0.000000\\r\\n"
            "004:07:08:10.000,0.023438,0.000000\\r\\n"
            "004:07:08:11.000,100.000000,0.000000\\r\\n"
            "004:07:08:12.000,0.000000,0.000000\\r\\n"
            "004:07:08:13.000,-0.000000,0.000000\\r\\n"
            "004:07:08:14.000,10000000000.000000,0.000000\\r\\n"
            "004:07:08:15.000,123456789.123457,0.000000\\r\\n"
            "004:07:08:16.000,0.000000,0.000000\\r\\n"
            "" },
        { "not-a-number-and-infinity",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1, kBase + 2 },
                                          { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                            -std::numeric_limits<double>::infinity() },
                                          { 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,nan,0.000000\\r\\n"
            "004:07:08:10.000,inf,0.000000\\r\\n"
            "004:07:08:11.000,-inf,0.000000\\r\\n"
            "" },
        { "two-samples-in-one-millisecond-the-last-wins",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { kBase, kBase + 1, kBase + 1.0001, kBase + 2 }, { 1, 2, 3, 4 }, { 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000\\r\\n"
            "004:07:08:10.000,3.000000,0.000000\\r\\n"
            "004:07:08:11.000,4.000000,0.000000\\r\\n"
            "" },
        { "samples-out-of-order-are-written-in-time-order",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { kBase + 2, kBase, kBase + 1 }, { 3, 1, 2 }, { 30, 10, 20 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,10.000000\\r\\n"
            "004:07:08:10.000,2.000000,20.000000\\r\\n"
            "004:07:08:11.000,3.000000,30.000000\\r\\n"
            "" },
        { "a-day-boundary",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { 5 * 86400.0 + 86399.5, 6 * 86400.0 + 0.25 }, { 1, 2 }, { 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "006:23:59:59.500,1.000000,0.000000\\r\\n"
            "007:00:00:00.250,2.000000,0.000000\\r\\n"
            "" },
        { "a-recording-starting-just-before-midnight-and-a-long-one",
          [](PlotViewModel& vm) {
              vm.addStreamData(lockStream("A", 1, { 86399.999, 86400.0, 2 * 86400.0 + 3661.5 }, { 1, 2, 3 }, { 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "001:23:59:59.999,1.000000,0.000000\\r\\n"
            "002:00:00:00.000,2.000000,0.000000\\r\\n"
            "003:01:01:01.500,3.000000,0.000000\\r\\n"
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

// ---------------------------------------------------------------------------
// The implementations the core replaced, kept here as oracles: the importer that read with QFile and
// QTextStream into QString, the exporter that was PlotViewModel::exportCsv, and the clock formatter.
// They are verbatim apart from names and the types they hand back, so a difference with the core is a
// change of behaviour and not a difference in what was copied.
// ---------------------------------------------------------------------------

namespace oracle {

struct Series
{
    QString name;
    QString streamLabel;
    int receiverIndex = 0;
    int channelIndex = 0;
    int streamOrder = 0;
    int sourceId = 0;
    MetricType metricType = MetricType::SNR;
    QVector<double> xValues;
    QVector<double> yValues;
    double yMinCached = std::numeric_limits<double>::max();
    double yMaxCached = std::numeric_limits<double>::lowest();
};

struct Result
{
    bool success = false;
    QVector<Series> series;
    int baseDay = 0;
    double baseTimeOffset = 0.0;
    double xMax = 0.0;
    int skippedRows = 0;
};

bool parseCombinedTimestamp(QStringView stamp, int& out_day, double& out_seconds)
{
    // Format: "DDD:HH:MM:SS.mmm"
    QVarLengthArray<QStringView, 4> parts;
    for (QStringView token : qTokenize(stamp, u':', Qt::KeepEmptyParts))
    {
        parts.append(token);
    }
    if (parts.size() != 4)
    {
        return false;
    }

    bool ok_d = false;
    bool ok_h = false;
    bool ok_m = false;
    bool ok_s = false;
    int    day     = parts[0].toInt(&ok_d);
    int    hours   = parts[1].toInt(&ok_h);
    int    minutes = parts[2].toInt(&ok_m);
    double seconds = parts[3].toDouble(&ok_s);  // "SS.mmm"
    if (!ok_d || !ok_h || !ok_m || !ok_s)
    {
        return false;
    }

    out_day     = day;
    out_seconds = (hours * UIConstants::kSecondsPerHour)
                  + (minutes * UIConstants::kSecondsPerMinute)
                  + seconds;
    return true;
}

void parseDataRows(QTextStream& stream, int param_count, QVector<Series>& series,
                   int& out_base_day, double& out_base_time_offset, int& out_skipped_rows)
{
    double first_time = -1.0;
    int first_day = 0;

    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        if (line.isEmpty())
        {
            continue;
        }

        QVarLengthArray<QStringView, 64> fields;
        for (QStringView token : qTokenize(QStringView{line}, u',', Qt::KeepEmptyParts))
        {
            fields.append(token);
        }
        if (fields.size() < param_count + 1)
        {
            out_skipped_rows++;
            continue;
        }

        int day = 0;
        double time_seconds = 0.0;
        if (!parseCombinedTimestamp(fields[0], day, time_seconds))
        {
            out_skipped_rows++;
            continue;
        }

        if (first_time < 0.0)
        {
            first_day            = day;
            first_time           = time_seconds;
            out_base_day         = day;
            out_base_time_offset = time_seconds;
        }

        double elapsed = ((day - first_day) * UIConstants::kSecondsPerDay)
                         + (time_seconds - first_time);

        for (int i = 0; i < param_count; i++)
        {
            bool ok = false;
            double value = fields[i + 1].toDouble(&ok);
            if (ok)
            {
                series[i].xValues.append(elapsed);
                series[i].yValues.append(value);
                series[i].yMinCached = qMin(series[i].yMinCached, value);
                series[i].yMaxCached = qMax(series[i].yMaxCached, value);
            }
        }
    }
}

Result parse(const QString& filepath)
{
    Result result;

    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return result;
    }

    QTextStream stream(&file);

    QString header_line = stream.readLine();
    if (header_line.isEmpty())
    {
        return result;
    }

    QStringList columns = header_line.split(',');
    if (columns.size() < 2)
    {
        return result;
    }
    if (columns[0].trimmed() != QLatin1String(SeriesColumnSchema::kTimeHeader))
    {
        return result;
    }

    int param_count = static_cast<int>(columns.size() - 1);
    QVector<Series> series(param_count);

    QMap<int, QMap<int, QMap<int, int>>> stream_receiver_channel_count;

    for (int i = 0; i < param_count; i++)
    {
        Series& s = series[i];
        const SeriesColumnSchema::ParsedColumn col =
            SeriesColumnSchema::parseColumnHeader(columns[i + 1].trimmed().toStdString());
        s.name          = QString::fromStdString(col.name);
        s.streamLabel   = QString::fromStdString(col.streamLabel);
        s.metricType    = col.metricType;
        s.streamOrder   = col.streamOrder;
        s.receiverIndex = col.receiverIndex;
        s.sourceId      = col.sourceId;

        if (s.metricType == MetricType::SNR)
        {
            QMap<int, int>& receiver_channel_count =
                stream_receiver_channel_count[s.sourceId][s.streamOrder];
            s.channelIndex = receiver_channel_count.value(s.receiverIndex, 0);
            receiver_channel_count[s.receiverIndex]++;
        }
    }

    parseDataRows(stream, param_count, series, result.baseDay, result.baseTimeOffset,
                  result.skippedRows);

    file.close();

    bool has_any_data = false;
    for (const auto& s : series)
    {
        if (!s.xValues.isEmpty())
        {
            has_any_data = true;
            break;
        }
    }

    if (!has_any_data)
    {
        return result;
    }

    double x_max = 0.0;
    for (const auto& s : series)
    {
        if (!s.xValues.isEmpty() && s.xValues.last() > x_max)
        {
            x_max = s.xValues.last();
        }
    }

    result.series  = std::move(series);
    result.xMax    = x_max;
    result.success = true;
    return result;
}

/// PlotViewModel::formatTime, as it was.
QString formatTime(int base_day, double base_time_offset, double elapsed, bool includeMilliseconds)
{
    double total = base_time_offset + elapsed;

    int day = base_day + static_cast<int>(total / UIConstants::kSecondsPerDay);
    total = fmod(total, static_cast<double>(UIConstants::kSecondsPerDay));
    if (total < 0.0)
    {
        total += UIConstants::kSecondsPerDay;
        day--;
    }

    int hours   = static_cast<int>(total) / UIConstants::kSecondsPerHour;
    int minutes = (static_cast<int>(total) % UIConstants::kSecondsPerHour) / UIConstants::kSecondsPerMinute;
    int seconds = static_cast<int>(total) % UIConstants::kSecondsPerMinute;

    constexpr int kBase10 = 10;
    QString result = QString("%1:%2:%3:%4")
        .arg(day, 3, kBase10, QChar('0'))
        .arg(hours, 2, kBase10, QChar('0'))
        .arg(minutes, 2, kBase10, QChar('0'))
        .arg(seconds, 2, kBase10, QChar('0'));

    if (includeMilliseconds)
    {
        const int msec = static_cast<int>((total - qFloor(total)) * 1000.0 + 0.5);
        result += QString(".%1").arg(msec, 3, kBase10, QChar('0'));
    }
    return result;
}

/// PlotViewModel::exportCsv, as it was, over a list of series and a time base.
bool exportCsv(const QString& filepath, const QVector<Series>& all, int baseDay, double baseTimeOffset,
               int sourceId)
{
    if (all.isEmpty()) return false;

    auto included = [sourceId](const Series& s) {
        return sourceId < 0 || s.sourceId == sourceId;
    };

    bool any_included = false;
    for (const auto& s : all)
    {
        if (included(s)) { any_included = true; break; }
    }
    if (!any_included) return false;

    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream out(&file);

    out << SeriesColumnSchema::kTimeHeader;
    for (const auto& s : all)
    {
        if (included(s))
            out << "," << QString::fromStdString(
                SeriesColumnSchema::columnHeader(s.metricType, s.name.toStdString(), s.sourceId));
    }
    out << "\n";

    QMap<qint64, QMap<int, double>> rows;

    for (int i = 0; i < all.size(); ++i)
    {
        if (!included(all[i])) continue;
        const auto& s = all[i];
        for (int j = 0; j < s.xValues.size(); ++j)
        {
            qint64 ms = qRound(s.xValues[j] * 1000.0);
            rows[ms][i] = s.yValues[j];
        }
    }

    for (auto it = rows.begin(); it != rows.end(); ++it)
    {
        qint64 ms = it.key();
        double elapsed = ms / 1000.0;

        out << formatTime(baseDay, baseTimeOffset, elapsed, /*includeMilliseconds=*/true);

        const auto& vals = it.value();
        for (int i = 0; i < all.size(); ++i)
        {
            if (!included(all[i])) continue;
            if (vals.contains(i))
            {
                out << "," << QString::number(vals[i], 'f', 6);
            }
            else
            {
                out << ",";
            }
        }
        out << "\n";
    }

    file.close();
    return true;
}

}  // namespace oracle

namespace {

/// A deterministic generator, so a failure names the same case every run.
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

/// A double as exact bits, with every NaN the same, so two parses can be compared for identity.
QString bits(double v)
{
    if (std::isnan(v))
        return "nan";
    quint64 b = 0;
    std::memcpy(&b, &v, sizeof b);
    return QString::number(b, 16);
}

template <typename Vec>
QString bitsOf(const Vec& values)
{
    QStringList parts;
    for (const double v : values)
        parts << bits(v);
    return parts.join(' ');
}

QString digestOf(const oracle::Result& r)
{
    QString out = QString("%1 %2 %3 %4 %5 n=%6").arg(r.success).arg(r.baseDay).arg(bits(r.baseTimeOffset))
                      .arg(bits(r.xMax)).arg(r.skippedRows).arg(r.series.size());
    for (const oracle::Series& s : r.series)
        out += QString("\n[%1|%2|%3|%4|%5|%6|%7|%8|%9] x=%10 y=%11")
                   .arg(s.name, s.streamLabel).arg(int(s.metricType)).arg(s.receiverIndex).arg(s.channelIndex)
                   .arg(s.streamOrder).arg(s.sourceId).arg(bits(s.yMinCached), bits(s.yMaxCached))
                   .arg(bitsOf(s.xValues), bitsOf(s.yValues));
    return out;
}

QString digestOf(const CsvParseResult& r)
{
    QString out = QString("%1 %2 %3 %4 %5 n=%6").arg(r.success).arg(r.baseDay).arg(bits(r.baseTimeOffset))
                      .arg(bits(r.xMax)).arg(r.skippedRows).arg(r.series.size());
    for (const CsvSeries& s : r.series)
        out += QString("\n[%1|%2|%3|%4|%5|%6|%7|%8|%9] x=%10 y=%11")
                   .arg(QString::fromStdString(s.name), QString::fromStdString(s.streamLabel))
                   .arg(int(s.metricType)).arg(s.receiverIndex).arg(s.channelIndex).arg(s.streamOrder)
                   .arg(s.sourceId).arg(bits(s.yMin), bits(s.yMax))
                   .arg(bitsOf(s.xValues), bitsOf(s.yValues));
    return out;
}

/// A CSV as a hand-edited, re-saved or damaged file might be: headers and cells from pools of the
/// awkward and the ordinary, short and long rows, blank lines, the three line endings, a byte-order mark.
QByteArray generateCsv(Rng& rng)
{
    static const QStringList headers = {
        "A Lock (%)", "A Accumulated Missed Frames", "B Lock (%)", "40 - Ch A L_RCVR1", "40 - Ch A R_RCVR1",
        "40 - Ch A C_RCVR1", "41 - B L_RCVR2", "S2| A Lock (%)", "S1| 40 - Ch A L_RCVR1",
        "S3| B Accumulated Missed Frames", QString::fromUtf8("R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)"), "",
        " padded Lock (%) ", "x", "12 - ", "S| x", "1 - y z_RCVR", "A Lock (%) extra", "40 - Ch A L_RCVR1",
    };
    static const QStringList cells = {
        "12.5", "-3", "1e2", "+4", " 7 ", "", "", "", "x", "nan", "inf", "-inf", ".5", "5.", "0x10", "1,5",
        "1e999", "00012", "-0", "\t3", "4\t", "99.999999", "0.1", "100", "Infinity", "NaN", "-nan", "1e-3",
    };
    static const QStringList odd_stamps = {
        "1:2:3:4", " 1:00:00:00 ", "1:00:00", "1:00:00:00:00", "a:b:c:d", "1::2:3", "-1:00:00:00", "1:25:61:75.5",
        "1:00:00:1e1", "+1:+0:+0:+0", "", "x", "1.5:00:00:00", "1:00:00:00,5",
    };

    QString text;
    // header line
    const int nseries = 1 + rng.below(6);
    QString first = rng.chance(92) ? QString::fromLatin1(kTimeHeader)
                                   : rng.pick(QStringList{ "Day", "time (DOY:HH:MM:SS.mmm)", " Time (DOY:HH:MM:SS.mmm) ", "" });
    text += first;
    for (int i = 0; i < nseries; ++i)
        text += "," + rng.pick(headers);
    if (rng.chance(3))
        text += ",";

    auto eol = [&]() -> QString {
        const int r = rng.below(100);
        if (r < 70) return "\n";
        if (r < 90) return "\r\n";
        if (r < 93) return "\r\r\n";
        if (r < 96) return "\n\n";
        if (r < 98) return "\r";
        return "\r\n\r\n";
    };
    text += eol();

    const int rows = rng.below(31);
    int day = rng.below(400);
    int seconds_of_day = rng.below(86400);
    for (int r = 0; r < rows; ++r)
    {
        if (rng.chance(4))
        {
            text += rng.chance(50) ? "" : "   ";
            text += eol();
            continue;
        }
        // time advances most rows, and sometimes jumps back or across a day
        seconds_of_day += rng.below(3) == 0 ? 0 : rng.below(5);
        if (rng.chance(5)) seconds_of_day -= rng.below(10);
        if (seconds_of_day >= 86400) { seconds_of_day -= 86400; ++day; }
        if (seconds_of_day < 0) seconds_of_day = 0;
        QString stamp = rng.chance(8) ? rng.pick(odd_stamps)
                                      : QString("%1:%2:%3:%4.%5")
                                            .arg(day, rng.chance(80) ? 3 : 1, 10, QChar('0'))
                                            .arg(seconds_of_day / 3600, 2, 10, QChar('0'))
                                            .arg(seconds_of_day / 60 % 60, 2, 10, QChar('0'))
                                            .arg(seconds_of_day % 60, 2, 10, QChar('0'))
                                            .arg(rng.below(1000), 3, 10, QChar('0'));
        text += stamp;
        int fields = nseries;
        if (rng.chance(8)) fields = rng.below(nseries + 2);   // short or long row
        for (int i = 0; i < fields; ++i)
            text += "," + rng.pick(cells);
        if (r + 1 < rows || rng.chance(70))
            text += eol();
    }

    // encoding
    const int enc = rng.below(100);
    if (enc < 5)
    {
        QByteArray out("\xEF\xBB\xBF", 3);
        return out + text.toUtf8();
    }
    if (enc < 10 || enc < 15)
    {
        const bool little = enc < 10;
        QByteArray out = little ? QByteArray("\xFF\xFE", 2) : QByteArray("\xFE\xFF", 2);
        for (const QChar c : text)
        {
            const char lo = char(c.unicode() & 0xFF);
            const char hi = char(c.unicode() >> 8);
            out += little ? lo : hi;
            out += little ? hi : lo;
        }
        return out;
    }
    return text.toUtf8();
}

}  // namespace

void TestCsvSeries::theQtFreeImporterReadsEveryGeneratedFileLikeTheQtOne()
{
    constexpr int kFiles = 400;
    Rng rng(20261022u);
    int loaded = 0, failed = 0, with_skips = 0, multi = 0, bom = 0, utf16 = 0, snr = 0, lock = 0;
    QStringList problems;
    const QString path = QDir::tempPath() + "/tst_csvseries_generated.csv";
    for (int n = 0; n < kFiles; ++n)
    {
        const QByteArray bytes = generateCsv(rng);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
        f.close();

        const oracle::Result old_result = oracle::parse(path);
        const CsvParseResult new_result = CsvSeriesParser::parse(toFsPath(path));
        const QString a = digestOf(old_result);
        const QString b = digestOf(new_result);
        if (a != b && problems.size() < 3)
            problems << QString("case %1 (%2):\n--- old\n%3\n--- new\n%4").arg(n).arg(escaped(bytes.left(300))).arg(a, b);

        (old_result.success ? loaded : failed)++;
        with_skips += old_result.skippedRows > 0;
        multi += old_result.series.size() > 1;
        for (const oracle::Series& s : old_result.series)
        {
            snr += s.metricType == MetricType::SNR;
            lock += s.metricType == MetricType::FrameSyncLock;
        }
        bom += bytes.startsWith("\xEF\xBB\xBF");
        utf16 += bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF");
    }
    QFile::remove(path);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n\n")));
    // The generator must reach each outcome in volume, or "no difference" proves nothing.
    QVERIFY2(loaded > 150, qPrintable(QString::number(loaded)));
    QVERIFY2(failed > 20, qPrintable(QString::number(failed)));
    QVERIFY2(with_skips > 30, qPrintable(QString::number(with_skips)));
    QVERIFY2(multi > 150, qPrintable(QString::number(multi)));
    QVERIFY2(bom > 6, qPrintable(QString::number(bom)));
    QVERIFY2(utf16 > 20, qPrintable(QString::number(utf16)));
    QVERIFY2(snr > 150, qPrintable(QString::number(snr)));
    QVERIFY2(lock > 150, qPrintable(QString::number(lock)));
}

namespace {

/// Series for the exporter: names, kinds and sources from small pools, samples on a grid that is full
/// of exact millisecond ties (multiples of 1/16 s are halves of a millisecond at x.5) and
/// repeated, out-of-order and negative times, values that are ties at six decimals, huge, tiny,
/// negative zero, infinite and not a number.
QVector<oracle::Series> generateSeries(Rng& rng)
{
    static const QStringList names = {
        "Ch 5", "A", "", "40 - Ch 5 L_RCVR1", "40 - Ch 5 R_RCVR1", "S1| odd", QString::fromUtf8("R\xC3\xA9" "cepteur \xCE\xA9"),
        "a, b", "tab\there",
    };
    static const QVector<double> values = {
        0.0, -0.0, 1.0, -1.0, 99.9999995, 0.0078125, 0.0234375, -0.0078125, 1e-7, -5e-7, 1e10, 123456789.123456789,
        0.5, 2.5, -2.5, 100.0, 0.000001, 0.0000005, std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1e300, 4.9e-324, 12345.6789012,
    };
    QVector<oracle::Series> all;
    const int count = rng.below(6);
    for (int i = 0; i < count; ++i)
    {
        oracle::Series s;
        s.name = rng.pick(names);
        s.metricType = MetricType(rng.below(3));
        s.sourceId = rng.chance(60) ? 0 : rng.below(4);
        const int samples = rng.below(14);
        double t = rng.chance(30) ? -rng.below(5) * 0.25 : 0.0;
        for (int j = 0; j < samples; ++j)
        {
            switch (rng.below(6))
            {
            case 0: t += 1.0; break;
            case 1: t += rng.below(40) / 16.0; break;
            case 2: t += rng.below(1000) / 1000.0; break;
            case 3: t += rng.below(2000) / 2000.0; break;    // exact thousandths and their neighbours
            case 4: t -= rng.below(20) / 16.0; break;        // out of order
            default: break;                                  // the same time again
            }
            s.xValues.append(t + (rng.chance(10) ? 1e-9 * rng.below(5) : 0.0));
            s.yValues.append(rng.chance(30) ? rng.pick(values) : rng.below(100000) / 1000.0 - 20.0);
        }
        all.append(s);
    }
    return all;
}

}  // namespace

void TestCsvSeries::theQtFreeWriterWritesEveryGeneratedSeriesLikeTheQtOne()
{
    constexpr int kCases = 500;
    Rng rng(20261023u);
    int written = 0, declined = 0, multi_source = 0, with_nonfinite = 0, with_empty_cells = 0, big = 0;
    QStringList problems;
    const QString path_old = QDir::tempPath() + "/tst_csvseries_old_export.csv";
    const QString path_new = QDir::tempPath() + "/tst_csvseries_new_export.csv";
    for (int n = 0; n < kCases; ++n)
    {
        const QVector<oracle::Series> all = generateSeries(rng);
        const int base_day = rng.below(400);
        const double base_offset = rng.chance(20) ? 0.0 : (rng.chance(20) ? 86399.999 : rng.below(86400 * 100) / 100.0);
        const int source_id = rng.chance(60) ? -1 : rng.below(4);

        QFile::remove(path_old);
        QFile::remove(path_new);
        const bool old_ok = oracle::exportCsv(path_old, all, base_day, base_offset, source_id);

        std::vector<CsvColumn> columns;
        for (const oracle::Series& s : all)
        {
            if (source_id >= 0 && s.sourceId != source_id)
                continue;
            columns.push_back({ s.metricType, s.name.toStdString(), s.sourceId,
                                std::span<const double>(s.xValues.constData(), size_t(s.xValues.size())),
                                std::span<const double>(s.yValues.constData(), size_t(s.yValues.size())) });
        }
        const bool new_ok = CsvSeriesWriter::write(toFsPath(path_new), columns, CsvTimeBase{ base_day, base_offset });

        const QByteArray old_bytes = readBytes(path_old);
        const QByteArray new_bytes = readBytes(path_new);
        if ((old_ok != new_ok || old_bytes != new_bytes) && problems.size() < 3)
            problems << QString("case %1: old %2, new %3\n--- old\n%4\n--- new\n%5")
                            .arg(n).arg(old_ok).arg(new_ok).arg(escaped(old_bytes.left(600)), escaped(new_bytes.left(600)));

        (old_ok ? written : declined)++;
        multi_source += columns.size() > 1 && source_id < 0;
        with_nonfinite += old_bytes.contains("nan") || old_bytes.contains("inf");
        with_empty_cells += old_bytes.contains(",,") || old_bytes.contains(",\r\n");
        big += old_bytes.size() > 400;
    }
    QFile::remove(path_old);
    QFile::remove(path_new);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n\n")));
    QVERIFY2(written > 300, qPrintable(QString::number(written)));
    QVERIFY2(declined > 35, qPrintable(QString::number(declined)));
    QVERIFY2(multi_source > 120, qPrintable(QString::number(multi_source)));
    QVERIFY2(with_nonfinite > 12, qPrintable(QString::number(with_nonfinite)));
    QVERIFY2(with_empty_cells > 120, qPrintable(QString::number(with_empty_cells)));
    QVERIFY2(big > 120, qPrintable(QString::number(big)));
}

void TestCsvSeries::theClockStampIsTheSameAsTheQStringOne()
{
    constexpr int kCases = 8000;
    Rng rng(20261024u);
    int negative = 0, past_a_day = 0, with_ms = 0;
    for (int n = 0; n < kCases; ++n)
    {
        const int base_day = rng.chance(5) ? -rng.below(5) : rng.below(1000);
        const double base_offset = rng.chance(20) ? 0.0 : rng.below(86400 * 1000) / 1000.0;
        double elapsed;
        switch (rng.below(4))
        {
        case 0: elapsed = rng.below(2000000) / 1000.0; break;
        case 1: elapsed = -rng.below(200000) / 1000.0; break;
        case 2: elapsed = (rng.below(2000000) - 1000000) / 1024.0; break;
        default: elapsed = (double(rng.below(1000000)) - 500000.0) * 0.001 + rng.below(1000) * 1e-7; break;
        }
        for (const bool ms : { false, true })
        {
            const QString want = oracle::formatTime(base_day, base_offset, elapsed, ms);
            const QString got = QString::fromStdString(clocktime::format(base_day, base_offset, elapsed, ms));
            QVERIFY2(want == got, qPrintable(QString("day %1 offset %2 elapsed %3 ms=%4: Qt '%5', core '%6'")
                                                  .arg(base_day).arg(base_offset, 0, 'g', 17).arg(elapsed, 0, 'g', 17)
                                                  .arg(ms).arg(want, got)));
        }
        negative += elapsed < 0;
        past_a_day += (base_offset + elapsed) >= 86400.0 || (base_offset + elapsed) < 0.0;
        with_ms += (elapsed - std::floor(elapsed)) != 0.0;
    }
    QVERIFY2(negative > 800, qPrintable(QString::number(negative)));
    QVERIFY2(past_a_day > 400, qPrintable(QString::number(past_a_day)));
    QVERIFY2(with_ms > 2000, qPrintable(QString::number(with_ms)));
}

void TestCsvSeries::numbersReadLikeQt()
{
    // The importer reads every cell and every timestamp field with textparse. A cell the Qt parser
    // took must read to the same bits, and a cell it refused must be refused - a spreadsheet's
    // "Infinity" or "NaN(1)" is not a number to Qt, and std::from_chars would say it is.
    QStringList texts = {
        "", " ", "+", "-", "nan", "NaN", "NAN", "-nan", "+nan", "inf", "Inf", "INF", "+inf", "-inf", "-INF",
        "infinity", "Infinity", "-Infinity", "+infinity", "nan(1)", "NaN(abc)", "nan()", "infin", "in", "na", "n", "i",
        "1e5", "1E5", "1e", "1e+", "1e-", "e5", ".", "5.", ".5", "-.5", "+.5", "-.", "+.", "0x10", "0X1p3", "0b101",
        "1_0", "1 0", "1,5", "1.5.5", "--5", "+-5", "-+5", "++5", "1e400", "-1e400", "1e-400", "4.9e-324",
        "2.2250738585072011e-308", "1.7976931348623157e308", "1.7976931348623159e308", "00005", "5e0005", "5e+05",
        " 5", "5 ", " 5 ", "\t5\n", "5\x0b", "5\x0c", "5\r", "\n5", "5f", "5d", "5l", "1e1e1", "0", "-0", "+0", "0.0", "-0.0",
        "123456789012345678901234567890", "0.1", "0.30000000000000004", "1e22", "1e23", "9007199254740993", "100", "007",
        "2147483647", "2147483648", "-2147483648", "-2147483649", "+2147483647", "1.", "1.e5", ".e5", "E5",
    };
    // Plus a few thousand strings over the alphabet where the cases above live.
    const QString alphabet = QStringLiteral("0123456789+-.eE  infaINFNAx(),_\t");
    quint32 state = 20261022u;
    auto next = [&state]() { state = state * 1664525u + 1013904223u; return state >> 8; };
    for (int i = 0; i < 20000; ++i)
    {
        QString t;
        const int len = int(next() % 9);
        for (int k = 0; k < len; ++k)
            t += alphabet[int(next() % quint32(alphabet.size()))];
        texts << t;
    }

    QStringList problems;
    int accepted = 0;
    int refused = 0;
    for (const QString& text : texts)
    {
        bool qt_ok = false;
        const double qt_value = text.toDouble(&qt_ok);
        double value = 0.0;
        const bool ok = textparse::parseDouble(text.toStdString(), value);
        bool same = (ok == qt_ok);
        if (same && ok)
            same = (std::isnan(value) && std::isnan(qt_value)) || std::memcmp(&value, &qt_value, sizeof value) == 0;
        if (!same && problems.size() < 12)
            problems << QString("double '%1': Qt %2 %3, textparse %4 %5").arg(escaped(text.toUtf8()))
                            .arg(qt_ok).arg(num(qt_value)).arg(ok).arg(num(value));
        (qt_ok ? accepted : refused)++;

        bool qt_int_ok = false;
        const int qt_int = text.toInt(&qt_int_ok);
        int int_value = 0;
        const bool int_ok = textparse::parseInteger(text.toStdString(), int_value);
        if ((int_ok != qt_int_ok || (int_ok && int_value != qt_int)) && problems.size() < 12)
            problems << QString("int '%1': Qt %2 %3, textparse %4 %5").arg(escaped(text.toUtf8()))
                            .arg(qt_int_ok).arg(qt_int).arg(int_ok).arg(int_value);
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n")));
    // The generator must reach both outcomes in volume, or "no difference" proves nothing.
    QVERIFY2(accepted > 1000, qPrintable(QString::number(accepted)));
    QVERIFY2(refused > 5000, qPrintable(QString::number(refused)));
}

void TestCsvSeries::aUtf16FileWithCrlfLineEndsLoads()
{
    // A spreadsheet's "Unicode text" is UTF-16 with CR LF: the core decodes before it looks for line
    // ends, as the Qt reader did.
    const QByteArray bytes = utf16le(QString::fromLatin1(kTimeHeader + ",A Lock (%)\r\n1:00:00:00.000,50\r\n1:00:00:01.000,60\r\n"));
    const QString path = writeBytes(bytes);
    const oracle::Result old_result = oracle::parse(path);
    const CsvParseResult result = CsvSeriesParser::parse(toFsPath(path));
    QFile::remove(path);
    QVERIFY(old_result.success);
    QCOMPARE(digestOf(result), digestOf(old_result));
    QVERIFY(result.success);
    QCOMPARE(int(result.series.size()), 1);
    QCOMPARE(int(result.series[0].xValues.size()), 2);
    QCOMPARE(result.series[0].yValues[1], 60.0);
}

void TestCsvSeries::aLargeExportCrossesTheFlushThresholdIntact()
{
    // The writer hands its buffer to the file whenever it passes a mebibyte; every other test writes a
    // few hundred bytes, so a flush that lost or repeated a chunk would pass them all. 30,000 rows of
    // three columns is about 1.5 MB, compared byte for byte with what the Qt exporter wrote.
    constexpr int kRows = 30000;
    QVector<oracle::Series> all(3);
    for (int c = 0; c < 3; ++c)
    {
        all[c].name = QString("Ch %1").arg(c);
        all[c].metricType = MetricType::FrameSyncLock;
        for (int r = 0; r < kRows; ++r)
        {
            all[c].xValues.append(r * 0.01);
            all[c].yValues.append(50.0 + (r % 997) * 0.0625 + c);
        }
    }
    const QString path_old = QDir::tempPath() + "/tst_csvseries_big_old.csv";
    const QString path_new = QDir::tempPath() + "/tst_csvseries_big_new.csv";
    QVERIFY(oracle::exportCsv(path_old, all, 3, 1234.5, -1));

    std::vector<CsvColumn> columns;
    for (const oracle::Series& s : all)
        columns.push_back({ s.metricType, s.name.toStdString(), s.sourceId,
                            std::span<const double>(s.xValues.constData(), size_t(s.xValues.size())),
                            std::span<const double>(s.yValues.constData(), size_t(s.yValues.size())) });
    QVERIFY(CsvSeriesWriter::write(toFsPath(path_new), columns, CsvTimeBase{ 3, 1234.5 }));

    const QByteArray old_bytes = readBytes(path_old);
    const QByteArray new_bytes = readBytes(path_new);
    QFile::remove(path_old);
    QFile::remove(path_new);
    QVERIFY2(old_bytes.size() > (1 << 20) + 100000, qPrintable(QString::number(old_bytes.size())));   // past the threshold
    QCOMPARE(new_bytes.size(), old_bytes.size());
    QVERIFY(new_bytes == old_bytes);
}

void TestCsvSeries::anExportReplacesTheFileThatWasThere()
{
    const QString path = QDir::tempPath() + "/tst_csvseries_replace.csv";
    QFile::remove(path);

    PlotViewModel longer;
    longer.addStreamData(lockStream("A", 1, { kBase, kBase + 1, kBase + 2, kBase + 3 }, { 1, 2, 3, 4 }, { 0, 0, 0, 0 }));
    QVERIFY(longer.exportCsv(path));

    PlotViewModel shorter;
    shorter.addStreamData(lockStream("B", 2, { kBase }, { 9 }, { 0 }));
    QVERIFY(shorter.exportCsv(path));

    const QByteArray replaced = readBytes(path);
    QFile::remove(path);
    QVERIFY(replaced.contains("B Lock"));
    QVERIFY(!replaced.contains("A Lock"));
    QCOMPARE(replaced.count('\n'), 2);   // the header and the one row, nothing left of the longer file
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
