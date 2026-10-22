/**
 * @file templatematcher.cpp
 * @brief Implementation of the exact-set channel-ID matcher for Batch Apply.
 */

#include "templatematcher.h"

#include <algorithm>
#include <iterator>

namespace {

/// @p ids as a sorted set: ascending, each once.
std::vector<int> asSet(std::vector<int> ids)
{
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

TemplateMatcher::MatchResult TemplateMatcher::match(const std::vector<int>& templateIds,
                                                    const std::vector<int>& fileIds)
{
    const std::vector<int> wanted = asSet(templateIds);
    const std::vector<int> present = asSet(fileIds);

    MatchResult result;
    std::set_difference(wanted.begin(), wanted.end(), present.begin(), present.end(),
                        std::back_inserter(result.missing));
    std::set_difference(present.begin(), present.end(), wanted.begin(), wanted.end(),
                        std::back_inserter(result.extra));
    result.ok = result.missing.empty() && result.extra.empty();
    return result;
}
