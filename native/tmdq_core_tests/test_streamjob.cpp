// Stream jobs: turning one stream's configuration into a processing job (`buildStreamJob`, plan step 1.5): the
// validation and its messages, the receiver-parameters file and the default word map, the linear calibration and the
// non-linear profiles that attach to it. No Qt; the core half of the Qt TestStreamJob. That suite pinned the builder through
// MainViewModel::buildStreamJob, which converts the user's Qt settings (QString, QHash) into the builder's plain
// ones and turns its report into signals; the cases here hand the builder those plain settings directly. What the wrapper
// does stays in TestStreamJob, which keeps a row for each thing it converts.
//
// The oracle is the view-model code the builder replaced, restated over std types (the Qt suite kept it over QString and
// QFileInfo): run beside the builder over generated configurations. It shares with the builder the frame-sync decode, the
// acquisition flags and `FrameSetup`, which have their own suites.

#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "constants.h"
#include "doctest.h"
#include "framesetup.h"
#include "framesync.h"
#include "pipelinerunner.h"
#include "streamjobbuilder.h"
#include "test_support.h"

using namespace tmdq_test;

namespace {

// ---------------------------------------------------------------------------
// Helpers. Everything the tests do with the builder goes through build() and digest().
// ---------------------------------------------------------------------------

struct Built
{
    bool ok = false;
    std::string error;
    std::vector<std::string> log;
    StreamJob job;
};

/// Which receiver-parameters file the application root offers as its default.
enum class DefaultFile { None, Good, PartlyOutsideFrame, MetadataOnly, Garbage, WordAtTheEdge };

/// A directory with every receiver-parameters file the cases use, and application roots built over it.
struct Fixtures
{
    TempDir dir;

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
        fs::create_directories(path("a-folder.toml"));
    }

    void write(const std::string& name, const std::string& bytes) const { writeBytes(path(name), bytes); }

    fs::path path(const std::string& name) const { return dir.path() / fs::path(name); }

    /// An application root whose settings/receiver_params/default.toml is the named fixture.
    std::string appRoot(DefaultFile kind) const
    {
        static const char* sources[] = { nullptr, "good3.toml", "partly-outside.toml", "metadata.toml", "garbage.toml", "word-50.toml" };
        const fs::path root = dir.path() / std::format("root-{}", int(kind));
        fs::create_directories(root / "settings" / "receiver_params");
        if (sources[int(kind)])
        {
            fs::copy_file(path(sources[int(kind)]), root / "settings" / "receiver_params" / "default.toml",
                          fs::copy_options::overwrite_existing);
        }
        return root.string();
    }
};

Built build(const StreamJobConfig& cfg, const std::string& app_root, bool swap_bytes)
{
    ProcessingParams base;
    base.timeChannelId = 3;
    base.sourceId = 2;
    base.startSeconds = 11;
    base.stopSeconds = 99;
    base.filename = "base.ch10";

    StreamJobContext context;
    context.appRoot = app_root;
    context.swapBytes = swap_bytes;

    Built out;
    StreamJobReport report;
    out.ok = buildStreamJob(cfg, base, context, out.job, report);
    out.error = report.error;
    out.log = report.log;
    return out;
}

/// `QString::number(v, 'g', 15)`, and the spellings Qt gives the specials.
std::string num(double v)
{
    if (std::isnan(v))
    {
        return "nan";
    }
    if (std::isinf(v))
    {
        return v > 0 ? "inf" : "-inf";
    }
    return std::format("{:.15g}", v);
}

/// UTF-8 text with its non-ASCII characters spelled out as <U+XXXX>.
std::string asciiOnly(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size();)
    {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80)
        {
            out += static_cast<char>(b);
            ++i;
            continue;
        }
        int extra = 0;
        std::uint32_t cp = 0;
        if ((b & 0xE0) == 0xC0) { extra = 1; cp = b & 0x1F; }
        else if ((b & 0xF0) == 0xE0) { extra = 2; cp = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { extra = 3; cp = b & 0x07; }
        bool ok = extra > 0;
        for (int k = 1; ok && k <= extra; ++k)
        {
            ok = i + static_cast<std::size_t>(k) < s.size() && (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) == 0x80;
            if (ok)
            {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
            }
        }
        if (!ok)
        {
            cp = 0xFFFD;
            extra = 0;
        }
        out += std::format("<U+{:04X}>", cp);
        i += 1 + static_cast<std::size_t>(extra);
    }
    return out;
}

std::string joinLog(const std::vector<std::string>& log)
{
    std::string out;
    for (std::size_t i = 0; i < log.size(); ++i)
    {
        out += (i ? " | " : "") + log[i];
    }
    return out;
}

/// The outcome in one line: whether it was built, the message, the log, and - for a job that was built - every worker
/// parameter the configuration decides and the whole frame-parameter table.
std::string digest(const Built& b)
{
    std::string out = std::format("{} error='{}' log=[{}]", b.ok ? "ok" : "rejected", asciiOnly(b.error), asciiOnly(joinLog(b.log)));
    if (!b.ok)
    {
        return out;
    }
    const ProcessingParams& p = b.job.params;
    out += std::format(" | file={} src={} time={} pcm={} sync={:x}/{:x}/{} words={} bits={} window={}-{} period={} rand={} inv={} swap={} mode={} rate={} label={}",
                       p.filename.string(), p.sourceId, p.timeChannelId, p.pcmChannelId, p.frameSync, p.frameSyncMask,
                       p.syncPatternLength, p.wordsInMinorFrame, p.bitsInMinorFrame, p.startSeconds, p.stopSeconds,
                       num(p.samplePeriodSec), int(p.isRandomized), int(p.isInverted), int(p.swapBytes), int(p.mode),
                       num(p.dataRateBps), asciiOnly(p.streamLabel));
    out += " | setup:";
    if (!b.job.frameSetup)
    {
        return out + " none";
    }
    out += std::format(" n={}", b.job.frameSetup->length());
    for (int i = 0; i < b.job.frameSetup->length(); ++i)
    {
        const ParameterInfo* q = b.job.frameSetup->getParameter(i);
        out += std::format(" [{} w={} slope={} scale={} en={} profile={}]", q->name, q->word, num(q->slope), num(q->scale),
                           int(q->is_enabled), int(q->profile.valid));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

struct Case
{
    const char* name;
    std::function<void(StreamJobConfig&, const Fixtures&)> adjust;
    DefaultFile default_file;
    bool swap_bytes;
    const char* expect;
};

StreamJobConfig validReceiver(const Fixtures& f)
{
    StreamJobConfig cfg;
    cfg.pcmChannelId       = 5;
    cfg.label              = "Rcvr";
    cfg.mode               = StreamMode::ReceiverChannelInfo;
    cfg.syncPattern        = "FE6B2840";
    cfg.syncMask           = "FFFFFFFF";
    cfg.bitsInMinorFrame   = 800;
    cfg.samplePeriodIndex  = 1;
    cfg.polarityIndex      = 0;
    cfg.slopeIndex         = 0;
    cfg.scaleDdBPerV       = 10.0;
    cfg.numReceivers       = 2;
    cfg.receiverChannels   = 3;
    cfg.receiverParamsToml = f.path("good3.toml");
    return cfg;
}

CalibrationProfile profile()
{
    CalibrationProfile p;
    p.valid = true;
    p.points = { {1000.0, 0.0}, {2000.0, 6.0} };
    return p;
}

using Adj = std::function<void(StreamJobConfig&, const Fixtures&)>;

const std::vector<Case>& cases()
{
    static const std::vector<Case> list = {
        // --- lock-only: no receiver parameters at all
        { "lock-only-has-an-empty-setup", Adj([](StreamJobConfig& c, const Fixtures&) { c.mode = StreamMode::FrameSyncLockStats; }), DefaultFile::None, true, "ok error='' log=[]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=1 rate=0 label=Rcvr"
            " | setup: n=0" },
        { "lock-only-ignores-the-receiver-fields", Adj([](StreamJobConfig& c, const Fixtures&) {
              c.mode = StreamMode::FrameSyncLockStats; c.scaleDdBPerV = -1; c.slopeIndex = 99; c.receiverParamsToml = "no-such-file.toml"; }), DefaultFile::None, false, "ok error='' log=[]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=0 mode=1 rate=0 label=Rcvr"
            " | setup: n=0" },

        // --- an explicit receiver-parameters file, and the linear calibration
        { "explicit-file-three-parameters", Adj([](StreamJobConfig&, const Fixtures&) {}), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "negative-polarity", Adj([](StreamJobConfig& c, const Fixtures&) { c.polarityIndex = 1; c.slopeIndex = 1; c.scaleDdBPerV = 12.5; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=-0.00190737773708705 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=-0.00190737773708705 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=-0.00190737773708705 scale=-32767.5 en=1 profile=0]" },
        { "any-other-polarity-reads-as-positive", Adj([](StreamJobConfig& c, const Fixtures&) { c.polarityIndex = 5; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "every-valid-slope-index-0", Adj([](StreamJobConfig& c, const Fixtures&) { c.slopeIndex = 0; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "every-valid-slope-index-1", Adj([](StreamJobConfig& c, const Fixtures&) { c.slopeIndex = 1; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00152590218966964 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00152590218966964 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00152590218966964 scale=-32767.5 en=1 profile=0]" },
        { "every-valid-slope-index-2", Adj([](StreamJobConfig& c, const Fixtures&) { c.slopeIndex = 2; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00152590218966964 scale=0 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00152590218966964 scale=0 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00152590218966964 scale=0 en=1 profile=0]" },
        { "every-valid-slope-index-3", Adj([](StreamJobConfig& c, const Fixtures&) { c.slopeIndex = 3; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.000762951094834821 scale=0 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.000762951094834821 scale=0 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.000762951094834821 scale=0 en=1 profile=0]" },
        { "slope-index-below-the-range", Adj([](StreamJobConfig& c, const Fixtures&) { c.slopeIndex = -1; }), DefaultFile::None, true, "rejected error='Rcvr: Slope index is invalid.' log=[]" },
        { "slope-index-above-the-range", Adj([](StreamJobConfig& c, const Fixtures&) { c.slopeIndex = 4; }), DefaultFile::None, true, "rejected error='Rcvr: Slope index is invalid.' log=[]" },
        { "scale-zero", Adj([](StreamJobConfig& c, const Fixtures&) { c.scaleDdBPerV = 0.0; }), DefaultFile::None, true, "rejected error='Rcvr: Scale (dB/V) must be a positive number.' log=[]" },
        { "scale-negative", Adj([](StreamJobConfig& c, const Fixtures&) { c.scaleDdBPerV = -3.0; }), DefaultFile::None, true, "rejected error='Rcvr: Scale (dB/V) must be a positive number.' log=[]" },
        { "scale-not-a-number", Adj([](StreamJobConfig& c, const Fixtures&) { c.scaleDdBPerV = std::numeric_limits<double>::quiet_NaN(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=nan scale=nan en=1 profile=0]"
            " [R_RCVR1 w=1 slope=nan scale=nan en=1 profile=0]"
            " [C_RCVR1 w=2 slope=nan scale=nan en=1 profile=0]" },
        { "scale-tiny", Adj([](StreamJobConfig& c, const Fixtures&) { c.scaleDdBPerV = 1e-9; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=3.05180437933928e-13 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=3.05180437933928e-13 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=3.05180437933928e-13 scale=-32767.5 en=1 profile=0]" },
        { "scale-large", Adj([](StreamJobConfig& c, const Fixtures&) { c.scaleDdBPerV = 1e6; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=305.180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=305.180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=305.180437933928 scale=-32767.5 en=1 profile=0]" },

        // --- an explicit file that cannot be used
        { "file-missing", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "file-missing-unlabelled", Adj([](StreamJobConfig& c, const Fixtures& f) { c.label.clear(); c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Ch 5: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "file-missing-named-with-backslashes", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml = "C:\\nowhere\\sub\\absent.toml"; }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "file-is-a-folder", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("a-folder.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'a-folder.toml'. Check the word map.' log=[]" },
        { "file-has-a-word-outside-the-frame", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("outside-only.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'outside-only.toml'. Check the word map.' log=[]" },
        { "file-with-one-good-and-one-bad-word", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("partly-outside.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'partly-outside.toml'. Check the word map.' log=[]" },
        { "file-with-only-metadata", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("metadata.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file contains no parameters.' log=[]" },
        { "file-is-not-toml", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("garbage.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'garbage.toml'. Check the word map.' log=[]" },
        { "file-is-empty", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("empty.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'empty.toml'. Check the word map.' log=[]" },
        { "file-with-word-zero", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("word-zero.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-zero.toml'. Check the word map.' log=[]" },
        { "word-50-in-800-bits", Adj([](StreamJobConfig& c, const Fixtures& f) { c.bitsInMinorFrame = 800; c.receiverParamsToml = f.path("word-50.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-50.toml'. Check the word map.' log=[]" },
        { "word-50-in-801-bits", Adj([](StreamJobConfig& c, const Fixtures& f) { c.bitsInMinorFrame = 801; c.receiverParamsToml = f.path("word-50.toml"); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=51 bits=801 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=49 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "word-51-in-800-bits", Adj([](StreamJobConfig& c, const Fixtures& f) { c.bitsInMinorFrame = 800; c.receiverParamsToml = f.path("word-51.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-51.toml'. Check the word map.' log=[]" },
        { "word-51-in-801-bits", Adj([](StreamJobConfig& c, const Fixtures& f) { c.bitsInMinorFrame = 801; c.receiverParamsToml = f.path("word-51.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-51.toml'. Check the word map.' log=[]" },
        { "word-51-in-816-bits", Adj([](StreamJobConfig& c, const Fixtures& f) { c.bitsInMinorFrame = 816; c.receiverParamsToml = f.path("word-51.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Failed to load Receiver Parameters from 'word-51.toml'. Check the word map.' log=[]" },
        { "file-name-with-non-ascii-letters", Adj([](StreamJobConfig& c, const Fixtures& f) {
              c.receiverParamsToml = f.path("unicode-\xCE\xA9.toml"); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "file-missing-with-a-non-ascii-name", Adj([](StreamJobConfig& c, const Fixtures& f) {
              c.label = "R\xC3\xA9" "cepteur"; c.receiverParamsToml = f.path("absent-\xCE\xA9.toml"); }), DefaultFile::None, true, "rejected error='R<U+00E9>cepteur: Receiver Parameters file 'absent-<U+03A9>.toml' was not found.' log=[]" },

        // --- no file named: the shipped default file, then the default word map
        { "no-file-and-no-default-builds-the-default-map", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "no-file-default-map-one-by-one", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.numReceivers = 1; c.receiverChannels = 1; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "no-file-default-map-fills-all-but-the-sync-word", Adj([](StreamJobConfig& c, const Fixtures&) {
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
        { "no-file-default-map-too-big", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.numReceivers = 50; c.receiverChannels = 1; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-default-map-no-receivers", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.numReceivers = 0; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-default-map-negative-channels", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.receiverChannels = -2; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-default-map-product-overflows", Adj([](StreamJobConfig& c, const Fixtures&) {
              c.receiverParamsToml.clear(); c.numReceivers = 100000; c.receiverChannels = 100000; }), DefaultFile::None, true, "rejected error='Rcvr: Num Receivers x Receiver Channels exceeds the words available in the minor frame.' log=[]" },
        { "no-file-uses-the-default-file", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::Good, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "an-explicit-file-beats-the-default-file", Adj([](StreamJobConfig& c, const Fixtures& f) { c.receiverParamsToml = f.path("unicode-\xCE\xA9.toml"); }), DefaultFile::Good, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        // Changed on purpose (plan step 1.5); this built SEVEN parameters, L_RCVR1 twice. A default.toml that
        // loads in part before it hits a word outside the frame leaves the words it did read in the table, and
        // the fallback to the default word map APPENDED to them, so the job processed with a map nobody wrote.
        // FrameSetup::saveReceiverParamsFile already cleared first; building a job did not. It now does, and
        // a partly usable default file falls back to the same six-parameter map as an unusable one.
        { "default-file-partly-outside-the-frame", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::PartlyOutsideFrame, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        // The word-count boundary applies to the default file as it does to a named one (new in the port, from the
        // mutation check): word 50 is outside an 800-bit frame (50 words) and inside an 801-bit one (51).
        { "default-file-with-a-word-at-the-edge-falls-back", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::WordAtTheEdge, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "default-file-with-a-word-at-the-edge-loads-in-a-longer-frame", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); c.bitsInMinorFrame = 801; }), DefaultFile::WordAtTheEdge, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=51 bits=801 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=1"
            " [L_RCVR1 w=49 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "default-file-with-only-metadata", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::MetadataOnly, true, "rejected error='Rcvr: Receiver Parameters file contains no parameters.' log=[]" },
        { "default-file-is-not-toml", Adj([](StreamJobConfig& c, const Fixtures&) { c.receiverParamsToml.clear(); }), DefaultFile::Garbage, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },

        // --- calibration profiles
        { "two-profiles-both-attach", Adj([](StreamJobConfig& c, const Fixtures&) { c.calibrationByWord[0] = profile(); c.calibrationByWord[2] = profile(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 2 profile(s), attached 2 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]" },
        { "a-profile-for-a-word-the-map-lacks", Adj([](StreamJobConfig& c, const Fixtures&) { c.calibrationByWord[9] = profile(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 1 profile(s), attached 0 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "an-invalid-profile-does-not-attach", Adj([](StreamJobConfig& c, const Fixtures&) { c.calibrationByWord[1] = CalibrationProfile(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 1 profile(s), attached 0 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "profiles-are-never-looked-at-in-lock-only-mode", Adj([](StreamJobConfig& c, const Fixtures&) {
              c.mode = StreamMode::FrameSyncLockStats; c.calibrationByWord[0] = profile(); }), DefaultFile::None, true, "ok error='' log=[]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=1 rate=0 label=Rcvr"
            " | setup: n=0" },
        { "profiles-on-the-default-map", Adj([](StreamJobConfig& c, const Fixtures&) {
              c.receiverParamsToml.clear(); c.calibrationByWord[4] = profile(); }), DefaultFile::None, true, "ok error='' log=[  Rcvr: non-linear calibration <U+2014> provided 1 profile(s), attached 1 to word map.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=6"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [L_RCVR2 w=3 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR2 w=4 slope=0.00305180437933928 scale=-32767.5 en=1 profile=1]"
            " [C_RCVR2 w=5 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },

        // --- the order the checks run in
        { "a-bad-pattern-is-reported-before-a-bad-file", Adj([](StreamJobConfig& c, const Fixtures& f) { c.syncPattern = "ZZ"; c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Invalid frame sync pattern 'ZZ'.' log=[]" },
        { "a-short-frame-is-reported-before-a-bad-file", Adj([](StreamJobConfig& c, const Fixtures& f) { c.bitsInMinorFrame = 10; c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Bits/Frame must be >= 64.' log=[]" },
        { "a-bad-file-is-reported-before-a-bad-scale", Adj([](StreamJobConfig& c, const Fixtures& f) { c.scaleDdBPerV = -1; c.receiverParamsToml = f.path("absent.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file 'absent.toml' was not found.' log=[]" },
        { "no-parameters-is-reported-before-a-bad-scale", Adj([](StreamJobConfig& c, const Fixtures& f) { c.scaleDdBPerV = -1; c.receiverParamsToml = f.path("metadata.toml"); }), DefaultFile::None, true, "rejected error='Rcvr: Receiver Parameters file contains no parameters.' log=[]" },
        { "a-bad-scale-is-reported-before-a-bad-slope", Adj([](StreamJobConfig& c, const Fixtures&) { c.scaleDdBPerV = -1; c.slopeIndex = 99; }), DefaultFile::None, true, "rejected error='Rcvr: Scale (dB/V) must be a positive number.' log=[]" },
        { "the-descriptions-use-the-label-or-the-channel", Adj([](StreamJobConfig& c, const Fixtures&) { c.label.clear(); c.scaleDdBPerV = -1; }), DefaultFile::None, true, "rejected error='Ch 5: Scale (dB/V) must be a positive number.' log=[]" },

        // --- the worker parameters, for a receiver stream
        { "receiver-stream-parameters", Adj([](StreamJobConfig& c, const Fixtures&) {
              c.randomized = true; c.inverted = true; c.dataRateMbps = 2.5; c.samplePeriodIndex = 2; c.pcmChannelId = 21; }), DefaultFile::None, false, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=21 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.01 rand=1 inv=1 swap=0 mode=0 rate=2500000 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "sample-period-1s", Adj([](StreamJobConfig& c, const Fixtures&) { c.samplePeriodIndex = 0; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
        { "sample-period-out-of-range", Adj([](StreamJobConfig& c, const Fixtures&) { c.samplePeriodIndex = 7; }), DefaultFile::None, true, "ok error='' log=[  Rcvr: no non-linear calibration profiles configured <U+2014> using linear calibration.]"
            " | file=base.ch10 src=2 time=3 pcm=5 sync=fe6b2840/ffffffff/32 words=50 bits=800 window=11-99 period=0.1 rand=0 inv=0 swap=1 mode=0 rate=0 label=Rcvr"
            " | setup: n=3"
            " [L_RCVR1 w=0 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [R_RCVR1 w=1 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]"
            " [C_RCVR1 w=2 slope=0.00305180437933928 scale=-32767.5 en=1 profile=0]" },
    };
    return list;
}

}  // namespace

TEST_CASE("streamjob: every configuration builds, or is refused, as recorded")
{
    static const Fixtures fixtures;
    for (const Case& c : cases())
    {
        INFO("case " << c.name);
        StreamJobConfig cfg = validReceiver(fixtures);
        c.adjust(cfg, fixtures);
        CHECK(digest(build(cfg, fixtures.appRoot(c.default_file), c.swap_bytes)) == std::string(c.expect));
    }
}

// ---------------------------------------------------------------------------
// The code the builder replaced, restated over std types, beside the builder over generated configurations
// ---------------------------------------------------------------------------

namespace oracle {

/// MainViewModel::buildStreamJob as it was, apart from its types: the application root and byte order are arguments (they
/// were view-model members), the log lines are appended to a list (they were emitted as signals), and there is ONE line
/// marked below - the fix for the appended default map.
bool buildStreamJob(const StreamJobConfig& cfg, const ProcessingParams& base, const std::string& m_app_root, bool m_swap_bytes,
                    StreamJob& out_job, std::string& error, std::vector<std::string>& log)
{
    const std::string stream_desc = cfg.label.empty() ? ("Ch " + std::to_string(cfg.pcmChannelId)) : cfg.label;

    Acquisition acquisition;
    switch (resolveFrameSync(cfg.syncPattern, cfg.syncMask, acquisition.sync))
    {
        case FrameSyncError::None:
            break;
        case FrameSyncError::PatternEmpty:
            error = stream_desc + ": A frame sync pattern is required.";
            return false;
        case FrameSyncError::PatternInvalid:
            error = stream_desc + ": Invalid frame sync pattern '" + cfg.syncPattern + "'.";
            return false;
        case FrameSyncError::MaskInvalid:
            error = stream_desc + ": Invalid frame sync mask '" + cfg.syncMask + "'.";
            return false;
    }

    const int bits_in_minor_frame = cfg.bitsInMinorFrame;
    if (bits_in_minor_frame < PCMConstants::kMinFrameLengthBits)
    {
        error = stream_desc + ": Bits/Frame must be >= " + std::to_string(PCMConstants::kMinFrameLengthBits) + ".";
        return false;
    }
    int words_in_minor_frame = FrameSetup::wordsInMinorFrame(bits_in_minor_frame);

    acquisition.bitsInMinorFrame = bits_in_minor_frame;
    acquisition.randomized       = cfg.randomized;
    acquisition.inverted         = cfg.inverted;
    acquisition.dataRateMbps     = cfg.dataRateMbps;
    acquisition.swapBytes        = m_swap_bytes;

    out_job.params = base;
    out_job.params.pcmChannelId = cfg.pcmChannelId;
    applyAcquisition(out_job.params, acquisition);
    out_job.params.mode        = cfg.mode;
    out_job.params.streamLabel = stream_desc;

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
    if (cfg.receiverParamsToml.empty())
    {
        const fs::path default_rcvr_params = fs::path(m_app_root) / UIConstants::kSettingsDirName /
                                             UIConstants::kReceiverParamsDirName / UIConstants::kDefaultTomlFilename;
        std::error_code ec;
        if (fs::exists(default_rcvr_params, ec) && out_job.frameSetup->tryLoadingFile(default_rcvr_params, words_in_minor_frame))
        {
        }
        else
        {
            out_job.frameSetup->clearParameters();   // THE ONE CHANGED LINE: the original appended to a partial map
            std::string map_error;
            if (!out_job.frameSetup->buildDefaultReceiverMap(cfg.numReceivers, cfg.receiverChannels, words_in_minor_frame, map_error))
            {
                error = stream_desc + ": " + map_error;
                return false;
            }
        }
    }
    else if (std::error_code ec; !fs::exists(cfg.receiverParamsToml, ec))
    {
        error = stream_desc + ": Receiver Parameters file '" + cfg.receiverParamsToml.filename().string() + "' was not found.";
        return false;
    }
    else if (!out_job.frameSetup->tryLoadingFile(cfg.receiverParamsToml, words_in_minor_frame))
    {
        error = stream_desc + ": Failed to load Receiver Parameters from '" + cfg.receiverParamsToml.filename().string() +
                "'. Check the word map.";
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

    if (!cfg.calibrationByWord.empty())
    {
        const int attached = out_job.frameSetup->attachCalibrationProfiles(cfg.calibrationByWord);
        log.push_back("  " + stream_desc + ": non-linear calibration \xE2\x80\x94 provided " + std::to_string(cfg.calibrationByWord.size()) +
                      " profile(s), attached " + std::to_string(attached) + " to word map.");
    }
    else
    {
        log.push_back("  " + stream_desc + ": no non-linear calibration profiles configured \xE2\x80\x94 using linear calibration.");
    }

    return true;
}

}  // namespace oracle

namespace {

/// A deterministic generator (the Qt suite's, state for state), so a failure names the same case every run.
struct Rng
{
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int below(int n) { return int(next() % std::uint32_t(n)); }
    bool chance(int percent) { return below(100) < percent; }
    template <typename T>
    const T& pick(const std::vector<T>& list) { return list[static_cast<std::size_t>(below(int(list.size())))]; }
};

}  // namespace

TEST_CASE("streamjob: the builder makes every generated job as the code it replaced did")
{
    constexpr int kCases = 1500;
    static const Fixtures fixtures;

    // Receiver-parameters files by name ("" = none named, "absent.toml" = a path with no file behind it).
    static const std::vector<std::string> tomls = {
        "", "", "", "", "", "good3.toml", "good3.toml", "absent.toml", "partly-outside.toml", "outside-only.toml", "metadata.toml",
        "garbage.toml", "empty.toml", "word-zero.toml", "word-50.toml", "word-51.toml", "a-folder.toml",
    };
    static const std::vector<std::string> patterns = { "FE6B2840", "fe6b2840", "334AABBF", "", "ZZ", "  AB  ", "0xFE6B2840", "1FFFFFFFFFFFFFFFF", "A" };
    static const std::vector<std::string> masks = { "", "FFFFFFFF", "FFFF0000", "XYZ", " ", "0", "1FFFFFFFFFFFFFFFF" };
    static const std::vector<int> bits = { 800, 801, 816, 64, 63, 0, -5, 65536, 8000, 32767, 1000000 };
    static const std::vector<double> scales = { 10.0, 12.5, 1.0, 0.0, -3.0, 1e-9, 1e6, std::numeric_limits<double>::quiet_NaN(),
                                                std::numeric_limits<double>::infinity() };
    static const std::vector<int> counts = { 2, 3, 1, 7, 50, 0, -1, 100000, 4, 16 };
    static const std::vector<std::string> labels = { "Rcvr", "", "Stream", "R\xC3\xA9" "cepteur \xCE\xA9" };

    Rng rng(20261028u);
    int built = 0, rejected = 0, receiver = 0, fell_back = 0, with_profiles = 0, from_default_file = 0, errors_with_file_names = 0;
    for (int n = 0; n < kCases; ++n)
    {
        StreamJobConfig cfg;
        cfg.pcmChannelId = rng.below(40) - 3;
        cfg.label = rng.pick(labels);
        cfg.mode = rng.chance(30) ? StreamMode::FrameSyncLockStats : StreamMode::ReceiverChannelInfo;
        cfg.syncPattern = rng.pick(patterns);
        cfg.syncMask = rng.pick(masks);
        cfg.bitsInMinorFrame = rng.chance(70) ? 800 : rng.pick(bits);
        cfg.randomized = rng.chance(30);
        cfg.inverted = rng.chance(30);
        cfg.dataRateMbps = rng.chance(50) ? 0.0 : rng.below(100) / 8.0;
        cfg.samplePeriodIndex = rng.below(6) - 1;
        cfg.polarityIndex = rng.below(4) - 1;
        cfg.slopeIndex = rng.chance(80) ? rng.below(4) : rng.below(8) - 2;
        cfg.scaleDdBPerV = rng.chance(70) ? 10.0 : rng.pick(scales);
        cfg.numReceivers = rng.pick(counts);
        cfg.receiverChannels = rng.pick(counts);
        const std::string toml = rng.pick(tomls);
        if (!toml.empty())
        {
            cfg.receiverParamsToml = fixtures.path(toml);
        }
        else if (rng.chance(70))
        {   // most streams with no file named get counts that make a map, so the default map and file are reached
            cfg.numReceivers = 1 + rng.below(5);
            cfg.receiverChannels = 1 + rng.below(4);
        }
        if (rng.chance(40))
        {
            for (int k = rng.below(4); k > 0; --k)
            {
                CalibrationProfile p;
                p.valid = rng.chance(75);
                p.points = { {1000.0, 0.0}, {2000.0, 6.0} };
                cfg.calibrationByWord[rng.below(9)] = p;   // a repeated word replaces its profile
            }
        }
        const DefaultFile default_file = DefaultFile(rng.below(5));
        const bool swap_bytes = rng.chance(50);
        const std::string root = fixtures.appRoot(default_file);

        ProcessingParams base;
        base.timeChannelId = rng.below(5);
        base.sourceId = rng.below(3);

        Built want;
        want.ok = oracle::buildStreamJob(cfg, base, root, swap_bytes, want.job, want.error, want.log);
        Built got;
        {
            StreamJobContext context;
            context.appRoot = root;
            context.swapBytes = swap_bytes;
            StreamJobReport report;
            got.ok = buildStreamJob(cfg, base, context, got.job, report);
            got.error = report.error;
            got.log = report.log;
        }
        INFO("case " << n);
        CHECK(digest(got) == digest(want));

        (want.ok ? built : rejected)++;
        if (cfg.mode == StreamMode::ReceiverChannelInfo)
        {
            ++receiver;
            if (toml.empty())
            {
                if (default_file == DefaultFile::Good && want.ok)
                {
                    ++from_default_file;
                }
                if (want.ok && want.job.frameSetup && want.job.frameSetup->length() == cfg.numReceivers * cfg.receiverChannels)
                {
                    ++fell_back;
                }
            }
        }
        with_profiles += want.ok && !cfg.calibrationByWord.empty() && cfg.mode == StreamMode::ReceiverChannelInfo;
        errors_with_file_names += contains(want.error, ".toml'");
    }
    // The generator must reach each outcome in volume, or "no difference" proves nothing.
    CHECK(built > 120);
    CHECK(rejected > 300);
    CHECK(receiver > 800);
    CHECK(fell_back > 30);
    CHECK(from_default_file > 8);
    CHECK(with_profiles > 20);
    CHECK(errors_with_file_names > 100);
}

TEST_CASE("streamjob: the default file is read under an application root with a non-ASCII name")
{
    // The application root arrives as UTF-8 text and must reach the file system as the folder it names.
    TempDir tmp;
    const fs::path root = tmp.path() / fs::path(utf8(u8"app-Ω-café"));
    fs::create_directories(root / "settings" / "receiver_params");
    writeBytes(root / "settings" / "receiver_params" / "default.toml", "[L_RCVR1]\nWord = 1\n[R_RCVR1]\nWord = 2\n");

    StreamJobConfig cfg;
    cfg.pcmChannelId = 5;
    cfg.mode = StreamMode::ReceiverChannelInfo;
    cfg.syncPattern = "FE6B2840";
    cfg.bitsInMinorFrame = 800;
    cfg.numReceivers = 7;          // the default map would have 7 x 3 = 21 parameters: the file is what loaded
    cfg.receiverChannels = 3;

    StreamJobContext context;
    context.appRoot = root.string();
    StreamJob job;
    StreamJobReport report;
    REQUIRE_MESSAGE(buildStreamJob(cfg, ProcessingParams{}, context, job, report), report.error);
    CHECK(job.frameSetup->length() == 2);
    CHECK(report.log.size() == 1);
    CHECK(report.error.empty());
}

TEST_CASE("streamjob: errors and log lines are plain UTF-8 text, a rejection leaves no log and a success no error")
{
    StreamJobConfig cfg;
    cfg.pcmChannelId = 5;
    cfg.label = "R\xC3\xA9" "cepteur";
    cfg.mode = StreamMode::FrameSyncLockStats;
    cfg.syncPattern = "ZZ";
    cfg.bitsInMinorFrame = 800;
    StreamJob job;
    StreamJobReport report;
    CHECK_FALSE(buildStreamJob(cfg, ProcessingParams{}, StreamJobContext{}, job, report));
    CHECK(report.error == "R\xC3\xA9" "cepteur: Invalid frame sync pattern 'ZZ'.");
    CHECK(report.log.empty());

    cfg.syncPattern = "FE6B2840";
    StreamJobReport ok_report;
    CHECK(buildStreamJob(cfg, ProcessingParams{}, StreamJobContext{}, job, ok_report));
    CHECK(ok_report.error.empty());
    CHECK(ok_report.log.empty());   // a lock-only stream has nothing to say
    CHECK(job.params.swapBytes == true);   // the context's default, since the stream has no say
}
