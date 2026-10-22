/**
 * @file templatematcher.h
 * @brief Validates that a .ch10 file's PCM channel-ID set matches a processing
 *        template's, for Batch Apply (docs/processing-template-design.md).
 *
 * Qt-free core logic over plain channel ids (de-Qt, plan step 1.3). Matching is
 * exact-set: a file is accepted only if its channel-ID set is identical to the
 * template's -- no positional mapping, no subset tolerance. This is precisely
 * what makes it safe to feed the template's stored StreamConfigs (with their
 * original pcmChannelIds) straight to processing.
 *
 * Reading the ids out of a template or a reader's channel list is the caller's
 * (`BatchController::matchFile`), because those types carry Qt.
 */

#ifndef TEMPLATEMATCHER_H
#define TEMPLATEMATCHER_H

#include <vector>

namespace TemplateMatcher
{
    /// Why a file did or didn't match, so the caller can explain a rejection.
    struct MatchResult
    {
        bool ok = false;           ///< True iff missing and extra are both empty.
        std::vector<int> missing;  ///< Channel ids the template needs but the file lacks (sorted, each once).
        std::vector<int> extra;    ///< Channel ids the file has but the template doesn't (sorted, each once).
    };

    /// Exact-set comparison of the template's channel ids against the file's. Order and repeats in
    /// either list do not matter: each is a set. Two empty sets are the same set, so they match.
    MatchResult match(const std::vector<int>& templateIds, const std::vector<int>& fileIds);
}

#endif // TEMPLATEMATCHER_H
