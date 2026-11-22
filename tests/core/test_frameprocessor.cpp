// FrameProcessor: frame sync, word extraction and windowed averaging, on synthetic bit streams and on the committed
// recordings (ported in plan step 2.4; the data-driven cases are loops).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "calibrationextractor.h"
#include "calibrationprofile.h"
#include "ch10packetreader.h"
#include "chapter10reader.h"
#include "constants.h"
#include "doctest.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "framesync.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "stepdetector.h"
#include "stepconfigfile.h"
#include "test_support.h"

using namespace tmdq_test;

/// The bit-level helpers are private statics; the processor names this struct its friend so the tests can reach them.
struct FrameProcessorTestAccess
{
    static void derandomize(uint8_t* data, uint64_t total_bits, uint16_t& lfsr)
    {
        FrameProcessor::derandomizeBitstream(data, total_bits, lfsr);
    }
};

namespace {

/// A ProcessingParams with this suite's defaults: the TRC recording's 32-bit sync, 49 words, 800-bit frames.
ProcessingParams makeTestParams(const fs::path& filename = {},
                                int time_channel_id = -1,
                                int pcm_channel_id = -1,
                                uint64_t frame_sync = 0xFE6B2840,
                                int sync_len = 32,
                                int words_in_frame = 49,
                                int bits_in_frame = 800)
{
    ProcessingParams p;
    // Every recording here except the Safran one is an ordinary Chapter 10 file, which means byte-swapped; only Safran
    // is legacy and overrides this. Set explicitly rather than inherited, so a change to the DTO default cannot
    // silently retune what these tests are decoding.
    p.swapBytes         = true;
    p.filename          = filename;
    p.timeChannelId     = time_channel_id;
    p.pcmChannelId      = pcm_channel_id;
    p.frameSync         = frame_sync;
    p.syncPatternLength = sync_len;
    p.wordsInMinorFrame = words_in_frame;
    p.bitsInMinorFrame  = bits_in_frame;
    return p;
}

/// Runs the processor through the whole reader-and-queue pipeline, as PipelineRunner does: the reader resolves
/// the TMATS attributes in prepare(), then reads on its own thread while process() consumes on this one. @p prepared
/// says whether prepare() succeeded, so a broken fixture is reported as itself rather than as a processing result.
bool runWithReader(FrameProcessor& fp, ProcessingParams& p, FrameSetup* setup, bool* prepared = nullptr)
{
    PacketQueue queue;
    p.packetQueue = &queue;

    Ch10PacketReader reader;
    std::vector<ProcessingParams*> params_list = {&p};
    std::string error;
    const bool ready = reader.prepare(p.filename, p.timeChannelId, params_list, error);
    if (prepared != nullptr)
    {
        *prepared = ready;
    }
    if (!ready)
    {
        MESSAGE("prepare() failed: " << error);
        return false;
    }

    std::thread read([&reader]() { reader.run(); });
    const bool ok = fp.process(p, setup);
    queue.close(); // a run that ended early must not leave the reader blocked on a full queue
    read.join();
    p.packetQueue = nullptr;
    return ok;
}

/// Packs a string of '1' and '0' MSB first into bytes, for synthetic PCM streams.
std::vector<uint8_t> packBitString(const std::string& bits)
{
    std::vector<uint8_t> data((bits.size() + 7) / 8, 0);
    for (std::size_t i = 0; i < bits.size(); i++)
    {
        if (bits[i] == '1')
        {
            data[i / 8] = static_cast<uint8_t>(data[i / 8] | (0x80 >> (i % 8)));
        }
    }
    return data;
}

/// Everything a FrameProcessor reports, in order.
struct Capture
{
    Strings           logs;
    Strings           errors;
    std::vector<bool> finished;
};

/// Records into a Capture, on the processing thread (the test thread in every case here).
class CaptureSink final : public ProcessingEvents
{
public:
    explicit CaptureSink(Capture& c) : m_c(c) {}
    void logMessage(const std::string& m) override { m_c.logs.push_back(m); }
    void errorOccurred(const std::string& m) override { m_c.errors.push_back(m); }
    void processingFinished(bool ok) override { m_c.finished.push_back(ok); }

private:
    Capture& m_c;
};

void captureEvents(FrameProcessor& fp, Capture& c)
{
    fp.setEvents(std::make_shared<CaptureSink>(c));
}

std::string joined(const Strings& lines)
{
    std::string all;
    for (const std::string& line : lines)
    {
        all += (all.empty() ? "" : " | ") + line;
    }
    return all;
}

/// A 4-bit-sync, 12-bit-frame synthetic stream, laid out [w0][w1][sync]: 1001 is the sync, each word is 4 bits.
/// Fills @p p's resolved attributes; the caller sets what it varies (label, window, ...).
void syntheticParams(ProcessingParams& p, PacketQueue& queue)
{
    p.packetQueue      = &queue;
    p.pcmChannelId     = 7;
    p.streamLabel      = "Rcvr Bus";
    p.mode             = StreamMode::FrameSyncLockStats;
    p.samplePeriodSec  = 1e9;
    p.useDataRateClock = true;
    p.isRandomized     = false;
    p.isInverted       = false;

    p.resolvedAttrs.syncPat      = 0x9; // 1001b
    p.resolvedAttrs.syncMask     = 0xF;
    p.resolvedAttrs.syncPatLen   = 4;
    p.resolvedAttrs.bitsInFrame  = 12;
    p.resolvedAttrs.wordsInFrame = 2;
    p.resolvedAttrs.wordLen      = 4;
    p.resolvedAttrs.wordMask     = 0xF;
    p.resolvedAttrs.minSyncs     = 1;
    p.resolvedAttrs.delta100ns   = 1.0;
    p.resolvedAttrs.needsSwap    = false;
    p.resolvedAttrs.resolved     = true;
}

/// Queues one packet carrying @p bits, MSB first.
void enqueueBits(PacketQueue& queue, const std::string& bits, double base_abs_seconds = 0.0)
{
    PacketItem item;
    item.payload        = std::make_shared<const std::vector<uint8_t>>(packBitString(bits));
    item.baseAbsSeconds = base_abs_seconds;
    item.packetBits     = bits.size();
    REQUIRE(queue.enqueue(item));
}

void enqueueEndOfStream(PacketQueue& queue)
{
    PacketItem eos;
    eos.endOfStream = true;
    REQUIRE(queue.enqueue(eos));
}

/// One frame of the synthetic layout: word 0 = @p w0, word 1 = 0, then the sync. A w0 of 2, 3 or 9 would put 1001
/// inside the frame or across its start, so the timed tests avoid them.
std::string frame(int w0)
{
    std::string bits;
    for (int b = 3; b >= 0; b--)
    {
        bits += ((w0 >> b) & 1) != 0 ? '1' : '0';
    }
    return bits + "0000" "1001";
}

/// The leading sync, then one frame per value: frame k's sync ends at bit 12k + 3.
std::string stream(std::initializer_list<int> w0s)
{
    std::string bits = "1001";
    for (int w0 : w0s)
    {
        bits += frame(w0);
    }
    return bits;
}

/// IRIG 106's randomizer, written independently of the processor's derandomizer: a 15-stage register whose stages
/// 14 and 15 are XORed into each bit, fed with what it OUTPUTS (the derandomizer is fed with what it receives).
std::string scramble(const std::string& bits)
{
    unsigned reg = 0;
    std::string out;
    for (char c : bits)
    {
        const unsigned o = (c == '1' ? 1u : 0u) ^ ((reg >> 13) & 1u) ^ ((reg >> 14) & 1u);
        reg = ((reg << 1) | o) & 0x7FFFu;
        out += o != 0 ? '1' : '0';
    }
    return out;
}

struct Packet
{
    std::string bits;
    double      base = 0.0; ///< The packet's time (baseAbsSeconds).
};

/// @p bits in packets of @p size bits, each stamped with its first bit's time at one second per bit from @p origin.
std::vector<Packet> split(const std::string& bits, std::size_t size, double origin)
{
    std::vector<Packet> packets;
    for (std::size_t at = 0; at < bits.size(); at += size)
    {
        packets.push_back({bits.substr(at, size), origin + static_cast<double>(at)});
    }
    return packets;
}

/// The synthetic layout timed at one second per bit (a bit period of 1e7 x 100 ns), averaged over 15 s windows.
ProcessingParams timedParams(StreamMode mode, bool rate_clock)
{
    PacketQueue unused;
    ProcessingParams p;
    syntheticParams(p, unused);
    p.packetQueue              = nullptr;
    p.mode                     = mode;
    p.useDataRateClock         = rate_clock;
    p.samplePeriodSec          = 15.0;
    p.resolvedAttrs.delta100ns = 1e7;
    return p;
}

/// Processes @p packets, each queued as one packet, and returns the result.
ProcessedStreamData runPackets(ProcessingParams p, const FrameSetup* setup, const std::vector<Packet>& packets,
                               Capture* events = nullptr, bool* ok = nullptr)
{
    PacketQueue queue;
    p.packetQueue = &queue;
    for (const Packet& packet : packets)
    {
        enqueueBits(queue, packet.bits, packet.base);
    }
    enqueueEndOfStream(queue);

    FrameProcessor fp;
    if (events != nullptr)
    {
        captureEvents(fp, *events);
    }
    const bool processed = fp.process(p, setup);
    if (ok != nullptr)
    {
        *ok = processed;
    }
    return fp.takeResult();
}

void checkSeries(const std::vector<double>& actual, const std::vector<double>& expected, const char* what)
{
    INFO(what);
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); i++)
    {
        INFO("sample " << i);
        CHECK(actual[i] == doctest::Approx(expected[i]).epsilon(1e-12));
    }
}

/// One receiver word at index 0, slope 1 and offset 0, so a value is the averaged raw count.
ParameterInfo* addWord0(FrameSetup& setup)
{
    setup.addParameter("W0", 0);
    ParameterInfo* param = setup.getParameter(0);
    param->slope      = 1.0;
    param->scale      = 0.0;
    return param;
}

/// The shipped word map, settings/receiver_params/default.toml: 16 receivers x 3 channels = 48 data words + 1 sync.
void loadDefaultFrameSetup(FrameSetup& setup)
{
    REQUIRE(setup.tryLoadingFile(repoFile(fs::path("settings") / "receiver_params" / "default.toml"), 49));
}

/// Gives every parameter of the shipped word map this slope and offset.
void setupParams(FrameSetup& setup, double slope, double offset)
{
    loadDefaultFrameSetup(setup);
    for (int i = 0; i < setup.length(); i++)
    {
        ParameterInfo* param = setup.getParameter(i);
        param->slope      = slope;
        param->scale      = offset;
    }
}

/// The TRC recording (RNRZ-L, 800-bit frames, 48 receiver words), with its time and PCM channels.
struct Trc
{
    fs::path path;
    int      time_id = -1;
    int      pcm_id  = -1;
};

Trc loadTrc()
{
    Trc t;
    t.path = recordingFile("agc_rnrz-l_trc_testfile.ch10");
    Chapter10Reader reader;
    REQUIRE_MESSAGE(reader.loadChannels(t.path), reader.lastError());
    t.pcm_id  = reader.getFirstPCMChannelID();
    t.time_id = reader.getCurrentTimeChannelID();
    REQUIRE(t.pcm_id >= 0);
    REQUIRE(t.time_id >= 0);
    return t;
}

/// The TRC recording's parameters for a whole-recording run at a one-second period.
ProcessingParams trcParams(const Trc& t, StreamMode mode)
{
    ProcessingParams p = makeTestParams(t.path, t.time_id, t.pcm_id);
    p.samplePeriodSec = 1.0;
    p.isRandomized    = true;
    p.mode            = mode;
    return p;
}

/// PRN-15 is 2^15-1 bits, deliberately NOT a whole number of 16-bit words. buildStreamJob resolves that with
/// ceiling division (FrameSetup::wordsInMinorFrame); mirroring the expression keeps the Safran cases honest if the
/// geometry ever changes.
/// The floor finds the sync and then extracts no frames, which is a different failure.
constexpr int kPrnFrameBits  = 32767;
constexpr int kPrnFrameWords = (kPrnFrameBits + PCMConstants::kCommonWordLen - 1) / PCMConstants::kCommonWordLen;

/// The Safran recording's time channel.
int safranTimeChannel(const fs::path& path)
{
    Chapter10Reader reader;
    REQUIRE_MESSAGE(reader.loadChannels(path), reader.lastError());
    const int time_id = reader.getCurrentTimeChannelID();
    REQUIRE(time_id >= 0);
    return time_id;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Constructor and abort
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: a non-ASCII path opens in both readers")
{
    // Both readers hand the path to irig106's enI106Ch10Open(), which opens it with the C runtime's narrow
    // open(char*). They used to encode it differently - the metadata reader in the local code page, the packet reader
    // as UTF-8 - so a file under a folder like C:/Users/Jos<e-acute>/ listed its channels and then failed the moment
    // processing started. Users' own folders are exactly where recordings get copied. Both now pass the process code
    // page, which this executable's manifest makes UTF-8, as the application's does.
    REQUIRE_MESSAGE(GetACP() == 65001u, "the test executable must run in the UTF-8 code page (its manifest)");

    const fs::path src = recordingFile("step_cal, example.ch10");
    const std::u8string folders[] = {
        u8"caf\u00E9",     // in the Windows-1252 code page: the ANSI path API can spell it
        u8"omega-\u03A9",  // outside it: no ANSI code page on a Western system can spell it at all
    };
    for (const std::u8string& folder : folders)
    {
        INFO("folder " << std::string(folder.begin(), folder.end()));
        TempDir tmp;
        const fs::path dir = tmp.path() / folder;
        REQUIRE(fs::create_directory(dir));
        const fs::path dst = dir / "recording.ch10";
        REQUIRE(fs::copy_file(src, dst));

        Chapter10Reader meta;
        REQUIRE_MESSAGE(meta.loadChannels(dst), "metadata reader could not open the path: " << meta.lastError());

        Ch10PacketReader reader;
        std::vector<ProcessingParams*> none;
        std::string error;
        reader.prepare(dst, meta.getCurrentTimeChannelID(), none, error);
        CHECK_MESSAGE(error != "Error opening data file.",
                      "packet reader could not open a path the metadata reader opened");
    }
}

TEST_CASE("FrameProcessor: reports exactly these messages")
{
    // The text of every message FrameProcessor reports, exactly, in order. Written against v2.14.2's
    // implementation and kept as it was across the move to std::format and an event sink: a format specifier is the
    // kind of thing a port gets subtly wrong (%1 of %2, 'f' with one decimal) and nothing else would notice.
    const std::string tag = "[CH 7 Rcvr Bus] ";
    const std::string unicode_label = utf8(u8"R\u00E9cepteur \u03A9");
    const std::string unicode_tag = "[CH 7 " + unicode_label + "] ";

    struct Row
    {
        const char* name;
        bool        returned;
        Strings     logs;
        Strings     errors;
    };
    const Row rows[] = {
        {"success", true,
         {tag + "processing...",
          tag + "4 bytes processed, 3 syncs found, 2 frames extracted.",
          tag + "complete — 1 samples extracted, elapsed N.Ns."},
         {}},
        // A label outside ASCII (an accented letter and Greek omega) goes in, is quoted in every message's tag, and
        // comes back out unchanged in the result.
        {"non-ascii-label", true,
         {unicode_tag + "processing...",
          unicode_tag + "4 bytes processed, 3 syncs found, 2 frames extracted.",
          unicode_tag + "complete — 1 samples extracted, elapsed N.Ns."},
         {}},
        // A pattern with a leading zero (0110) is not matched against the first three bits of the stream (110),
        // which would equal it before a whole pattern's worth of bits had arrived.
        {"leading-zero-pattern", false,
         {tag + "processing...",
          tag + "2 bytes processed, 0 syncs found, 0 frames extracted."},
         {tag + "no frame sync found — pattern 0x6 (4 bits), 12 bits/frame."}},
        // A frame is extracted only once all its words have arrived: four words cannot fit the 12-bit frame, so every
        // sync is on its boundary and none yields a frame.
        {"words-past-the-frame", false,
         {tag + "processing...",
          tag + "4 bytes processed, 3 syncs found, 0 frames extracted."},
         {tag + "sync found but no frames extracted — pattern 0x9 (4 bits), 12 bits/frame; 3 syncs, 2 boundary-aligned."}},
        // The mask is printed only when it constrains something.
        {"masked-pattern-no-sync", false,
         {tag + "processing...",
          tag + "2 bytes processed, 0 syncs found, 0 frames extracted."},
         {tag + "no frame sync found — pattern 0x9 (4 bits, mask 0xE), 12 bits/frame."}},
        {"sync-but-no-frames", false,
         {tag + "processing...",
          tag + "1 bytes processed, 1 syncs found, 0 frames extracted."},
         {tag + "sync found but no frames extracted — pattern 0x9 (4 bits), 12 bits/frame; 1 syncs, 0 boundary-aligned."}},
        {"cancelled", false,
         {tag + "processing...", tag + "processing cancelled by user."},
         {}},
        // The label "Ch 7" is the fallback buildStreamJob uses when TMATS has no name, so the tag drops it; an empty
        // label drops it too.
        {"no-packet-queue", false, {}, {"[CH 7] Internal error: no packet queue for stream."}},
        {"attributes-unresolved", false, {}, {"[CH 7] Internal error: PCM attributes not resolved."}},
        // Odd-length payloads cannot be byte-pair swapped. Every packet odd: the setting had no effect at all. Some
        // odd: one stream carrying two byte orders.
        {"every-payload-odd", false,
         {tag + "processing...",
          tag + "all 1 payloads are odd-length and cannot be byte-pair swapped — "
                "processed as Legacy Chapter 10 regardless of the setting.",
          tag + "3 bytes processed, 2 syncs found, 0 frames extracted."},
         {tag + "sync found but no frames extracted — pattern 0x9 (4 bits), 12 bits/frame; 2 syncs, 0 boundary-aligned."}},
        {"some-payloads-odd", true,
         {tag + "processing...",
          tag + "warning: byte swap skipped on 1 of 2 packets (odd length) — mixed byte order, sync may be unreliable.",
          tag + "5 bytes processed, 4 syncs found, 1 frames extracted.",
          tag + "complete — 1 samples extracted, elapsed N.Ns."},
         {}},
    };

    for (const Row& row : rows)
    {
        const std::string scenario = row.name;
        INFO("row " << scenario);

        PacketQueue q;
        ProcessingParams p;
        FrameSetup s;
        syntheticParams(p, q);

        if (scenario == "success" || scenario == "non-ascii-label")
        {
            if (scenario == "non-ascii-label")
            {
                p.streamLabel = unicode_label;
            }
            enqueueBits(q, "1001" "0000" "00001001" "0000" "00001001");
            enqueueEndOfStream(q);
        }
        else if (scenario == "leading-zero-pattern")
        {
            p.resolvedAttrs.syncPat = 0x6;
            enqueueBits(q, "1101" "0000" "0000");
            enqueueEndOfStream(q);
        }
        else if (scenario == "words-past-the-frame")
        {
            p.resolvedAttrs.wordsInFrame = 4;
            enqueueBits(q, "1001" "0000" "00001001" "0000" "00001001");
            enqueueEndOfStream(q);
        }
        else if (scenario == "masked-pattern-no-sync")
        {
            p.resolvedAttrs.syncMask = 0xE;
            enqueueBits(q, "0000" "0000" "0000");
            enqueueEndOfStream(q);
        }
        else if (scenario == "sync-but-no-frames" || scenario == "cancelled")
        {
            enqueueBits(q, "1001");
            enqueueEndOfStream(q);
        }
        else if (scenario == "no-packet-queue")
        {
            p.packetQueue = nullptr;
            p.streamLabel = "Ch 7";
        }
        else if (scenario == "attributes-unresolved")
        {
            p.resolvedAttrs.resolved = false;
            p.streamLabel = "";
        }
        else if (scenario == "every-payload-odd")
        {
            p.resolvedAttrs.needsSwap = true;
            enqueueBits(q, "100100000000000000001001"); // 3 bytes
            enqueueEndOfStream(q);
        }
        else if (scenario == "some-payloads-odd")
        {
            p.resolvedAttrs.needsSwap = true;
            enqueueBits(q, "100100000000000000001001"); // 3 bytes (odd)
            enqueueBits(q, "1001000000001001");         // 2 bytes (even)
            enqueueEndOfStream(q);
        }

        FrameProcessor fp;
        Capture c;
        captureEvents(fp, c);
        if (scenario == "cancelled")
        {
            fp.requestAbort();
        }
        const bool ok = fp.process(p, &s);

        // The one non-deterministic piece: how long the run took, printed to a tenth.
        const std::regex elapsed("elapsed [0-9]+\\.[0-9]s");
        Strings actual;
        for (const std::string& m : c.logs)
        {
            actual.push_back(std::regex_replace(m, elapsed, "elapsed N.Ns"));
        }

        CHECK(ok == row.returned);
        CHECK(actual == row.logs);
        CHECK(c.errors == row.errors);
        if (scenario == "non-ascii-label")
        {
            // The label also travels into the result, where the plot reads it.
            CHECK(fp.result().streamLabel == unicode_label);
        }
        if (scenario == "success")
        {
            CHECK(fp.result().pcmChannelId == 7);
            CHECK(fp.result().mode == StreamMode::FrameSyncLockStats);
        }
        // Exactly one completion notice per run, and it agrees with the return value.
        CHECK(c.finished == std::vector<bool>{row.returned});
    }
}

TEST_CASE("FrameProcessor: constructor defaults")
{
    // A fresh processor reports nowhere and holds an empty result; aborting one before any run is harmless.
    FrameProcessor fp;
    CHECK_FALSE(fp.result().hasSamples());
    CHECK(fp.result().channels.empty());
    fp.requestAbort();
}

TEST_CASE("FrameProcessor: request abort sets the flag")
{
    // The flag is read before each packet is taken, so a processor aborted beforehand ends the run as cancelled - not
    // as a stream with no sync - even when the stream has nothing in it but its end. (The "cancelled" row of the
    // messages case has a packet, which the check inside the bit loop would also catch.)
    PacketQueue q;
    ProcessingParams p;
    FrameSetup s;
    syntheticParams(p, q);
    enqueueEndOfStream(q);

    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);
    fp.requestAbort();
    CHECK_FALSE(fp.process(p, &s));
    REQUIRE_FALSE(cap.logs.empty());
    CHECK(cap.logs.back() == "[CH 7 Rcvr Bus] processing cancelled by user.");
    CHECK(cap.finished == std::vector<bool>{false});
    CHECK_FALSE(fp.result().hasSamples());
}

// ---------------------------------------------------------------------------------------------------------------------
// The bit-level helpers
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: derandomize leaves a short buffer as it was")
{
    std::vector<uint8_t> data(1, 0xAB);
    const std::vector<uint8_t> original = data;
    uint16_t lfsr = 0;
    FrameProcessorTestAccess::derandomize(data.data(), 8, lfsr);
    CHECK(data[0] == original[0]);
}

TEST_CASE("FrameProcessor: derandomize changes a longer buffer")
{
    std::vector<uint8_t> data(4, 0xFF);
    const std::vector<uint8_t> original = data;
    uint16_t lfsr = 0;
    FrameProcessorTestAccess::derandomize(data.data(), 32, lfsr);
    CHECK_MESSAGE(data != original, "32 bits of all-1s should change after derandomization");
}

TEST_CASE("FrameProcessor: derandomize undoes the IRIG 106 randomizer, in pieces, and resynchronizes")
{
    // An independent randomizer scrambles a pseudo-random stream; derandomizing it in pieces of every size from one
    // byte up, carrying the register between them, must give back every bit.
    std::mt19937 gen(20261025);
    std::string plain;
    for (int i = 0; i < 8 * 600; i++)
    {
        plain += (gen() & 1u) != 0 ? '1' : '0';
    }
    const std::vector<uint8_t> original = packBitString(plain);
    std::vector<uint8_t> data = packBitString(scramble(plain));
    REQUIRE(data != original);

    uint16_t lfsr = 0;
    std::size_t at = 0;
    for (std::size_t piece = 1; at < data.size(); piece++)
    {
        const std::size_t n = std::min(piece, data.size() - at);
        FrameProcessorTestAccess::derandomize(data.data() + at, 8 * n, lfsr);
        at += n;
    }
    CHECK(data == original);

    // The descrambler is self-synchronizing: started in the wrong state, it is right again once its 15 stages hold
    // received bits, and from then on.
    std::vector<uint8_t> late = packBitString(scramble(plain));
    uint16_t wrong = 0x5A5A;
    FrameProcessorTestAccess::derandomize(late.data(), 8 * late.size(), wrong);
    for (std::size_t i = 15; i < plain.size(); i++)
    {
        const bool bit = ((late[i / 8] >> (7 - i % 8)) & 1) != 0;
        if (bit != (plain[i] == '1'))
        {
            FAIL_CHECK("bit " << i << " is wrong after the register has filled");
            break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Process error paths
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: an invalid time channel, PCM channel or file fails the run")
{
    // A run the reader never prepared has no packet queue; each of these parameter sets ends there, with an error and
    // a failed completion.
    struct Row
    {
        const char* name;
        const char* file;
        int         time_id;
        int         pcm_id;
    };
    const Row rows[] = {
        {"invalid time channel", "dummy.ch10", -1, 1},
        {"invalid PCM channel", "dummy.ch10", 1, -1},
        {"invalid file", "nonexistent_file.ch10", 1, 1},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        FrameProcessor fp;
        Capture cap;
        captureEvents(fp, cap);

        FrameSetup setup;
        ProcessingParams p = makeTestParams(row.file, row.time_id, row.pcm_id);
        p.samplePeriodSec = 1.0;
        CHECK_FALSE(fp.process(p, &setup));
        CHECK_FALSE(cap.errors.empty());
        CHECK(cap.finished == std::vector<bool>{false});
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// In-memory accumulation, on the TRC recording
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: a receiver run accumulates receiver data")
{
    const Trc t = loadTrc();
    FrameSetup setup;
    setupParams(setup, 1.0, 0.0);

    ProcessingParams p = trcParams(t, StreamMode::ReceiverChannelInfo);
    p.streamLabel = "Ch test";

    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);
    const auto began = std::chrono::steady_clock::now();
    REQUIRE_MESSAGE(runWithReader(fp, p, &setup), "processing should succeed on the valid RNRZ-L recording");
    const double wall_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();

    // The completion line's elapsed time is in seconds, and no longer than the run took from outside.
    REQUIRE_FALSE(cap.logs.empty());
    std::smatch elapsed;
    REQUIRE_MESSAGE(std::regex_search(cap.logs.back(), elapsed, std::regex("elapsed ([0-9]+\\.[0-9])s\\.$")),
                    cap.logs.back());
    CHECK(std::stod(elapsed[1].str()) <= wall_sec + 0.05);

    const ProcessedStreamData& r = fp.result();
    REQUIRE_MESSAGE(r.hasSamples(), "the result must hold at least one time sample");
    // Receiver (SNR) streams are always considered locked (they come from a separate piece of equipment), so no
    // lock or missed-frames series is produced.
    CHECK_MESSAGE(r.lockPercent.empty(), "receiver mode must produce no lock series");
    CHECK_MESSAGE(r.accumulatedMissedFrames.empty(), "receiver mode must produce no missed-frames series");
    CHECK(static_cast<int>(r.channels.size()) == setup.length());
    CHECK(r.streamLabel == "Ch test");
    CHECK(r.mode == StreamMode::ReceiverChannelInfo);

    // Every channel's value vector is parallel to the time vector.
    for (const ProcessedChannelSeries& ch : r.channels)
    {
        CHECK(ch.values.size() == r.timesSec.size());
    }
    // Timestamps are non-decreasing.
    for (std::size_t i = 1; i < r.timesSec.size(); i++)
    {
        CHECK(r.timesSec[i] >= r.timesSec[i - 1]);
    }
}

TEST_CASE("FrameProcessor: a lock-only run has no channels")
{
    const Trc t = loadTrc();
    ProcessingParams p = trcParams(t, StreamMode::FrameSyncLockStats);

    FrameSetup empty_setup; // no word map for lock-only mode
    FrameProcessor fp;
    REQUIRE_MESSAGE(runWithReader(fp, p, &empty_setup), "lock-only processing should succeed");

    const ProcessedStreamData& r = fp.result();
    REQUIRE_MESSAGE(r.hasSamples(), "a lock-only result must hold time samples");
    CHECK_MESSAGE(r.channels.empty(), "lock-only mode must produce no receiver channels");
    CHECK(r.lockPercent.size() == r.timesSec.size());
    CHECK(r.accumulatedMissedFrames.size() == r.timesSec.size());
    CHECK(r.mode == StreamMode::FrameSyncLockStats);
}

TEST_CASE("FrameProcessor: the accumulated frame sync errors never decrease")
{
    const Trc t = loadTrc();
    ProcessingParams p = trcParams(t, StreamMode::FrameSyncLockStats);

    FrameSetup empty_setup;
    FrameProcessor fp;
    REQUIRE_MESSAGE(runWithReader(fp, p, &empty_setup), "lock-only processing should succeed");

    const ProcessedStreamData& r = fp.result();
    REQUIRE_MESSAGE(r.hasSamples(), "the result must hold time samples");
    REQUIRE(r.accumulatedMissedFrames.size() == r.timesSec.size());

    // The accumulated count starts at >= 0 and never decreases.
    CHECK(r.accumulatedMissedFrames.front() >= 0.0);
    for (std::size_t i = 1; i < r.accumulatedMissedFrames.size(); i++)
    {
        INFO("sample " << i);
        CHECK_MESSAGE(r.accumulatedMissedFrames[i] >= r.accumulatedMissedFrames[i - 1],
                      "frame sync error accumulation must be monotonically non-decreasing");
    }
}

TEST_CASE("FrameProcessor: the slope scales the values")
{
    const Trc t = loadTrc();

    auto run = [&](double slope) -> double {
        FrameSetup setup;
        setupParams(setup, slope, 0.0);
        ProcessingParams p = trcParams(t, StreamMode::ReceiverChannelInfo);
        FrameProcessor fp;
        REQUIRE(runWithReader(fp, p, &setup));
        REQUIRE_FALSE(fp.result().channels.empty());
        REQUIRE_FALSE(fp.result().channels[0].values.empty());
        return fp.result().channels[0].values.front();
    };

    const double v1 = run(1.0);
    const double v2 = run(2.0);
    REQUIRE_MESSAGE(std::abs(v1) > 1e-10, "the first AGC sample is zero, so the slope ratio cannot show");
    CHECK_MESSAGE(std::abs(v2 - 2.0 * v1) < 1e-6, "expected v2 (" << v2 << ") = 2 * v1 (" << v1 << ")");
}

TEST_CASE("FrameProcessor: a sync mask does not mask the receiver words")
{
    // The sync mask says which bits of the SYNC PATTERN must match; it has nothing to do with the receiver words that
    // follow it. irig106's Set_Attributes_Ext_PcmF1 ANDs its word mask with the sync mask, and the reader used to pass
    // that on, so a mask that ignored even the lowest sync bit cleared the lowest bit of every AGC sample (0x00FF00FF
    // kept only the low byte; 0xFFFF0000 would have zeroed them all). Ignoring sync bits must change no value.
    const Trc t = loadTrc();

    auto run = [&](uint64_t sync_pattern, uint64_t sync_mask) -> ProcessedStreamData {
        FrameSetup setup;
        setupParams(setup, 1.0, 0.0); // slope 1, offset 0: the values are the averaged raw counts
        ProcessingParams p = trcParams(t, StreamMode::ReceiverChannelInfo);
        p.frameSync     = sync_pattern;
        p.frameSyncMask = sync_mask;
        FrameProcessor fp;
        REQUIRE(runWithReader(fp, p, &setup));
        return fp.takeResult();
    };

    const ProcessedStreamData full = run(0xFE6B2840ull, 0); // every bit of the pattern
    // Sync bits 8-15 ignored: the other 24 still find every frame of this recording, and the old code's word mask
    // would have been 0x00FF, keeping only each word's low byte. The pattern carries zeros where the mask ignores - the
    // matcher compares the MASKED stream with the pattern as written, as irig106's own decoder does, so a pattern with
    // ones there could never match. (Ignoring only the lowest sync bit shows nothing: this recording's raw words never
    // set their lowest bit.)
    const ProcessedStreamData masked = run(0xFE6B0040ull, 0xFFFF00FFull);
    REQUIRE(full.hasSamples());
    REQUIRE(masked.timesSec.size() == full.timesSec.size());
    REQUIRE(masked.channels.size() == full.channels.size());

    // The recording's averaged counts reach far above 255, so a cleared high byte would show.
    bool any_above_a_byte = false;
    for (std::size_t c = 0; c < full.channels.size(); c++)
    {
        REQUIRE(masked.channels[c].values.size() == full.channels[c].values.size());
        for (std::size_t i = 0; i < full.channels[c].values.size(); i++)
        {
            if (masked.channels[c].values[i] != full.channels[c].values[i])
            {
                FAIL_CHECK("channel " << c << " sample " << i << ": " << masked.channels[c].values[i]
                                      << " with the mask, " << full.channels[c].values[i] << " without");
            }
            any_above_a_byte = any_above_a_byte || full.channels[c].values[i] > 255.0;
        }
    }
    CHECK_MESSAGE(any_above_a_byte, "no value exceeded 255, so this recording could not show a cleared high byte");
}

TEST_CASE("FrameProcessor: a shorter period gives more samples")
{
    const Trc t = loadTrc();

    auto countAt = [&](double period) -> std::size_t {
        FrameSetup setup;
        setupParams(setup, 1.0, 0.0);
        ProcessingParams p = trcParams(t, StreamMode::ReceiverChannelInfo);
        p.samplePeriodSec = period;
        FrameProcessor fp;
        REQUIRE(runWithReader(fp, p, &setup));
        return fp.result().timesSec.size();
    };

    const std::size_t n1s  = countAt(TimeConstants::kSamplePeriod1s);
    const std::size_t n10ms = countAt(TimeConstants::kSamplePeriod10ms);
    CHECK_MESSAGE(n1s > 0, "a 1 s period run should produce samples");
    CHECK_MESSAGE(n10ms > n1s, "a 10 ms period (" << n10ms << ") should give more samples than 1 s (" << n1s << ")");
}

// ---------------------------------------------------------------------------------------------------------------------
// The calibration round trip on a real recording (US5.3)
// ---------------------------------------------------------------------------------------------------------------------

namespace {

/// Records whether an extraction ended well. Called on the extractor's driver thread; read after wait().
class FinishSink final : public ProcessingEvents
{
public:
    void logMessage(const std::string&) override {}
    void errorOccurred(const std::string&) override {}
    void processingFinished(bool ok) override { m_ok = ok; }

    std::atomic<bool> m_ok{false};
};

} // namespace

TEST_CASE("FrameProcessor: a calibration round trip on a real recording produces clean steps")
{
    // End to end on real decoded data (not synthetic, unlike the step detector's same-data round trip): the TRC
    // recording is BOTH the calibration file and the "main" file being measured. Since they are the same recording,
    // the calibration should reproduce the step values (0, 6, 12, ... 60 dB) when re-applied - the whole real
    // pipeline (derandomization, frame sync, word extraction, CalibrationExtractor, StepDetector,
    // interpolateCalibration) exercised together on genuine decoded RNRZ-L data rather than hand-built raw series.
    const Trc t = loadTrc();

    std::vector<StepDefinition> steps;
    std::string step_error;
    REQUIRE_MESSAGE(parseStepConfig(repoFile(fs::path("settings") / "rcvr_cals" / "default.toml"), steps,
                                                  step_error),
                    step_error);
    REQUIRE_FALSE(steps.empty());

    // ---- Extraction pass: build calibration profiles from this same recording ----
    CalibrationExtractor::Request req;
    req.calFilename   = t.path;
    req.timeChannelId = t.time_id;
    req.pcmChannelId  = t.pcm_id;
    // 8000 bits is this recording's true minor-frame length; RNRZ-L; 0.8 Mbps (800 kbps, ~100 frames/s) drives the
    // adaptive extract period. An ordinary Chapter 10 recording, so it needs the byte swap. Stated rather than
    // inherited, so a change to the DTO default cannot silently retune it.
    REQUIRE(resolveFrameSync("FE6B2840", std::string(), req.acquisition.sync) == FrameSyncError::None);
    req.acquisition.bitsInMinorFrame = 8000;
    req.acquisition.randomized       = true;
    req.acquisition.dataRateMbps     = 0.8;
    req.acquisition.swapBytes        = true;
    req.numReceivers     = 16;
    req.receiverChannels = 3;
    req.steps            = steps;

    CalibrationExtractor extractor;
    auto events = std::make_shared<FinishSink>();
    extractor.start(req, events);
    extractor.wait();
    REQUIRE_MESSAGE(events->m_ok.load(), extractor.summary());

    std::map<int, CalibrationProfile> calibration_by_word;
    for (const CalibrationChannelResult& r : extractor.results())
    {
        if (r.profile.valid)
        {
            calibration_by_word.emplace(r.word, r.profile);
        }
    }
    REQUIRE_MESSAGE(!calibration_by_word.empty(), "expected at least one channel to calibrate from the real recording");

    // ---- Main pass: re-measure the SAME recording with the extracted profiles ----
    FrameSetup setup;
    static const char* kPrefixes[] = {"L", "R", "C"};
    for (int r = 0; r < req.numReceivers; r++)
    {
        for (int c = 0; c < req.receiverChannels; c++)
        {
            setup.addParameter(std::string(kPrefixes[c]) + "_RCVR" + std::to_string(r + 1), r * req.receiverChannels + c);
        }
    }
    for (int i = 0; i < setup.length(); i++)
    {
        ParameterInfo* param = setup.getParameter(i);
        param->slope      = 1.0;
        param->scale      = 0.0;
        const auto it = calibration_by_word.find(param->word);
        if (it != calibration_by_word.end())
        {
            param->profile = it->second;
        }
    }

    ProcessingParams p = makeTestParams(t.path, t.time_id, t.pcm_id, 0xFE6B2840, 32, 500, 8000);
    // A 100 ms window: at this recording's ~8000-bit / ~100 frames-per-second rate the shipped 10 ms period averages
    // only ~1 frame (a sparse, gap-laced series), while 1 s leaves only a few samples per ~4.5 s dwell. 100 ms is the
    // neighbourhood the extractor sizes itself to, giving clean plateaus for the re-detection below. The rate clock
    // matches the extraction pass (and avoids IRIG-time fragility).
    p.samplePeriodSec  = 0.1;
    p.useDataRateClock = true;
    p.isRandomized     = true;
    p.mode             = StreamMode::ReceiverChannelInfo;

    FrameProcessor fp;
    REQUIRE_MESSAGE(runWithReader(fp, p, &setup), "the main pass should succeed");
    const ProcessedStreamData& result = fp.result();

    // ---- Verify: each detected plateau in the calibrated output lands within a small tolerance of SOME multiple of
    //      6 dB. Scoped to StepDetector's own plateau detection (rather than the raw 198 s series, only ~55 s of which
    //      is the step sweep - the rest is pre/post-roll content never meant to land on a clean multiple), and each
    //      detected point is checked against its nearest multiple of 6 rather than a chronologically paired step:
    //      detect() sorts its points ascending by value, so a pre-roll plateau ahead of the sweep would misalign
    //      index-based pairing even though the calibration is correct. "Close to *a* multiple of 6" sidesteps that
    //      ordering and tests the property the user cares about.
    constexpr double kToleranceDb = 1.0;
    bool any_channel_checked = false;
    for (const ProcessedChannelSeries& series : result.channels)
    {
        if (calibration_by_word.count(series.word) == 0 || series.values.empty())
        {
            continue;
        }

        const StepDetector::Result det = StepDetector::detect(series.values, p.samplePeriodSec, steps);
        if (!det.profile.valid)
        {
            continue; // this word's main-pass series showed no clean re-detectable plateaus
        }

        any_channel_checked = true;
        for (const CalibrationPoint& pt : det.profile.points)
        {
            const double nearest_multiple_of_6 = std::round(pt.rawAvg / 6.0) * 6.0;
            if (std::abs(pt.rawAvg - nearest_multiple_of_6) > kToleranceDb)
            {
                FAIL_CHECK(series.name << " (word " << series.word << "): plateau at " << pt.rawAvg
                                       << " dB is not close to a 6 dB multiple");
            }
        }
    }
    CHECK_MESSAGE(any_channel_checked, "no calibrated channel produced a re-detectable step series");
}

// ---------------------------------------------------------------------------------------------------------------------
// Off-phase sync and lock loss, on synthetic streams
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: an off-phase sync after lock loss is not extracted")
{
    // A sync match that is NOT boundary-aligned (a false positive while unlocked, such as one right after a loss of
    // lock) must never trigger sample extraction. The 4-bit sync / 4-bit word stream:
    //   1. an initial sync match (never boundary-aligned by definition) that starts collecting word A = 0;
    //   2. zero filler long enough to exceed bitsInFrame and force a loss of lock;
    //   3. an off-phase sync-pattern collision, not on the frame boundary, while still unlocked - this must be
    //      rejected rather than extracting the stale word A;
    //   4. word B = 15, then a genuine boundary-aligned sync that confirms the new frame and extracts word B.
    // If the off-phase match in step 3 were treated as a confirmed frame, the average would mix in the stale word A
    // (0) and read 7.5 instead of 15.
    std::string bits;
    bits += "1001";      // initial sync acquisition (not boundary-aligned)
    bits += "0000";      // word 0 of frame 1 = 0
    bits += "000000000"; // filler -> minor_frame_bit_count exceeds bitsInFrame, losing lock
    bits += "1001";      // off-phase false-positive sync while unlocked (NOT boundary-aligned)
    bits += "1111";      // word 0 of frame 2 = 15
    bits += "00001001";  // word 1 filler + genuine sync, boundary-aligned -> confirms frame 2

    FrameSetup setup;
    addWord0(setup);

    PacketQueue queue;
    ProcessingParams p;
    syntheticParams(p, queue);
    p.streamLabel = "off-phase-test";
    p.mode        = StreamMode::ReceiverChannelInfo;
    enqueueBits(queue, bits);
    enqueueEndOfStream(queue);

    FrameProcessor fp;
    REQUIRE_MESSAGE(fp.process(p, &setup), "the synthetic off-phase-sync stream should process");

    const ProcessedStreamData& r = fp.result();
    REQUIRE_MESSAGE((!r.channels.empty() && !r.channels[0].values.empty()), "expected at least one extracted sample");
    // Only the boundary-aligned sync yields a sample (word B = 15). Had the off-phase false positive after lock loss
    // been extracted, the stale word A (0) would be averaged in, giving 7.5.
    CHECK(r.channels[0].values.front() == 15.0);
}

TEST_CASE("FrameProcessor: a non-linear calibration averages raw counts before interpolating")
{
    // A CalibrationProfile maps the AVERAGED raw count of a plateau to its true dB, so within an output window the
    // processor must average the raw counts first and apply interpolateCalibration() once - NOT interpolate every
    // frame and average the results. For a kinked (non-linear) profile the two orders differ.
    //
    // Two frames in one output window, word 0 = 0 and word 0 = 15, with a convex profile, flat low and steep high:
    //     raw  0.0 -> 10 dB
    //     raw  7.5 -> 10 dB   (vertex)
    //     raw 15.0 -> 160 dB
    // The mean raw is 7.5, so the right (raw-first) output is interpolate(7.5) = 10 dB. The wrong (per-frame-first)
    // order would give mean(interpolate(0), interpolate(15)) = mean(10, 160) = 85 dB.
    std::string bits;
    bits += "1001"; // initial sync acquisition (not boundary-aligned)
    bits += "0000"; // frame 1, word 0 = 0
    bits += "0000"; // frame 1, word 1 (unused)
    bits += "1001"; // boundary sync -> extract frame 1 (word 0 = 0)
    bits += "1111"; // frame 2, word 0 = 15
    bits += "0000"; // frame 2, word 1 (unused)
    bits += "1001"; // boundary sync -> extract frame 2 (word 0 = 15)

    FrameSetup setup;
    ParameterInfo* param = addWord0(setup); // slope and offset are ignored once a valid profile is present
    CalibrationProfile profile;
    profile.points.push_back({0.0, 10.0});
    profile.points.push_back({7.5, 10.0});
    profile.points.push_back({15.0, 160.0});
    profile.valid  = true;
    param->profile = profile;

    PacketQueue queue;
    ProcessingParams p;
    syntheticParams(p, queue);
    p.streamLabel = "nonlinear-avg-test";
    p.mode        = StreamMode::ReceiverChannelInfo;
    enqueueBits(queue, bits);
    enqueueEndOfStream(queue);

    FrameProcessor fp;
    REQUIRE_MESSAGE(fp.process(p, &setup), "the synthetic non-linear-calibration stream should process");

    const ProcessedStreamData& r = fp.result();
    REQUIRE_MESSAGE((!r.channels.empty() && !r.channels[0].values.empty()), "expected at least one extracted sample");
    // Right (average raw, then calibrate once): interpolate(mean(0, 15) = 7.5) = 10 dB. Wrong: mean(10, 160) = 85 dB.
    CHECK_MESSAGE(std::abs(r.channels[0].values.front() - 10.0) < 1e-9,
                  "expected 10.0 dB (raw averaged before calibration), got " << r.channels[0].values.front());
}

// ---------------------------------------------------------------------------------------------------------------------
// Windows, clocks, lock and missed frames, on synthetic streams timed at one second per bit
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: each window averages its own frames, on either clock")
{
    // Frames end (their sync's last bit) at bits 15, 27, 39, 51, 63, 75. With 15 s windows from 0, window 0 holds bits
    // 0..15 - the bit AT a window's end time is still in it - and window k holds bits 15k+1 .. 15k+15. So the frames
    // land: {1}, {4}, {5}, {6}, {7, 15}. Each window's average is of its own frames only.
    const std::string bits = stream({1, 4, 5, 6, 7, 15});
    const std::vector<double> values = {1, 4, 5, 6, 11};
    // The first four bits come before any sync, so 12 of window 0's 16 bits are locked; every later bit is.
    const std::vector<double> lock   = {75, 100, 100, 100, 100};
    const std::vector<double> missed = {0, 0, 0, 0, 0};

    for (const bool rate_clock : {true, false})
    {
        // The rate clock counts from the first bit and ignores the packets' times (5000 here). The IRIG clock starts
        // the windows at the first packet's time and places each bit by its own packet's time, so the stream goes in
        // 7-bit packets - across every frame and window boundary - stamped 1000 + their first bit.
        INFO((rate_clock ? "the rate clock" : "the IRIG clock"));
        const std::vector<Packet> packets = rate_clock ? std::vector<Packet>{{bits, 5000.0}} : split(bits, 7, 1000.0);
        const double t0 = rate_clock ? 0.0 : 1000.0;
        const std::vector<double> times = {t0, t0 + 15, t0 + 30, t0 + 45, t0 + 60};

        FrameSetup setup;
        addWord0(setup);
        Capture cap;
        const ProcessedStreamData receiver = runPackets(timedParams(StreamMode::ReceiverChannelInfo, rate_clock), &setup,
                                                        packets, &cap);
        // The completion line counts every window recorded: the four closed during the run and the one at the end.
        REQUIRE_FALSE(cap.logs.empty());
        CHECK(contains(cap.logs.back(), "complete — 5 samples extracted"));
        checkSeries(receiver.timesSec, times, "receiver times");
        REQUIRE(receiver.channels.size() == 1);
        checkSeries(receiver.channels[0].values, values, "receiver values");

        FrameSetup none;
        const ProcessedStreamData locked = runPackets(timedParams(StreamMode::FrameSyncLockStats, rate_clock), &none,
                                                      packets);
        checkSeries(locked.timesSec, times, "lock times");
        checkSeries(locked.lockPercent, lock, "lock percent");
        checkSeries(locked.accumulatedMissedFrames, missed, "missed frames");
    }
}

TEST_CASE("FrameProcessor: a lost lock counts missed frames and leaves windows with no frames at zero")
{
    // Two frames, 30 bits with no sync, then a sync off the frame boundary and two more frames. Bits: frames end at 15
    // and 27; the frame counter overruns at 40 and 52 (one missed frame each, and lock is lost at 40); the sync at
    // 58..61 reacquires off-phase; frames end at 73 and 85. Windows: 0..15, 16..30, 31..45, 46..60, 61..75, 76..85.
    const std::string bits = stream({1, 4}) + std::string(30, '0') + "1001" + frame(5) + frame(6);
    const std::vector<double> times = {0, 15, 30, 45, 60, 75};

    FrameSetup setup;
    addWord0(setup);
    const ProcessedStreamData receiver = runPackets(timedParams(StreamMode::ReceiverChannelInfo, true), &setup, {{bits}});
    checkSeries(receiver.timesSec, times, "receiver times");
    REQUIRE(receiver.channels.size() == 1);
    // Windows that had bits but no frame are recorded, at zero - not as the average of nothing.
    checkSeries(receiver.channels[0].values, {1, 4, 0, 0, 5, 6}, "receiver values");

    FrameSetup none;
    const ProcessedStreamData locked = runPackets(timedParams(StreamMode::FrameSyncLockStats, true), &none, {{bits}});
    checkSeries(locked.timesSec, times, "lock times");
    // Locked: 4..15 of 0..15; all of 16..30; 31..39 of 31..45; none of 46..60; 62..75 of 61..75; all of 76..85.
    checkSeries(locked.lockPercent, {75, 100, 60, 0, 1400.0 / 15.0, 100}, "lock percent");
    // Counted where they happen, bit by bit - never extrapolated as well, since these windows had bits.
    checkSeries(locked.accumulatedMissedFrames, {0, 0, 1, 2, 2, 2}, "missed frames");
}

TEST_CASE("FrameProcessor: unsynced bits miss one frame per frame length")
{
    // One frame (its sync ends at bit 15), then 169 bits with no sync. The frame counter overruns 13 bits after the
    // sync and every 12 bits after that: at bits 28, 40, ..., 184 - fourteen missed frames.
    const std::string bits = stream({1}) + std::string(169, '0');

    ProcessingParams whole = timedParams(StreamMode::FrameSyncLockStats, true);
    whole.samplePeriodSec = 1e9;
    FrameSetup none;
    const ProcessedStreamData all = runPackets(whole, &none, {{bits}});
    checkSeries(all.timesSec, {0}, "times");
    checkSeries(all.accumulatedMissedFrames, {14}, "missed frames");
    // Locked from bit 4 to bit 27: 24 of 185.
    checkSeries(all.lockPercent, {2400.0 / 185.0}, "lock percent");
}

TEST_CASE("FrameProcessor: a recording gap extrapolates the missed frames from the data rate")
{
    // Two frames in a packet at 1000 s (bits at 1000..1027), then three in a packet at 1100 s (1100..1135). The 15 s
    // windows from 1000: 1000..1015 and 1015..1030 hold the first packet; the four from 1030 to 1090 hold no bits at
    // all - a gap in the recording - and each is recorded with no lock and the frames it would have carried at the
    // bit rate; 1090..1105, 1105..1120 and 1120..1135 hold the second packet.
    //
    // The gap drops lock, and the frame counter carries on across it, so the second packet's first frame boundary
    // only starts the count again (it is not extracted) and its first locked bit is the one after its second.
    const std::vector<Packet> packets = {{stream({1, 4}), 1000.0}, {frame(5) + frame(6) + frame(7), 1100.0}};
    const std::vector<double> times = {1000, 1015, 1030, 1045, 1060, 1075, 1090, 1105, 1120};

    FrameSetup setup;
    addWord0(setup);
    const ProcessedStreamData receiver = runPackets(timedParams(StreamMode::ReceiverChannelInfo, false), &setup, packets);
    checkSeries(receiver.timesSec, times, "receiver times");
    REQUIRE(receiver.channels.size() == 1);
    checkSeries(receiver.channels[0].values, {1, 4, 0, 0, 0, 0, 0, 0, 6.5}, "receiver values");

    const std::vector<double> lock = {75, 100, 0, 0, 0, 0, 0, 0, 80};
    FrameSetup none;
    // No operator rate: the bit period gives 1 bit/s, so a 15 s gap window would have carried 15 bits, one whole
    // 12-bit frame.
    const ProcessedStreamData tmats = runPackets(timedParams(StreamMode::FrameSyncLockStats, false), &none, packets);
    checkSeries(tmats.timesSec, times, "lock times");
    checkSeries(tmats.lockPercent, lock, "lock percent");
    checkSeries(tmats.accumulatedMissedFrames, {0, 0, 1, 2, 3, 4, 4, 4, 4}, "missed frames at the bit period's rate");

    // An operator's data rate wins over the bit period: 12 bit/s is fifteen frames per gap window.
    ProcessingParams rated = timedParams(StreamMode::FrameSyncLockStats, false);
    rated.dataRateBps = 12.0;
    const ProcessedStreamData user = runPackets(rated, &none, packets);
    checkSeries(user.lockPercent, lock, "lock percent at the operator's rate");
    checkSeries(user.accumulatedMissedFrames, {0, 0, 15, 30, 45, 60, 60, 60, 60}, "missed frames at the operator's rate");
}

TEST_CASE("FrameProcessor: frames are extracted only after the minimum number of syncs")
{
    // "1001001" holds two syncs three bits apart: the first starts collecting, and the second - off-phase, while not
    // yet locked - starts again, part-way through a word, which must not shift the next frame's words. Then four
    // frames, ending at bits 18, 30, 42 and 54. A boundary sync adds to the count; an off-phase one sets it to 1.
    const std::string bits = "1001001" + frame(1) + frame(4) + frame(7) + frame(12);
    for (const uint32_t min_syncs : {2u, 3u})
    {
        INFO("minimum syncs " << min_syncs);
        ProcessingParams p = timedParams(StreamMode::ReceiverChannelInfo, true);
        p.samplePeriodSec        = 1e9;
        p.resolvedAttrs.minSyncs = min_syncs;
        FrameSetup setup;
        addWord0(setup);
        const ProcessedStreamData r = runPackets(p, &setup, {{bits}});
        REQUIRE(r.channels.size() == 1);
        // Two syncs: the first frame's boundary is the second, so all four frames count. Three: it is not yet enough.
        checkSeries(r.channels[0].values, {min_syncs == 2 ? 24.0 / 4 : 23.0 / 3}, "the average");
    }
}

TEST_CASE("FrameProcessor: an off-phase sync while locked leaves the frame's words where they are")
{
    // In lock, a match off the frame boundary is data that happens to look like the sync, so it is ignored - and its
    // bit is still a bit of the word being read. The middle frame's word 0 is 1001, the sync itself, so the pattern
    // matches at the word's last bit, four bits into a locked frame. That bit used to be dropped from the word count,
    // so word 0 was read one bit late, as 0010 = 2, and the average was (1 + 2 + 4) / 3.
    const std::string bits = stream({1, 9, 4});
    for (const StreamMode mode : {StreamMode::ReceiverChannelInfo, StreamMode::FrameSyncLockStats})
    {
        INFO("mode " << static_cast<int>(mode));
        ProcessingParams p = timedParams(mode, true);
        p.samplePeriodSec = 1e9;
        FrameSetup setup;
        addWord0(setup);
        Capture events;
        const ProcessedStreamData r = runPackets(p, &setup, {{bits}}, &events);
        // The match is counted as found (the leading sync, three boundary syncs and it) and costs no frame.
        CHECK(joined(events.logs).find("5 syncs found, 3 frames extracted.") != std::string::npos);
        if (mode == StreamMode::ReceiverChannelInfo)
        {
            REQUIRE(r.channels.size() == 1);
            checkSeries(r.channels[0].values, {(1.0 + 9.0 + 4.0) / 3}, "the average of word 0");
        }
        else
        {
            // The lock holds: every bit after the leading sync's four is in lock.
            checkSeries(r.lockPercent, {100.0 * 36 / 40}, "the lock percentage");
        }
    }
}

TEST_CASE("FrameProcessor: every word of the map becomes a channel, each calibrated as (raw + offset) x slope")
{
    // Word 0 is 5 then 6. A word the frame does not have (before it, or past its two words) stays at zero rather than
    // reading outside the frame; and a lock-only stream has no channels whatever its word map says.
    FrameSetup setup;
    setup.addParameter("W0", 0);
    setup.addParameter("Cal", 0);
    setup.addParameter("Before", -1);
    setup.addParameter("Past", 2);
    for (int i = 0; i < setup.length(); i++)
    {
        ParameterInfo* param = setup.getParameter(i);
        param->slope      = 1.0;
        param->scale      = 0.0;
    }
    setup.getParameter(1)->slope = 2.0;
    setup.getParameter(1)->scale = 3.0;

    ProcessingParams p = timedParams(StreamMode::ReceiverChannelInfo, true);
    p.samplePeriodSec = 1e9;
    const ProcessedStreamData r = runPackets(p, &setup, {{stream({5, 6})}});
    REQUIRE(r.channels.size() == 4);
    const char* names[] = {"W0", "Cal", "Before", "Past"};
    const int words[] = {0, 0, -1, 2};
    const double values[] = {5.5, (5.5 + 3.0) * 2.0, 0.0, 0.0};
    for (std::size_t i = 0; i < 4; i++)
    {
        INFO("channel " << i);
        CHECK(r.channels[i].name == names[i]);
        CHECK(r.channels[i].word == words[i]);
        REQUIRE(r.channels[i].values.size() == 1);
        CHECK(r.channels[i].values[0] == values[i]);
    }

    p.mode = StreamMode::FrameSyncLockStats;
    const ProcessedStreamData lock_only = runPackets(p, &setup, {{stream({5, 6})}});
    CHECK(lock_only.hasSamples());
    CHECK(lock_only.channels.empty());
}

TEST_CASE("FrameProcessor: a run reads its word map and changes nothing in it, so a second run over it gives the same answer")
{
    // The window's running sums are the run's own (they were once kept in the word map, where a run cancelled
    // mid-window left its sum for the next). process() takes the map as const; this holds the behaviour that follows:
    // two runs over one map agree, and the map is as it was.
    FrameSetup setup;
    setup.addParameter("W0", 0);
    setup.addParameter("Cal", 0);
    setup.getParameter(0)->slope = 1.0;
    setup.getParameter(1)->slope = 2.0;
    setup.getParameter(1)->scale = 3.0;

    ProcessingParams p = timedParams(StreamMode::ReceiverChannelInfo, true);
    p.samplePeriodSec = 1e9;
    const ProcessedStreamData first  = runPackets(p, &setup, {{stream({5, 6})}});
    const ProcessedStreamData second = runPackets(p, &setup, {{stream({5, 6})}});
    REQUIRE(first.channels.size() == 2);
    REQUIRE(second.channels.size() == 2);
    for (std::size_t i = 0; i < 2; i++)
    {
        INFO("channel " << i);
        REQUIRE(first.channels[i].values.size() == 1);
        CHECK(second.channels[i].values == first.channels[i].values);
    }
    CHECK(first.channels[0].values[0] == 5.5);
    CHECK(first.channels[1].values[0] == (5.5 + 3.0) * 2.0);

    REQUIRE(setup.length() == 2);
    CHECK(setup.getParameter(0)->slope == 1.0);
    CHECK(setup.getParameter(0)->scale == 0.0);
    CHECK(setup.getParameter(1)->slope == 2.0);
    CHECK(setup.getParameter(1)->scale == 3.0);
}

TEST_CASE("FrameProcessor: a randomized stream decodes across packet boundaries")
{
    // The descrambler's register carries from one packet to the next, and only a packet's own bits go through it (not
    // the padding of its last byte): the stream of the windows case, randomized, in 7-bit packets, gives the same
    // values as the plain stream.
    const std::string bits = stream({1, 4, 5, 6, 7, 15});
    const std::string randomized = scramble(bits);
    REQUIRE(randomized != bits);

    ProcessingParams p = timedParams(StreamMode::ReceiverChannelInfo, true);
    p.isRandomized = true;
    FrameSetup setup;
    addWord0(setup);
    const ProcessedStreamData r = runPackets(p, &setup, split(randomized, 7, 0.0));
    REQUIRE(r.channels.size() == 1);
    checkSeries(r.channels[0].values, {1, 4, 5, 6, 11}, "values");
}

TEST_CASE("FrameProcessor: an inverted stream is inverted before it is scanned")
{
    // The success row of the messages case with every bit flipped (its 4 bytes whole), processed as inverted.
    std::string flipped;
    for (char c : std::string("1001" "0000" "00001001" "0000" "00001001"))
    {
        flipped += c == '1' ? '0' : '1';
    }
    ProcessingParams p = timedParams(StreamMode::FrameSyncLockStats, true);
    p.samplePeriodSec = 1e9;
    p.isInverted      = true;
    FrameSetup none;
    Capture cap;
    bool ok = false;
    runPackets(p, &none, {{flipped}}, &cap, &ok);
    CHECK(ok);
    REQUIRE(cap.logs.size() == 3);
    CHECK(cap.logs[1] == "[CH 7 Rcvr Bus] 4 bytes processed, 3 syncs found, 2 frames extracted.");
}

TEST_CASE("FrameProcessor: a packet with no payload adds nothing")
{
    // A packet that carries no payload is processed as empty, not as a second copy of the packet before it.
    PacketQueue q;
    ProcessingParams p;
    syntheticParams(p, q);
    enqueueBits(q, "1001" "0000" "00001001" "0000" "00001001");
    PacketItem empty;
    REQUIRE(q.enqueue(empty));
    enqueueEndOfStream(q);

    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);
    FrameSetup none;
    CHECK(fp.process(p, &none));
    REQUIRE(cap.logs.size() == 3);
    CHECK(cap.logs[1] == "[CH 7 Rcvr Bus] 4 bytes processed, 3 syncs found, 2 frames extracted.");
}

// ---------------------------------------------------------------------------------------------------------------------
// Byte order, on the Safran recording
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: with the byte swap off a sync is found that the swap destroys")
{
    // The Safran recording is byte-ordered the opposite way to every other recording here: channel 13 carries PRN-15
    // (334AABBF, 32767-bit frames) in the raw stream, and the byte swap destroys the pattern outright.
    //
    // Before ProcessingParams::swapBytes existed the swap was unconditional - the reader filled needsSwap from an
    // irig106 attribute that nothing ever assigns, so it was a constant `true` wearing the costume of a lookup - and
    // this recording could not be processed at all.
    //
    // Both directions are asserted. Checking only that swap=false locks would pass against a build that ignored the
    // flag and never swapped anything, which would silently break every other recording in this suite.
    const fs::path path = recordingFile("safran_testfile, sample.ch10");
    const int time_id = safranTimeChannel(path);

    // Channel 13 specifically. Channels 11 and 12 carry no PRN-15 under any transform, and channel 14's payloads are
    // an odd number of bytes - which SwapBytes_PcmF1 declines to swap - so 14 locks either way and would prove nothing
    // about the flag.
    constexpr int kPrnChannelId = 13;

    struct Outcome
    {
        bool   processed = false;
        double peakLock  = 0.0;
    };
    auto run = [&](bool swap) -> Outcome {
        ProcessingParams p = makeTestParams(path, time_id, kPrnChannelId, 0x334AABBF, 32, kPrnFrameWords, kPrnFrameBits);
        p.samplePeriodSec = 1.0;
        p.isRandomized    = false;
        p.swapBytes       = swap;
        p.mode            = StreamMode::FrameSyncLockStats;

        FrameSetup empty_setup; // no word map for lock-only mode
        FrameProcessor fp;
        bool prepared = false;
        Outcome out;
        out.processed = runWithReader(fp, p, &empty_setup, &prepared);
        REQUIRE_MESSAGE(prepared, "prepare() failed (swap=" << swap << ")");
        for (double v : fp.result().lockPercent)
        {
            out.peakLock = std::max(out.peakLock, v);
        }
        return out;
    };

    const Outcome unswapped = run(false);
    const Outcome swapped   = run(true);

    CHECK_MESSAGE(unswapped.processed, "with the byte swap off the PRN-15 sync is present and processing must succeed");
    CHECK_MESSAGE(unswapped.peakLock > 50.0, "expected lock with the byte swap off, peak was " << unswapped.peakLock << "%");

    // The swap destroys the pattern, so the processor reports "no frame sync found" and returns false. That failure IS
    // the expected result here - it is exactly what the operator saw before the setting existed.
    CHECK_MESSAGE(!swapped.processed, "with the byte swap on the sync must be destroyed and processing must fail");
    CHECK(swapped.peakLock == 0.0);
}

TEST_CASE("FrameProcessor: an odd payload's skipped swap is reported")
{
    // SwapBytes_PcmF1 refuses an odd byte count and returns before touching the buffer. FrameProcessor used to discard
    // that return, so with the swap ON an odd-length payload was silently left unswapped while even-length packets in
    // the same recording were swapped - one stream carrying two byte orders, with nothing in the log to say so. That
    // silence is what made the Safran recording hard to diagnose: channel 14 locked and channel 13 did not, under
    // identical settings, for a reason nothing surfaced.
    //
    // Channel 14 is the all-odd case: every payload is 875 bytes, so the swap is inert for the whole stream and the
    // operator's setting has no effect at all.
    const fs::path path = recordingFile("safran_testfile, sample.ch10");
    const int time_id = safranTimeChannel(path);
    constexpr int kOddPayloadChannelId = 14;

    ProcessingParams p = makeTestParams(path, time_id, kOddPayloadChannelId, 0x334AABBF, 32, kPrnFrameWords,
                                        kPrnFrameBits);
    p.samplePeriodSec = 1.0;
    p.isRandomized    = false;
    p.swapBytes       = true; // asked for - and silently not delivered
    p.mode            = StreamMode::FrameSyncLockStats;

    FrameSetup empty_setup;
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);
    bool prepared = false;
    runWithReader(fp, p, &empty_setup, &prepared);
    REQUIRE(prepared);

    const std::string log = joined(cap.logs);
    CHECK_MESSAGE(contains(log, "odd-length"), "the skip must be reported; the log was: " << log);
    // Every packet on this channel is odd, so the message must say the stream was forced to Legacy Chapter 10
    // regardless of the setting - not warn about a half-swapped stream, which is the other, genuinely corrupting case.
    CHECK_MESSAGE(contains(log, "processed as Legacy Chapter 10 regardless of the setting"),
                  "an all-odd stream must report the setting as unreachable, not a mixed-order warning; the log was: "
                      << log);
}

// ---------------------------------------------------------------------------------------------------------------------
// The failure report, on the TRC recording with the wrong frame length
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("FrameProcessor: off-phase syncs are not counted as boundary-aligned")
{
    // The off-phase branch of the scanner used to increment boundary_syncs, so a match that was NOT on a frame boundary
    // was reported as "boundary-aligned" - the exact opposite of the truth - and it set current_sync_run without ever
    // folding it into max_sync_run. A real failure report read "1 total | 1 boundary-aligned | longest run=0", which is
    // self-contradictory and pointed the reader at the wrong cause.
    //
    // Reproduced deterministically with the RIGHT sync pattern and a WRONG frame length: matches occur at the real
    // 800-bit spacing, so minor_frame_bit_count never equals the configured 1234 and no match is ever boundary-aligned.
    const Trc t = loadTrc();
    constexpr int kWrongFrameBits = 1234; // real frames are 800 bits
    ProcessingParams p = makeTestParams(t.path, t.time_id, t.pcm_id, 0xFE6B2840, 32, 49, kWrongFrameBits);
    p.samplePeriodSec = 1.0;
    p.isRandomized    = true;
    p.mode            = StreamMode::FrameSyncLockStats;

    FrameSetup empty_setup;
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);
    bool prepared = false;
    const bool ok = runWithReader(fp, p, &empty_setup, &prepared);
    REQUIRE(prepared);
    const std::string reported = cap.errors.empty() ? std::string() : cap.errors.back();

    CHECK_MESSAGE(!ok, "a wrong frame length must not yield extracted frames");
    CHECK_MESSAGE(contains(reported, "no frames extracted"), "unexpected error: " << reported);
    // Every match here is off-phase, so the count must be zero. Before the fix this read as thousands of
    // "boundary-aligned" syncs.
    CHECK_MESSAGE(contains(reported, "0 boundary-aligned"),
                  "off-phase matches must not be counted as boundary-aligned; the message was:\n" << reported);
}

TEST_CASE("FrameProcessor: a failure report names the channel and the sync pattern")
{
    // Four streams are processed by parallel workers into one shared log, so their messages interleave in an order that
    // is not deterministic between runs. Before this, a failure report carried no identity at all: four "pattern was
    // not found" lines, no way to tell which channel each belonged to.
    //
    // The report also printed sync_len but never the pattern itself - and PRN-11 and PRN-15 are both 32-bit patterns,
    // so a stream running the wrong one was invisible in the report. That is exactly how a PRN-11 config on a PRN-15
    // stream went undiagnosed: every printed field looked reasonable.
    const Trc t = loadTrc();
    // Right pattern, wrong frame length: matches occur at the real 800-bit spacing so none is ever boundary-aligned,
    // which lands in the detailed report.
    ProcessingParams p = makeTestParams(t.path, t.time_id, t.pcm_id, 0xFE6B2840, 32, 49, 1234);
    p.samplePeriodSec = 1.0;
    p.isRandomized    = true;
    p.mode            = StreamMode::FrameSyncLockStats;
    p.streamLabel     = "PCM03";

    FrameSetup empty_setup;
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);
    bool prepared = false;
    runWithReader(fp, p, &empty_setup, &prepared);
    REQUIRE(prepared);
    const std::string reported = cap.errors.empty() ? std::string() : cap.errors.back();

    const std::string expected_tag = "[CH " + std::to_string(t.pcm_id) + " PCM03]";

    // Identity on the failure report...
    CHECK_MESSAGE(reported.starts_with(expected_tag), "the report must lead with " << expected_tag << "; got:\n" << reported);

    // ...and on EVERY routine message too, not just the error. A tagged error among untagged progress lines is still
    // ambiguous once four streams interleave.
    REQUIRE_MESSAGE(!cap.logs.empty(), "expected at least one progress message");
    for (const std::string& m : cap.logs)
    {
        CHECK_MESSAGE(m.starts_with(expected_tag), "untagged log line: " << m);
    }

    // The pattern itself, uppercase hex - the field that distinguishes PRN-11 from PRN-15 when both report
    // sync_len=32.
    CHECK_MESSAGE(contains(reported, "pattern 0xFE6B2840"), "the sync pattern must be printed; got:\n" << reported);
    // An all-ones mask is every ordinary configuration, so printing it is noise. It appears only when it is actually
    // constraining the match.
    CHECK_MESSAGE(!contains(reported, "mask"), "a default mask must not be printed; got:\n" << reported);
}

// ---------------------------------------------------------------------------------------------------------------------
// The scan's output, pinned
// ---------------------------------------------------------------------------------------------------------------------

namespace {

/// A small deterministic generator (xorshift64*): the standard engines are specified, but their distributions are not,
/// and these scenarios must come out the same on every toolchain.
class Rng
{
public:
    explicit Rng(uint64_t seed) : m_state(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { next(); next(); }

    uint64_t next()
    {
        m_state ^= m_state >> 12;
        m_state ^= m_state << 25;
        m_state ^= m_state >> 27;
        return m_state * 0x2545F4914F6CDD1Dull;
    }

    /// An integer in [lo, hi].
    uint64_t between(uint64_t lo, uint64_t hi) { return lo + next() % (hi - lo + 1); }
    bool     chance(unsigned percent) { return next() % 100 < percent; }

private:
    uint64_t m_state;
};

struct Fnv
{
    uint64_t h = 0xCBF29CE484222325ull;
    void byte(uint8_t b) { h = (h ^ b) * 0x100000001B3ull; }
    void u64(uint64_t v) { for (int i = 0; i < 8; i++) byte(static_cast<uint8_t>(v >> (8 * i))); }
    void real(double v) { uint64_t bits = 0; std::memcpy(&bits, &v, sizeof bits); u64(bits); }
    void text(const std::string& s) { for (const char c : s) byte(static_cast<uint8_t>(c)); byte(0); }
};

/// One scenario of the pinned set: a random frame geometry, a random stream (frames, noise, flipped bits, sync-like
/// data, lost stretches) cut into random packets (odd lengths, time gaps, times that step back), a random clock, window
/// and mode. Everything the scan decides on is varied, so a change to how it reads bits or closes windows shows here.
/// The result is folded to one number: every series value, to the last bit, and the run's reported counts.
uint64_t scenarioHash(uint64_t seed, bool* produced_frames = nullptr, std::size_t* windows = nullptr)
{
    Rng rng(seed);

    const uint32_t word_len  = rng.chance(60) ? 16u : static_cast<uint32_t>(rng.chance(50) ? 8u : 4u);
    const uint32_t sync_mult = static_cast<uint32_t>(rng.between(1, 4));
    const uint32_t sync_len  = sync_mult * word_len > 32 ? 32u : sync_mult * word_len;
    const uint32_t words     = static_cast<uint32_t>(rng.between(2, 9));
    const uint32_t extra     = static_cast<uint32_t>(rng.between(0, 3)); // bits between a frame's last word and the next sync
    const uint32_t frame_len = sync_len + words * word_len + extra;
    const uint64_t full_mask = (1ull << sync_len) - 1;
    const uint64_t pattern   = rng.next() & full_mask;
    // One scenario in six masks off one of the pattern's bits.
    const uint64_t mask      = rng.chance(17) ? (full_mask & ~(1ull << rng.between(0, sync_len - 1))) : full_mask;

    // Frames: the sync, then random words; between runs of them noise, flipped bits and decoy syncs.
    std::string bits;
    const auto put_value = [&bits](uint64_t v, uint32_t n) {
        for (uint32_t b = n; b-- > 0;) bits += ((v >> b) & 1) != 0 ? '1' : '0';
    };
    const std::size_t target = static_cast<std::size_t>(rng.between(300, 4000));
    while (bits.size() < target)
    {
        const uint64_t what = rng.between(0, 99);
        if (what < 70)
        {
            put_value(pattern, sync_len);
            for (uint32_t w = 0; w < words; w++) put_value(rng.next(), word_len);
            for (uint64_t pad = rng.between(0, 3) == 0 ? 0 : extra; pad > 0; pad--) // a frame now and then is the wrong length
                bits += rng.chance(50) ? '1' : '0';
        }
        else if (what < 80)
        {
            for (uint64_t n = rng.between(1, 3 * frame_len); n > 0; n--) bits += rng.chance(50) ? '1' : '0';
        }
        else if (what < 90)
        {
            put_value(pattern, sync_len); // a decoy: a sync where no frame starts
        }
        else if (!bits.empty())
        {
            const std::size_t at = static_cast<std::size_t>(rng.between(0, bits.size() - 1));
            bits[at] = bits[at] == '1' ? '0' : '1';
        }
    }

    // The clock: bit period, window, and where each packet says it starts.
    const bool   rate_clock = rng.chance(50);
    const double delta      = rng.chance(50) ? 1e7 : static_cast<double>(rng.between(1, 50)) * 1e5 + 0.5 * static_cast<double>(rng.between(0, 1)); // 100 ns units
    const double bit_s      = delta * PCMConstants::k100NsToSeconds;
    // A window is often a whole number of bit periods, so that a bit's time lands on the window's end to the last
    // digit - or one off it, from rounding: the bit that closes the window is then a matter of the arithmetic.
    const double whole      = bit_s * static_cast<double>(rng.between(5, 200));
    const double period     = rng.chance(10) ? 1e9 : (rng.chance(50) ? whole : whole + 0.01 * static_cast<double>(rng.between(0, 99)));

    std::vector<Packet> packets;
    double origin = 1000.0 + static_cast<double>(rng.between(0, 86399));
    for (std::size_t at = 0; at < bits.size();)
    {
        const std::size_t size = static_cast<std::size_t>(rng.chance(20) ? rng.between(1, 9) : rng.between(10, 700));
        packets.push_back({bits.substr(at, size), origin + bit_s * static_cast<double>(at)});
        const uint64_t odd = rng.between(0, 99);
        if (odd < 6) origin += period * static_cast<double>(rng.between(1, 6)); // a gap in the recording
        else if (odd < 10) origin -= bit_s * static_cast<double>(rng.between(1, 30)); // a time that steps back
        at += size;
    }

    ProcessingParams p;
    PacketQueue unused;
    syntheticParams(p, unused);
    p.packetQueue                  = nullptr;
    p.mode                         = rng.chance(50) ? StreamMode::ReceiverChannelInfo : StreamMode::FrameSyncLockStats;
    p.useDataRateClock             = rate_clock;
    p.samplePeriodSec              = period;
    p.dataRateBps                  = rng.chance(30) ? static_cast<double>(rng.between(1, 5000)) : 0.0;
    p.resolvedAttrs.syncPat        = pattern & mask;
    p.resolvedAttrs.syncMask       = mask;
    p.resolvedAttrs.syncPatLen     = sync_len;
    p.resolvedAttrs.bitsInFrame    = frame_len;
    p.resolvedAttrs.wordsInFrame   = words;
    p.resolvedAttrs.wordLen        = word_len;
    p.resolvedAttrs.wordMask       = (1ull << word_len) - 1;
    p.resolvedAttrs.minSyncs       = static_cast<uint32_t>(rng.between(1, 3));
    p.resolvedAttrs.delta100ns     = delta;

    FrameSetup setup;
    for (uint32_t w = 0; w < std::min<uint32_t>(words, 3); w++)
    {
        setup.addParameter("W" + std::to_string(w), static_cast<int>(w));
        ParameterInfo* param = setup.getParameter(static_cast<int>(w));
        param->slope = 0.5 + 0.25 * static_cast<double>(w);
        param->scale = static_cast<double>(w);
    }

    Capture events;
    bool    ok = false;
    const ProcessedStreamData r = runPackets(p, &setup, packets, &events, &ok);

    Fnv f;
    f.u64(ok ? 1 : 0);
    for (const std::string& line : events.logs)
    {
        // The counts the run reports; its timing ("elapsed 0.3s") is not a result.
        if (line.find("bytes processed") != std::string::npos) f.text(line);
    }
    for (const std::string& line : events.errors) f.text(line);
    for (const double v : r.timesSec) f.real(v);
    for (const double v : r.lockPercent) f.real(v);
    for (const double v : r.accumulatedMissedFrames) f.real(v);
    for (const ProcessedChannelSeries& c : r.channels)
    {
        f.u64(static_cast<uint64_t>(c.word));
        for (const double v : c.values) f.real(v);
    }
    if (produced_frames != nullptr)
    {
        *produced_frames = ok;
    }
    if (windows != nullptr) *windows = r.timesSec.size();
    return f.h;
}

} // namespace

TEST_CASE("FrameProcessor: the scan's output on random synthetic streams is what it was")
{
    // Pinned from the scan before it was rewritten for speed (the per-bit loop, with a time check on every bit): the
    // rewrite must give these exact numbers, so a stream's every window, lock percentage and averaged word is the same
    // to the last bit. Regenerate (print the hashes) only for a change that is MEANT to move the scan's output.
    constexpr uint64_t kPinned[] = {
        0xAF3E2E5F53E3ACBBull,
        0xCE19E4A19C34C217ull,
        0x53A0C4D2ABF39263ull,
        0x43DDDC7A1FF7E418ull,
        0x0531A3DCB11782EFull,
        0xF39A6FC9EF621349ull,
        0x746882E97E6DB6EDull,
        0x4226D46A92DF52E2ull,
        0x12DB2155EAF6717Dull,
        0x87916AEC63C1CB25ull,
        0x66669E7129A1B895ull,
        0xECC44E3A188D7728ull,
        0x06FB27021C480EE5ull,
        0x75B36ACCBB21D927ull,
        0xA1AA17428FD0ED14ull,
        0xAB24C3C5B5D47C7Bull,
        0x8DB651C0B780EE70ull,
        0x613AA30ECD4B0D24ull,
        0xA41A4438D29717B3ull,
        0xB98B346E34927A73ull,
        0xD745BE175A75FFF6ull,
        0xE85B744DF099EAF7ull,
        0xCC4275D6248110AAull,
        0xD234111B0FEF506Eull,
        0xEB2BD4F93F2246E9ull,
        0xCA366D6C59F8B021ull,
        0x67BF19C21157E81Dull,
        0xF73180AFF5E81AACull,
        0x2AA2C3D15520ACB4ull,
        0xC4919758963D3185ull,
        0x8D3D3499871DA3D8ull,
        0xEDF64C6771021A7Eull,
        0x41E5F626BA1B9A85ull,
        0x1F1E82B46B86AB73ull,
        0x57B83DA707CA9E16ull,
        0x93A1E01829DA0641ull,
        0x70BADA74E73A29E9ull,
        0x72202F9D0028E0C4ull,
        0xD2164F0376C262C1ull,
        0x5A161A65B0B38B91ull,
        0x41DD76AC5BFF7B95ull,
        0x415404155D811295ull,
        0x69A233597911C2A4ull,
        0x7CA9D885DE169B9Dull,
        0x6178E6134A99A605ull,
        0xA8482F70BF57A575ull,
        0xF6EC27EE883C988Cull,
        0xA6A41E5114EBC303ull,
        0xD6B4C5DD388E7B9Full,
        0xDADF24B88D04B5D1ull,
        0x34610ADC814AB510ull,
        0x6FAAFF7A60E5F91Cull,
        0xF54291803246DB15ull,
        0x6193CC391231BAEAull,
        0x9EAD14F6CC1CFB26ull,
        0x039A75659A3EE841ull,
        0x45574DB87D7B8B81ull,
        0xBFC4C75EE0CB5DA0ull,
        0x651BC2F8377D92CDull,
        0x9C664DE7251A39C4ull,
    };
    constexpr std::size_t kScenarios = sizeof kPinned / sizeof kPinned[0];

    std::size_t with_frames = 0, with_many_windows = 0;
    for (std::size_t i = 0; i < kScenarios; i++)
    {
        bool        frames  = false;
        std::size_t windows = 0;
        const uint64_t got  = scenarioHash(i + 1, &frames, &windows);
        with_frames += frames ? 1 : 0;
        with_many_windows += windows > 5 ? 1 : 0;
        if (std::getenv("TMDQ_PRINT_SCAN_PINS") != nullptr)
        {
            std::printf("        0x%016llXull, // %zu: %s, %zu windows\n", static_cast<unsigned long long>(got), i + 1,
                        frames ? "frames" : "no frames", windows);
        }
        else
        {
            INFO("scenario " << i + 1);
            CHECK(got == kPinned[i]);
        }
    }
    // The set must exercise the scan, not sit on failures or on a single window.
    CHECK(with_frames > kScenarios / 2);
    CHECK(with_many_windows > kScenarios / 3);
}
