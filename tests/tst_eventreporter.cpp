#include "tst_eventreporter.h"

#include <memory>
#include <string>
#include <vector>

#include <QtTest>

#include "processingevents.h"

namespace {

/// Records every call, in order, as "kind:argument".
class Recorder final : public ProcessingEvents
{
public:
    void logMessage(const std::string& m) override { calls.push_back("log:" + m); }
    void errorOccurred(const std::string& m) override { calls.push_back("error:" + m); }
    void processingFinished(bool ok) override { calls.push_back(ok ? "finished:true" : "finished:false"); }
    void progressUpdated(int p) override { calls.push_back("progress:" + std::to_string(p)); }

    std::vector<std::string> calls;
};

} // namespace

// A source with nobody listening reports into the void: this is the state every reader and
// processor is in until setEvents() is called, and the state the tests that do not care run in.
void TestEventReporter::withNoSinkEveryCallIsHarmless()
{
    EventReporter reporter;
    reporter.log("a");
    reporter.error("b");
    reporter.finished(true);
    reporter.progress(50);
    QVERIFY(true);   // reaching here without a crash is the assertion
}

void TestEventReporter::eachCallReachesItsOwnSinkMethodWithItsArgument()
{
    auto sink = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(sink);

    reporter.log("hello");
    reporter.error("broken");
    reporter.finished(false);
    reporter.finished(true);
    reporter.progress(37);

    const std::vector<std::string> expected{"log:hello", "error:broken", "finished:false",
                                            "finished:true", "progress:37"};
    QCOMPARE(sink->calls, expected);
}

void TestEventReporter::callsArriveInTheOrderTheyWereMade()
{
    auto sink = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(sink);

    for (int i = 0; i < 20; ++i)
    {
        reporter.log(std::to_string(i));
        reporter.progress(i);
    }
    QCOMPARE(sink->calls.size(), std::size_t{40});
    for (int i = 0; i < 20; ++i)
    {
        QCOMPARE(sink->calls[2 * i], "log:" + std::to_string(i));
        QCOMPARE(sink->calls[2 * i + 1], "progress:" + std::to_string(i));
    }
}

void TestEventReporter::aReplacedSinkReceivesLaterCallsAndTheOldOneNone()
{
    auto first = std::make_shared<Recorder>();
    auto second = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(first);
    reporter.log("one");
    reporter.setSink(second);
    reporter.log("two");

    QCOMPARE(first->calls, (std::vector<std::string>{"log:one"}));
    QCOMPARE(second->calls, (std::vector<std::string>{"log:two"}));
}

void TestEventReporter::clearingTheSinkSilencesTheReporter()
{
    auto sink = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(sink);
    reporter.log("heard");
    reporter.setSink(nullptr);
    reporter.log("unheard");
    reporter.error("unheard");

    QCOMPARE(sink->calls, (std::vector<std::string>{"log:heard"}));
}

// A source is handed a shared sink so a driver thread can outlive its caller; the reporter has to
// be what keeps it alive, not a borrowed pointer.
void TestEventReporter::theReporterKeepsTheSinkAlive()
{
    std::weak_ptr<Recorder> weak;
    EventReporter reporter;
    {
        auto sink = std::make_shared<Recorder>();
        weak = sink;
        reporter.setSink(sink);
    }
    QVERIFY(!weak.expired());
    reporter.setSink(nullptr);
    QVERIFY(weak.expired());
}
