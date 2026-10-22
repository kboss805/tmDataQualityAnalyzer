/**
 * @file tst_streamjob.cpp
 * @brief Implementation of the stream-job construction tests.
 */

#include "tst_streamjob.h"

#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "framesetup.h"
#include "framesync.h"
#include "mainviewmodel.h"
#include "pipelinerunner.h"
#include "qtbridge.h"
#include "streamconfig.h"
#include "streamjobbuilder.h"

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
    static const QList<Case> list = {
        // --- lock-only: no receiver parameters at all
        { "lock-only-has-an-empty-setup", Adj([](StreamConfig& c, const Fixtures&) { c.mode = StreamMode::FrameSyncLockStats; }), DefaultFile::None, true, "ok error='' log=[]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=1 rate=0 label=Rcvr"
            " | setup: n=0" },
        { "lock-only-ignores-the-receiver-fields", Adj([](StreamConfig& c, const Fixtures&) {
              c.mode = StreamMode::FrameSyncLockStats; c.scaleDdBPerV = -1; c.slopeIndex = 99; c.receiverParamsToml = "no-such-file.toml"; }), DefaultFile::None, false, "ok error='' log=[]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=0 mode=1 rate=0 label=Rcvr"
            " | setup: n=0" },

        // --- an explicit receiver-parameters file, and the linear calibration
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
        { "any-other-polarity-reads-as-positive", Adj([](StreamConfig& c, const Fixtures&) { c.polarityIndex = 5; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "every-valid-slope-index-0", Adj([](StreamConfig& c, const Fixtures&) { c.slopeIndex = 0; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "every-valid-slope-index-1", Adj([](StreamConfig& c, const Fixtures&) { c.slopeIndex = 1; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00152590218966964 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00152590218966964 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00152590218966964 scale=-32767.5 en=1 profile=0]" },
        { "every-valid-slope-index-2", Adj([](StreamConfig& c, const Fixtures&) { c.slopeIndex = 2; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00152590218966964 scale=0 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00152590218966964 scale=0 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00152590218966964 scale=0 en=1 profile=0]" },
        { "every-valid-slope-index-3", Adj([](StreamConfig& c, const Fixtures&) { c.slopeIndex = 3; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.000762951094834821 scale=0 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.000762951094834821 scale=0 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.000762951094834821 scale=0 en=1 profile=0]" },
        { "slope-index-below-the-range", Adj([](StreamConfig& c, const Fixtures&) { c.slopeIndex = -1; }), DefaultFile::None, true, "rejected error='Rcvr: Slope index is invalid.' log=[]" },
        { "slope-index-above-the-range", Adj([](StreamConfig& c, const Fixtures&) { c.slopeIndex = 4; }), DefaultFile::None, true, "rejected error='Rcvr: Slope index is invalid.' log=[]" },
        { "scale-zero", Adj([](StreamConfig& c, const Fixtures&) { c.scaleDdBPerV = 0.0; }), DefaultFile::None, true, "rejected error='Rcvr: Scale (dB/V) must be a positive number.' log=[]" },
        { "scale-negative", Adj([](StreamConfig& c, const Fixtures&) { c.scaleDdBPerV = -3.0; }), DefaultFile::None, true, "rejected error='Rcvr: Scale (dB/V) must be a positive number.' log=[]" },
        { "scale-not-a-number", Adj([](StreamConfig& c, const Fixtures&) { c.scaleDdBPerV = std::numeric_limits<double>::quiet_NaN(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=nan scale=nan en=1 profile=0]"
            " [R_RCVR1 w=1 slope=nan scale=nan en=1 profile=0]"
            " [C_RCVR1 w=2 slope=nan scale=nan en=1 profile=0]" },
        { "scale-tiny", Adj([](StreamConfig& c, const Fixtures&) { c.scaleDdBPerV = 1e-9; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=3.05180437933928e-13 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=3.05180437933928e-13 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=3.05180437933928e-13 scale=-32767.5 en=1 profile=0]" },
        { "scale-large", Adj([](StreamConfig& c, const Fixtures&) { c.scaleDdBPerV = 1e6; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=305.180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=305.180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=305.180437933928 scale=-32767.5 en=1 profile=0]" },

        // --- an explicit file that cannot be used
        { "file-missing", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "file-missing-unlabelled", Adj([](StreamConfig& c, const Fixtures& f) { c.label.clear(); c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Ch 5: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "file-missing-named-with-backslashes", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml = "C:\\nowhere\\sub\\absent.toml"; }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "file-is-a-folder", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("a-folder.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'a-folder.toml'. Check the word map.' log=[]" },
        { "file-has-a-word-outside-the-frame", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("outside-only.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'outside-only.toml'. Check the word map.' log=[]" },
        { "file-with-one-good-and-one-bad-word", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("partly-outside.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'partly-outside.toml'. Check the word map.' log=[]" },
        { "file-with-only-metadata", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("metadata.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file contains no parameters.' log=[]" },
        { "file-is-not-toml", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("garbage.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'garbage.toml'. Check the word map.' log=[]" },
        { "file-is-empty", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("empty.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'empty.toml'. Check the word map.' log=[]" },
        { "file-with-word-zero", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("word-zero.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-zero.toml'. Check the word map.' log=[]" },
        { "word-50-in-800-bits", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.bitsInMinorFrame = 800; c.receiverParamsToml = f.path("word-50.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-50.toml'. Check the word map.' log=[]" },
        { "word-50-in-801-bits", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.bitsInMinorFrame = 801; c.receiverParamsToml = f.path("word-50.toml"); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=51 bits=801 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=49 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "word-51-in-800-bits", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.bitsInMinorFrame = 800; c.receiverParamsToml = f.path("word-51.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-51.toml'. Check the word map.' log=[]" },
        { "word-51-in-801-bits", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.bitsInMinorFrame = 801; c.receiverParamsToml = f.path("word-51.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-51.toml'. Check the word map.' log=[]" },
        { "word-51-in-816-bits", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.bitsInMinorFrame = 816; c.receiverParamsToml = f.path("word-51.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-51.toml'. Check the word map.' log=[]" },
        { "file-name-with-non-ascii-letters", Adj([](StreamConfig& c, const Fixtures& f) {
              c.receiverParamsToml = f.path(QString::fromUtf8("unicode-\xCE\xA9.toml")); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "file-missing-with-a-non-ascii-name", Adj([](StreamConfig& c, const Fixtures& f) {
              c.label = QString::fromUtf8("R\xC3\xA9" "cepteur"); c.receiverParamsToml = f.path(QString::fromUtf8("absent-\xCE\xA9.toml")); }), DefaultFile::None, true, "rejected error='R<U+00E9>cepteur: Receiver Parameters file 'absent-<U+03A9>.toml' was not found.' log=[]" },

        // --- no file named: the shipped default file, then the default word map
        { "no-file-and-no-default-builds-the-default-map", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "no-file-default-map-one-by-one", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.numReceivers = 1; c.receiverChannels = 1; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "no-file-default-map-fills-all-but-the-sync-word", Adj([](StreamConfig& c, const Fixtures&) {
              c.receiverParamsToml.clear(); c.numReceivers = 7; c.receiverChannels = 7; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=49"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH4_RCVR1 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH5_RCVR1 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH6_RCVR1 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH7_RCVR1 w=6 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=7 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=8 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=9 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH4_RCVR2 w=10 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH5_RCVR2 w=11 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH6_RCVR2 w=12 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH7_RCVR2 w=13 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR3 w=14 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR3 w=15 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR3 w=16 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH4_RCVR3 w=17 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH5_RCVR3 w=18 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH6_RCVR3 w=19 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH7_RCVR3 w=20 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR4 w=21 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR4 w=22 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR4 w=23 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH4_RCVR4 w=24 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH5_RCVR4 w=25 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH6_RCVR4 w=26 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH7_RCVR4 w=27 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR5 w=28 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR5 w=29 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR5 w=30 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH4_RCVR5 w=31 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH5_RCVR5 w=32 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH6_RCVR5 w=33 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH7_RCVR5 w=34 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR6 w=35 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR6 w=36 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR6 w=37 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH4_RCVR6 w=38 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH5_RCVR6 w=39 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH6_RCVR6 w=40 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH7_RCVR6 w=41 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR7 w=42 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR7 w=43 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR7 w=44 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH4_RCVR7 w=45 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH5_RCVR7 w=46 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH6_RCVR7 w=47 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [CH7_RCVR7 w=48 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "no-file-default-map-too-big", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.numReceivers = 50; c.receiverChannels = 1; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-default-map-no-receivers", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.numReceivers = 0; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-default-map-negative-channels", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.receiverChannels = -2; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-default-map-product-overflows", Adj([](StreamConfig& c, const Fixtures&) {
              c.receiverParamsToml.clear(); c.numReceivers = 100000; c.receiverChannels = 100000; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-uses-the-default-file", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::Good, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "an-explicit-file-beats-the-default-file", Adj([](StreamConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path(QString::fromUtf8("unicode-\xCE\xA9.toml")); }), DefaultFile::Good, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        // Changed on purpose (plan step 1.5); this built SEVEN parameters, L_RCVR1 twice. A default.toml that
        // loads in part before it hits a word outside the frame leaves the words it did read in the table, and
        // the fallback to the default word map APPENDED to them, so the job processed with a map nobody wrote.
        // FrameSetup::saveReceiverParamsFile already cleared first; building a job did not. It now does, and
        // a partly usable default file falls back to the same six-parameter map as an unusable one.
        { "default-file-partly-outside-the-frame", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::PartlyOutsideFrame, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "default-file-with-only-metadata", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::MetadataOnly, true, "rejected error='Rcvr: Receiver Parameters file contains no parameters.' log=[]" },
        { "default-file-is-not-toml", Adj([](StreamConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::Garbage, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },

        // --- calibration profiles
        { "two-profiles-both-attach", Adj([](StreamConfig& c, const Fixtures&) { c.calibrationByWord.insert(0, profile()); c.calibrationByWord.insert(2, profile()); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 2 profile(s), attached 2 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]" },
        { "a-profile-for-a-word-the-map-lacks", Adj([](StreamConfig& c, const Fixtures&) { c.calibrationByWord.insert(9, profile()); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 1 profile(s), attached 0 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "an-invalid-profile-does-not-attach", Adj([](StreamConfig& c, const Fixtures&) { c.calibrationByWord.insert(1, CalibrationProfile()); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 1 profile(s), attached 0 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "profiles-are-never-looked-at-in-lock-only-mode", Adj([](StreamConfig& c, const Fixtures&) {
              c.mode = StreamMode::FrameSyncLockStats; c.calibrationByWord.insert(0, profile()); }), DefaultFile::None, true, "ok error='' log=[]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=1 rate=0 label=Rcvr"
            " | setup: n=0" },
        { "profiles-on-the-default-map", Adj([](StreamConfig& c, const Fixtures&) {
              c.receiverParamsToml.clear(); c.calibrationByWord.insert(4, profile()); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 1 profile(s), attached 1 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },

        // --- the order the checks run in
        { "a-bad-pattern-is-reported-before-a-bad-file", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.pattern = "ZZ"; c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Invalid frame sync pattern 'ZZ'.' log=[]" },
        { "a-short-frame-is-reported-before-a-bad-file", Adj([](StreamConfig& c, const Fixtures& f) { c.sync.bitsInMinorFrame = 10; c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Bits/Frame must be >= 64.' log=[]" },
        { "a-bad-file-is-reported-before-a-bad-scale", Adj([](StreamConfig& c, const Fixtures& f) { c.scaleDdBPerV = -1; c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "no-parameters-is-reported-before-a-bad-scale", Adj([](StreamConfig& c, const Fixtures& f) { c.scaleDdBPerV = -1; c.receiverParamsToml = f.path("metadata.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file contains no parameters.' log=[]" },
        { "a-bad-scale-is-reported-before-a-bad-slope", Adj([](StreamConfig& c, const Fixtures&) { c.scaleDdBPerV = -1; c.slopeIndex = 99; }), DefaultFile::None, true, "rejected error='Rcvr: Scale (dB/V) must be a positive number.' log=[]" },
        { "the-descriptions-use-the-label-or-the-channel", Adj([](StreamConfig& c, const Fixtures&) { c.label.clear(); c.scaleDdBPerV = -1; }), DefaultFile::None, true, "rejected error='Ch 5: Scale (dB/V) must be a positive number.' log=[]" },

        // --- the worker parameters, for a receiver stream
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
        { "sample-period-out-of-range", Adj([](StreamConfig& c, const Fixtures&) { c.samplePeriodIndex = 7; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
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

// ---------------------------------------------------------------------------
// The implementation the core replaced, kept here as an oracle: MainViewModel::buildStreamJob, verbatim
// apart from its inputs (the application root and byte order were view-model members), its log lines
// (they were emitted as signals) and ONE line marked below - the fix for the appended default map. A
// difference with the code that replaced it is a change of behaviour, not of what was copied.
// ---------------------------------------------------------------------------

namespace oracle {

bool buildStreamJob(const StreamConfig& cfg, const ProcessingParams& base, const QString& m_app_root, bool m_swap_bytes,
                    StreamJob& out_job, QString& error, QStringList& log)
{
    const QString stream_desc = cfg.label.isEmpty()
        ? ("Ch " + QString::number(cfg.pcmChannelId)) : cfg.label;

    Acquisition acquisition;
    switch (resolveFrameSync(cfg.sync.pattern.toStdString(), cfg.sync.mask.toStdString(),
                             acquisition.sync))
    {
        case FrameSyncError::None:
            break;
        case FrameSyncError::PatternEmpty:
            error = stream_desc + ": A frame sync pattern is required.";
            return false;
        case FrameSyncError::PatternInvalid:
            error = stream_desc + ": Invalid frame sync pattern '" + cfg.sync.pattern + "'.";
            return false;
        case FrameSyncError::MaskInvalid:
            error = stream_desc + ": Invalid frame sync mask '" + cfg.sync.mask + "'.";
            return false;
    }

    const int bits_in_minor_frame = cfg.sync.bitsInMinorFrame;
    if (bits_in_minor_frame < PCMConstants::kMinFrameLengthBits)
    {
        error = stream_desc + ": Bits/Frame must be >= " +
                QString::number(PCMConstants::kMinFrameLengthBits) + ".";
        return false;
    }
    int words_in_minor_frame = FrameSetup::wordsInMinorFrame(bits_in_minor_frame);

    acquisition.bitsInMinorFrame = bits_in_minor_frame;
    acquisition.randomized       = cfg.sync.randomized;
    acquisition.inverted         = cfg.sync.inverted;
    acquisition.dataRateMbps     = cfg.sync.dataRateMbps;
    acquisition.swapBytes        = m_swap_bytes;

    out_job.params = base;
    out_job.params.pcmChannelId       = cfg.pcmChannelId;
    applyAcquisition(out_job.params, acquisition);
    out_job.params.mode               = cfg.mode;
    out_job.params.streamLabel        = stream_desc.toStdString();

    switch (cfg.samplePeriodIndex)
    {
        case 0: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod1s;    break;
        case 1: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod100ms; break;
        case 2: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod10ms;  break;
        default: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod100ms; break;
    }

    if (cfg.mode == StreamMode::FrameSyncLockStats)
    {
        out_job.frameSetup = std::make_shared<FrameSetup>();
        return true;
    }

    out_job.frameSetup = std::make_shared<FrameSetup>();
    if (cfg.receiverParamsToml.isEmpty())
    {
        const QString default_rcvr_params = m_app_root + "/" + UIConstants::kSettingsDirName +
            "/" + UIConstants::kReceiverParamsDirName + "/" + UIConstants::kDefaultTomlFilename;
        if (QFileInfo::exists(default_rcvr_params) &&
            out_job.frameSetup->tryLoadingFile(toFsPath(default_rcvr_params), words_in_minor_frame))
        {
        }
        else
        {
            out_job.frameSetup->clearParameters();   // THE ONE CHANGED LINE: the original appended to a partial map
            std::string map_error;
            if (!out_job.frameSetup->buildDefaultReceiverMap(cfg.numReceivers, cfg.receiverChannels,
                                                             words_in_minor_frame, map_error))
            {
                error = stream_desc + ": " + QString::fromStdString(map_error);
                return false;
            }
        }
    }
    else if (!QFileInfo::exists(cfg.receiverParamsToml))
    {
        error = stream_desc + ": Receiver Parameters file '" +
                QFileInfo(cfg.receiverParamsToml).fileName() + "' was not found.";
        return false;
    }
    else if (!out_job.frameSetup->tryLoadingFile(toFsPath(cfg.receiverParamsToml), words_in_minor_frame))
    {
        error = stream_desc + ": Failed to load Receiver Parameters from '" +
                QFileInfo(cfg.receiverParamsToml).fileName() + "'. Check the word map.";
        return false;
    }
    if (out_job.frameSetup->length() == 0)
    {
        error = stream_desc + ": Receiver Parameters file contains no parameters.";
        return false;
    }

    int polarity_idx      = cfg.polarityIndex;
    int slope_idx         = cfg.slopeIndex;
    double scale_dB_per_V = cfg.scaleDdBPerV;

    if (scale_dB_per_V <= 0)
    {
        error = stream_desc + ": Scale (dB/V) must be a positive number.";
        return false;
    }
    if (slope_idx < 0 || slope_idx > UIConstants::kMaxSlopeIndex)
    {
        error = stream_desc + ": Slope index is invalid.";
        return false;
    }

    out_job.frameSetup->applyLinearCalibration(polarity_idx, slope_idx, scale_dB_per_V);

    if (!cfg.calibrationByWord.isEmpty())
    {
        std::unordered_map<int, CalibrationProfile> profiles;
        for (auto it = cfg.calibrationByWord.constBegin(); it != cfg.calibrationByWord.constEnd(); ++it)
            profiles.emplace(it.key(), it.value());
        const int attached = out_job.frameSetup->attachCalibrationProfiles(profiles);
        log << "  " + stream_desc + ": non-linear calibration " + QString::fromUtf8("\xE2\x80\x94") + " provided "
                    + QString::number(cfg.calibrationByWord.size())
                    + " profile(s), attached " + QString::number(attached) + " to word map.";
    }
    else
    {
        log << "  " + stream_desc
            + ": no non-linear calibration profiles configured " + QString::fromUtf8("\xE2\x80\x94") + " using linear calibration.";
    }

    return true;
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

}  // namespace

void TestStreamJob::theCoreBuildsEveryGeneratedJobLikeTheOldViewModelCode()
{
    constexpr int kCases = 1500;
    static const Fixtures fixtures;
    QVERIFY(fixtures.dir.isValid());

    // Receiver-parameters files by name ("" = none named, "absent" = a path with no file behind it).
    static const QStringList tomls = {
        "", "", "", "", "", "good3.toml", "good3.toml", "absent.toml", "partly-outside.toml", "outside-only.toml", "metadata.toml",
        "garbage.toml", "empty.toml", "word-zero.toml", "word-50.toml", "word-51.toml", "a-folder.toml",
    };
    static const QStringList patterns = { "FE6B2840", "fe6b2840", "334AABBF", "", "ZZ", "  AB  ", "0xFE6B2840", "1FFFFFFFFFFFFFFFF", "A" };
    static const QStringList masks = { "", "FFFFFFFF", "FFFF0000", "XYZ", " ", "0", "1FFFFFFFFFFFFFFFF" };
    static const QList<int> bits = { 800, 801, 816, 64, 63, 0, -5, 65536, 8000, 32767, 1000000 };
    static const QList<double> scales = { 10.0, 12.5, 1.0, 0.0, -3.0, 1e-9, 1e6, std::numeric_limits<double>::quiet_NaN(),
                                          std::numeric_limits<double>::infinity() };
    static const QList<int> counts = { 2, 3, 1, 7, 50, 0, -1, 100000, 4, 16 };
    static const QStringList labels = { "Rcvr", "", "Stream", QString::fromUtf8("R\xC3\xA9" "cepteur \xCE\xA9") };

    Rng rng(20261028u);
    int built = 0, rejected = 0, receiver = 0, fell_back = 0, with_profiles = 0, from_default_file = 0, errors_with_file_names = 0;
    QStringList problems;
    for (int n = 0; n < kCases; ++n)
    {
        StreamConfig cfg;
        cfg.pcmChannelId = rng.below(40) - 3;
        cfg.label = rng.pick(labels);
        cfg.mode = rng.chance(30) ? StreamMode::FrameSyncLockStats : StreamMode::ReceiverChannelInfo;
        cfg.sync.pattern = rng.pick(patterns);
        cfg.sync.mask = rng.pick(masks);
        cfg.sync.bitsInMinorFrame = rng.chance(70) ? 800 : rng.pick(bits);
        cfg.sync.randomized = rng.chance(30);
        cfg.sync.inverted = rng.chance(30);
        cfg.sync.dataRateMbps = rng.chance(50) ? 0.0 : rng.below(100) / 8.0;
        cfg.samplePeriodIndex = rng.below(6) - 1;
        cfg.polarityIndex = rng.below(4) - 1;
        cfg.slopeIndex = rng.chance(80) ? rng.below(4) : rng.below(8) - 2;
        cfg.scaleDdBPerV = rng.chance(70) ? 10.0 : rng.pick(scales);
        cfg.numReceivers = rng.pick(counts);
        cfg.receiverChannels = rng.pick(counts);
        const QString toml = rng.pick(tomls);
        if (!toml.isEmpty())
            cfg.receiverParamsToml = fixtures.path(toml);
        else if (rng.chance(70))
        {   // most streams with no file named get counts that make a map, so the default map and file are reached
            cfg.numReceivers = 1 + rng.below(5);
            cfg.receiverChannels = 1 + rng.below(4);
        }
        if (rng.chance(40))
            for (int k = rng.below(4); k > 0; --k)
            {
                CalibrationProfile p;
                p.valid = rng.chance(75);
                p.points = { {1000.0, 0.0}, {2000.0, 6.0} };
                cfg.calibrationByWord.insert(rng.below(9), p);
            }
        const DefaultFile default_file = DefaultFile(rng.below(5));
        const bool swap_bytes = rng.chance(50);
        const QString root = fixtures.appRoot(default_file);

        ProcessingParams base;
        base.timeChannelId = rng.below(5);
        base.sourceId = rng.below(3);

        Built want;
        want.ok = oracle::buildStreamJob(cfg, base, root, swap_bytes, want.job, want.error, want.log);
        Built got;
        {
            MainViewModel vm;
            TestStreamJob::setAppRoot(vm, root);
            vm.setSwapBytes(swap_bytes);
            QSignalSpy spy(&vm, &MainViewModel::logMessageReceived);
            got.ok = TestStreamJob::buildStreamJob(vm, cfg, base, got.job, got.error);
            for (const QList<QVariant>& args : spy)
                got.log << args.at(0).toString();
        }
        const QString a = digest(want);
        const QString b = digest(got);
        if (a != b && problems.size() < 3)
            problems << QString("case %1:\n  old %2\n  new %3").arg(n).arg(a, b);

        (want.ok ? built : rejected)++;
        if (cfg.mode == StreamMode::ReceiverChannelInfo)
        {
            ++receiver;
            if (toml.isEmpty())
            {
                if (default_file == DefaultFile::Good && want.ok)
                    ++from_default_file;
                if (want.ok && want.job.frameSetup && want.job.frameSetup->length() == cfg.numReceivers * cfg.receiverChannels)
                    ++fell_back;
            }
        }
        with_profiles += want.ok && !cfg.calibrationByWord.isEmpty() && cfg.mode == StreamMode::ReceiverChannelInfo;
        errors_with_file_names += want.error.contains(".toml'");
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n")));
    // The generator must reach each outcome in volume, or "no difference" proves nothing.
    QVERIFY2(built > 120, qPrintable(QString::number(built)));
    QVERIFY2(rejected > 300, qPrintable(QString::number(rejected)));
    QVERIFY2(receiver > 800, qPrintable(QString::number(receiver)));
    QVERIFY2(fell_back > 30, qPrintable(QString::number(fell_back)));
    QVERIFY2(from_default_file > 8, qPrintable(QString::number(from_default_file)));
    QVERIFY2(with_profiles > 20, qPrintable(QString::number(with_profiles)));
    QVERIFY2(errors_with_file_names > 100, qPrintable(QString::number(errors_with_file_names)));
}

void TestStreamJob::theCoreReadsTheDefaultFileUnderANonAsciiApplicationRoot()
{
    // The application root arrives as UTF-8 text and must reach the file system as the folder it names.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.filePath(QString::fromUtf8("app-\xCE\xA9-caf\xC3\xA9"));
    QVERIFY(QDir().mkpath(root + "/settings/receiver_params"));
    QFile f(root + "/settings/receiver_params/default.toml");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("[L_RCVR1]\nWord = 1\n[R_RCVR1]\nWord = 2\n");
    f.close();

    StreamJobConfig cfg;
    cfg.pcmChannelId = 5;
    cfg.mode = StreamMode::ReceiverChannelInfo;
    cfg.syncPattern = "FE6B2840";
    cfg.bitsInMinorFrame = 800;
    cfg.numReceivers = 7;          // the default map would have 7 x 3 = 21 parameters: the file is what loaded
    cfg.receiverChannels = 3;

    StreamJobContext context;
    context.appRoot = root.toStdString();
    StreamJob job;
    StreamJobReport report;
    QVERIFY2(::buildStreamJob(cfg, ProcessingParams{}, context, job, report), report.error.c_str());
    QCOMPARE(job.frameSetup->length(), 2);
    QCOMPARE(int(report.log.size()), 1);
    QVERIFY(report.error.empty());
}

void TestStreamJob::theCoreReportsErrorsAndLogLinesAsPlainText()
{
    // The report is UTF-8 std::string and a rejection leaves the log empty; a success leaves the error empty.
    StreamJobConfig cfg;
    cfg.pcmChannelId = 5;
    cfg.label = "R\xC3\xA9" "cepteur";
    cfg.mode = StreamMode::FrameSyncLockStats;
    cfg.syncPattern = "ZZ";
    cfg.bitsInMinorFrame = 800;
    StreamJob job;
    StreamJobReport report;
    QVERIFY(!::buildStreamJob(cfg, ProcessingParams{}, StreamJobContext{}, job, report));
    QCOMPARE(report.error, std::string("R\xC3\xA9" "cepteur: Invalid frame sync pattern 'ZZ'."));
    QVERIFY(report.log.empty());

    cfg.syncPattern = "FE6B2840";
    StreamJobReport ok_report;
    QVERIFY(::buildStreamJob(cfg, ProcessingParams{}, StreamJobContext{}, job, ok_report));
    QVERIFY(ok_report.error.empty());
    QVERIFY(ok_report.log.empty());   // a lock-only stream has nothing to say
    QCOMPARE(job.params.swapBytes, true);   // the context's default, since the stream has no say
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
