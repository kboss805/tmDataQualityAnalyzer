#include "tst_packetqueue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <QtTest>

#include "packetqueue.h"

namespace {

using namespace std::chrono_literals;

/// How long a thread that SHOULD be blocked is given to prove it is not. A correct
/// queue never lets it through no matter how long this is, so the figure only trades
/// test time against the chance of missing a queue that blocks a moment too late. A
/// queue that fails to block at all is caught on the first scheduling slice.
constexpr auto kSettle = 120ms;

/// Upper bound on any wait for something that SHOULD happen. Generous because a loaded
/// CI machine is the realistic worst case; a correct queue finishes in microseconds.
constexpr auto kPatience = 5000ms;

PacketItem makeItem(uint64_t seq)
{
    PacketItem item;
    item.packetBits     = seq;
    item.baseAbsSeconds = static_cast<double>(seq) * 0.5;
    item.payload        = std::make_shared<const std::vector<uint8_t>>(
        std::size_t{3}, static_cast<uint8_t>(seq & 0x7F));
    return item;
}

/// Polls @p pred until it is true or @p limit elapses.
template <typename Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds limit = kPatience)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!pred())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

/// Closes the queue and joins on scope exit.
///
/// A test that fails partway returns through QVERIFY, and a still-blocked helper
/// thread would then hang the join in std::thread's destructor - or terminate the
/// process, since destroying a joinable std::thread is std::terminate. Closing first
/// releases anything blocked in enqueue()/dequeue(), so a failing test fails instead of
/// taking the whole suite down with it. (The same hazard the throughput benchmark had to
/// learn about with live QThreads: destroying a running thread aborts the run, and the
/// results of every other suite are lost with it.)
///
/// The one case close() cannot rescue is close() ITSELF being broken: the helper then
/// stays blocked forever and the join below would never return. A join with no deadline
/// would turn that defect into a hung suite, and CI sets no timeout-minutes, so it would
/// hold the self-hosted runner for GitHub's six-hour default. So the join is bounded, and
/// if it cannot complete the process aborts with a message naming the cause. Loud and
/// fast beats silent and six hours.
///
/// Found by mutation: a close() that records the flag but never notifies hung the
/// validation run past 150 s instead of failing.
struct ThreadGuard
{
    PacketQueue& queue;
    std::thread  thread;

    ThreadGuard(PacketQueue& q, std::thread&& t) : queue(q), thread(std::move(t)) {}
    ThreadGuard(const ThreadGuard&)            = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

    ~ThreadGuard()
    {
        queue.close();
        if (!thread.joinable())
        {
            return;
        }

        // Join from a second thread so this one can give up waiting.
        std::atomic<bool> joined{false};
        std::thread joiner([this, &joined] {
            thread.join();
            joined = true;
        });

        if (!waitUntil([&] { return joined.load(); }, 5s))
        {
            qFatal("PacketQueue::close() did not release a blocked helper thread within "
                   "5 s - close() is not waking its waiters. Aborting rather than hanging "
                   "the suite (CI has no job timeout).");
        }
        joiner.join();
    }
};

} // namespace

void TestPacketQueue::itemsComeOutInTheOrderTheyWentIn()
{
    PacketQueue q(16);
    for (uint64_t i = 0; i < 10; ++i)
    {
        QVERIFY(q.enqueue(makeItem(i)));
    }
    for (uint64_t i = 0; i < 10; ++i)
    {
        const PacketItem got = q.dequeue();
        QVERIFY(!got.endOfStream);
        QCOMPARE(got.packetBits, i);
    }
}

void TestPacketQueue::oneItemOnSeveralQueuesSharesItsBytes()
{
    // The reader hands the same packet to every stream configured on a channel, and it
    // is the pipeline's single bottleneck. The payload must travel as a shared pointer:
    // a by-value vector would copy the whole packet once per queue on that thread, and
    // nothing else in the suite would notice, because every copy holds the same bytes.
    PacketQueue a;
    PacketQueue b;
    PacketQueue c;

    PacketItem item = makeItem(7);
    const auto* bytes = item.payload.get();
    QVERIFY(a.enqueue(item));
    QVERIFY(b.enqueue(item));
    QVERIFY(c.enqueue(item));

    QCOMPARE(a.dequeue().payload.get(), bytes);
    QCOMPARE(b.dequeue().payload.get(), bytes);
    QCOMPARE(c.dequeue().payload.get(), bytes);
}

void TestPacketQueue::everyFieldOfAnItemSurvivesTheQueue()
{
    PacketQueue q;

    PacketItem in;
    // An embedded NUL on purpose: a payload is raw PCM bits, not text, and a copy that
    // went through a C string would stop at the first zero byte.
    in.payload        = std::make_shared<const std::vector<uint8_t>>(
        std::vector<uint8_t>{'a', 'b', 0, 'c', 'd'});
    in.baseAbsSeconds = 1777584474.153;
    in.packetBits     = 40;
    in.endOfStream    = false;
    QVERIFY(q.enqueue(in));

    const PacketItem out = q.dequeue();
    QVERIFY(out.payload != nullptr);
    QCOMPARE(out.payload->size(), std::size_t{5});
    QVERIFY(*out.payload == *in.payload);
    QCOMPARE(out.baseAbsSeconds, in.baseAbsSeconds);
    QCOMPARE(out.packetBits, in.packetBits);
    QVERIFY(!out.endOfStream);
}

void TestPacketQueue::producerBlocksWhenFullAndResumesAfterADequeue()
{
    PacketQueue q(2);
    QVERIFY(q.enqueue(makeItem(0)));
    QVERIFY(q.enqueue(makeItem(1)));

    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<bool> accepted{false};
    ThreadGuard guard(q, std::thread([&] {
        started  = true;
        accepted = q.enqueue(makeItem(2));
        done     = true;
    }));

    QVERIFY(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    // The bound is the whole point: it is what stops a slow worker from letting the
    // reader buffer an entire recording in RAM.
    QVERIFY2(!done.load(), "a third enqueue into a depth-2 queue must block");

    const PacketItem first = q.dequeue();
    QCOMPARE(first.packetBits, uint64_t(0));

    QVERIFY2(waitUntil([&] { return done.load(); }),
             "freeing a slot must release the blocked producer");
    QVERIFY(accepted.load());

    QCOMPARE(q.dequeue().packetBits, uint64_t(1));
    QCOMPARE(q.dequeue().packetBits, uint64_t(2));
}

void TestPacketQueue::consumerBlocksWhenEmptyAndResumesAfterAnEnqueue()
{
    PacketQueue q;

    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    PacketItem received;
    ThreadGuard guard(q, std::thread([&] {
        started  = true;
        received = q.dequeue();
        done     = true;
    }));

    QVERIFY(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    QVERIFY2(!done.load(), "dequeue on an empty queue must block");

    QVERIFY(q.enqueue(makeItem(7)));
    QVERIFY2(waitUntil([&] { return done.load(); }),
             "an enqueue must release the blocked consumer");
    QVERIFY(!received.endOfStream);
    QCOMPARE(received.packetBits, uint64_t(7));
}

void TestPacketQueue::closeReleasesABlockedProducer()
{
    PacketQueue q(1);
    QVERIFY(q.enqueue(makeItem(0)));

    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<bool> accepted{true};
    ThreadGuard guard(q, std::thread([&] {
        started  = true;
        accepted = q.enqueue(makeItem(1));
        done     = true;
    }));

    QVERIFY(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    QVERIFY(!done.load());

    q.close();
    QVERIFY2(waitUntil([&] { return done.load(); }),
             "close() must wake a producer blocked on a full queue");
    // Cooperative cancellation: the reader is told its item was discarded.
    QVERIFY(!accepted.load());
}

void TestPacketQueue::closeReleasesABlockedConsumerWithAnEndOfStreamSentinel()
{
    PacketQueue q;

    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    PacketItem received;
    ThreadGuard guard(q, std::thread([&] {
        started  = true;
        received = q.dequeue();
        done     = true;
    }));

    QVERIFY(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    QVERIFY(!done.load());

    q.close();
    QVERIFY2(waitUntil([&] { return done.load(); }),
             "close() must wake a consumer blocked on an empty queue");
    // The worker terminates on this sentinel without special-casing a closed queue.
    QVERIFY(received.endOfStream);
}

void TestPacketQueue::closedQueueDrainsItsItemsBeforeReportingEndOfStream()
{
    PacketQueue q(8);
    QVERIFY(q.enqueue(makeItem(10)));
    QVERIFY(q.enqueue(makeItem(11)));
    QVERIFY(q.enqueue(makeItem(12)));
    q.close();

    // Closing must not throw away work that was already accepted.
    for (uint64_t expect = 10; expect <= 12; ++expect)
    {
        const PacketItem got = q.dequeue();
        QVERIFY2(!got.endOfStream, "queued items must still be delivered after close()");
        QCOMPARE(got.packetBits, expect);
    }
    QVERIFY(q.dequeue().endOfStream);
    // And the sentinel is repeatable, not consumed by being read once.
    QVERIFY(q.dequeue().endOfStream);
}

void TestPacketQueue::enqueueAfterCloseDiscardsAndReportsFalse()
{
    PacketQueue q;
    q.close();
    QVERIFY(!q.enqueue(makeItem(1)));
    // Nothing was stored, so the first dequeue is already the sentinel.
    QVERIFY(q.dequeue().endOfStream);
}

void TestPacketQueue::nonPositiveDepthFallsBackToTheDefault()
{
    // The constructor documents max_depth <= 0 as "use the default" (64). A queue that
    // took 0 literally would block its very first enqueue forever.
    for (const int depth : {0, -5})
    {
        PacketQueue q(depth);

        std::atomic<int> accepted{0};
        ThreadGuard guard(q, std::thread([&] {
            for (uint64_t i = 0; i < 80; ++i)
            {
                if (!q.enqueue(makeItem(i)))
                {
                    return;
                }
                ++accepted;
            }
        }));

        QVERIFY2(waitUntil([&] { return accepted.load() >= 64; }),
                 qPrintable(QString("depth %1 never accepted 64 items").arg(depth)));
        std::this_thread::sleep_for(kSettle);
        // Exactly the default: the 65th enqueue is the one that blocks.
        QCOMPARE(accepted.load(), 64);
    }
}

void TestPacketQueue::queueNeverHoldsMoreThanItsDepth()
{
    // Depth 3 with nobody consuming: a producer offering ten items must stall at three.
    // This is the check that distinguishes "full at depth" from "full at depth + 1".
    PacketQueue q(3);

    std::atomic<int> accepted{0};
    ThreadGuard guard(q, std::thread([&] {
        for (uint64_t i = 0; i < 10; ++i)
        {
            if (!q.enqueue(makeItem(i)))
            {
                return;
            }
            ++accepted;
        }
    }));

    QVERIFY(waitUntil([&] { return accepted.load() >= 3; }));
    std::this_thread::sleep_for(kSettle);
    QCOMPARE(accepted.load(), 3);
}

void TestPacketQueue::oneProducerOneConsumerKeepEveryItemInOrder()
{
    // The real usage shape, under contention: a tiny depth forces the producer and the
    // consumer to hand off constantly, which is where a lost wakeup would strand one of
    // them. A watchdog turns that hang into a failure rather than a stuck suite.
    constexpr uint64_t kItems = 20000;
    PacketQueue q(4);

    std::atomic<bool> finished{false};
    std::thread watchdog([&] {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (!finished.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(5ms);
        }
        if (!finished.load())
        {
            q.close();   // releases the consumer so the count check below can fail
            // And if even that does not release it, close() is broken and the consumer
            // below is stuck for good - abort instead of hanging an unbounded CI job.
            if (!waitUntil([&] { return finished.load(); }, 5s))
            {
                qFatal("PacketQueue stress test deadlocked and close() did not release "
                       "the consumer - aborting rather than hanging the suite.");
            }
        }
    });

    ThreadGuard producer(q, std::thread([&] {
        for (uint64_t i = 0; i < kItems; ++i)
        {
            if (!q.enqueue(makeItem(i)))
            {
                return;
            }
        }
        PacketItem eos;
        eos.endOfStream = true;
        q.enqueue(std::move(eos));
    }));

    uint64_t seen         = 0;
    bool     out_of_order = false;
    for (;;)
    {
        const PacketItem got = q.dequeue();
        if (got.endOfStream)
        {
            break;
        }
        if (got.packetBits != seen)
        {
            out_of_order = true;
        }
        ++seen;
    }
    finished = true;
    watchdog.join();

    QVERIFY2(!out_of_order, "items arrived out of order");
    QCOMPARE(seen, kItems);
}
