/**
 * @file batchcontroller.cpp
 * @brief Implementation of the Apply Template batch loop (US1.1).
 */

#include "batchcontroller.h"

#include <QDir>
#include <QFileInfo>

#include "chapter10reader.h"
#include "plotviewmodel.h"
#include "templatematcher.h"

namespace {

/// Spells out a channel-set mismatch for the confirmation dialog: which channels
/// the template expects and the file lacks, and which the file has spare.
QString describeMismatch(const TemplateMatcher::MatchResult& match)
{
    QStringList parts;
    if (!match.missing.isEmpty())
    {
        QStringList ids;
        for (int id : match.missing)
        {
            ids << QString::number(id);
        }
        parts << QObject::tr("missing channel(s): %1").arg(ids.join(", "));
    }
    if (!match.extra.isEmpty())
    {
        QStringList ids;
        for (int id : match.extra)
        {
            ids << QString::number(id);
        }
        parts << QObject::tr("extra channel(s): %1").arg(ids.join(", "));
    }
    return parts.join("; ");
}

} // namespace

BatchController::BatchController(MainViewModel* viewModel, PlotViewModel* plotViewModel,
                                 QObject* parent)
    : QObject(parent)
    , m_view_model(viewModel)
    , m_plot_view_model(plotViewModel)
{
}

QVector<BatchController::FileCheck> BatchController::validate(const ProcessingTemplate& tmpl,
                                                              const QStringList& files)
{
    QVector<FileCheck> checks;
    checks.reserve(files.size());
    for (const QString& file_path : files)
    {
        FileCheck check;
        check.filepath = file_path;

        Chapter10Reader reader;
        if (!reader.loadChannels(file_path))
        {
            check.ok     = false;
            check.reason = tr("could not read channels");
        }
        else
        {
            const TemplateMatcher::MatchResult match =
                TemplateMatcher::matchFile(tmpl, reader.getPCMChannelList());
            check.ok = match.ok;
            if (!match.ok)
            {
                check.reason = describeMismatch(match);
            }
        }
        checks.append(check);
    }
    return checks;
}

ProcessingTemplate BatchController::buildTemplate(const Source& src, const PlotViewModel& plot)
{
    ProcessingTemplate tmpl;
    tmpl.appVersion       = AppVersion::toString();
    tmpl.name             = QFileInfo(src.filepath).baseName();
    tmpl.timeChannelIndex = src.timeChannelIndex;
    tmpl.swapBytes        = src.swapBytes;

    for (const StreamConfig& cfg : src.streamConfigs)
    {
        TemplateStreamEntry entry;
        entry.config = cfg;

        // Capture the current appearance of every plot series this stream produced,
        // keyed (within the entry) by metric/receiver/channel. Only series from this
        // source and this stream's channel id are this entry's; a non-processed
        // stream produces none, leaving the appearance list empty.
        for (const PlotSeriesData& series : plot.allSeries())
        {
            if (series.sourceId != src.sourceId || series.streamOrder != cfg.pcmChannelId)
            {
                continue;
            }

            SeriesAppearance appearance;
            appearance.metricType    = series.metricType;
            appearance.receiverIndex = series.receiverIndex;
            appearance.channelIndex  = series.channelIndex;
            appearance.name          = series.name;
            appearance.color         = series.color;
            entry.appearance.append(appearance);
        }

        tmpl.entries.append(entry);
    }
    return tmpl;
}

void BatchController::applyAppearance(const ProcessingTemplate& tmpl, int sourceId,
                                      PlotViewModel& plot)
{
    for (const TemplateStreamEntry& entry : tmpl.entries)
    {
        for (const SeriesAppearance& appearance : entry.appearance)
        {
            for (const PlotSeriesData& series : plot.allSeries())
            {
                if (series.sourceId != sourceId
                    || series.streamOrder != entry.config.pcmChannelId
                    || series.metricType != appearance.metricType
                    || series.receiverIndex != appearance.receiverIndex
                    || series.channelIndex != appearance.channelIndex)
                {
                    continue;
                }
                plot.renameSeriesById(series.id, appearance.name);
                plot.recolorSeriesById(series.id, appearance.color);
                break;
            }
        }
    }
    // renameSeriesById/recolorSeriesById are pure setters; one commit refreshes views.
    plot.commitAppearanceChanges();
}

void BatchController::setImageExporter(ImageExporter exporter)
{
    m_export_image = std::move(exporter);
}

bool BatchController::active() const
{
    return m_active;
}

void BatchController::start(const ProcessingTemplate& tmpl, const QStringList& files,
                            const Options& options)
{
    if (files.isEmpty())
    {
        return;
    }

    m_template  = tmpl;
    m_files     = files;
    m_options   = options;
    m_index     = 0;
    m_processed = 0;
    m_skipped   = 0;
    m_active    = true;

    // A batch always starts a fresh session/plot.
    m_view_model->clearState();
    m_plot_view_model->clearData();

    emit message(MainViewModel::LogLevel::Success,
                 tr("Processing %1 file(s)...").arg(m_files.size()));
    advance();
}

void BatchController::advance()
{
    while (m_index < m_files.size())
    {
        const QString path = m_files.at(m_index);

        if (!QFileInfo::exists(path))
        {
            emit message(MainViewModel::LogLevel::Warning, tr("Skipped missing batch file: ") + path);
            ++m_skipped;
            ++m_index;
            continue;
        }

        // Every file is retained in memory (accumulated onto the shared axis) so the
        // user can browse them via the plot's file selector; per-file export, if
        // requested, runs as a post-pass in finish().
        m_view_model->addSource(path);
        return; // wait for onSourceReady()
    }

    finish();
}

void BatchController::onSourceReady()
{
    applySourceConfig();
}

void BatchController::applySourceConfig()
{
    // Belt-and-suspenders re-check against the freshly loaded file (the up-front
    // validation could be stale if the file changed on disk since it was picked).
    const TemplateMatcher::MatchResult match =
        TemplateMatcher::matchFile(m_template, m_view_model->reader()->getPCMChannelList());
    if (!match.ok)
    {
        emit message(MainViewModel::LogLevel::Warning,
                     tr("Skipped (channels no longer match template): ")
                         + QFileInfo(m_view_model->inputFilename()).fileName());
        ++m_skipped;
        ++m_index;
        advance();
        return;
    }

    // Files match exactly, so the template's stored pcmChannelIds are correct for
    // this file -- feed its configs straight through to processing.
    QVector<StreamConfig> configs;
    configs.reserve(m_template.entries.size());
    for (const TemplateStreamEntry& entry : m_template.entries)
    {
        configs.append(entry.config);
    }

    m_view_model->setTimeChannelIndex(m_template.timeChannelIndex);
    // A batch is one vendor's files, so the template's byte order applies to all.
    m_view_model->setSwapBytes(m_template.swapBytes);
    m_view_model->setStreamConfigs(configs);
    m_view_model->startProcessing();
}

void BatchController::onProcessingFinished(bool success)
{
    const QString base = QFileInfo(m_view_model->inputFilename()).baseName();

    if (success)
    {
        ++m_processed;

        // The just-finished run was appended as the newest source before this
        // signal (MainViewModel::onCoordinatorProcessingFinished).
        const int sourceId = m_view_model->sources().isEmpty()
                                 ? 0
                                 : m_view_model->sources().last().sourceId;

        // Label the source for the plot's file selector, and reapply the template's
        // saved names/colors onto this file's fresh series.
        m_plot_view_model->setSourceLabel(sourceId, base);
        if (m_options.reuseAppearance)
        {
            applyAppearance(m_template, sourceId, *m_plot_view_model);
        }
    }
    else
    {
        ++m_skipped;
        emit message(MainViewModel::LogLevel::Error, tr("Batch file failed to process: ") + base);
    }

    ++m_index;
    advance();
}

void BatchController::finish()
{
    m_active = false;

    const QVector<Source>& sources = m_view_model->sources();

    // Optional per-file export post-pass: isolate each source in turn (so the CSV
    // and the rendered images both cover just that file), then export. Runs over the
    // fully-retained plot after all files are processed.
    if (m_options.exportPerFile)
    {
        // One CSV per file (carries every metric's columns), plus, when the batch has
        // frame-sync data, a separate image for each left-axis metric: Frame Sync
        // Lock % and Accumulated Missed Frames. Restore the user's view afterward.
        const PlotViewModel::LockAxisView original_view = m_plot_view_model->lockAxisView();
        const bool has_frame_sync = m_plot_view_model->hasLockSeries()
                                    || m_plot_view_model->hasMissedFramesSeries();

        for (const Source& src : sources)
        {
            const QString base = QFileInfo(src.filepath).baseName();
            m_plot_view_model->setVisibleSource(src.sourceId); // rebuilds chart to this file

            const QString csv_path = QDir(m_options.outputDir).filePath(base + ".csv");
            if (m_plot_view_model->exportCsv(csv_path, src.sourceId))
            {
                emit message(MainViewModel::LogLevel::Success, tr("Exported: ") + csv_path);
            }
            else
            {
                emit message(MainViewModel::LogLevel::Error, tr("Failed to export CSV: ") + csv_path);
            }

            if (!m_export_image)
            {
                continue; // no renderer supplied (headless): the CSVs still land
            }
            if (has_frame_sync)
            {
                m_plot_view_model->setLockAxisView(PlotViewModel::LockAxisView::LockPercent);
                m_export_image(QDir(m_options.outputDir).filePath(base + "_framesync_lock.png"));
                m_plot_view_model->setLockAxisView(PlotViewModel::LockAxisView::MissedFrames);
                m_export_image(QDir(m_options.outputDir).filePath(base + "_missed_frames.png"));
            }
            else
            {
                m_export_image(QDir(m_options.outputDir).filePath(base + ".png"));
            }
        }

        m_plot_view_model->setLockAxisView(original_view);
    }

    // Default the plot to the first processed file (browse intent); a single file
    // leaves the selector disabled and shows everything. "All files (overlaid)" is
    // available from the dropdown.
    if (sources.size() > 1)
    {
        m_plot_view_model->setVisibleSource(sources.first().sourceId);
        m_plot_view_model->setPlotTitle(QFileInfo(sources.first().filepath).baseName());
    }
    else if (sources.size() == 1)
    {
        m_plot_view_model->setPlotTitle(QFileInfo(sources.first().filepath).baseName());
    }

    emit message(MainViewModel::LogLevel::Success,
                 tr("Batch complete: %1 processed, %2 skipped.%3")
                     .arg(m_processed)
                     .arg(m_skipped)
                     .arg(m_options.exportPerFile
                              ? tr(" Output written to %1.").arg(m_options.outputDir)
                              : QString()));
    emit finished();
}
