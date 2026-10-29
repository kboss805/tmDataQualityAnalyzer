// TemplateMatcher: the exact-set channel-id comparison behind Batch Apply (US1.1). No Qt; ported from the Qt
// TestTemplateMatcher. Its cases read the matcher through BatchController::matchFile, which turns a template and a
// reader's channel list into ids; that wiring is the ViewModel's and is now tested in TestBatchController. Everything
// about the comparison itself is here, called directly.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "templatematcher.h"

namespace {

using Ids = std::vector<int>;

constexpr int kMin = std::numeric_limits<int>::min();
constexpr int kMax = std::numeric_limits<int>::max();

std::string join(const Ids& ids)
{
    std::string text;
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        text += (i ? "," : "") + std::to_string(ids[i]);
    }
    return text;
}

/// The comparison written a second way, element by element over std::set, sharing nothing with the matcher's
/// sort / unique / set_difference. The Qt suite's oracle was the QSet implementation the core replaced; this is the
/// same rule (each list a set, missing and extra each sorted and once) with no Qt in it.
TemplateMatcher::MatchResult oracle(const Ids& templateIds, const Ids& fileIds)
{
    const std::set<int> wanted(templateIds.begin(), templateIds.end());
    const std::set<int> present(fileIds.begin(), fileIds.end());
    TemplateMatcher::MatchResult result;
    for (int id : wanted)
    {
        if (present.count(id) == 0)
        {
            result.missing.push_back(id);
        }
    }
    for (int id : present)
    {
        if (wanted.count(id) == 0)
        {
            result.extra.push_back(id);
        }
    }
    result.ok = result.missing.empty() && result.extra.empty();
    return result;
}

} // namespace

TEST_CASE("TemplateMatcher: every template channel is missing from a file with none")
{
    const auto result = TemplateMatcher::match({ 48, 32, 40 }, {});
    CHECK(!result.ok);
    CHECK(result.missing == Ids{ 32, 40, 48 });
    CHECK(result.extra.empty());
}

TEST_CASE("TemplateMatcher: identical channel sets match")
{
    const auto result = TemplateMatcher::match({ 32, 40, 48 }, { 32, 40, 48 });
    CHECK(result.ok);
    CHECK(result.missing.empty());
    CHECK(result.extra.empty());
}

TEST_CASE("TemplateMatcher: the order of either list does not matter")
{
    // Exact-SET match: order in the file's channel list is irrelevant, and so is the template's.
    CHECK(TemplateMatcher::match({ 32, 40, 48 }, { 48, 32, 40 }).ok);
    CHECK(TemplateMatcher::match({ 48, 40, 32 }, { 32, 40, 48 }).ok);
}

TEST_CASE("TemplateMatcher: a file missing a channel is refused, naming it")
{
    const auto result = TemplateMatcher::match({ 32, 40, 48 }, { 32, 40 });
    CHECK(!result.ok);
    CHECK(result.missing == Ids{ 48 });
    CHECK(result.extra.empty());
}

TEST_CASE("TemplateMatcher: a file with an extra channel is refused, naming it")
{
    const auto result = TemplateMatcher::match({ 32, 40 }, { 32, 40, 48 });
    CHECK(!result.ok);
    CHECK(result.missing.empty());
    CHECK(result.extra == Ids{ 48 });
}

TEST_CASE("TemplateMatcher: missing and extra are reported together")
{
    const auto result = TemplateMatcher::match({ 32, 40 }, { 40, 48 });
    CHECK(!result.ok);
    CHECK(result.missing == Ids{ 32 });
    CHECK(result.extra == Ids{ 48 });
}

TEST_CASE("TemplateMatcher: ok means nothing missing and nothing extra")
{
    CHECK(TemplateMatcher::match({ 32, 40 }, { 32, 40 }).ok);
    CHECK(!TemplateMatcher::match({ 32, 40 }, { 32 }).ok);
    CHECK(!TemplateMatcher::match({ 32 }, { 32, 40 }).ok);
    CHECK(!TemplateMatcher::match({ 32, 40 }, { 40, 48 }).ok);
}

TEST_CASE("TemplateMatcher: a template and a file with no channels match")
{
    // Vacuously the same set. A caller that must refuse an empty template has to say so itself.
    const auto result = TemplateMatcher::match({}, {});
    CHECK(result.ok);
    CHECK(result.missing.empty());
    CHECK(result.extra.empty());
}

TEST_CASE("TemplateMatcher: an empty template finds every file channel extra")
{
    const auto result = TemplateMatcher::match({}, { 40, 32 });
    CHECK(!result.ok);
    CHECK(result.missing.empty());
    CHECK(result.extra == Ids{ 32, 40 });
}

TEST_CASE("TemplateMatcher: a repeated channel counts once")
{
    // Two entries for one channel, or a file listing a channel twice, are still one channel.
    CHECK(TemplateMatcher::match({ 32, 32, 40 }, { 32, 40 }).ok);
    CHECK(TemplateMatcher::match({ 32, 40 }, { 32, 40, 40, 32 }).ok);

    CHECK(TemplateMatcher::match({ 32 }, { 40, 40, 32 }).extra == Ids{ 40 });   // reported once
    CHECK(TemplateMatcher::match({ 40, 40, 32 }, { 32 }).missing == Ids{ 40 });
    // Repeats that are not next to each other: collapsing only neighbours would leave one of these twice.
    CHECK(TemplateMatcher::match({ 7 }, { 9, 7, 9, 8, 9 }).extra == Ids{ 8, 9 });
    CHECK(TemplateMatcher::match({ 9, 7, 9, 8, 9 }, { 7 }).missing == Ids{ 8, 9 });
}

TEST_CASE("TemplateMatcher: missing and extra come back sorted")
{
    const auto result = TemplateMatcher::match({ 50, 10, 30, 20, 40, 7 }, { 35, 5, 25, 15, 7 });
    CHECK(result.missing == Ids{ 10, 20, 30, 40, 50 });
    CHECK(result.extra == Ids{ 5, 15, 25, 35 });
}

TEST_CASE("TemplateMatcher: any integer can be a channel id")
{
    const auto result = TemplateMatcher::match({ kMax, -1, 0, kMin }, { 0, kMax, 77 });
    CHECK(result.missing == Ids{ kMin, -1 });
    CHECK(result.extra == Ids{ 77 });
}

TEST_CASE("TemplateMatcher: matches a set comparison written another way, over generated inputs")
{
    std::uint32_t state = 20261025u;
    auto next = [&state]() { state = state * 1664525u + 1013904223u; return state >> 8; };
    auto below = [&](int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); };

    // Ids from a small pool so the sets overlap, plus the awkward ones.
    const Ids pool = { -1, 0, 1, 2, 3, 5, 8, 13, 21, 32, 33, 40, 48, 64, 255, 1000, kMin, kMax };
    auto draw = [&]() {
        Ids ids;
        const int n = below(9);
        for (int i = 0; i < n; ++i)
        {
            ids.push_back(pool[static_cast<std::size_t>(below(static_cast<int>(pool.size())))]);
        }
        return ids;
    };

    int same = 0, onlyMissing = 0, onlyExtra = 0, both = 0, withRepeats = 0;
    for (int i = 0; i < 4000; ++i)
    {
        const Ids templateIds = draw();
        Ids fileIds;
        switch (below(4))
        {
        case 0:   // the same set, reshuffled and with repeats
            fileIds = templateIds;
            for (std::size_t k = 0; k < fileIds.size(); ++k)
            {
                std::swap(fileIds[k], fileIds[static_cast<std::size_t>(below(static_cast<int>(fileIds.size())))]);
            }
            if (!fileIds.empty() && below(2))
            {
                fileIds.push_back(fileIds[static_cast<std::size_t>(below(static_cast<int>(fileIds.size())))]);
            }
            break;
        case 1:   // the template's channels less some
            for (int id : templateIds)
            {
                if (below(3))
                {
                    fileIds.push_back(id);
                }
            }
            break;
        case 2:   // the template's channels plus some
        {
            fileIds = templateIds;
            const Ids more = draw();
            fileIds.insert(fileIds.end(), more.begin(), more.end());
            break;
        }
        default:  // unrelated
            fileIds = draw();
            break;
        }

        const auto want = oracle(templateIds, fileIds);
        const auto got = TemplateMatcher::match(templateIds, fileIds);
        INFO("template " << join(templateIds) << " file " << join(fileIds));
        REQUIRE(got.ok == want.ok);
        REQUIRE(got.missing == want.missing);
        REQUIRE(got.extra == want.extra);

        same += want.ok;
        onlyMissing += !want.missing.empty() && want.extra.empty();
        onlyExtra += want.missing.empty() && !want.extra.empty();
        both += !want.missing.empty() && !want.extra.empty();
        withRepeats += std::set<int>(templateIds.begin(), templateIds.end()).size() != templateIds.size();
    }
    // The generator must reach every outcome in volume, or "no difference" proves nothing.
    CHECK(same > 500);
    CHECK(onlyMissing > 500);
    CHECK(onlyExtra > 500);
    CHECK(both > 500);
    CHECK(withRepeats > 500);
}
