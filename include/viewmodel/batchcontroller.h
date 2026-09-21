/**
 * @file batchcontroller.h
 * @brief Drives Apply Template's sequential batch run (US1.1).
 *
 * The batch is N runs of the ordinary single-file pipeline, one after another,
 * accumulating into one PlotViewModel - not a concurrent read. This class owns
 * that turn-taking: which files are queued, which one is in flight, what to do
 * when it finishes, and the per-file export post-pass at the end.
 *
 * It lived in MainView, where docs/CLAUDE.md had to document a state machine
 * sitting in the View layer. The View's remaining part is the part that is
 * genuinely the View's: showing the confirmation dialog and rendering the plot
 * to an image.
 */

#ifndef BATCHCONTROLLER_H
#define BATCHCONTROLLER_H

#include <functional>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mainviewmodel.h"
#include "processingtemplate.h"
#include "source.h"

class PlotViewModel;

/**
 * @brief Sequential Apply Template batch loop over files that share a channel layout.
 */
class BatchController : public QObject
{
    Q_OBJECT

public:
    /// One file's up-front check against the template.
    struct FileCheck
    {
        QString filepath;
        bool    ok = true;
        QString reason;   ///< Why it was rejected; empty when ok.
    };

    /// What the confirmation dialog settled before the run starts.
    struct Options
    {
        bool    exportPerFile   = false; ///< Also write a CSV + image per file.
        bool    reuseAppearance = true;  ///< Reapply the template's series names/colors.
        QString outputDir;               ///< Where the per-file exports go.
    };

    /// Renders the plot to @p path, returning whether it was written. Supplied by
    /// the View: the image comes from the widget, and only the widget can draw it.
    /// The controller decides when and where, not how.
    using ImageExporter = std::function<bool(const QString&)>;

    BatchController(MainViewModel* viewModel, PlotViewModel* plotViewModel,
                    QObject* parent = nullptr);

    /// Checks each file's PCM channel set against @p tmpl, so the caller can show
    /// which files will run and which are rejected, and why. Reads each file's
    /// channels, so it is the slow part of starting a batch.
    static QVector<FileCheck> validate(const ProcessingTemplate& tmpl, const QStringList& files);

    /// Captures a processed source as a template: its per-stream configs, time
    /// channel and byte order, plus each plot series' current name and color.
    static ProcessingTemplate buildTemplate(const Source& src, const PlotViewModel& plot);

    /// Reapplies @p tmpl's saved series names/colors onto the series belonging to
    /// @p sourceId, matched by channel id + metric/receiver/channel.
    static void applyAppearance(const ProcessingTemplate& tmpl, int sourceId, PlotViewModel& plot);

    void setImageExporter(ImageExporter exporter);

    /// True while a batch is in flight: the View routes its processing signals here
    /// instead of treating them as a single-file run.
    bool active() const;

    /// Starts a run over @p files, which must already have passed validate().
    /// Clears the session first - a batch always starts a fresh plot.
    void start(const ProcessingTemplate& tmpl, const QStringList& files, const Options& options);

    /// The reader has a new source open: apply the template's configs and process it.
    void onSourceReady();

    /// The run in flight finished; record it and move to the next file.
    void onProcessingFinished(bool success);

signals:
    /// A line for the log. The level is the View's to render, not to decide.
    void message(MainViewModel::LogLevel level, const QString& text);

    /// Every file has been processed (or skipped) and the post-pass is done.
    void finished();

private:
    void advance();
    void applySourceConfig();
    void finish();

    MainViewModel* m_view_model      = nullptr;
    PlotViewModel* m_plot_view_model = nullptr;
    ImageExporter  m_export_image;

    bool               m_active = false;          ///< True while a run is in flight.
    ProcessingTemplate m_template;                ///< The template being applied to every file.
    QStringList        m_files;                   ///< The matched files, in order.
    Options            m_options;                 ///< What the confirmation dialog settled.
    int                m_index     = 0;           ///< Index of the file in flight.
    int                m_processed = 0;           ///< Files processed successfully.
    int                m_skipped   = 0;           ///< Files skipped or failed.
};

#endif // BATCHCONTROLLER_H
