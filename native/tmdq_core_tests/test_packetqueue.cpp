// PacketQueue: the bounded reader-to-worker hand-off, tested directly rather than only through the pipeline. No Qt;
// ported from the Qt TestPacketQueue, whose cases are all here under the same descriptions.
//
// Until the Qt suite existed the queue was only exercised incidentally, through the full reader + FrameProcessor
// pipeline. A lost wakeup or a wrong fullness test shows up there as a hang or a silently shortened stream, far from
// its cause.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "packetqueue.h"

namespace {

using namespace std::chrono_literals;

/// How long a thread that SHOULD be blocked is given to prove it is not. A correct queue never lets it through no
/// matter how long this is, so the figure only trades test time against the chance of missing a queue that blocks a
/// moment too late. A queue that fails to block at all is caught on the first scheduling slice.
constexpr auto kSettle = 120ms;

/// Upper bound on any wait for something that SHOULD happen. Generous because a loaded CI machine is the realistic
/// worst case; a correct queue finishes in microseconds.
constexpr auto kPatience = 5000ms;

PacketItem makeItem(uint64_t seq)
{
    PacketItem item;
    item.packetBits     = seq;
    item.baseAbsSeconds = static_cast<double>(seq) * 0.5;
    item.payload        = std::make_shared<const std::vector<uint8_t>>(std::size_t{3}, static_cast<uint8_t>(seq & 0x7F));
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

/// Ends the process with a message. A deadlock the queue's own close() cannot release would otherwise hang the run,
/// and CI sets no job timeout: loud and fast beats silent and six hours.
[[noreturn]] void abortRun(const char* why)
{
    std::fprintf(stderr, "FATAL: %s\n", why);
    std::fflush(stderr);
    std::abort();
}

/// Closes the queue and joins on scope exit.
///
/// A REQUIRE that fails partway throws out of the test, and a still-blocked helper thread would then hang the join -
/// or terminate the process, since destroying a joinable std::thread is std::terminate. Closing first releases anything
/// blocked in enqueue()/dequeue(), so a failing test fails instead of taking the whole run down with it.
///
/// The one case close() cannot rescue is close() ITSELF being broken: the helper then stays blocked forever and the
/// join would never return. So the join is bounded, and if it cannot complete the process aborts with a message naming
/// the cause. (Found by mutation in the Qt suite: a close() that records the flag but never notifies hung the
/// validation run past 150 s instead of failing.)
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
            abortRun("PacketQueue::close() did not release a blocked helper thread within 5 s - close() is not waking "
                     "its waiters. Aborting rather than hanging the run (CI has no job timeout).");
        }
        joiner.join();
    }
};

} // namespace

TEST_CASE("PacketQueue: items come out in the order they went in")
{
    PacketQueue q(16);
    for (uint64_t i = 0; i < 10; ++i)
    {
        REQUIRE(q.enqueue(makeItem(i)));
    }
    for (uint64_t i = 0; i < 10; ++i)
    {
        const PacketItem got = q.dequeue();
        CHECK(!got.endOfStream);
        CHECK(got.packetBits == i);
    }
}

TEST_CASE("PacketQueue: one item on several queues shares its bytes")
{
    // The reader hands the same packet to every stream configured on a channel, and it is the pipeline's single
    // bottleneck. The payload must travel as a shared pointer: a by-value vector would copy the whole packet once per
    // queue on that thread, and nothing else in the suite would notice, because every copy holds the same bytes.
    PacketQueue a;
    PacketQueue b;
    PacketQueue c;

    PacketItem item = makeItem(7);
    const auto* bytes = item.payload.get();
    REQUIRE(a.enqueue(item));
    REQUIRE(b.enqueue(item));
    REQUIRE(c.enqueue(item));

    CHECK(a.dequeue().payload.get() == bytes);
    CHECK(b.dequeue().payload.get() == bytes);
    CHECK(c.dequeue().payload.get() == bytes);
}

TEST_CASE("PacketQueue: every field of an item survives the queue")
{
    PacketQueue q;

    PacketItem in;
    // An embedded NUL on purpose: a payload is raw PCM bits, not text, and a copy that went through a C string would
    // stop at the first zero byte.
    in.payload        = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{'a', 'b', 0, 'c', 'd'});
    in.baseAbsSeconds = 1777584474.153;
    in.packetBits     = 40;
    in.endOfStream    = false;
    REQUIRE(q.enqueue(in));

    const PacketItem out = q.dequeue();
    REQUIRE(out.payload != nullptr);
    CHECK(out.payload->size() == std::size_t{5});
    CHECK(*out.payload == *in.payload);
    CHECK(out.baseAbsSeconds == in.baseAbsSeconds);
    CHECK(out.packetBits == in.packetBits);
    CHECK(!out.endOfStream);
}

TEST_CASE("PacketQueue: an end-of-stream item the producer sends travels like any other")
{
    // The reader ends each stream with its own sentinel; the queue must not swallow it or treat it as a close.
    PacketQueue q;
    REQUIRE(q.enqueue(makeItem(1)));
    PacketItem eos;
    eos.endOfStream = true;
    REQUIRE(q.enqueue(eos));
    REQUIRE(q.enqueue(makeItem(2)));   // still open: a sentinel is data, not a close

    CHECK(q.dequeue().packetBits == 1u);
    CHECK(q.dequeue().endOfStream);
    const PacketItem after = q.dequeue();
    CHECK(!after.endOfStream);
    CHECK(after.packetBits == 2u);
}

TEST_CASE("PacketQueue: producer blocks when full and resumes after a dequeue")
{
    PacketQueue q(2);
    REQUIRE(q.enqueue(makeItem(0)));
    REQUIRE(q.enqueue(makeItem(1)));

    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<bool> accepted{false};
    ThreadGuard guard(q, std::thread([&] {
        started  = true;
        accepted = q.enqueue(makeItem(2));
        done     = true;
    }));

    REQUIRE(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    // The bound is the whole point: it is what stops a slow worker from letting the reader buffer an entire recording
    // in RAM.
    REQUIRE_MESSAGE(!done.load(), "a third enqueue into a depth-2 queue must block");

    const PacketItem first = q.dequeue();
    CHECK(first.packetBits == 0u);

    REQUIRE_MESSAGE(waitUntil([&] { return done.load(); }), "freeing a slot must release the blocked producer");
    CHECK(accepted.load());

    CHECK(q.dequeue().packetBits == 1u);
    CHECK(q.dequeue().packetBits == 2u);
}

TEST_CASE("PacketQueue: consumer blocks when empty and resumes after an enqueue")
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

    REQUIRE(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    REQUIRE_MESSAGE(!done.load(), "dequeue on an empty queue must block");

    REQUIRE(q.enqueue(makeItem(7)));
    REQUIRE_MESSAGE(waitUntil([&] { return done.load(); }), "an enqueue must release the blocked consumer");
    CHECK(!received.endOfStream);
    CHECK(received.packetBits == 7u);
}

TEST_CASE("PacketQueue: close releases a blocked producer")
{
    PacketQueue q(1);
    REQUIRE(q.enqueue(makeItem(0)));

    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<bool> accepted{true};
    ThreadGuard guard(q, std::thread([&] {
        started  = true;
        accepted = q.enqueue(makeItem(1));
        done     = true;
    }));

    REQUIRE(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    REQUIRE(!done.load());

    q.close();
    REQUIRE_MESSAGE(waitUntil([&] { return done.load(); }), "close() must wake a producer blocked on a full queue");
    // Cooperative cancellation: the reader is told its item was discarded.
    CHECK(!accepted.load());
}

TEST_CASE("PacketQueue: close releases a blocked consumer with an end-of-stream sentinel")
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

    REQUIRE(waitUntil([&] { return started.load(); }));
    std::this_thread::sleep_for(kSettle);
    REQUIRE(!done.load());

    q.close();
    REQUIRE_MESSAGE(waitUntil([&] { return done.load(); }), "close() must wake a consumer blocked on an empty queue");
    // The worker terminates on this sentinel without special-casing a closed queue.
    CHECK(received.endOfStream);
}

TEST_CASE("PacketQueue: the closed queue's sentinel carries nothing")
{
    // A worker that reached the end of a closed queue must not mistake leftovers for data: no payload, no bits, no time.
    PacketQueue q;
    q.close();
    const PacketItem eos = q.dequeue();
    CHECK(eos.endOfStream);
    CHECK(eos.payload == nullptr);
    CHECK(eos.packetBits == 0u);
    CHECK(eos.baseAbsSeconds == 0.0);
}

TEST_CASE("PacketQueue: closed queue drains its items before reporting end of stream")
{
    PacketQueue q(8);
    REQUIRE(q.enqueue(makeItem(10)));
    REQUIRE(q.enqueue(makeItem(11)));
    REQUIRE(q.enqueue(makeItem(12)));
    q.close();

    // Closing must not throw away work that was already accepted.
    for (uint64_t expect = 10; expect <= 12; ++expect)
    {
        const PacketItem got = q.dequeue();
        CHECK_MESSAGE(!got.endOfStream, "queued items must still be delivered after close()");
        CHECK(got.packetBits == expect);
    }
    CHECK(q.dequeue().endOfStream);
    // And the sentinel is repeatable, not consumed by being read once.
    CHECK(q.dequeue().endOfStream);
}

TEST_CASE("PacketQueue: enqueue after close discards and reports false")
{
    PacketQueue q;
    q.close();
    CHECK(!q.enqueue(makeItem(1)));
    // Nothing was stored, so the first dequeue is already the sentinel.
    CHECK(q.dequeue().endOfStream);
}

TEST_CASE("PacketQueue: a full queue that is closed refuses its blocked producer but keeps what it holds")
{
    // The reader's cancel path: it is blocked on a full queue when the run is cancelled. The item it was offering is
    // discarded, but the ones already queued are still the worker's to drain.
    PacketQueue q(2);
    REQUIRE(q.enqueue(makeItem(0)));
    REQUIRE(q.enqueue(makeItem(1)));
    std::atomic<bool> done{false};
    std::atomic<bool> accepted{true};
    ThreadGuard guard(q, std::thread([&] {
        accepted = q.enqueue(makeItem(2));
        done     = true;
    }));
    std::this_thread::sleep_for(kSettle);
    REQUIRE(!done.load());
    q.close();
    REQUIRE(waitUntil([&] { return done.load(); }));
    CHECK(!accepted.load());
    CHECK(q.dequeue().packetBits == 0u);
    CHECK(q.dequeue().packetBits == 1u);
    CHECK(q.dequeue().endOfStream);
}

TEST_CASE("PacketQueue: non-positive depth falls back to the default")
{
    // The constructor documents max_depth <= 0 as "use the default" (64). A queue that took 0 literally would block its
    // very first enqueue forever.
    for (const int depth : {0, -5})
    {
        INFO("depth " << depth);
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

        REQUIRE_MESSAGE(waitUntil([&] { return accepted.load() >= 64; }), "never accepted 64 items");
        std::this_thread::sleep_for(kSettle);
        // Exactly the default: the 65th enqueue is the one that blocks.
        CHECK(accepted.load() == 64);
    }
}

TEST_CASE("PacketQueue: queue never holds more than its depth")
{
    // Depth 3 with nobody consuming: a producer offering ten items must stall at three. This is the check that
    // distinguishes "full at depth" from "full at depth + 1".
    for (const int depth : {1, 3, 7})
    {
        INFO("depth " << depth);
        PacketQueue q(depth);

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

        REQUIRE(waitUntil([&] { return accepted.load() >= depth; }));
        std::this_thread::sleep_for(kSettle);
        CHECK(accepted.load() == depth);
    }
}

TEST_CASE("PacketQueue: one producer one consumer keep every item in order")
{
    // The real usage shape, under contention: a tiny depth forces the producer and the consumer to hand off constantly,
    // which is where a lost wakeup would strand one of them. A watchdog turns that hang into a failure rather than a
    // stuck run.
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
            // And if even that does not release it, close() is broken and the consumer below is stuck for good - abort
            // instead of hanging an unbounded CI job.
            if (!waitUntil([&] { return finished.load(); }, 5s))
            {
                abortRun("PacketQueue stress test deadlocked and close() did not release the consumer - aborting "
                         "rather than hanging the run.");
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

    CHECK_MESSAGE(!out_of_order, "items arrived out of order");
    CHECK(seen == kItems);
}
