#include "tst_ch10packetreader.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include "ch10packetreader.h"
#include "chapter10reader.h"
#include "irig106lock.h"
#include "packetqueue.h"
#include "processingparams.h"
#include "qtbridge.h"

namespace {

/// Everything a reader reports, in order.
struct Events
{
    QStringList logs;
    QStringList errors;
    QList<int>  progress;
};

// The three functions below are the only place these tests touch the reader's reporting
// and its path/error types, so the Qt-signals-to-event-sink port changes them and nothing
// else: the expectations were written against the signals.

/// Receives what the reader reports (on the thread that calls run(), which is the test thread).
class EventsSink final : public ProcessingEvents
{
public:
    explicit EventsSink(Events& e) : m_e(e) {}
    void logMessage(const std::string& m) override { m_e.logs << QString::fromStdString(m); }
    void errorOccurred(const std::string& m) override { m_e.errors << QString::fromStdString(m); }
    void processingFinished(bool) override {}
    void progressUpdated(int p) override { m_e.progress << p; }

private:
    Events& m_e;
};

void listen(Ch10PacketReader& r, Events& e)
{
    r.setEvents(std::make_shared<EventsSink>(e));
}

bool prepareReader(Ch10PacketReader& r, const QString& path, int time_id,
                   std::vector<ProcessingParams*>& list, QString& error)
{
    std::string err;
    const bool ok = r.prepare(toFsPath(path), time_id, list, err);
    error = QString::fromStdString(err);
    return ok;
}

void runReader(Ch10PacketReader& r)
{
    r.run();
}

/// A small, committed, ordinary Chapter 10 recording with one PCM channel of interest.
struct Fixture
{
    QString path;
    int     time_id = -1;
    int     pcm_id  = -1;
};

bool loadFixture(Fixture& f)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    f.path = dir.filePath("data/test files/step_cal, example.ch10");
    if (!QFileInfo::exists(f.path))
    {
        return false;
    }
    Chapter10Reader meta;
    if (!meta.loadChannels(toFsPath(f.path)))
    {
        return false;
    }
    f.time_id = meta.getCurrentTimeChannelID();
    f.pcm_id  = meta.getFirstPCMChannelID();
    return f.time_id >= 0 && f.pcm_id >= 0;
}

ProcessingParams paramsFor(const Fixture& f, PacketQueue* queue)
{
    ProcessingParams p;
    p.filename          = toFsPath(f.path);
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

/// Consumes a queue on its own thread until the end marker, so the reader never blocks on
/// a full queue, and records what it saw. join() gives up after a timeout and closes the
/// queue, setting m_timed_out: a reader that never posts its end marker then fails its test
/// instead of hanging the whole run.
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

    void join(int timeout_ms = 20000)
    {
        QElapsedTimer waited;
        waited.start();
        while (!m_done.load() && waited.elapsed() < timeout_ms)
        {
            QThread::msleep(5);
        }
        if (!m_done.load())
        {
            m_timed_out = true;
            m_queue.close(); // releases the blocked dequeue
        }
        m_thread.join();
    }

    std::atomic<int>           m_packets{0};
    std::atomic<int>           m_end_markers{0};
    std::atomic<std::size_t>   m_bytes{0};
    std::vector<const void*>   m_pointers;
    bool                       m_timed_out = false;

private:
    PacketQueue&      m_queue;
    std::atomic<bool> m_done{false};
    std::thread       m_thread; // last: it starts running as soon as it is constructed
};

} // namespace

/// The packet count and payload size of the fixture's PCM channel. They are properties of
/// the committed recording; if it is ever replaced these move with it.
constexpr int         kFixturePackets = 1598;
constexpr std::size_t kFixtureBytes   = 6539016;

void TestCh10PacketReader::prepareReportsWhyItFailed_data()
{
    QTest::addColumn<QString>("what");
    QTest::addColumn<QString>("error");

    QTest::newRow("missing file")        << "missing" << "Error opening data file.";
    QTest::newRow("not a recording")     << "text"    << "Error opening data file.";
    QTest::newRow("empty file")          << "empty"   << "Error opening data file.";
    QTest::newRow("unknown pcm channel") << "badchan" << "Channel info not set up for PCM channel 999.";
    QTest::newRow("too short to sync")   << "short"   << "Error establishing time sync.";
}

void TestCh10PacketReader::prepareReportsWhyItFailed()
{
    // Each way setup can fail has its own message, and prepare() returns false rather than
    // leaving a half-open reader to be run. These are what the operator reads in the log.
    QFETCH(QString, what);
    QFETCH(QString, error);

    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    QTemporaryDir dir;
    QString path = fx.path;
    Fixture used = fx;
    if (what == "missing")
    {
        path = dir.filePath("absent.ch10");
    }
    else if (what == "text" || what == "empty" || what == "short")
    {
        path = dir.filePath("generated.ch10");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        if (what == "text")
        {
            f.write("this is not a recording\n");
        }
        else if (what == "short")
        {
            QFile src(fx.path);
            QVERIFY(src.open(QIODevice::ReadOnly));
            f.write(src.read(100)); // the start of a real file, cut before its first packet ends
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
    QString reported;
    QVERIFY(!prepareReader(r, path, fx.time_id, list, reported));
    QCOMPARE(reported, error);
}

void TestCh10PacketReader::aFullRunReportsItsStartAndEndsAtOneHundredPercent()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    Events e;
    listen(r, e);
    QString error;
    QVERIFY2(prepareReader(r, fx.path, fx.time_id, list, error), qPrintable(error));
    QVERIFY2(p.resolvedAttrs.resolved, "prepare() must resolve the stream's PCM attributes");

    Drain drain(q);
    runReader(r);
    drain.join();
    QVERIFY2(!drain.m_timed_out, "the reader never posted its end marker");

    // The first thing said is that reading began; anything after is a "N% read..." line
    // (the file reads faster than the 100 ms progress throttle, so usually none).
    QVERIFY(!e.logs.isEmpty());
    QCOMPARE(e.logs.first(), QStringLiteral("Reading Chapter 10 file..."));
    const QRegularExpression percent(QStringLiteral("^[0-9]+% read\\.\\.\\.$"));
    for (int i = 1; i < e.logs.size(); ++i)
    {
        QVERIFY2(percent.match(e.logs.at(i)).hasMatch(), qPrintable(e.logs.at(i)));
    }
    QVERIFY2(e.errors.isEmpty(), qPrintable(e.errors.join("\n")));

    // Progress never runs backwards, stays in range, and ends at exactly 100.
    QVERIFY(!e.progress.isEmpty());
    QCOMPARE(e.progress.last(), 100);
    for (int i = 0; i < e.progress.size(); ++i)
    {
        QVERIFY(e.progress.at(i) >= 0 && e.progress.at(i) <= 100);
        if (i > 0)
            QVERIFY(e.progress.at(i) >= e.progress.at(i - 1));
    }

    // What the queue received: every packet of the channel with its 4-byte channel-specific
    // header stripped, then exactly one end marker.
    QCOMPARE(drain.m_packets.load(), kFixturePackets);
    QCOMPARE(drain.m_bytes.load(), kFixtureBytes);
    QCOMPARE(drain.m_end_markers.load(), 1);
}

void TestCh10PacketReader::cancelReportsItAndStillEndsTheStream()
{
    // An abort is not a failure: it says so, reports 100% (the read is over), and still
    // posts the end marker - without it the worker would wait forever on its queue.
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    Events e;
    listen(r, e);
    QString error;
    QVERIFY2(prepareReader(r, fx.path, fx.time_id, list, error), qPrintable(error));

    r.requestAbort();
    Drain drain(q);
    runReader(r);
    drain.join();
    QVERIFY2(!drain.m_timed_out, "the reader never posted its end marker");

    QCOMPARE(e.logs, QStringList({QStringLiteral("Reading Chapter 10 file..."),
                                  QStringLiteral("Reading cancelled by user.")}));
    QVERIFY(e.errors.isEmpty());
    QCOMPARE(e.progress, QList<int>({100}));
    QCOMPARE(drain.m_packets.load(), 0);
    QCOMPARE(drain.m_end_markers.load(), 1);
}

namespace {

/// One packet of a Chapter 10 file, found by walking the headers directly. The layout is the
/// standard's, not the library's: a 24-byte header whose sync is 0xEB25, channel ID at byte 2,
/// total packet length at byte 4, data type at byte 15.
struct PacketAt
{
    qint64  offset;
    quint16 channel;
    quint32 length;
    quint8  dataType;
};

constexpr quint8 kDataTypeIrigTime = 0x11;
constexpr qint64 kHeaderBytes      = 24;

/// Walks the whole file. Fails (empty) unless the packets tile it exactly, which is what
/// makes the offsets below trustworthy.
std::vector<PacketAt> walkPackets(const QString& path)
{
    std::vector<PacketAt> packets;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return packets;
    }
    const QByteArray bytes = f.readAll();
    auto u16 = [&](qint64 at) { return static_cast<quint16>(static_cast<quint8>(bytes[at]) | (static_cast<quint8>(bytes[at + 1]) << 8)); };
    auto u32 = [&](qint64 at) { return static_cast<quint32>(u16(at)) | (static_cast<quint32>(u16(at + 2)) << 16); };

    qint64 offset = 0;
    while (offset + kHeaderBytes <= bytes.size())
    {
        if (u16(offset) != 0xEB25)
        {
            return {};
        }
        const quint32 length = u32(offset + 4);
        if (length < kHeaderBytes)
        {
            return {};
        }
        packets.push_back({offset, u16(offset + 2), length, static_cast<quint8>(bytes[offset + 15])});
        offset += length;
    }
    return offset == bytes.size() ? packets : std::vector<PacketAt>{};
}

} // namespace

void TestCh10PacketReader::aTruncatedFileReportsAReadErrorAndStillEndsTheStream_data()
{
    QTest::addColumn<QString>("where");
    QTest::addColumn<QString>("error");

    // Each place the file can end has its own read, and its own report.
    QTest::newRow("inside a PCM packet's data")
        << "pcm" << "File read error; aborting parsing.";
    QTest::newRow("inside a time packet's data")
        << "time" << "File read error; aborting parsing.";
    // A file that ends inside a packet HEADER is read as a clean end of file: no error is
    // reported, so a copy cut off there looks complete. Pinned as the behaviour it is, not
    // necessarily the one it should have - changing it deserves its own decision.
    QTest::newRow("inside a packet header")
        << "header" << "";
}

void TestCh10PacketReader::aTruncatedFileReportsAReadErrorAndStillEndsTheStream()
{
    // A recording cut off mid-packet (a copy that did not finish). The packets before the cut
    // are delivered, the failure is reported, and the stream is still terminated.
    QFETCH(QString, where);
    QFETCH(QString, error);

    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    const std::vector<PacketAt> packets = walkPackets(fx.path);
    QVERIFY2(!packets.empty(), "the header walk must tile the fixture exactly");

    // Pick the packet to cut into, well past the TMATS packet that prepare() reads.
    constexpr std::size_t kSkip = 40;
    qint64 cut_at = -1;
    int packets_before_cut = 0;
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
    QVERIFY2(cut_at > 0, "no packet of the wanted kind to cut into");

    QTemporaryDir dir;
    const QString cut = dir.filePath("cut.ch10");
    {
        QFile src(fx.path);
        QVERIFY(src.open(QIODevice::ReadOnly));
        QFile out(cut);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(src.read(cut_at));
    }
    for (const PacketAt& pk : packets)
    {
        if (pk.channel == fx.pcm_id && pk.offset + pk.length <= cut_at)
            ++packets_before_cut;
    }

    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    Events e;
    listen(r, e);
    QString prepare_error;
    QVERIFY2(prepareReader(r, cut, fx.time_id, list, prepare_error), qPrintable(prepare_error));

    Drain drain(q);
    runReader(r);
    drain.join();

    QVERIFY2(!drain.m_timed_out, "the reader never posted its end marker");
    QCOMPARE(e.errors, error.isEmpty() ? QStringList() : QStringList({error}));
    // Everything before the cut was delivered, exactly: no packet lost, none from the broken one.
    QCOMPARE(drain.m_packets.load(), packets_before_cut);
    QCOMPARE(drain.m_end_markers.load(), 1);
    QVERIFY(!e.progress.isEmpty());
    QCOMPARE(e.progress.last(), 100);
}

void TestCh10PacketReader::aDamagedHeaderStopsTheReadOrCostsOnePacket_data()
{
    QTest::addColumn<QString>("what");
    QTest::addColumn<QString>("error");
    QTest::addColumn<bool>("stopsAtTheDamage");

    // A damaged header checksum ends the read there: the packets before it are delivered,
    // the failure is reported, and everything after is lost.
    QTest::newRow("header checksum overwritten")
        << "checksum" << "File read error during data collection." << true;
    // A damaged SYNC pattern is different: the library scans forward to the next sync, so the
    // read carries on, no error is reported, and exactly the one packet is missing. Pinned as
    // the behaviour it is - a silently dropped packet may deserve a warning, which would be
    // its own decision.
    QTest::newRow("sync pattern overwritten")
        << "sync" << "" << false;
}

void TestCh10PacketReader::aDamagedHeaderStopsTheReadOrCostsOnePacket()
{
    QFETCH(QString, what);
    QFETCH(QString, error);
    QFETCH(bool, stopsAtTheDamage);
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    const std::vector<PacketAt> packets = walkPackets(fx.path);
    QVERIFY2(!packets.empty(), "the header walk must tile the fixture exactly");

    // Damage the header of a PCM packet well into the file.
    constexpr std::size_t kSkip = 40;
    qint64 damage_at = -1;
    int packets_before = 0;
    for (std::size_t i = kSkip; i < packets.size(); ++i)
    {
        if (packets[i].channel == fx.pcm_id)
        {
            damage_at = packets[i].offset;
            break;
        }
    }
    QVERIFY(damage_at > 0);
    for (const PacketAt& pk : packets)
    {
        if (pk.channel == fx.pcm_id && pk.offset < damage_at)
            ++packets_before;
    }

    QTemporaryDir dir;
    const QString damaged = dir.filePath("damaged.ch10");
    {
        QFile src(fx.path);
        QVERIFY(src.open(QIODevice::ReadOnly));
        QByteArray bytes = src.readAll();
        if (what == "sync")
        {
            bytes[damage_at] = 0;
            bytes[damage_at + 1] = 0;
        }
        else
        {
            bytes[damage_at + 22] = static_cast<char>(bytes[damage_at + 22] ^ 0x5A); // header checksum
        }
        QFile out(damaged);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
    }

    PacketQueue q;
    ProcessingParams p = paramsFor(fx, &q);
    std::vector<ProcessingParams*> list{&p};
    Ch10PacketReader r;
    Events e;
    listen(r, e);
    QString prepare_error;
    QVERIFY2(prepareReader(r, damaged, fx.time_id, list, prepare_error), qPrintable(prepare_error));

    Drain drain(q);
    runReader(r);
    drain.join();
    QVERIFY2(!drain.m_timed_out, "the reader never posted its end marker");

    QCOMPARE(e.errors, error.isEmpty() ? QStringList() : QStringList({error}));
    QCOMPARE(drain.m_packets.load(), stopsAtTheDamage ? packets_before : kFixturePackets - 1);
    QCOMPARE(drain.m_end_markers.load(), 1);
    QVERIFY(!e.progress.isEmpty());
    QCOMPARE(e.progress.last(), 100);
}

void TestCh10PacketReader::everyQueueGetsEveryPacketAndTheyShareOneCopy()
{
    // Two streams on one channel (two queues). The reader dispatches each packet to both,
    // and the payload must be ONE buffer shared between them: a by-value copy per queue would
    // be made on this, the pipeline's single bottleneck thread, for nothing.
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    PacketQueue qa;
    PacketQueue qb;
    ProcessingParams pa = paramsFor(fx, &qa);
    ProcessingParams pb = paramsFor(fx, &qb);
    std::vector<ProcessingParams*> list{&pa, &pb};
    Ch10PacketReader r;
    QString error;
    QVERIFY2(prepareReader(r, fx.path, fx.time_id, list, error), qPrintable(error));

    Drain da(qa);
    Drain db(qb);
    runReader(r);
    da.join();
    db.join();
    QVERIFY2(!da.m_timed_out && !db.m_timed_out, "the reader never posted its end marker");

    QCOMPARE(da.m_packets.load(), kFixturePackets);
    QCOMPARE(db.m_packets.load(), kFixturePackets);
    QCOMPARE(da.m_end_markers.load(), 1);
    QCOMPARE(db.m_end_markers.load(), 1);
    QCOMPARE(da.m_pointers.size(), db.m_pointers.size());
    for (std::size_t i = 0; i < da.m_pointers.size(); ++i)
    {
        QVERIFY2(da.m_pointers[i] == db.m_pointers[i], qPrintable(QStringLiteral("packet %1").arg(i)));
    }
}

/// The vendored irig106 library keeps process-wide state (a handle table claimed with an
/// unsynchronised check-then-set, and the TMATS decoder's static pointers), so two threads
/// setting up at the same moment can be given the same handle or decode into each other's
/// tables. Driven from a bare C# host the first concurrent run crashed with an access
/// violation. The application opens each file once from one thread today; this pins that
/// doing it from many does not matter.
void TestCh10PacketReader::manyThreadsSettingUpAtOnceAllSucceedAndAgree()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    Chapter10Reader baseline;
    QVERIFY(baseline.loadChannels(toFsPath(fx.path)));
    const auto expected_pcm = baseline.getPCMChannelList();
    QVERIFY(!expected_pcm.empty());

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
                    if (!meta.loadChannels(toFsPath(fx.path)) || meta.getPCMChannelList() != expected_pcm)
                        ++failures;
                }
                else
                {
                    PacketQueue q;
                    ProcessingParams p = paramsFor(fx, &q);
                    std::vector<ProcessingParams*> list{&p};
                    Ch10PacketReader r;
                    std::string err;
                    if (!r.prepare(toFsPath(fx.path), fx.time_id, list, err) || !p.resolvedAttrs.resolved)
                        ++failures;
                }
            }
        });
    }
    while (ready.load() < kThreads) {}
    go = true;
    for (auto& th : threads)
        th.join();

    QCOMPARE(failures.load(), 0);
}

namespace {

/// Runs one reader's setup against @p path: the metadata reader's loadChannels(), or the packet
/// reader's prepare(). Returns whether it succeeded.
bool runSetup(bool via_prepare, const Fixture& fx, const QString& path)
{
    if (via_prepare)
    {
        PacketQueue q;
        ProcessingParams p = paramsFor(fx, &q);
        std::vector<ProcessingParams*> list{&p};
        Ch10PacketReader r;
        std::string err;
        return r.prepare(toFsPath(path), fx.time_id, list, err);
    }
    Chapter10Reader meta;
    return meta.loadChannels(toFsPath(path));
}

void addBothReaders()
{
    QTest::addColumn<bool>("viaPrepare");
    QTest::newRow("Chapter10Reader::loadChannels") << false;
    QTest::newRow("Ch10PacketReader::prepare") << true;
}

} // namespace

// The stress test shows the symptom when the lock is missing, but a race is only sometimes
// visible. This one is deterministic: while this thread holds the library lock, a setup on another
// thread must not get past it, and must succeed once the lock is released.
//
// It also sees WHERE the lock is taken. The recording is a copy this test holds open with no
// sharing, so any attempt to open it fails at once: a setup that reached open() while it should
// still be waiting - a lock taken after the open, or none - fails, and finishes, while the lock
// is still held. The handle is closed before the lock is released, so the open then succeeds.
void TestCh10PacketReader::setupWaitsForTheLockAnotherThreadHolds_data() { addBothReaders(); }

void TestCh10PacketReader::setupWaitsForTheLockAnotherThreadHolds()
{
    QFETCH(bool, viaPrepare);
#ifndef Q_OS_WIN
    QSKIP("uses a Windows exclusive-share file handle to see where the lock is taken");
#else
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString copy = dir.filePath("recording.ch10");
    QVERIFY(QFile::copy(fx.path, copy));
    HANDLE exclusive = CreateFileW(reinterpret_cast<LPCWSTR>(copy.utf16()), GENERIC_READ, 0, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY2(exclusive != INVALID_HANDLE_VALUE, "could not hold the copy open exclusively");

    std::atomic<bool> finished{false};
    std::atomic<bool> succeeded{false};
    std::thread worker;
    bool finished_while_held = false;
    {
        const auto held = lockIrig106Setup();
        worker = std::thread([&]() {
            succeeded = runSetup(viaPrepare, fx, copy);
            finished = true;
        });
        QThread::msleep(500);   // a setup takes milliseconds; this is far longer than it needs
        finished_while_held = finished.load();
        CloseHandle(exclusive); // let the open succeed once the worker is let through
    }                           // lock released here
    worker.join();

    QVERIFY2(!finished_while_held, "setup ran (or tried to open the file) while another thread held the irig106 lock");
    QVERIFY2(succeeded.load(), "setup did not succeed once the lock was released");
#endif
}

// The lock is held by an object that goes out of scope, so every early return in setup releases
// it. A setup that fails (a path that does not exist) must not leave the next one waiting forever.
void TestCh10PacketReader::aFailedSetupStillReleasesTheLock_data() { addBothReaders(); }

void TestCh10PacketReader::aFailedSetupStillReleasesTheLock()
{
    QFETCH(bool, viaPrepare);
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.filePath("no-such-recording.ch10");
    // prepare() sizes the file before it takes the lock, then fails to open it; loadChannels()
    // fails to open it. Either way the failure path is the one under test.
    QVERIFY(!runSetup(viaPrepare, fx, missing));

    std::unique_lock<std::mutex> probe(irig106SetupMutex(), std::try_to_lock);
    QVERIFY2(probe.owns_lock(), "a failed setup left the irig106 lock held");
    probe.unlock();

    QVERIFY2(runSetup(viaPrepare, fx, fx.path), "a setup after a failed one did not succeed");
}
