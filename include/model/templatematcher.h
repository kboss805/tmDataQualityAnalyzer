/**
 * @file templatematcher.h
 * @brief Validates that a .ch10 file's PCM channel-ID set matches a processing
 *        template's, for Batch Apply (docs/processing-template-design.md).
 *
 * Pure, UI-free, disk-free logic (same discipline as StepDetector /
 * SeriesColumnSchema) so it is unit-testable without a real file. Matching is
 * exact-set: a file is accepted only if its channel-ID set is identical to the
 * template's -- no positional mapping, no subset tolerance. This is precisely
 * what makes it safe to feed the template's stored StreamConfigs (with their
 * original pcmChannelIds) straight to processing.
 */

#ifndef TEMPLATEMATCHER_H
#define TEMPLATEMATCHER_H

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

struct ProcessingTemplate;

namespace TemplateMatcher
{
    /// Why a file did or didn't match, so the caller can explain a rejection.
    struct MatchResult
    {
        bool ok = false;         ///< True iff missing and extra are both empty.
        QVector<int> missing;    ///< Channel ids the template needs but the file lacks (sorted).
        QVector<int> extra;      ///< Channel ids the file has but the template doesn't (sorted).
    };

    /// The set of PCM channel ids the template configures (one per entry).
    QSet<int> templateChannelIds(const ProcessingTemplate& tmpl);

    /// Exact-set comparison of @p tmpl's channel ids against @p fileChannels
    /// (the reader's getPCMChannelList() form: (id, name) pairs).
    MatchResult matchFile(const ProcessingTemplate& tmpl,
                          const QList<QPair<int, QString>>& fileChannels);

    /// Convenience predicate: matchFile(...).ok.
    bool fileMatchesTemplate(const ProcessingTemplate& tmpl,
                             const QList<QPair<int, QString>>& fileChannels);
}

#endif // TEMPLATEMATCHER_H
