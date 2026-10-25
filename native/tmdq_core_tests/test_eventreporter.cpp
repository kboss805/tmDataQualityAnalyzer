// EventReporter: the null-safe forwarder every processing source reports through. No Qt; ported from the Qt
// TestEventReporter, whose cases are all here under the same descriptions.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
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

/// A sink that shows no progress: it does not override progressUpdated, which the interface makes optional.
class NoProgressSink final : public ProcessingEvents
{
public:
    void logMessage(const std::string& m) override { calls.push_back("log:" + m); }
    void errorOccurred(const std::string& m) override { calls.push_back("error:" + m); }
    void processingFinished(bool ok) override { calls.push_back(ok ? "finished:true" : "finished:false"); }

    std::vector<std::string> calls;
};

using Calls = std::vector<std::string>;

} // namespace

// A source with nobody listening reports into the void: this is the state every reader and processor is in until
// setEvents() is called, and the state the tests that do not care run in.
TEST_CASE("EventReporter: with no sink every call is harmless")
{
    EventReporter reporter;
    reporter.log("a");
    reporter.error("b");
    reporter.finished(true);
    reporter.progress(50);
    CHECK(true);   // reaching here without a crash is the assertion
}

TEST_CASE("EventReporter: each call reaches its own sink method with its argument")
{
    auto sink = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(sink);

    reporter.log("hello");
    reporter.error("broken");
    reporter.finished(false);
    reporter.finished(true);
    reporter.progress(37);

    CHECK(sink->calls == Calls{"log:hello", "error:broken", "finished:false", "finished:true", "progress:37"});
}

TEST_CASE("EventReporter: calls arrive in the order they were made")
{
    auto sink = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(sink);

    for (int i = 0; i < 20; ++i)
    {
        reporter.log(std::to_string(i));
        reporter.progress(i);
    }
    REQUIRE(sink->calls.size() == std::size_t{40});
    for (int i = 0; i < 20; ++i)
    {
        CHECK(sink->calls[static_cast<std::size_t>(2 * i)] == "log:" + std::to_string(i));
        CHECK(sink->calls[static_cast<std::size_t>(2 * i + 1)] == "progress:" + std::to_string(i));
    }
}

TEST_CASE("EventReporter: a replaced sink receives later calls and the old one none")
{
    auto first = std::make_shared<Recorder>();
    auto second = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(first);
    reporter.log("one");
    reporter.setSink(second);
    reporter.log("two");

    CHECK(first->calls == Calls{"log:one"});
    CHECK(second->calls == Calls{"log:two"});
}

TEST_CASE("EventReporter: clearing the sink silences the reporter")
{
    auto sink = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(sink);
    reporter.log("heard");
    reporter.setSink(nullptr);
    reporter.log("unheard");
    reporter.error("unheard");
    reporter.finished(true);
    reporter.progress(1);

    CHECK(sink->calls == Calls{"log:heard"});
}

// A source is handed a shared sink so a driver thread can outlive its caller; the reporter has to be what keeps it
// alive, not a borrowed pointer.
TEST_CASE("EventReporter: the reporter keeps the sink alive")
{
    std::weak_ptr<Recorder> weak;
    EventReporter reporter;
    {
        auto sink = std::make_shared<Recorder>();
        weak = sink;
        reporter.setSink(sink);
    }
    CHECK(!weak.expired());
    reporter.setSink(nullptr);
    CHECK(weak.expired());
}

TEST_CASE("EventReporter: a sink that shows no progress is not obliged to, and hears everything else")
{
    // progressUpdated has a default that does nothing, so the packet reader's progress reports cost such a sink nothing.
    auto sink = std::make_shared<NoProgressSink>();
    EventReporter reporter;
    reporter.setSink(sink);
    reporter.progress(10);
    reporter.log("still heard");
    reporter.progress(100);
    reporter.error("and this");
    reporter.finished(true);

    CHECK(sink->calls == Calls{"log:still heard", "error:and this", "finished:true"});
}

TEST_CASE("EventReporter: arguments pass through unchanged, whatever they are")
{
    auto sink = std::make_shared<Recorder>();
    EventReporter reporter;
    reporter.setSink(sink);
    const std::string utf8 = "R\xC3\xA9" "cepteur \xCE\xA9";   // UTF-8: an accented letter and a Greek capital omega
    reporter.log(utf8);
    reporter.log("");
    reporter.error(std::string("nul\0inside", 10));
    reporter.progress(0);
    reporter.progress(100);
    reporter.progress(-1);

    CHECK(sink->calls == Calls{"log:" + utf8, "log:", "error:" + std::string("nul\0inside", 10), "progress:0", "progress:100",
                               "progress:-1"});
}
