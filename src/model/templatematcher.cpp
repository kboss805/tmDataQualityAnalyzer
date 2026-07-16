/**
 * @file templatematcher.cpp
 * @brief Implementation of the exact-set channel-ID matcher for Batch Apply.
 */

#include "templatematcher.h"

#include <algorithm>

#include "processingtemplate.h"

QSet<int> TemplateMatcher::templateChannelIds(const ProcessingTemplate& tmpl)
{
    QSet<int> ids;
    for (const TemplateStreamEntry& entry : tmpl.entries)
        ids.insert(entry.config.pcmChannelId);
    return ids;
}

TemplateMatcher::MatchResult TemplateMatcher::matchFile(
    const ProcessingTemplate& tmpl,
    const QList<QPair<int, QString>>& fileChannels)
{
    const QSet<int> templateIds = templateChannelIds(tmpl);

    QSet<int> fileIds;
    for (const QPair<int, QString>& ch : fileChannels)
        fileIds.insert(ch.first);

    MatchResult result;
    for (int id : templateIds)
    {
        if (!fileIds.contains(id))
            result.missing.append(id);
    }
    for (int id : fileIds)
    {
        if (!templateIds.contains(id))
            result.extra.append(id);
    }

    std::sort(result.missing.begin(), result.missing.end());
    std::sort(result.extra.begin(), result.extra.end());
    result.ok = result.missing.isEmpty() && result.extra.isEmpty();
    return result;
}

bool TemplateMatcher::fileMatchesTemplate(const ProcessingTemplate& tmpl,
                                          const QList<QPair<int, QString>>& fileChannels)
{
    return matchFile(tmpl, fileChannels).ok;
}
