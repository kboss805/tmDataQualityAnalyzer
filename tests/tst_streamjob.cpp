/**
 * @file tst_streamjob.cpp
 * @brief Implementation of the tests of what MainViewModel does around the stream-job builder.
 */

#include "tst_streamjob.h"

#include <functional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "framesetup.h"
#include "mainviewmodel.h"
#include "pipelinerunner.h"
#include "streamconfig.h"

namespace {

// ---------------------------------------------------------------------------
// Helpers. Everything the tests do with the job builder goes through build() and digest(), so a move
// of the code behind them changes these bodies and no assertion.
// ---------------------------------------------------------------------------

struct Built
{
    bool ok = false;
    QString error;
    QStringList log;
    StreamJob job;
};

/// Which receiver-parameters file the application root offers as its default.
enum class DefaultFile { None, Good, PartlyOutsideFrame, MetadataOnly, Garbage };

/// A directory with every receiver-parameters file the cases use, and application roots built over it.
struct Fixtures
{
    QTemporaryDir dir;

    Fixtures()
    {
        write("good3.toml", "[L_RCVR1]\nWord = 1\n[R_RCVR1]\nWord = 2\n[C_RCVR1]\nWord = 3\n");
        write("partly-outside.toml", "[L_RCVR1]\nWord = 1\n[R_RCVR1]\nWord = 9999\n");
        write("outside-only.toml", "[L_RCVR1]\nWord = 9999\n");
        write("metadata.toml", "[Parameters]\nPolarity = 0\n");
        write("garbage.toml", "this is {{ not toml\n");
        write("empty.toml", "");
        write("word-zero.toml", "[L_RCVR1]\nWord = 0\n");
        write("word-50.toml", "[L_RCVR1]\nWord = 50\n");
        write("word-51.toml", "[L_RCVR1]\nWord = 51\n");
        write("unicode-\xCE\xA9.toml", "[L_RCVR1]\nWord = 1\n");
        QDir().mkpath(dir.filePath("a-folder.toml"));
    }

    void write(const QString& name, const QByteArray& bytes)
    {
        QFile f(dir.filePath(QString::fromUtf8(name.toUtf8())));
        if (f.open(QIODevice::WriteOnly))
            f.write(bytes);
    }

    QString path(const QString& name) const { return dir.filePath(name); }

    /// An application root whose settings/receiver_params/default.toml is the named fixture.
    QString appRoot(DefaultFile kind) const
    {
        static const char* sources[] = { nullptr, "good3.toml", "partly-outside.toml", "metadata.toml", "garbage.toml" };
        const QString root = dir.filePath(QString("root-%1").arg(int(kind)));
        QDir().mkpath(root + "/settings/receiver_params");
        if (sources[int(kind)])
        {
            QFile::remove(root + "/settings/receiver_params/default.toml");
            QFile::copy(path(sources[int(kind)]), root + "/settings/receiver_params/default.toml");
        }
        return root;
    }
};

Built build(const StreamConfig& cfg, const QString& app_root, bool swap_bytes)
{
    MainViewModel vm;
    TestStreamJob::setAppRoot(vm, app_root);
    vm.setSwapBytes(swap_bytes);
    QSignalSpy log(&vm, &MainViewModel::logMessageReceived);

    ProcessingParams base;
    base.timeChannelId = 3;
    base.sourceId = 2;
    base.startSeconds = 11;
    base.stopSeconds = 99;
    base.filename = "base.ch10";

    Built out;
    out.ok = TestStreamJob::buildStreamJob(vm, cfg, base, out.job, out.error);
    for (const QList<QVariant>& args : log)
        out.log << args.at(0).toString();
    return out;
}

QString num(double v) { return QString::number(v, 'g', 15); }

QString asciiOnly(const QString& s)
{
    QString out;
    for (const QChar c : s)
        out += (c.unicode() < 0x80) ? QString(c) : QString("<U+%1>").arg(uint(c.unicode()), 4, 16, QChar('0')).toUpper();
    return out;
}

/// The outcome in one line: whether it was built, the message, the log, and - for a job that was built -
/// every worker parameter the configuration decides and the whole frame-parameter table.
QString digest(const Built& b)
{
    QString out = QString("%1 error='%2' log=[%3]").arg(b.ok ? "ok" : "rejected", asciiOnly(b.error), asciiOnly(b.log.join(" | ")));
    if (!b.ok)
        return out;
    const ProcessingParams& p = b.job.params;
    out += QString(" | file=%1 src=%2 time=%3 pcm=%4 sync=%5/%6/%7 words=%8 bits=%9 window=%10-%11 period=%12 rand=%13 inv=%14 swap=%15 mode=%16 rate=%17 label=%18")
               .arg(QString::fromStdString(p.filename.string())).arg(p.sourceId).arg(p.timeChannelId).arg(p.pcmChannelId)
               .arg(p.frameSync, 0, 16).arg(p.frameSyncMask, 0, 16).arg(p.syncPatternLength)
               .arg(p.wordsInMinorFrame).arg(p.bitsInMinorFrame).arg(p.startSeconds).arg(p.stopSeconds)
               .arg(num(p.samplePeriodSec)).arg(p.isRandomized).arg(p.isInverted).arg(p.swapBytes)
               .arg(int(p.mode)).arg(num(p.dataRateBps)).arg(asciiOnly(QString::fromStdString(p.streamLabel)));
    out += " | setup:";
    if (!b.job.frameSetup)
        return out + " none";
    out += QString(" n=%1").arg(b.job.frameSetup->length());
    for (int i = 0; i < b.job.frameSetup->length(); ++i)
    {
        const ParameterInfo* q = b.job.frameSetup->getParameter(i);
        out += QString(" [%1 w=%2 slope=%3 scale=%4 en=%5 profile=%6]").arg(QString::fromStdString(q->name)).arg(q->word)
                   .arg(num(q->slope), num(q->scale)).arg(q->is_enabled).arg(q->profile.valid);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

struct Case
{
    const char* name;
    std::function<void(StreamConfig&, const Fixtures&)> adjust;
    DefaultFile default_file;
    bool swap_bytes;
    const char* expect;
};

StreamConfig validReceiver(const Fixtures& f)
{
    StreamConfig cfg;
    cfg.pcmChannelId          = 5;
    cfg.label                 = "Rcvr";
    cfg.mode                  = StreamMode::ReceiverChannelInfo;
    cfg.sync.pattern          = "FE6B2840";
    cfg.sync.mask             = "FFFFFFFF";
    cfg.sync.bitsInMinorFrame = 800;
    cfg.samplePeriodIndex     = 1;
    cfg.polarityIndex         = 0;
    cfg.slopeIndex            = 0;
    cfg.scaleDdBPerV          = 10.0;
    cfg.numReceivers          = 2;
    cfg.receiverChannels      = 3;
    cfg.receiverParamsToml    = f.path("good3.toml");
    return cfg;
}

CalibrationProfile profile()
{
    CalibrationProfile p;
    p.valid = true;
    p.points = { {1000.0, 0.0}, {2000.0, 6.0} };
    return p;
}

using Adj = std::function<void(StreamConfig&, const Fixtures&)>;
const QList<Case>& cases()
{
    // The rows whose answer shows what the wrapper does: it carries the user's Qt settings across (labels, the sync fields,
    // the file names, the profiles), reads the application root and byte order it holds, and says what the builder found as
    // QStrings and signals. Every other configuration is built, or refused, on the builder itself in
    // native/tmdq_core_tests/test_streamjob.cpp.
    static const QList<Case> list = {
        { "lock-only-has-an-empty-setup", Adj([](StreamConfig& c, const Fixtures&) { c.mode = StreamMode::FrameSyncLockStats; }), DefaultFile::None, true, "ok error='' log=[]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=1 rate=0 label=Rcvr"
            " | setup: n=0" },
        { "explicit-file-three-parameters", Adj([](StreamConfig&, const Fixtures&) {}), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "negative-polarity", Adj([](StreamConfig& c, const Fixtures&) { c.polarityIndex = 1; c.slopeIndex = 1; c.scaleDdBPerV = 12.5; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=-0.00190737773708705 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=-0.00190737773708705 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=-0.00190737773708705 scale=-32767.5 en=1 profile=0]" },
        { "slope-index-below-the-range", Adj([](StreamConfig& c, const Fixtures&) { c.slopeIndex = -1; }), DefaultFile::None, true, "rejected error='Rcvr: Slope index is invalid.' log=[]" },
        { "scale-zero", Adj([](StreamConfig& c, const Fixtures&) { c.scaleDdBPerV = 0.0; }), DefaultFile::None, true, "rejected error='Rcvr: Scale (dB/V) must be a positive number.' log=[]" },
        { "file-missing", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "file-name-with-non-ascii-letters", Adj([](StreamConfig& c, const Fixtures& f) {
              c.receiverParamsToml = f.path(QString::fromUtf8("unicode-\xCE\xA9.toml")); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "file-missing-with-a-non-ascii-name", Adj([](StreamConfig& c, const Fixtures& f) {
              c.label = QString::fromUtf8("R\xC3\xA9" "cepteur"); c.receiverParamsToml = f.path(QString::fromUtf8("absent-\xCE\xA9.toml")); }), DefaultFile::None, true, "rejected error='R<U+00E9>cepteur: Receiver Parameters file 'absent-<U+03A9>.toml' was not found.' log=[]" },
        { "no-file-and-no-default-builds-the-default-map", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "no-file-uses-the-default-file", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::Good, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "two-profiles-both-attach", Adj([](StreamConfig& c, const Fixtures&) { c.calibrationByWord.insert(0, profile()); c.calibrationByWord.insert(2, profile()); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 2 profile(s), attached 2 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]" },
        { "receiver-stream-parameters", Adj([](StreamConfig& c, const Fixtures&) {
              c.sync.randomized = true; c.sync.inverted = true; c.sync.dataRateMbps = 2.5; c.samplePeriodIndex = 2; c.pcmChannelId = 21; }), DefaultFile::None, false, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=21 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.01 rand=1 inv=1 swap=0 mode=0 rate=2500000 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "sample-period-1s", Adj([](StreamConfig& c, const Fixtures&) { c.samplePeriodIndex = 0; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "the-descriptions-use-the-label-or-the-channel", Adj([](StreamConfig& c, const Fixtures&) { c.label.clear(); c.scaleDdBPerV = -1; }), DefaultFile::None, true, "rejected error='Ch 5: Scale (dB/V) must be a positive number.' log=[]" },
        { "a-bad-pattern-is-reported-before-a-bad-file", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.pattern = "ZZ"; c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Invalid frame sync pattern 'ZZ'.' log=[]" },
    };
    return list;
}

}  // namespace

bool TestStreamJob::buildStreamJob(MainViewModel& vm, const StreamConfig& cfg, const ProcessingParams& base,
                                   StreamJob& job, QString& error)
{
    return vm.buildStreamJob(cfg, base, job, error);
}

void TestStreamJob::setAppRoot(MainViewModel& vm, const QString& root)
{
    vm.m_app_root = root;
}

void TestStreamJob::jobsKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < cases().size(); ++i)
        QTest::newRow(cases()[i].name) << i;
}

void TestStreamJob::jobsKeepTheirRecordedBehaviour()
{
    QFETCH(int, index);
    static const Fixtures fixtures;
    QVERIFY(fixtures.dir.isValid());
    const Case& c = cases()[index];

    StreamConfig cfg = validReceiver(fixtures);
    c.adjust(cfg, fixtures);
    const QString got = digest(build(cfg, fixtures.appRoot(c.default_file), c.swap_bytes));

    QCOMPARE(got, QString::fromLatin1(c.expect));
}
