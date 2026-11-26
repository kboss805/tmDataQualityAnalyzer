// containExceptions: the boundary at the top of each thread a run starts (exceptionboundary.h). The runs' own cases, a
// stream or a reader that throws, are in test_pipelinerunner.cpp and test_calibrationextractor.cpp.

#include <new>
#include <stdexcept>
#include <string>

#include "doctest.h"
#include "exceptionboundary.h"

TEST_CASE("ExceptionBoundary: a body that returns is not reported")
{
    int ran = 0;
    int reported = 0;
    CHECK(containExceptions([&]() { ++ran; }, [&](const char*) { ++reported; }));
    CHECK(ran == 1);
    CHECK(reported == 0);
}

TEST_CASE("ExceptionBoundary: what a body throws is reported in its own words, and goes no further")
{
    std::string words;
    CHECK_FALSE(containExceptions([]() { throw std::bad_alloc(); }, [&](const char* what) { words = what; }));
    CHECK(words == std::bad_alloc().what());

    CHECK_FALSE(containExceptions([]() { throw std::runtime_error("the disk went away"); },
                                  [&](const char* what) { words = what; }));
    CHECK(words == "the disk went away");

    // Something that is not a std::exception has no words of its own.
    CHECK_FALSE(containExceptions([]() { throw 42; }, [&](const char* what) { words = what; }));
    CHECK(words == "an unknown exception");
}

TEST_CASE("ExceptionBoundary: a report that throws in turn is dropped, not let out")
{
    // Building the report can run out of memory too; by then there is no one left to tell.
    bool escaped = false;
    try
    {
        CHECK_FALSE(containExceptions([]() { throw std::runtime_error("first"); },
                                      [](const char*) { throw std::bad_alloc(); }));
        CHECK_FALSE(containExceptions([]() { throw 1; }, [](const char*) { throw 2; }));
    }
    catch (...)
    {
        escaped = true;
    }
    CHECK_FALSE(escaped);
}
