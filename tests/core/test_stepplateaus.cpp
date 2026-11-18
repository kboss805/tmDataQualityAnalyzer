// stepplateaus: the primitives the step detector is built from, tested directly (follow-up audit S4). The walk over a run of
// values that moves one way was written twice - once to take the first run long enough, once to take the longest - and is
// one function now; these cases hold what counts as a run, and test_stepdetector.cpp holds both uses of it through detect().

#include <utility>
#include <vector>

#include "doctest.h"
#include "stepplateaus.h"

using namespace stepplateaus;

namespace {

/// Every run of @p values the walk visits, as (begin, end) pairs, never stopping early.
std::vector<std::pair<int, int>> runsOf(const std::vector<double>& values, double threshold)
{
    std::vector<std::pair<int, int>> runs;
    forEachMonotonicRun(values, threshold, [&](const Run& run) {
        runs.emplace_back(run.begin, run.end);
        return RunWalk::Continue;
    });
    return runs;
}

using Runs = std::vector<std::pair<int, int>>;

} // namespace

TEST_CASE("StepPlateaus: a run is a stretch of changes above the threshold that all go one way")
{
    // Up to 30, then down: the reversal ends the first run and starts the next from the same value.
    CHECK(runsOf({0, 10, 20, 30, 25, 15, 5}, 1.0) == Runs{{0, 3}, {3, 6}});
    // Down first, then up.
    CHECK(runsOf({5, 4, 3, 10, 20}, 0.5) == Runs{{0, 2}, {2, 4}});
    // One direction all the way is one run.
    CHECK(runsOf({0, 5, 10, 15, 20}, 1.0) == Runs{{0, 4}});
    CHECK(runsOf({20, 15, 10, 5, 0}, 1.0) == Runs{{0, 4}});
}

TEST_CASE("StepPlateaus: a change within the threshold starts no run and ends one")
{
    // Jitter below the threshold is skipped, and the climb after it is a run of its own.
    CHECK(runsOf({0, 0.5, 0.9, 10, 20}, 1.0) == Runs{{2, 4}});
    // A change of exactly the threshold is not above it.
    CHECK(runsOf({0, 1, 2}, 1.0).empty());
    // A flat stretch in the middle ends the run before it, and the next begins after.
    CHECK(runsOf({0, 10, 20, 20.2, 30, 40}, 1.0) == Runs{{0, 2}, {3, 5}});
}

TEST_CASE("StepPlateaus: fewer than two values have no run")
{
    CHECK(runsOf({}, 1.0).empty());
    CHECK(runsOf({5}, 1.0).empty());
}

TEST_CASE("StepPlateaus: a walk stops at the run its visitor stops at, and visits no more")
{
    int visits = 0;
    forEachMonotonicRun({0, 10, 20, 15, 5, 10, 20}, 1.0, [&](const Run&) {
        visits++;
        return RunWalk::Stop;
    });
    CHECK(visits == 1);

    // Stopping at the second of three.
    visits = 0;
    Runs seen;
    forEachMonotonicRun({0, 10, 20, 15, 5, 10, 20}, 1.0, [&](const Run& run) {
        visits++;
        seen.emplace_back(run.begin, run.end);
        return visits == 2 ? RunWalk::Stop : RunWalk::Continue;
    });
    CHECK(seen == Runs{{0, 2}, {2, 4}});
}

TEST_CASE("StepPlateaus: transitionDirection is the sign of a change beyond the threshold")
{
    CHECK(transitionDirection(5.0, 1.0) == 1);
    CHECK(transitionDirection(-5.0, 1.0) == -1);
    CHECK(transitionDirection(1.0, 1.0) == 0);
    CHECK(transitionDirection(-1.0, 1.0) == 0);
    CHECK(transitionDirection(0.0, 1.0) == 0);
}

TEST_CASE("StepPlateaus: sortByRawAvg puts the points in ascending raw order, each with its own level")
{
    std::vector<CalibrationPoint> points;
    for (const auto& [raw, db] : std::vector<std::pair<double, double>>{{30.0, 3.0}, {10.0, 1.0}, {20.0, 2.0}, {5.0, 0.5}})
    {
        CalibrationPoint p;
        p.rawAvg = raw;
        p.trueDb = db;
        points.push_back(p);
    }
    sortByRawAvg(points);
    REQUIRE(points.size() == 4);
    CHECK(points[0].rawAvg == 5.0);
    CHECK(points[0].trueDb == 0.5);
    CHECK(points[1].rawAvg == 10.0);
    CHECK(points[1].trueDb == 1.0);
    CHECK(points[2].rawAvg == 20.0);
    CHECK(points[3].rawAvg == 30.0);
    CHECK(points[3].trueDb == 3.0);
}

TEST_CASE("StepPlateaus: levels within the tolerance merge into one that keeps the later value and spans both")
{
    const std::vector<Level> in = {{10.0, 0, 5}, {10.5, 5, 9}, {20.0, 9, 14}, {20.2, 14, 20}};
    const std::vector<Level> out = coalesceLevels(in, 1.0);
    REQUIRE(out.size() == 2);
    CHECK(out[0].value == 10.5);
    CHECK(out[0].begin == 0);
    CHECK(out[0].end == 9);
    CHECK(out[1].value == 20.2);
    CHECK(out[1].begin == 9);
    CHECK(out[1].end == 20);

    // A tolerance smaller than the gaps merges nothing.
    CHECK(coalesceLevels(in, 0.1).size() == 4);
    CHECK(coalesceLevels({}, 1.0).empty());
}

TEST_CASE("StepPlateaus: the median is the middle value, or the mean of the two middle ones, and 0 for none")
{
    CHECK(median({}) == 0.0);
    CHECK(median({3.0}) == 3.0);
    CHECK(median({5.0, 1.0, 3.0}) == 3.0);
    CHECK(median({4.0, 1.0, 3.0, 2.0}) == 2.5);
    CHECK(medianAdjacentGap({1.0}) == 0.0);
    CHECK(medianAdjacentGap({0.0, 2.0, 7.0, 7.5}) == 2.0);   // gaps 2, 5, 0.5
}
