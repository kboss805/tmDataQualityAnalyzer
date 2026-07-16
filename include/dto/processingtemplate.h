/**
 * @file processingtemplate.h
 * @brief A file-path-independent, reusable set of per-stream processing settings
 *        (+ optional series appearance) applicable to any .ch10 file whose PCM
 *        channel-ID set matches (Batch Apply, docs/processing-template-design.md).
 *
 * A template carries NO file paths: it is applied to arbitrary future files whose
 * PCM channel-ID set is identical to the template's.
 */

#ifndef PROCESSINGTEMPLATE_H
#define PROCESSINGTEMPLATE_H

#include <QString>
#include <QVector>

#include "seriesappearance.h"
#include "streamconfig.h"

/// One stream's settings plus the optional appearance of the series it produces.
struct TemplateStreamEntry
{
    StreamConfig config;                    ///< Processing settings; pcmChannelId is the match key.
    QVector<SeriesAppearance> appearance;   ///< Optional per-series name/color; empty if not captured.
};

/**
 * @brief The whole template: an ordered list of stream entries.
 *
 * Each entry's @c config.pcmChannelId is a real match key here (a target file is
 * accepted only if its channel-ID set is identical to the template's), unlike
 * its display-only role in the setup dialog.
 */
struct ProcessingTemplate
{
    int     schemaVersion = 1;   ///< See ProcessingTemplateSchema::kCurrentSchemaVersion.
    QString appVersion;          ///< Informational only; not a load gate.
    QString name;                ///< Optional friendly name for the template.
    int     timeChannelIndex = 0; ///< Time-channel combo index to apply when processing each batch file.
    QVector<TemplateStreamEntry> entries;
};

#endif // PROCESSINGTEMPLATE_H
