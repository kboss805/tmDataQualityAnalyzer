// Ch10PacketReader: the single-pass reader, tested directly on the committed step_cal recording. No Qt; ported from
// the Qt TestCh10PacketReader, whose cases are all here under the same descriptions (its data-driven cases are loops).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "doctest.h"
#include "ch10packetreader.h"
#include "chapter10reader.h"
#include "irig106lock.h"
#include "packetqueue.h"
#include "processingparams.h"
#include "test_support.h"

using namespace tmdq_test;
using namespace std::chrono_literals;

namespace {

/// Everything a reader reports, in order.
struct Events
{
    Strings          logs;
    Strings          errors;
    std::vector<int> progress;
};

/// Receives what the reader reports (on the thread that calls run(), which is the test thread).
class EventsSink final : public ProcessingEvents
{
public:
    explicit EventsSink(Events& e) : m_e(e) {}
    void logMessage(const std::string& m) override { m_e.logs.push_back(m); }
    void errorOccurred(const std::string& m) override { m_e.errors.push_back(m); }
    void processingFinished(bool) override {}
    void progressUpdated(int p) override { m_e.progress.push_back(p); }

private:
    Events& m_e;
};

void listen(Ch10PacketReader& r, Events& e)
{
    r.setEvents(std::make_shared<EventsSink>(e));
}

/// A small, committed, ordinary Chapter 10 recording with one PCM channel of interest.
struct Fixture
{
    fs::path path;
    int      time_id = -1;
    int      pcm_id  = -1;
};

Fixture loadFixture()
{
    Fixture f;
    f.path = repoFile(fs::path("tests") / "data" / "test files" / "step_cal, example.ch10");
    Chapter10Reader meta;
    REQUIRE_MESSAGE(meta.loadChannels(f.path), meta.lastError());
    f.time_id = meta.getCurrentTimeChannelID();
    f.pcm_id  = meta.getFirstPCMChannelID();
    REQUIRE(f.time_id >= 0);
    REQUIRE(f.pcm_id >= 0);
    return f;
}

ProcessingParams paramsFor(const Fixture& f, PacketQueue* queue)
{
    ProcessingParams p;
    p.filename          = f.path;
    p.timeChannelId     = f.time_id;
    p.pcmChannelId      = f.pcm_id;
    p.frameSync         = 0xFE6B2840;
    p.syncPatternLength = 32;
    p.wordsInMinorFrame = 50;
    p.bitsInMinorFrame  = 800;
    p.mode              = StreamMode::FrameSyncLockStats;
    p.swapBytes         = true;
    p.packetQueue       = queue;
    return p;
}

/// Consumes a queue on its own thread until the end marker, so the reader never blocks on a full queue, and records
/// what it saw. join() gives up after a timeout and closes the queue, setting m_timed_out: a reader that never posts its
/// end marker then fails its test instead of hanging the whole run.
class Drain
{
public:
    explicit Drain(PacketQueue& q)
        : m_queue(q)
        , m_thread([this, &q]() {
              while (true)
              {
                  PacketItem item = q.dequeue();
                  if (item.endOfStream)
                  {
                      ++m_end_markers;
                      break;
                  }
                  ++m_packets;
                  m_bytes += item.payload ? item.payload->size() : 0;
                  m_pointers.push_back(item.payload.get());
              }
              m_done = true;
          })
    {
    }

    void join(std::chrono::milliseconds timeout = 20000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!m_done.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(5ms);
        }
        if (!m_done.load())
        {
            m_timed_out = true;
            m_queue.close(); // releases the blocked dequeue
        }
        m_thread.join();
    }

    std::atomic<int>         m_packets{0};
    std::atomic<int>         m_end_markers{0};
    std::atomic<std::size_t> m_bytes{0};
    std::vector<const void*> m_pointers;
    bool                     m_timed_out = false;

private:
    PacketQueue&      m_queue;
    std::atomic<bool> m_done{false};
    std::thread       m_thread; // last: it starts running as soon as it is constructed
};

/// The packet count and payload size of the fixture's PCM channel. They are properties of the committed recording; if
/// it is ever replaced these move with it.
constexpr int         kFixturePackets = 1598;
constexpr std::size_t kFixtureBytes   = 6539016;

/// One packet of a Chapter 10 file, found by walking the headers directly. The layout is the standard's, not the
/// library's: a 24-byte header whose sync is 0xEB25, channel ID at byte 2, total packet length at byte 4, data type at
/// byte 15.
struct PacketAt
{
    std::int64_t  offset;
    std::uint16_t channel;
    std::uint32_t length;
    std::uint8_t  dataType;
};

constexpr std::uint8_t kDataTypeIrigTime = 0x11;
constexpr std::int64_t kHeaderBytes      = 24;

/// Walks the whole file. Fails (empty) unless the packets tile it exactly, which is what makes the offsets below
/// trustworthy.
std::vector<PacketAt> walkPackets(const std::string& bytes)
{
    std::vector<PacketAt> packets;
    auto byte = [&](std::int64_t at) { return static_cast<std::uint8_t>(bytes[static_cast<std::size_t>(at)]); };
    auto u16  = [&](std::int64_t at) { return static_cast<std::uint16_t>(byte(at) | (byte(at + 1) << 8)); };
    auto u32  = [&](std::int64_t at) { return static_cast<std::uint32_t>(u16(at)) | (static_cast<std::uint32_t>(u16(at + 2)) << 16); };

    const auto size = static_cast<std::int64_t>(bytes.size());
    std::int64_t offset = 0;
    while (offset + kHeaderBytes <= size)
    {
        if (u16(offset) != 0xEB25)
        {
            return {};
        }
        const std::uint32_t length = u32(offset + 4);
        if (length < kHeaderBytes)
        {
            return {};
        }
        packets.push_back({offset, u16(offset + 2), length, byte(offset + 15)});
        offset += length;
    }
    return offset == size ? packets : std::vector<PacketAt>{};
}

/// Runs one reader's setup against @p path: the metadata reader's loadChannels(), or the packet reader's prepare().
bool runSetup(bool via_prepare, const Fixture& fx, const fs::path& path)
{
    if (via_prepare)
    {
        PacketQueue q;
        ProcessingParams p = paramsFor(fx, &q);
        std::vector<ProcessingParams*> list{&p};
        Ch10PacketReader r;
        std::string err;
        return r.prepare(path, fx.time_id, list, err);
    }
    Chapter10Reader meta;
    return meta.loadChannels(path);
}

} // namespace

TEST_CASE("Ch10PacketReader: prepare reports why it failed")
{
    // Each way setup can fail has its own message, and prepare() returns false rather than leaving a half-open reader to
    // be run. These are what the operator reads in the log.
    const Fixture fx = loadFixture();
    struct Row { const char* name; const char* what; const char* error; };
    const Row rows[] = {
        {"missing file",        "missing", "Error opening data file."},
        {"not a recording",     "text",    "Error opening data file."},
        {"empty file",          "empty",   "Error opening data file."},
        {"unknown pcm channel", "badchan", "Channel info not set up for PCM channel 999."},
        {"too short to sync",   "short",   "Error establishing time sync."},
        // irig106 only WARNS when the first packet's header cannot be read, or is not TMATS; the open goes ahead, and the
        // reader's own checks say what is wrong. Refusing the warning would report every such file as unopenable.
        {"cut inside the first header", "header10", "Error establishing time sync."},
        {"first packet not TMATS",      "notmats",  "Failed to find TMATS message."},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        const std::string what = row.what;
        TempDir dir;
        fs::path path = fx.path;
        Fixture used = fx;
        if (what == "missing")
        {
            path = dir.file("absent.ch10");
        }
        else if (what == "header10")
        {
            path = dir.file("generated.ch10");
            writeBytes(path, readBytes(fx.path).substr(0, 10));
        }
        else if (what == "notmats")
        {
            // The recording without its first packet (the TMATS): the packets still tile, the first is now another kind.
            const std::string bytes = readBytes(fx.path);
            const std::vector<PacketAt> packets = walkPackets(bytes);
            REQUIRE(packets.size() > 1);
            path = dir.file("generated.ch10");
            writeBytes(path, bytes.substr(packets[1].offset));
        }
        else if (what == "text" || what == "empty" || what == "short")
        {
            path = dir.file("generated.ch10");
            if (what == "text")
            {
                writeBytes(path, "this is not a recording\n");
            }
            else if (what == "short")
            {
                writeBytes(path, readBytes(fx.path).substr(0, 100)); // the start of a real file, cut before its first packet ends
            }
            else
            {
                writeBytes(path, "");
            }
        }
        else if (what == "badchan")
        {
            used.pcm_id = 999;
        }

        PacketQueue q;
        ProcessingParams p = paramsFor(used, &q);
        std::vector<ProcessingParams*> list{&p};
        Ch10PacketReader r;
        std::string reported;
        CHECK(!r.prepare(path, fx.time_id, list, reported));
        CHECK(reported == row.error);
    }
}

TEST_CASE("Ch10PacketReader: a full run reports its start and ends at one hundred percent")
{
    const Fixture fx = loadFixture();
    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    Events e;
    listen(r, e);
    std::string error;
    REQUIRE_MESSAGE(r.prepare(fx.path, fx.time_id, list, error), error);
    REQUIRE_MESSAGE(p.resolvedAttrs.resolved, "prepare() must resolve the stream's PCM attributes");

    Drain drain(q);
    r.run();
    drain.join();
    REQUIRE_MESSAGE(!drain.m_timed_out, "the reader never posted its end marker");

    // The first thing said is that reading began; anything after is a "N% read..." line (the file reads faster than the
    // 100 ms progress throttle, so usually none).
    REQUIRE(!e.logs.empty());
    CHECK(e.logs.front() == "Reading Chapter 10 file...");
    const std::regex percent("^[0-9]+% read\\.\\.\\.$");
    for (std::size_t i = 1; i < e.logs.size(); ++i)
    {
        CHECK_MESSAGE(std::regex_match(e.logs[i], percent), e.logs[i]);
    }
    CHECK(e.errors.empty());

    // Progress never runs backwards, stays in range, and ends at exactly 100.
    REQUIRE(!e.progress.empty());
    CHECK(e.progress.back() == 100);
    for (std::size_t i = 0; i < e.progress.size(); ++i)
    {
        CHECK((e.progress[i] >= 0 && e.progress[i] <= 100));
        if (i > 0)
        {
            CHECK(e.progress[i] >= e.progress[i - 1]);
        }
    }

    // What the queue received: every packet of the channel with its 4-byte channel-specific header stripped, then
    // exactly one end marker.
    CHECK(drain.m_packets.load() == kFixturePackets);
    CHECK(drain.m_bytes.load() == kFixtureBytes);
    CHECK(drain.m_end_markers.load() == 1);
}

TEST_CASE("Ch10PacketReader: cancel reports it and still ends the stream")
{
    // An abort is not a failure: it says so, reports 100% (the read is over), and still posts the end marker - without
    // it the worker would wait forever on its queue.
    const Fixture fx = loadFixture();
    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    Events e;
    listen(r, e);
    std::string error;
    REQUIRE_MESSAGE(r.prepare(fx.path, fx.time_id, list, error), error);

    r.requestAbort();
    Drain drain(q);
    r.run();
    drain.join();
    REQUIRE_MESSAGE(!drain.m_timed_out, "the reader never posted its end marker");

    CHECK(e.logs == Strings{"Reading Chapter 10 file...", "Reading cancelled by user."});
    CHECK(e.errors.empty());
    CHECK(e.progress == std::vector<int>{100});
    CHECK(drain.m_packets.load() == 0);
    CHECK(drain.m_end_markers.load() == 1);
}

TEST_CASE("Ch10PacketReader: a truncated file reports a read error and still ends the stream")
{
    // A recording cut off mid-packet (a copy that did not finish). The packets before the cut are delivered, the failure
    // is reported, and the stream is still terminated. Each place the file can end has its own read, and its own report.
    //
    // A file that ends inside a packet HEADER is read as a clean end of file: no error is reported, so a copy cut off
    // there looks complete. Pinned as the behaviour it is, not necessarily the one it should have - changing it deserves
    // its own decision.
    const Fixture fx = loadFixture();
    const std::string original = readBytes(fx.path);
    const std::vector<PacketAt> packets = walkPackets(original);
    REQUIRE_MESSAGE(!packets.empty(), "the header walk must tile the fixture exactly");

    struct Row { const char* name; const char* where; const char* error; };
    const Row rows[] = {
        {"inside a PCM packet's data",  "pcm",    "File read error; aborting parsing."},
        {"inside a time packet's data", "time",   "File read error; aborting parsing."},
        {"inside a packet header",      "header", ""},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        const std::string where = row.where;

        // Pick the packet to cut into, well past the TMATS packet that prepare() reads.
        constexpr std::size_t kSkip = 40;
        std::int64_t cut_at = -1;
        for (std::size_t i = kSkip; i < packets.size(); ++i)
        {
            const PacketAt& pk = packets[i];
            const bool is_time = pk.dataType == kDataTypeIrigTime && pk.channel == fx.time_id;
            const bool is_pcm  = pk.channel == fx.pcm_id;
            if ((where == "time" && is_time) || (where == "pcm" && is_pcm) || (where == "header" && is_pcm))
            {
                cut_at = pk.offset + (where == "header" ? 10 : kHeaderBytes + 2);
                break;
            }
        }
        REQUIRE_MESSAGE(cut_at > 0, "no packet of the wanted kind to cut into");

        TempDir dir;
        const fs::path cut = dir.file("cut.ch10");
        writeBytes(cut, original.substr(0, static_cast<std::size_t>(cut_at)));
        int packets_before_cut = 0;
        for (const PacketAt& pk : packets)
        {
            if (pk.channel == fx.pcm_id && pk.offset + pk.length <= cut_at)
            {
                ++packets_before_cut;
            }
        }

        PacketQueue q;
        ProcessingParams p = paramsFor(fx, &q);
        std::vector<ProcessingParams*> list{&p};
        Ch10PacketReader r;
        Events e;
        listen(r, e);
        std::string prepare_error;
        REQUIRE_MESSAGE(r.prepare(cut, fx.time_id, list, prepare_error), prepare_error);

        Drain drain(q);
        r.run();
        drain.join();

        REQUIRE_MESSAGE(!drain.m_timed_out, "the reader never posted its end marker");
        CHECK(e.errors == (std::string(row.error).empty() ? Strings{} : Strings{row.error}));
        // Everything before the cut was delivered, exactly: no packet lost, none from the broken one.
        CHECK(drain.m_packets.load() == packets_before_cut);
        CHECK(drain.m_end_markers.load() == 1);
        REQUIRE(!e.progress.empty());
        CHECK(e.progress.back() == 100);
    }
}

TEST_CASE("Ch10PacketReader: a damaged header stops the read, or costs one packet")
{
    // A damaged header checksum ends the read there: the packets before it are delivered, the failure is reported, and
    // everything after is lost. A damaged SYNC pattern is different: the library scans forward to the next sync, so the
    // read carries on, no error is reported, and exactly the one packet is missing. Pinned as the behaviour it is - a
    // silently dropped packet may deserve a warning, which would be its own decision.
    const Fixture fx = loadFixture();
    const std::string original = readBytes(fx.path);
    const std::vector<PacketAt> packets = walkPackets(original);
    REQUIRE_MESSAGE(!packets.empty(), "the header walk must tile the fixture exactly");

    // Damage the header of a PCM packet well into the file.
    constexpr std::size_t kSkip = 40;
    std::int64_t damage_at = -1;
    for (std::size_t i = kSkip; i < packets.size(); ++i)
    {
        if (packets[i].channel == fx.pcm_id)
        {
            damage_at = packets[i].offset;
            break;
        }
    }
    REQUIRE(damage_at > 0);
    int packets_before = 0;
    for (const PacketAt& pk : packets)
    {
        if (pk.channel == fx.pcm_id && pk.offset < damage_at)
        {
            ++packets_before;
        }
    }

    struct Row { const char* name; const char* what; const char* error; bool stopsAtTheDamage; };
    const Row rows[] = {
        {"header checksum overwritten", "checksum", "File read error during data collection.", true},
        {"sync pattern overwritten",    "sync",     "",                                         false},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        std::string bytes = original;
        const auto at = static_cast<std::size_t>(damage_at);
        if (std::string(row.what) == "sync")
        {
            bytes[at] = 0;
            bytes[at + 1] = 0;
        }
        else
        {
            bytes[at + 22] = static_cast<char>(bytes[at + 22] ^ 0x5A); // header checksum
        }
        TempDir dir;
        const fs::path damaged = dir.file("damaged.ch10");
        writeBytes(damaged, bytes);

        PacketQueue q;
        ProcessingParams p = paramsFor(fx, &q);
        std::vector<ProcessingParams*> list{&p};
        Ch10PacketReader r;
        Events e;
        listen(r, e);
        std::string prepare_error;
        REQUIRE_MESSAGE(r.prepare(damaged, fx.time_id, list, prepare_error), prepare_error);

        Drain drain(q);
        r.run();
        drain.join();
        REQUIRE_MESSAGE(!drain.m_timed_out, "the reader never posted its end marker");

        CHECK(e.errors == (std::string(row.error).empty() ? Strings{} : Strings{row.error}));
        CHECK(drain.m_packets.load() == (row.stopsAtTheDamage ? packets_before : kFixturePackets - 1));
        CHECK(drain.m_end_markers.load() == 1);
        REQUIRE(!e.progress.empty());
        CHECK(e.progress.back() == 100);
    }
}

TEST_CASE("Ch10PacketReader: every queue gets every packet, and they share one copy")
{
    // Two streams on one channel (two queues). The reader dispatches each packet to both, and the payload must be ONE
    // buffer shared between them: a by-value copy per queue would be made on this, the pipeline's single bottleneck
    // thread, for nothing.
    const Fixture fx = loadFixture();
    PacketQueue qa;
    PacketQueue qb;
    ProcessingParams pa = paramsFor(fx, &qa);
    ProcessingParams pb = paramsFor(fx, &qb);
    std::vector<ProcessingParams*> list{&pa, &pb};
    Ch10PacketReader r;
    std::string error;
    REQUIRE_MESSAGE(r.prepare(fx.path, fx.time_id, list, error), error);

    Drain da(qa);
    Drain db(qb);
    r.run();
    da.join();
    db.join();
    REQUIRE_MESSAGE((!da.m_timed_out && !db.m_timed_out), "the reader never posted its end marker");

    CHECK(da.m_packets.load() == kFixturePackets);
    CHECK(db.m_packets.load() == kFixturePackets);
    CHECK(da.m_end_markers.load() == 1);
    CHECK(db.m_end_markers.load() == 1);
    REQUIRE(da.m_pointers.size() == db.m_pointers.size());
    for (std::size_t i = 0; i < da.m_pointers.size(); ++i)
    {
        INFO("packet " << i);
        CHECK(da.m_pointers[i] == db.m_pointers[i]);
    }
}

/// The vendored irig106 library keeps process-wide state (a handle table claimed with an unsynchronised check-then-set,
/// and the TMATS decoder's static pointers), so two threads setting up at the same moment can be given the same handle
/// or decode into each other's tables. Driven from a bare C# host the first concurrent run crashed with an access
/// violation. The application opens each file once from one thread today; this pins that doing it from many does not
/// matter.
TEST_CASE("Ch10PacketReader: many threads setting up at once all succeed and agree")
{
    const Fixture fx = loadFixture();

    Chapter10Reader baseline;
    REQUIRE(baseline.loadChannels(fx.path));
    const auto expected_pcm = baseline.getPCMChannelList();
    REQUIRE(!expected_pcm.empty());

    constexpr int kThreads = 8;
    constexpr int kRounds = 40;
    std::atomic<bool> go{false};
    std::atomic<int> failures{0};
    std::atomic<int> ready{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t]() {
            ++ready;
            while (!go.load()) {}   // release everyone together: the race needs overlap
            for (int round = 0; round < kRounds; ++round)
            {
                if ((round + t) % 2 == 0)
                {
                    Chapter10Reader meta;
                    if (!meta.loadChannels(fx.path) || meta.getPCMChannelList() != expected_pcm)
                    {
                        ++failures;
                    }
                }
                else
                {
                    PacketQueue q;
                    ProcessingParams p = paramsFor(fx, &q);
                    std::vector<ProcessingParams*> list{&p};
                    Ch10PacketReader r;
                    std::string err;
                    if (!r.prepare(fx.path, fx.time_id, list, err) || !p.resolvedAttrs.resolved)
                    {
                        ++failures;
                    }
                }
            }
        });
    }
    while (ready.load() < kThreads) {}
    go = true;
    for (auto& th : threads)
    {
        th.join();
    }

    CHECK(failures.load() == 0);
}

// The stress test shows the symptom when the lock is missing, but a race is only sometimes visible. This one is
// deterministic: while this thread holds the library lock, a setup on another thread must not get past it, and must
// succeed once the lock is released.
//
// It also sees WHERE the lock is taken. The recording is a copy this test holds open with no sharing, so any attempt to
// open it fails at once: a setup that reached open() while it should still be waiting - a lock taken after the open, or
// none - fails, and finishes, while the lock is still held. The handle is closed before the lock is released, so the
// open then succeeds.
TEST_CASE("Ch10PacketReader: setup waits for the lock another thread holds")
{
    const Fixture fx = loadFixture();
    for (const bool via_prepare : {false, true})
    {
        INFO((via_prepare ? "Ch10PacketReader::prepare" : "Chapter10Reader::loadChannels"));
        TempDir dir;
        const fs::path copy = dir.file("recording.ch10");
        REQUIRE(fs::copy_file(fx.path, copy));
        HANDLE exclusive = CreateFileW(copy.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE_MESSAGE(exclusive != INVALID_HANDLE_VALUE, "could not hold the copy open exclusively");

        std::atomic<bool> finished{false};
        std::atomic<bool> succeeded{false};
        std::thread worker;
        bool finished_while_held = false;
        {
            const auto held = lockIrig106Setup();
            worker = std::thread([&]() {
                succeeded = runSetup(via_prepare, fx, copy);
                finished = true;
            });
            std::this_thread::sleep_for(500ms);   // a setup takes milliseconds; this is far longer than it needs
            finished_while_held = finished.load();
            CloseHandle(exclusive);               // let the open succeed once the worker is let through
        }                                         // lock released here
        worker.join();

        CHECK_MESSAGE(!finished_while_held, "setup ran (or tried to open the file) while another thread held the irig106 lock");
        CHECK_MESSAGE(succeeded.load(), "setup did not succeed once the lock was released");
    }
}

// The lock is held by an object that goes out of scope, so every early return in setup releases it. A setup that fails
// (a path that does not exist) must not leave the next one waiting forever.
TEST_CASE("Ch10PacketReader: a failed setup still releases the lock")
{
    const Fixture fx = loadFixture();
    for (const bool via_prepare : {false, true})
    {
        INFO((via_prepare ? "Ch10PacketReader::prepare" : "Chapter10Reader::loadChannels"));
        TempDir dir;
        const fs::path missing = dir.file("no-such-recording.ch10");
        // prepare() sizes the file before it takes the lock, then fails to open it; loadChannels() fails to open it.
        // Either way the failure path is the one under test.
        CHECK(!runSetup(via_prepare, fx, missing));

        std::unique_lock<std::mutex> probe(irig106SetupMutex(), std::try_to_lock);
        CHECK_MESSAGE(probe.owns_lock(), "a failed setup left the irig106 lock held");
        if (probe.owns_lock())
        {
            probe.unlock();
        }

        CHECK_MESSAGE(runSetup(via_prepare, fx, fx.path), "a setup after a failed one did not succeed");
    }
}

// ---------------------------------------------------------------------------
// What prepare() hands each stream, and what each packet carries. The Qt suite counted packets and bytes; these pin the
// fields a worker actually reads.
// ---------------------------------------------------------------------------

TEST_CASE("Ch10PacketReader: prepare resolves every attribute a worker reads")
{
    const Fixture fx = loadFixture();

    // The mask: an explicit one is used as given; none means every bit of the pattern.
    PacketQueue q1;
    PacketQueue q2;
    ProcessingParams with_mask = paramsFor(fx, &q1);
    with_mask.frameSyncMask = 0x00FF00FF;
    with_mask.swapBytes     = false;
    with_mask.bitsInMinorFrame = 790;   // not 16 x words, so bits and words are each read from their own field
    ProcessingParams no_mask = paramsFor(fx, &q2);
    no_mask.frameSyncMask     = 0;
    no_mask.syncPatternLength = 24;
    no_mask.dataRateBps       = 400000.0;   // an operator override of the TMATS rate
    std::vector<ProcessingParams*> list{&with_mask, &no_mask};
    Ch10PacketReader r;
    std::string error;
    REQUIRE_MESSAGE(r.prepare(fx.path, fx.time_id, list, error), error);

    const ResolvedPcmAttrs& a = with_mask.resolvedAttrs;
    CHECK(a.resolved);
    CHECK(a.syncPat == 0xFE6B2840u);
    CHECK(a.syncMask == 0x00FF00FFu);
    CHECK(a.syncPatLen == 32u);
    CHECK(a.bitsInFrame == 790u);
    CHECK(a.wordsInFrame == 50u);
    CHECK(a.wordLen == 16u);
    // NOT asserted for this stream: wordMask. irig106's Set_Attributes_Ext_PcmF1 ANDs the common word mask with the
    // frame-sync mask, so with this sync mask the word mask comes out 0xFF and FrameProcessor masks every receiver word
    // to its low byte. That is a defect (recorded in docs/claude.md, plan step 2.4, TestCh10PacketReader), not the
    // behaviour this suite should pin. The unmasked stream below has the word mask it should.
    CHECK(a.minSyncs >= 1u);
    CHECK(!a.needsSwap);                       // the operator's byte order, not the library's
    // step_cal declares 0.8 Mbps in its TMATS (TestChapter10Reader pins that), so a bit is 12.5 x 100 ns.
    CHECK(a.delta100ns == doctest::Approx(12.5));

    const ResolvedPcmAttrs& b = no_mask.resolvedAttrs;
    CHECK(b.resolved);
    CHECK(b.syncMask == 0xFFFFFFu);            // 24 bits of pattern, all significant
    CHECK(b.syncPatLen == 24u);
    CHECK(b.wordMask == 0xFFFFu);              // all 16 bits of a word, with every bit of the sync significant
    CHECK(b.needsSwap);
    CHECK(b.delta100ns == doctest::Approx(25.0));   // the override wins: 1e7 / 400,000
}

TEST_CASE("Ch10PacketReader: each packet carries its bit count and a base time that never runs backwards")
{
    const Fixture fx = loadFixture();
    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    std::string error;
    REQUIRE_MESSAGE(r.prepare(fx.path, fx.time_id, list, error), error);

    std::vector<PacketItem> items;
    std::thread consumer([&]() {
        while (true)
        {
            PacketItem item = q.dequeue();
            if (item.endOfStream)
            {
                break;
            }
            items.push_back(std::move(item));
        }
    });
    r.run();
    consumer.join();

    REQUIRE(static_cast<int>(items.size()) == kFixturePackets);
    double previous = 0.0;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        INFO("packet " << i);
        REQUIRE(items[i].payload != nullptr);
        CHECK(items[i].packetBits == items[i].payload->size() * 8);
        CHECK(items[i].baseAbsSeconds > 0.0);
        // Strictly later: packets are tens of milliseconds apart, so a time that lost its fractional second would repeat.
        CHECK(items[i].baseAbsSeconds > previous);
        previous = items[i].baseAbsSeconds;
    }
    // The recording spans a little under a minute of packets, and its first packet's base time sits on IRIG time, so
    // successive packets are close together - not each at the epoch, and not a constant.
    CHECK(items.back().baseAbsSeconds > items.front().baseAbsSeconds);
    CHECK(items.back().baseAbsSeconds - items.front().baseAbsSeconds < 3600.0);
}

TEST_CASE("Ch10PacketReader: a queue shared by two streams gets each packet twice and one end marker")
{
    // Two streams handed the same queue: the reader routes the packet to it once per stream, but the end of the stream
    // is posted once per QUEUE - a second marker would be read by the worker as a second, empty stream ending.
    const Fixture fx = loadFixture();
    PacketQueue shared;
    ProcessingParams pa = paramsFor(fx, &shared);
    ProcessingParams pb = paramsFor(fx, &shared);
    std::vector<ProcessingParams*> list{&pa, &pb};
    Ch10PacketReader r;
    std::string error;
    REQUIRE_MESSAGE(r.prepare(fx.path, fx.time_id, list, error), error);

    Drain drain(shared);
    r.run();
    drain.join();
    REQUIRE(!drain.m_timed_out);
    CHECK(drain.m_packets.load() == 2 * kFixturePackets);
    CHECK(drain.m_end_markers.load() == 1);
    // And nothing after it. The drain stopped at the first marker and the reader has returned, so anything else it
    // posted is still queued: a probe enqueued now must be the next thing out, not a second end marker.
    PacketItem probe;
    probe.packetBits = 12345;
    REQUIRE(shared.enqueue(probe));
    const PacketItem next = shared.dequeue();
    CHECK(!next.endOfStream);
    CHECK(next.packetBits == 12345u);
}

TEST_CASE("Ch10PacketReader: only the channels a stream asked for reach its queue")
{
    // The committed PRN sample carries PCM data on channels 13 and 15. A stream on 13 must receive exactly 13's packets
    // - counted here by walking the headers - and none of 15's. The file is large enough (77 MB) that the read outlasts
    // the 100 ms progress gate, so its progress reports are real intermediate values too.
    const fs::path path = repoFile(fs::path("tests") / "data" / "test files" / "prn_testfile, sample.ch10");
    Chapter10Reader meta;
    REQUIRE_MESSAGE(meta.loadChannels(path), meta.lastError());
    const int time_id = meta.getCurrentTimeChannelID();
    REQUIRE(time_id >= 0);

    const std::vector<PacketAt> packets = walkPackets(readBytes(path));
    REQUIRE_MESSAGE(!packets.empty(), "the header walk must tile the recording exactly");
    constexpr std::uint8_t kDataTypePcmFmt1 = 0x09;
    int on_13 = 0;
    int on_15 = 0;
    for (const PacketAt& pk : packets)
    {
        if (pk.dataType == kDataTypePcmFmt1)
        {
            on_13 += pk.channel == 13 ? 1 : 0;
            on_15 += pk.channel == 15 ? 1 : 0;
        }
    }
    REQUIRE(on_13 > 0);
    REQUIRE(on_15 > 0);   // the other channel really has data to leak

    Fixture fx;
    fx.path = path;
    fx.time_id = time_id;
    fx.pcm_id = 13;
    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    p.frameSync = 0x334AABBF;
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    Events e;
    listen(r, e);
    std::string error;
    REQUIRE_MESSAGE(r.prepare(path, time_id, list, error), error);

    Drain drain(q);
    r.run();
    drain.join(60000ms);
    REQUIRE(!drain.m_timed_out);
    CHECK(drain.m_packets.load() == on_13);
    CHECK(drain.m_end_markers.load() == 1);
    CHECK(e.errors.empty());

    REQUIRE(!e.progress.empty());
    CHECK(e.progress.back() == 100);
    for (std::size_t i = 0; i < e.progress.size(); ++i)
    {
        CHECK((e.progress[i] >= 0 && e.progress[i] <= 100));
        if (i > 0)
        {
            CHECK(e.progress[i] >= e.progress[i - 1]);
        }
    }
}
