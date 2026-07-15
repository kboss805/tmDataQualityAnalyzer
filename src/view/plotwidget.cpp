/**
 * @file plotwidget.cpp
 * @brief Implementation of PlotWidget — QCustomPlot chart with controls.
 */

#include "plotwidget.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QToolTip>
#include <QtSvg/QSvgGenerator>
#include <QVBoxLayout>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>

#include "qcustomplot.h"

#include "constants.h"
#include "plotviewmodel.h"
#include "exportdialog.h"
#include "plotcustomizationdialog.h"

namespace {
    /// @return true for metrics drawn on the left axis (lock % and frame-sync errors).
    bool isLeftAxisMetric(PlotSeriesData::MetricType type)
    {
        return type == PlotSeriesData::MetricType::FrameSyncLock
            || type == PlotSeriesData::MetricType::AccumulatedMissedFrames;
    }

    /// @return the legend-overlay display text for a series. SNR series carry a
    /// "<pcmChannelId> - <streamLabel> <ch.name>" identity in s.name (that exact
    /// format matters for CSV export/import and renaming — see the NOTE in
    /// PlotViewModel::addStreamData() — so it is left untouched there). The legend
    /// only needs the channel number and receiver/channel suffix to disambiguate
    /// series on screen, not the full TMATS-derived stream title, which can be long
    /// enough to crowd the overlay. Rebuilt from receiverIndex/channelIndex rather
    /// than parsed out of s.name so it can't drift from the SNR naming convention.
    QString legendDisplayName(const PlotSeriesData& s)
    {
        if (s.metricType != PlotSeriesData::MetricType::SNR)
        {
            return s.name;
        }
        const QString prefix = (s.channelIndex >= 0
                                && s.channelIndex < static_cast<int>(UIConstants::kChannelPrefixes.size()))
            ? QString(UIConstants::kChannelPrefixes[s.channelIndex])
            : QString::number(s.channelIndex + 1);
        return QString("CH%1 %2_RCVR%3").arg(s.streamOrder).arg(prefix).arg(s.receiverIndex);
    }
}

////////////////////////////////////////////////////////////////////////////////
// TimeHackTicker
////////////////////////////////////////////////////////////////////////////////

QString TimeHackTicker::getTickLabel(double tick, const QLocale& /*locale*/,
                                     QChar /*formatChar*/, int /*precision*/)
{
    if (m_vm != nullptr)
    {
        return m_vm->formatTime(tick);
    }
    return QString::number(tick, 'f', 1);
}

double TimeHackTicker::getTickStep(const QCPRange& range)
{
    const int count = qMax(mTickCount, 2);
    return range.size() / (count - 1);
}

int TimeHackTicker::getSubTickCount(double /*tickStep*/)
{
    return 0;
}

QVector<double> TimeHackTicker::createTickVector(double tickStep, const QCPRange& range)
{
    const int count = qMax(mTickCount, 2);
    QVector<double> ticks;
    ticks.reserve(count);
    for (int i = 0; i < count; i++)
    {
        ticks.append(range.lower + i * tickStep);
    }
    return ticks;
}

////////////////////////////////////////////////////////////////////////////////
// PlotWidget
////////////////////////////////////////////////////////////////////////////////

PlotWidget::PlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setUpLayout();
    setUpConnections();
}

void PlotWidget::setViewModel(PlotViewModel* vm)
{
    if (m_view_model != nullptr)
    {
        QObject::disconnect(m_view_model, nullptr, this, nullptr);
    }

    m_view_model = vm;
    if (vm == nullptr)
    {
        return;
    }

    // Configure custom time ticker for X axis
    QSharedPointer<TimeHackTicker> ticker(new TimeHackTicker);
    ticker->setViewModel(vm);
    ticker->setTickCount(PlotConstants::kTickCount);
    m_plot->xAxis->setTicker(ticker);

    connect(vm, &PlotViewModel::dataChanged,  this, &PlotWidget::onDataChanged);
    connect(vm, &PlotViewModel::dataChanged,  this, [this]() { showLoadingIndicator(false); });
    connect(vm, &PlotViewModel::loadStarted,  this, [this]() { showLoadingIndicator(true);  });
    connect(vm, &PlotViewModel::loadFailed,   this, [this]() { showLoadingIndicator(false); });
    connect(vm, &PlotViewModel::seriesVisibilityChanged, this, &PlotWidget::onSeriesVisibilityToggled);
    connect(vm, &PlotViewModel::seriesAppearanceChanged, this, &PlotWidget::onSeriesAppearanceChanged);
    connect(vm, &PlotViewModel::axisRangeChanged, this, &PlotWidget::updateAxes);
    connect(vm, &PlotViewModel::plotTitleChanged, this, &PlotWidget::updateTitle);
    connect(vm, &PlotViewModel::lockAxisViewChanged, this, &PlotWidget::onLockAxisViewChanged);
    connect(vm, &PlotViewModel::sourcesChanged, this, &PlotWidget::populateSourceCombo);
    populateSourceCombo();
}

void PlotWidget::setLogTextProvider(std::function<QString()> provider)
{
    m_log_text_provider = std::move(provider);
}

void PlotWidget::applyTheme(bool dark)
{
    QColor bg = dark ? PlotConstants::kDarkBackground : PlotConstants::kLightBackground;
    QColor fg = dark ? PlotConstants::kDarkForeground : PlotConstants::kLightForeground;
    QColor grid = dark ? PlotConstants::kDarkGridColor : PlotConstants::kLightGridColor;
    m_title_color = dark ? QColor("#60CDFF") : QColor("#005FB8");

    m_plot->setBackground(QBrush(bg));
    m_plot->xAxis->setBasePen(QPen(fg));
    m_plot->yAxis->setBasePen(QPen(fg));
    m_plot->xAxis->setTickPen(QPen(fg));
    m_plot->yAxis->setTickPen(QPen(fg));
    m_plot->xAxis->setSubTickPen(QPen(fg));
    m_plot->yAxis->setSubTickPen(QPen(fg));
    m_plot->xAxis->setTickLabelColor(fg);
    m_plot->yAxis->setTickLabelColor(fg);
    m_plot->xAxis->setLabelColor(fg);
    m_plot->yAxis->setLabelColor(fg);
    m_plot->xAxis->grid()->setPen(QPen(grid, 0, Qt::DotLine));
    m_plot->yAxis->grid()->setPen(QPen(grid, 0, Qt::DotLine));

    m_plot->yAxis2->setBasePen(QPen(fg));
    m_plot->yAxis2->setTickPen(QPen(fg));
    m_plot->yAxis2->setSubTickPen(QPen(fg));
    m_plot->yAxis2->setTickLabelColor(fg);
    m_plot->yAxis2->setLabelColor(fg);

    // Update existing title element color
    if (m_plot->plotLayout()->elementCount() > 1)
    {
        QCPTextElement* title = qobject_cast<QCPTextElement*>(m_plot->plotLayout()->element(0, 0));
        if (title != nullptr)
        {
            title->setTextColor(m_title_color);
        }
    }

    m_dark_theme = dark;
    styleLegendOverlay(dark);

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}



void PlotWidget::rebuildChart()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;

    const auto& all_series = m_view_model->allSeries();

    // Reconcile graphs against the series list by stable series id: a stream
    // appended mid-run reuses every existing graph (no re-setData of unchanged
    // data) instead of clearing and recopying the whole chart. A given id's data
    // is immutable — reprocessing a stream yields new ids — so a reused graph never
    // needs its data re-copied; only color/visibility (cheap) are refreshed.
    QHash<int, QCPGraph*> next_by_id;
    next_by_id.reserve(static_cast<int>(all_series.size()));
    QVector<QCPGraph*> ordered;
    ordered.reserve(all_series.size());

    for (const PlotSeriesData& s : all_series)
    {
        QCPGraph* graph = m_graph_by_id.take(s.id); // reuse this id's graph if one exists
        if (graph == nullptr)
        {
            QCPAxis* value_axis = isLeftAxisMetric(s.metricType)
                ? m_plot->yAxis : m_plot->yAxis2;
            graph = m_plot->addGraph(m_plot->xAxis, value_axis);
            graph->setName(s.name);
            graph->setData(s.xValues, s.yValues, true);
        }
        // Color and visibility can change on append (the re-sort recolors left-axis
        // series) or on a metric toggle, so refresh them on every reconcile.
        graph->setPen(QPen(s.color, PlotConstants::kGraphPenWidth));
        graph->setVisible(m_view_model->effectiveVisible(s));
        next_by_id.insert(s.id, graph);
        ordered.append(graph);
    }

    // Graphs left in m_graph_by_id belong to series that are gone (reprocess
    // replaced them, or the data was cleared) — remove them from the chart.
    for (auto it = m_graph_by_id.cbegin(); it != m_graph_by_id.cend(); ++it)
    {
        m_plot->removeGraph(it.value());
    }
    m_graph_by_id = std::move(next_by_id);
    m_graphs = std::move(ordered);

    // Set axis labels. The left axis label tracks the active left-axis view.
    m_plot->xAxis->setLabel(PlotConstants::kXAxisLabel);
    m_plot->yAxis->setLabel(
        m_view_model->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames
            ? PlotConstants::kMissedFramesAxisLabel
            : PlotConstants::kYAxisLabel);

    // Update title and axes without triggering extra replots
    updateTitle();
    updateAxes();
    updateAxisViewCombo();

    // Enable all controls when data is loaded
    bool has_data = m_view_model->hasData();
    m_title_edit->setEnabled(has_data);
    m_x_start_edit->setEnabled(has_data);
    m_x_stop_edit->setEnabled(has_data);
    m_reset_btn->setEnabled(has_data);
    m_customize_btn->setEnabled(has_data);
    m_left_y_max_spin->setEnabled(has_data);
    m_right_y_max_spin->setEnabled(has_data);
    m_plot->setInteractions(has_data
        ? QCP::iRangeDrag | QCP::iRangeZoom
        : QCP::Interactions());

    if (has_data)
    {
        m_x_start_edit->setText(m_view_model->formatTime(m_view_model->xViewMin()));
        m_x_stop_edit->setText(m_view_model->formatTime(m_view_model->xViewMax()));
    }

    m_plot->replot(QCustomPlot::rpQueuedReplot);
    m_updating_from_vm = false;
}

void PlotWidget::onDataChanged()
{
    rebuildChart();
    rebuildLegend();
    populateSourceCombo();
}

void PlotWidget::populateSourceCombo()
{
    if (m_source_combo == nullptr || m_view_model == nullptr)
    {
        return;
    }

    const QVector<QPair<int, QString>> sources = m_view_model->sourceList();

    // Signals blocked so rebuilding the items doesn't fire activated() and stomp
    // the ViewModel's current filter.
    QSignalBlocker blocker(m_source_combo);
    m_source_combo->clear();

    // Only meaningful with more than one source (US1.1): a single file has nothing
    // to switch between.
    if (sources.size() <= 1)
    {
        if (sources.size() == 1)
            m_source_combo->addItem(sources.first().second, sources.first().first);
        m_source_combo->setEnabled(false);
        return;
    }

    m_source_combo->addItem(QStringLiteral("All files (overlaid)"), -1);
    for (const QPair<int, QString>& src : sources)
        m_source_combo->addItem(src.second, src.first);

    // Full label as a per-item tooltip so a name too long for the box is still readable.
    for (int i = 0; i < m_source_combo->count(); ++i)
        m_source_combo->setItemData(i, m_source_combo->itemText(i), Qt::ToolTipRole);

    const int idx = m_source_combo->findData(m_view_model->visibleSource());
    m_source_combo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_source_combo->setEnabled(true);
}

void PlotWidget::onSeriesVisibilityToggled(int index)
{
    if (m_view_model == nullptr)
    {
        return;
    }
    if (index < 0 || index >= m_graphs.size())
    {
        return;
    }

    m_graphs[index]->setVisible(m_view_model->effectiveVisible(m_view_model->seriesAt(index)));
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    rebuildLegend();
}

void PlotWidget::onSeriesAppearanceChanged()
{
    if (m_view_model == nullptr)
    {
        return;
    }
    // Re-apply per-series color and visibility to the graphs (names live only in
    // the legend/VM), then rebuild the legend rows. Axes/data are untouched, so no
    // rebuildChart. Visibility is included here because PlotCustomizationDialog
    // batches its checkbox edits through setSeriesVisibleQuiet() + this one signal
    // instead of one seriesVisibilityChanged() per checkbox. Looked up by stable
    // series id via m_graph_by_id (not by position in m_graphs) so this stays
    // correct even if a background streamProcessed()/addStreamData() added or
    // reordered series while this dialog-driven signal was in flight.
    for (const PlotSeriesData& s : m_view_model->allSeries())
    {
        QCPGraph* graph = m_graph_by_id.value(s.id, nullptr);
        if (graph != nullptr)
        {
            graph->setPen(QPen(s.color, PlotConstants::kGraphPenWidth));
            graph->setVisible(m_view_model->effectiveVisible(s));
        }
    }
    rebuildLegend();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::onLockAxisViewChanged()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    // The metric toggle only flips per-series visibility and the left-axis label;
    // the graphs and their data are unchanged, so sync visibility in place instead
    // of tearing down and rebuilding every QCPGraph. Axis ranges arrive separately
    // via axisRangeChanged -> updateAxes(). Looked up by stable series id via
    // m_graph_by_id (not by position in m_graphs) so this stays correct even if a
    // background streamProcessed()/addStreamData() added or reordered series while
    // this signal was in flight.
    for (const PlotSeriesData& s : m_view_model->allSeries())
    {
        QCPGraph* graph = m_graph_by_id.value(s.id, nullptr);
        if (graph != nullptr)
        {
            // Re-apply color as well as visibility: a custom recolor propagates to the
            // lock/missed sibling in the ViewModel, and that sibling first becomes
            // visible here, so its pen must be refreshed from the (updated) series color.
            graph->setPen(QPen(s.color, PlotConstants::kGraphPenWidth));
            graph->setVisible(m_view_model->effectiveVisible(s));
        }
    }

    m_plot->yAxis->setLabel(
        m_view_model->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames
            ? PlotConstants::kMissedFramesAxisLabel
            : PlotConstants::kYAxisLabel);

    updateAxisViewCombo();
    rebuildLegend();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::updateAxes()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;

    m_plot->xAxis->setRange(m_view_model->xViewMin(), m_view_model->xViewMax());

    // Left axis (yAxis): uses user override if set, else 100 for Lock % or auto for Missed Frames.
    const double left_max = m_view_model->leftYMax();
    m_plot->yAxis->setRange(0.0, left_max);

    // Right axis (yAxis2) auto-scales to SNR data limits, or manual/user-override limits
    m_plot->yAxis2->setRange(m_view_model->yMin(), m_view_model->yMax());

    // Sync spinboxes without triggering their valueChanged -> vm override cycle
    m_updating_from_vm = true;
    m_left_y_max_spin->setValue(left_max);
    m_right_y_max_spin->setValue(m_view_model->yMax());
    m_updating_from_vm = false;

    m_x_start_edit->setText(m_view_model->formatTime(m_view_model->xViewMin()));
    m_x_stop_edit->setText(m_view_model->formatTime(m_view_model->xViewMax()));

    m_plot->replot(QCustomPlot::rpQueuedReplot);
    m_updating_from_vm = false;
}

void PlotWidget::updateTitle()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;
    m_title_edit->setText(m_view_model->plotTitle());

    // Show title on chart using a QCPTextElement if one exists, else create one
    if (m_plot->plotLayout()->elementCount() > 1)
    {
        QCPTextElement* title = qobject_cast<QCPTextElement*>(m_plot->plotLayout()->element(0, 0));
        if (title != nullptr)
        {
            title->setText(m_view_model->plotTitle());
            title->setTextColor(m_title_color);
        }
    }
    else
    {
        QCPTextElement* title = new QCPTextElement(m_plot, m_view_model->plotTitle());
        title->setFont(QFont("sans", PlotConstants::kTitleFontSize, QFont::Bold));
        title->setTextColor(m_title_color);
        m_plot->plotLayout()->insertRow(0);
        m_plot->plotLayout()->addElement(0, 0, title);
    }
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    m_updating_from_vm = false;
}

void PlotWidget::onCustomizePlotClicked()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    PlotCustomizationDialog dialog(m_view_model, this);
    dialog.exec();
}

void PlotWidget::onXRangeChanged()
{
    if (m_updating_from_vm || m_view_model == nullptr)
    {
        return;
    }

    const double data_min = m_view_model->xMin();
    const double data_max = m_view_model->xMax();

    const QString entered_start = m_x_start_edit->text();
    const QString entered_stop  = m_x_stop_edit->text();

    double raw_start = m_view_model->parseTime(entered_start);
    double raw_stop  = m_view_model->parseTime(entered_stop);

    double start = qBound(data_min, raw_start, data_max);
    double stop  = qBound(data_min, raw_stop,  data_max);

    // Warn if either value was clamped to the file bounds
    if (!qFuzzyCompare(start, raw_start))
    {
        emit logMessage(QString("<span style='color:#DAA520;'>Start \"%1\" is outside the file time range — clamped to file bounds.</span>")
                        .arg(entered_start));
    }
    if (!qFuzzyCompare(stop, raw_stop))
    {
        emit logMessage(QString("<span style='color:#DAA520;'>Stop \"%1\" is outside the file time range — clamped to file bounds.</span>")
                        .arg(entered_stop));
    }

    // Enforce start <= stop; clamp whichever field was just edited
    if (start > stop)
    {
        if (m_x_start_edit == focusWidget() || m_x_start_edit->hasFocus())
        {
            emit logMessage("<span style='color:#DAA520;'>Start time is after Stop — clamped to stop time.</span>");
            start = stop;
        }
        else
        {
            emit logMessage("<span style='color:#DAA520;'>Stop time is before Start — clamped to start time.</span>");
            stop = start;
        }
    }

    // Write clamped values back so the user sees what was applied
    m_updating_from_vm = true;
    m_x_start_edit->setText(m_view_model->formatTime(start));
    m_x_stop_edit->setText(m_view_model->formatTime(stop));
    m_updating_from_vm = false;

    m_view_model->setXViewRange(start, stop);
}

void PlotWidget::onResetAxes()
{
    if (m_view_model == nullptr)
    {
        return;
    }
    m_view_model->resetXRange();
    m_view_model->resetYRange();
}

void PlotWidget::onExportPlot()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    QSettings settings;
    QString last_dir = settings.value(UIConstants::kSettingsKeyLastCh10Dir, QCoreApplication::applicationDirPath()).toString();

    QString default_name = m_view_model->plotTitle().isEmpty() ? "plot" : m_view_model->plotTitle();
    default_name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString base_path = QDir(last_dir).filePath(default_name);

    ExportDialog dialog(base_path + ".csv", base_path + ".png", base_path + "_log.txt", this);
    if (dialog.exec() == QDialog::Accepted)
    {
        if (dialog.exportCsv())
        {
            if (m_view_model->exportCsv(dialog.csvPath()))
            {
                emit logMessage(QString("<span style='color:green;'>Data exported to <a href='file:///%1'>%1</a></span>")
                                .arg(dialog.csvPath()));
            }
            else
            {
                emit logMessage(QString("<span style='color:red;'>Error: Failed to export data to %1</span>")
                                .arg(dialog.csvPath()));
            }
        }

        if (dialog.exportImage())
        {
            exportImage(dialog.imagePath());
        }

        if (dialog.exportLog())
        {
            const QString log_path = dialog.logPath();
            const QString log_text = m_log_text_provider ? m_log_text_provider() : QString();

            QFile file(log_path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream stream(&file);
                stream << log_text;
                if (!log_text.endsWith('\n'))
                {
                    stream << '\n';
                }
                file.close();
                emit logMessage(QString("<span style='color:green;'>Log exported to <a href='file:///%1'>%1</a></span>")
                                .arg(log_path));
            }
            else
            {
                emit logMessage(QString("<span style='color:red;'>Error: Failed to export log to %1</span>")
                                .arg(log_path));
            }
        }
    }
}

bool PlotWidget::exportImage(const QString& path)
{
    if (m_view_model == nullptr)
    {
        return false;
    }

    QString filename = path;
    QString suffix = QFileInfo(filename).suffix().toLower();
    bool success = false;
    QString formatStr;

    if (suffix == "png")
    {
        // The legend floats over the chart as a child widget, so render it
        // onto the plot pixmap at its on-screen position (WYSIWYG — if the
        // legend is scrolled, the exported view matches).
        QPixmap px = m_plot->toPixmap(m_plot->width(), m_plot->height());
        if (m_legend_overlay != nullptr && m_legend_overlay->isVisible())
        {
            QPainter painter(&px);
            m_legend_overlay->render(&painter, m_legend_overlay->pos());
            painter.end();
        }
        success = px.save(filename, "PNG");
        formatStr = "PNG";
    }
    else if (suffix == "svg")
    {
        QSvgGenerator generator;
        generator.setFileName(filename);
        generator.setSize(m_plot->size());
        generator.setViewBox(QRect(0, 0, m_plot->width(), m_plot->height()));
        generator.setTitle(m_view_model->plotTitle());
        generator.setDescription("Generated by tmDataQualityAnalyzer");

        QCPPainter painter;
        success = painter.begin(&generator);
        if (success)
        {
            m_plot->toPainter(&painter);
            if (m_legend_overlay != nullptr && m_legend_overlay->isVisible())
            {
                m_legend_overlay->render(&painter, m_legend_overlay->pos());
            }
            painter.end();
        }
        formatStr = "SVG";
    }
    else
    {
        // Default to PDF
        if (!filename.endsWith(".pdf", Qt::CaseInsensitive))
        {
            filename += ".pdf";
        }
        success = m_plot->savePdf(filename);
        formatStr = "PDF";
    }

    if (success)
    {
        emit logMessage(QString("<span style='color:green;'>Plot exported as %1 to <a href='file:///%2'>%2</a></span>")
                        .arg(formatStr)
                        .arg(filename));
    }
    else
    {
        emit logMessage(QString("<span style='color:red;'>Error: Failed to export plot to %1</span>")
                        .arg(filename));
    }
    return success;
}

void PlotWidget::handlePlotXRangeChanged(double lower, double upper)
{
    if (m_updating_from_vm || m_view_model == nullptr)
    {
        return;
    }

    double x_max = m_view_model->xMax();
    double width = upper - lower;

    if (lower < 0.0)
    {
        lower = 0.0;
        upper = width;
    }
    if (upper > x_max)
    {
        upper = x_max;
        lower = qMax(0.0, x_max - width);
    }

    m_view_model->setXViewRange(lower, upper);
}

void PlotWidget::setUpLayout()
{
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(4, 8, 4, 4);
    main_layout->setSpacing(0);

    // --- Title row (Plot File | Plot Title | View Mode) ---
    auto* title_bar = new QHBoxLayout;
    title_bar->setSpacing(6);

    // Plot File (US1.1): which processed file to view after a multi-file batch.
    // Disabled unless more than one source is loaded.
    title_bar->addWidget(new QLabel(QStringLiteral("Plot File:")));
    m_source_combo = new QComboBox;
    m_source_combo->setToolTip(QStringLiteral("Select which processed file to view"));
    m_source_combo->setEnabled(false);
    m_source_combo->setMinimumWidth(160);
    m_source_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    title_bar->addWidget(m_source_combo);

    title_bar->addSpacing(20);

    title_bar->addWidget(new QLabel(QStringLiteral("Plot Title:")));
    m_title_edit = new QLineEdit;
    m_title_edit->setPlaceholderText(PlotConstants::kDefaultPlotTitle);
    m_title_edit->setToolTip("Plot title");
    m_title_edit->setEnabled(false);
    m_title_edit->setMinimumWidth(260);
    title_bar->addWidget(m_title_edit);

    // Empty space between the title and the right-aligned View Mode selector.
    title_bar->addStretch(1);

    // View Mode: the left axis metric — Lock % or Accumulation (missed frames).
    title_bar->addWidget(new QLabel(QStringLiteral("View Mode")));
    m_axis_view_combo = new QComboBox;
    m_axis_view_combo->setToolTip(QStringLiteral("Left axis metric: Frame Sync Lock % or "
                                                 "Accumulated Missed Frames"));
    m_axis_view_combo->addItem(QStringLiteral("Lock %"),
                               static_cast<int>(PlotViewModel::LockAxisView::LockPercent));
    m_axis_view_combo->addItem(QStringLiteral("Accumulation"),
                               static_cast<int>(PlotViewModel::LockAxisView::MissedFrames));
    m_axis_view_combo->setEnabled(false);
    m_axis_view_combo->setMinimumWidth(140);
    title_bar->addWidget(m_axis_view_combo);

    main_layout->addLayout(title_bar);
    main_layout->addSpacing(8);

    // --- QCustomPlot chart ---
    m_plot = new QCustomPlot(this);
    m_plot->setInteractions(QCP::Interactions());
    m_plot->axisRect()->setRangeDrag(Qt::Horizontal);
    m_plot->axisRect()->setRangeZoom(Qt::Horizontal);
    m_plot->xAxis->setLabel(PlotConstants::kXAxisLabel);
    m_plot->yAxis->setLabel(PlotConstants::kYAxisLabel);
    m_plot->yAxis->setRange(0, 100);
    m_plot->yAxis2->setVisible(true);
    m_plot->yAxis2->setLabel(PlotConstants::kSnrAxisLabel);
    m_plot->setMinimumHeight(PlotConstants::kPlotMinChartHeight);
    main_layout->addWidget(m_plot, 1);
    main_layout->addSpacing(4);

    // --- Movable legend overlay (floats over the chart interior) ---
    // A translucent, rounded frame parented to m_plot so it composites over the
    // chart. Holds a single-column, vertically scrolling list of line-swatch +
    // label rows; the user can drag it anywhere inside the plot (see eventFilter).
    m_legend_overlay = new QFrame(m_plot);
    m_legend_overlay->setObjectName("legendOverlay");
    m_legend_overlay->setCursor(Qt::OpenHandCursor);
    auto* overlay_layout = new QVBoxLayout(m_legend_overlay);
    overlay_layout->setContentsMargins(PlotConstants::kLegendContentMargin,
                                       PlotConstants::kLegendContentMargin,
                                       PlotConstants::kLegendContentMargin,
                                       PlotConstants::kLegendContentMargin);
    overlay_layout->setSpacing(0);

    m_legend_widget = new QWidget;
    m_legend_widget->setObjectName("legendContent");
    m_legend_rows = new QVBoxLayout(m_legend_widget);
    // Reserve a permanent right-side gutter matching the style's scrollbar width so
    // the vertical scrollbar — shown only once content overflows the height cap —
    // never sits on top of the last few characters of a row's label. Always-on
    // rather than conditional: whether a row's text runs into that gutter isn't
    // knowable until the scrollbar decision is made, so a fixed reservation is the
    // only way to guarantee no overlap regardless of content length.
    const int scrollbar_gutter = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    m_legend_rows->setContentsMargins(0, 0, scrollbar_gutter, 0);
    m_legend_rows->setSpacing(PlotConstants::kLegendRowSpacing);

    m_legend_scroll = new QScrollArea(m_legend_overlay);
    m_legend_scroll->setObjectName("legendScroll");
    m_legend_scroll->setWidget(m_legend_widget);
    m_legend_scroll->setWidgetResizable(true);
    m_legend_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_legend_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_legend_scroll->setFrameShape(QFrame::NoFrame);
    m_legend_scroll->viewport()->setAutoFillBackground(false);
    m_legend_scroll->viewport()->setCursor(Qt::OpenHandCursor);
    overlay_layout->addWidget(m_legend_scroll);

    // Drag wiring: each row is click-through (WA_TransparentForMouseEvents set in
    // rebuildLegend), so content presses fall to the viewport; the frame catches
    // presses on its padding. Both — plus m_plot resizes — route to eventFilter().
    m_legend_overlay->installEventFilter(this);
    m_legend_scroll->viewport()->installEventFilter(this);
    m_plot->installEventFilter(this);
    m_legend_overlay->hide();

    // Loading overlay (child of m_plot so it floats over the chart)
    m_loading_label = new QLabel("Loading...", m_plot);
    m_loading_label->setAlignment(Qt::AlignCenter);
    m_loading_label->setObjectName("LoadingLabel");
    m_loading_label->adjustSize();
    m_loading_label->hide();

    // --- Bottom bar: [axis controls] [16px] [legend panel] [stretch] [Export PDF] ---
    auto* bottom_bar = new QHBoxLayout;
    bottom_bar->setContentsMargins(0, 0, 0, 0);
    bottom_bar->setSpacing(0);

    // Axis controls grid (Start/Stop + Reset + Y-max spinboxes), top-aligned in the bar
    auto* axis_grid = new QGridLayout;
    axis_grid->setContentsMargins(0, 0, 0, 0);
    axis_grid->setHorizontalSpacing(4);
    axis_grid->setVerticalSpacing(4);
    // Col 2 is a dedicated 8px spacer between the first control and second label pair
    axis_grid->setColumnMinimumWidth(2, 8);

    axis_grid->addWidget(new QLabel("Start:"), 0, 0);
    m_x_start_edit = new QLineEdit;
    m_x_start_edit->setPlaceholderText("DDD:HH:MM:SS");
    m_x_start_edit->setToolTip("Start time (DDD:HH:MM:SS)");
    m_x_start_edit->setEnabled(false);
    axis_grid->addWidget(m_x_start_edit, 0, 1);

    axis_grid->addWidget(new QLabel("Stop:"), 0, 3);
    m_x_stop_edit = new QLineEdit;
    m_x_stop_edit->setPlaceholderText("DDD:HH:MM:SS");
    m_x_stop_edit->setToolTip("Stop time (DDD:HH:MM:SS)");
    m_x_stop_edit->setEnabled(false);
    axis_grid->addWidget(m_x_stop_edit, 0, 4);

    m_reset_btn = new QPushButton("Reset");
    m_reset_btn->setFlat(true);
    m_reset_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    m_reset_btn->setToolTip("Reset axes to auto range");
    m_reset_btn->setEnabled(false);
    axis_grid->addWidget(m_reset_btn, 0, 5);

    // Row 1: Left and right y-axis maximum overrides
    axis_grid->setColumnMinimumWidth(7, 8);

    axis_grid->addWidget(new QLabel("L Max:"), 1, 0);
    m_left_y_max_spin = new QDoubleSpinBox;
    m_left_y_max_spin->setRange(1.0, 10'000'000.0);
    m_left_y_max_spin->setDecimals(0);
    m_left_y_max_spin->setSingleStep(5.0);
    m_left_y_max_spin->setValue(100.0);
    m_left_y_max_spin->setToolTip("Maximum value for the left y-axis (lock % or missed frames). Use the arrows or type a value.");
    m_left_y_max_spin->setEnabled(false);
    axis_grid->addWidget(m_left_y_max_spin, 1, 1);

    axis_grid->addWidget(new QLabel("R Max:"), 1, 3);
    m_right_y_max_spin = new QDoubleSpinBox;
    m_right_y_max_spin->setRange(1.0, 10'000.0);
    m_right_y_max_spin->setDecimals(1);
    m_right_y_max_spin->setSingleStep(5.0);
    m_right_y_max_spin->setValue(100.0);
    m_right_y_max_spin->setToolTip("Maximum value for the right y-axis (SNR in dB). Use the arrows or type a value.");
    m_right_y_max_spin->setEnabled(false);
    axis_grid->addWidget(m_right_y_max_spin, 1, 4);

    QWidget* axis_widget = new QWidget;
    axis_widget->setLayout(axis_grid);
    axis_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    bottom_bar->addWidget(axis_widget, 0, Qt::AlignTop);

    bottom_bar->addStretch(1);

    m_customize_btn = new QPushButton("Customize Plot...");
    m_customize_btn->setEnabled(false);
    bottom_bar->addWidget(m_customize_btn, 0, Qt::AlignTop);

    main_layout->addLayout(bottom_bar);
}

void PlotWidget::setUpConnections()
{
    connect(m_title_edit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_updating_from_vm && m_view_model != nullptr)
        {
            m_view_model->setPlotTitle(m_title_edit->text());
        }
    });



    connect(m_x_start_edit, &QLineEdit::editingFinished, this, &PlotWidget::onXRangeChanged);
    connect(m_x_stop_edit, &QLineEdit::editingFinished, this, &PlotWidget::onXRangeChanged);

    connect(m_reset_btn, &QPushButton::clicked, this, &PlotWidget::onResetAxes);
    connect(m_customize_btn, &QPushButton::clicked, this, &PlotWidget::onCustomizePlotClicked);

    connect(m_left_y_max_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (!m_updating_from_vm && m_view_model != nullptr)
                    m_view_model->setLeftYMaxOverride(value);
            });
    connect(m_right_y_max_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (!m_updating_from_vm && m_view_model != nullptr)
                    m_view_model->setRightYMaxOverride(value);
            });
    // View Mode: user picks the left-axis metric (data = LockAxisView).
    connect(m_axis_view_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        if (m_view_model != nullptr)
            m_view_model->setLockAxisView(
                static_cast<PlotViewModel::LockAxisView>(m_axis_view_combo->currentData().toInt()));
    });
    // Plot File: user picks which processed file to view (data = sourceId; -1 = all).
    connect(m_source_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        if (m_view_model != nullptr)
            m_view_model->setVisibleSource(m_source_combo->currentData().toInt());
    });
    connect(m_plot, &QCustomPlot::mouseMove, this, &PlotWidget::onPlotMouseMove);

    connect(m_plot->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& range) { handlePlotXRangeChanged(range.lower, range.upper); });
}

////////////////////////////////////////////////////////////////////////////////
// Legend overlay (movable box floating over the chart)
////////////////////////////////////////////////////////////////////////////////

void PlotWidget::rebuildLegend()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        for (const LegendRow& row : std::as_const(m_legend_row_by_id))
        {
            m_legend_rows->removeWidget(row.widget);
            row.widget->deleteLater();
        }
        m_legend_row_by_id.clear();
        m_legend_overlay->hide();
        return;
    }

    const auto& all_series = m_view_model->allSeries();
    const PlotViewModel::LockAxisView axis_view = m_view_model->lockAxisView();
    int shown = 0;
    // Track the content's natural (unconstrained) size ourselves from each row's
    // sizeHint() as it's built, rather than asking the container widget for its
    // aggregate sizeHint() afterwards: once the QScrollArea (widgetResizable=true)
    // has squeezed m_legend_widget down to a small viewport on an earlier, sparser
    // rebuild, QWidget::sizeHint()/QLayout::sizeHint() can report a stale/zero size
    // on the next rebuild even though the layout correctly holds the new rows —
    // this was reproduced directly (row sizeHints valid, aggregate sizeHint (0,0)).
    int content_w = 0;
    int content_h = 0;

    // Reconciled by series id (mirrors rebuildChart()'s m_graph_by_id): a run with
    // many streams calls rebuildLegend() once per completed stream, so rebuilding
    // every row's widgets from scratch each time is O(streams^2) widget churn
    // across the run. Existing rows are updated in place and only reordered in
    // the layout (cheap bookkeeping); only genuinely new/removed series pay for
    // widget construction/destruction.
    QHash<int, LegendRow> next_row_by_id;
    next_row_by_id.reserve(static_cast<int>(all_series.size()));

    for (const PlotSeriesData& s : all_series)
    {
        // Only show visible series; skip whichever lock metric is not active so the
        // legend matches exactly what is drawn (and honor the active source filter).
        if (!m_view_model->effectiveVisible(s))
        {
            continue;
        }
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock &&
            axis_view != PlotViewModel::LockAxisView::LockPercent)
        {
            continue;
        }
        if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames &&
            axis_view != PlotViewModel::LockAxisView::MissedFrames)
        {
            continue;
        }

        LegendRow row = m_legend_row_by_id.take(s.id);
        if (row.widget == nullptr)
        {
            // Read-only row: [line swatch][label]. Both are click-through so a press
            // anywhere on the content falls to the viewport and starts a drag; color
            // and name are edited from the Customize Plot Series dialog.
            row.widget = new QWidget;
            row.widget->setObjectName("legendRow");
            row.widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            auto* row_layout = new QHBoxLayout(row.widget);
            row_layout->setContentsMargins(0, 0, 0, 0);
            row_layout->setSpacing(6);

            row.swatch = new QLabel;
            row.swatch->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            row.swatch->setFixedSize(PlotConstants::kLegendSwatchLen, PlotConstants::kLegendSwatchThick);

            row.label = new QLabel;
            row.label->setObjectName("legendLabel");
            row.label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

            row_layout->addWidget(row.swatch, 0, Qt::AlignVCenter);
            row_layout->addWidget(row.label, 1, Qt::AlignVCenter);
        }
        row.swatch->setStyleSheet(QString("background-color: %1; border-radius: 1px;").arg(s.color.name()));
        row.label->setText(legendDisplayName(s));

        // Re-add regardless of whether the row is new or reused: removeWidget() is
        // a no-op bookkeeping call for a widget not currently in the layout, and
        // this guarantees row order always matches all_series order (which can
        // shift — addStreamData() re-sorts by job submission index as parallel
        // streams complete out of order) without destroying/recreating widgets.
        m_legend_rows->removeWidget(row.widget);
        m_legend_rows->addWidget(row.widget);
        next_row_by_id.insert(s.id, row);

        const QSize row_hint = row.widget->sizeHint();
        content_w = qMax(content_w, row_hint.width());
        content_h += row_hint.height();
        if (shown > 0)
        {
            content_h += PlotConstants::kLegendRowSpacing;
        }
        ++shown;
    }

    // Rows left in m_legend_row_by_id belong to series that are no longer shown
    // (hidden, reprocessed away, or axis-view switched) — remove them.
    for (const LegendRow& row : std::as_const(m_legend_row_by_id))
    {
        m_legend_rows->removeWidget(row.widget);
        row.widget->deleteLater();
    }
    m_legend_row_by_id = std::move(next_row_by_id);

    if (shown == 0)
    {
        m_legend_overlay->hide();
        return;
    }

    // m_legend_rows carries a permanent right-side gutter (set in setUpLayout, same
    // pixelMetric) so the vertical scrollbar never overlaps a row's text; include it
    // here so the overlay is sized to fit that reserved space rather than clip it.
    const int scrollbar_gutter = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    styleLegendOverlay(m_dark_theme);
    layoutLegendOverlay(QSize(content_w + scrollbar_gutter, content_h));
    m_legend_overlay->show();
    m_legend_overlay->raise();
}

void PlotWidget::layoutLegendOverlay(const QSize& content)
{
    if (m_legend_overlay == nullptr || m_plot == nullptr)
    {
        return;
    }

    const int frame = 2 * PlotConstants::kLegendContentMargin;

    // Cap the legend to a fraction of the chart so a dense plot can't let it grow
    // to swallow the data; overflow past the height cap scrolls. The scrollbar
    // gutter is already folded into content's width (see rebuildLegend), so no
    // further reservation is needed here even when the height cap forces scrolling.
    const int max_w = qMax(80, static_cast<int>(m_plot->width()  * PlotConstants::kLegendMaxWidthFrac));
    const int max_h = qMax(60, static_cast<int>(m_plot->height() * PlotConstants::kLegendMaxHeightFrac));

    const int w = qMin(content.width() + frame, max_w);
    const int h = qMin(content.height() + frame, max_h);
    m_legend_overlay->resize(w, h);

    // Keep the default top-right anchor until the user drags it; afterwards just
    // keep it inside the (possibly resized) chart.
    if (m_legend_user_moved)
    {
        clampLegendIntoView();
    }
    else
    {
        positionLegendTopRight();
    }
}

void PlotWidget::positionLegendTopRight()
{
    if (m_legend_overlay == nullptr || m_plot == nullptr)
    {
        return;
    }
    const int m = PlotConstants::kLegendMarginPx;
    const int x = m_plot->width() - m_legend_overlay->width() - m;
    m_legend_overlay->move(qMax(m, x), m);
}

void PlotWidget::clampLegendIntoView()
{
    if (m_legend_overlay == nullptr || m_plot == nullptr)
    {
        return;
    }
    const int m = PlotConstants::kLegendMarginPx;
    const int max_x = qMax(m, m_plot->width()  - m_legend_overlay->width()  - m);
    const int max_y = qMax(m, m_plot->height() - m_legend_overlay->height() - m);
    QPoint p = m_legend_overlay->pos();
    p.setX(qBound(m, p.x(), max_x));
    p.setY(qBound(m, p.y(), max_y));
    m_legend_overlay->move(p);
}

void PlotWidget::styleLegendOverlay(bool dark)
{
    if (m_legend_overlay == nullptr)
    {
        return;
    }
    const QColor bg     = dark ? QColor(32, 32, 32, PlotConstants::kLegendBgAlpha)
                               : QColor(255, 255, 255, PlotConstants::kLegendBgAlpha);
    const QColor border = dark ? QColor(90, 90, 90) : QColor(170, 170, 170);
    const QColor text   = dark ? QColor(230, 230, 230) : QColor(30, 30, 30);
    // The app's global theme QSS (resources/win11-{dark,light}.qss) gives every
    // plain QWidget a solid background-color, so each legend row would otherwise
    // paint as an opaque chip against the overlay's own translucent background
    // (a boxed/tabular look). QWidget#legendRow overrides that with a more
    // specific id selector so only the swatch + text show through.
    m_legend_overlay->setStyleSheet(QString(
        "QFrame#legendOverlay { background-color: rgba(%1,%2,%3,%4);"
        " border: 1px solid %5; border-radius: %6px; }"
        "QScrollArea#legendScroll { background: transparent; border: none; }"
        "QWidget#legendContent { background: transparent; }"
        "QWidget#legendRow { background: transparent; }"
        "QLabel#legendLabel { background: transparent; color: %7; }")
        .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
        .arg(border.name())
        .arg(PlotConstants::kLegendCornerRadius)
        .arg(text.name()));
}

void PlotWidget::onPlotMouseMove(QMouseEvent* event)
{
    if (m_view_model == nullptr || !m_view_model->hasData() || m_graphs.isEmpty())
    {
        return;
    }

    const double x_coord = m_plot->xAxis->pixelToCoord(event->pos().x());

    // Find the nearest visible data point across all graphs
    double best_dist = std::numeric_limits<double>::max();
    double best_x = 0.0;
    double best_y = 0.0;
    QString best_name;
    int best_series_index = -1;

    const auto& all_series = m_view_model->allSeries();
    for (int i = 0; i < m_graphs.size() && i < static_cast<int>(all_series.size()); i++)
    {
        if (!m_view_model->effectiveVisible(all_series[i]))
        {
            continue;
        }
        const auto& xs = all_series[i].xValues;
        if (xs.isEmpty())
        {
            continue;
        }
        // Binary search for nearest x
        auto it = std::lower_bound(xs.constBegin(), xs.constEnd(), x_coord);
        for (auto check = it; check != xs.constEnd() && check - it < 2; ++check)
        {
            double dist = qAbs(*check - x_coord);
            if (dist < best_dist)
            {
                best_dist = dist;
                int idx = static_cast<int>(check - xs.constBegin());
                best_x = xs[idx];
                best_y = all_series[i].yValues[idx];
                best_name = all_series[i].name;
                best_series_index = i;
            }
        }
        if (it != xs.constBegin())
        {
            --it;
            double dist = qAbs(*it - x_coord);
            if (dist < best_dist)
            {
                best_dist = dist;
                int idx = static_cast<int>(it - xs.constBegin());
                best_x = xs[idx];
                best_y = all_series[i].yValues[idx];
                best_name = all_series[i].name;
                best_series_index = i;
            }
        }
    }

    // Only show tooltip if the nearest point is within 10 pixels
    const double pixel_dist = qAbs(m_plot->xAxis->coordToPixel(best_x) - event->pos().x());
    if (pixel_dist <= 10.0 && !best_name.isEmpty() && best_series_index >= 0)
    {
        const PlotSeriesData::MetricType metric = all_series[best_series_index].metricType;
        QString unit;
        switch (metric)
        {
            case PlotSeriesData::MetricType::FrameSyncLock:         unit = "%";        break;
            case PlotSeriesData::MetricType::AccumulatedMissedFrames: unit = " frames"; break;
            case PlotSeriesData::MetricType::SNR:                   unit = " dB";      break;
        }
        QString tip = QString("%1\n%2\n%3%4")
            .arg(best_name)
            .arg(m_view_model->formatTime(best_x))
            .arg(QString::number(best_y, 'f', 2))
            .arg(unit);
        QToolTip::showText(event->globalPosition().toPoint(), tip, m_plot);
    }
    else
    {
        QToolTip::hideText();
    }
}

void PlotWidget::updateAxisViewCombo()
{
    if (m_axis_view_combo == nullptr || m_view_model == nullptr)
    {
        return;
    }

    // The View Mode selector is only meaningful when both left-axis metrics exist
    // (a frame-sync stream produces both); SNR-only data leaves it disabled.
    const bool enabled = m_view_model->hasLockSeries() && m_view_model->hasMissedFramesSeries();
    m_axis_view_combo->setEnabled(enabled);

    // Sync the selection to the ViewModel's active view without firing activated().
    {
        QSignalBlocker blocker(m_axis_view_combo);
        const int idx = m_axis_view_combo->findData(static_cast<int>(m_view_model->lockAxisView()));
        if (idx >= 0)
            m_axis_view_combo->setCurrentIndex(idx);
    }

    if (enabled)
    {
        // Adjust the left spinbox range and step to match the active axis metric.
        const bool is_lock_pct = (m_view_model->lockAxisView() == PlotViewModel::LockAxisView::LockPercent);
        m_updating_from_vm = true;
        if (is_lock_pct)
        {
            m_left_y_max_spin->setRange(1.0, 100.0);
            m_left_y_max_spin->setSingleStep(5.0);
        }
        else
        {
            m_left_y_max_spin->setRange(1.0, 10'000'000.0);
            m_left_y_max_spin->setSingleStep(100.0);
        }
        m_left_y_max_spin->setValue(m_view_model->leftYMax());
        m_updating_from_vm = false;
    }
}

void PlotWidget::showLoadingIndicator(bool visible)
{
    if (!visible)
    {
        m_loading_label->hide();
        return;
    }
    m_loading_label->adjustSize();
    m_loading_label->move((m_plot->width()  - m_loading_label->width())  / 2,
                          (m_plot->height() - m_loading_label->height()) / 2);
    m_loading_label->raise();
    m_loading_label->show();
}

void PlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_loading_label != nullptr && m_loading_label->isVisible())
    {
        showLoadingIndicator(true);  // re-centre on resize
    }
}

bool PlotWidget::eventFilter(QObject* watched, QEvent* event)
{
    // Keep the legend inside the chart when the chart itself resizes.
    if (watched == m_plot && event->type() == QEvent::Resize
        && m_legend_overlay != nullptr && m_legend_overlay->isVisible())
    {
        if (m_legend_user_moved)
        {
            clampLegendIntoView();
        }
        else
        {
            positionLegendTopRight();
        }
        return false; // let QCustomPlot handle its own resize too
    }

    // Drag the legend: content rows are click-through, so presses arrive on the
    // viewport; presses on the frame padding arrive on the frame itself.
    const bool on_legend = (watched == m_legend_overlay)
        || (m_legend_scroll != nullptr && watched == m_legend_scroll->viewport());
    if (on_legend && m_legend_overlay != nullptr && m_legend_overlay->isVisible())
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
            {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton)
                {
                    m_dragging_legend   = true;
                    m_drag_start_global = me->globalPosition().toPoint();
                    m_legend_start_pos  = m_legend_overlay->pos();
                    m_legend_overlay->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
                break;
            }
            case QEvent::MouseMove:
            {
                if (m_dragging_legend)
                {
                    auto* me = static_cast<QMouseEvent*>(event);
                    const QPoint delta = me->globalPosition().toPoint() - m_drag_start_global;
                    m_legend_overlay->move(m_legend_start_pos + delta);
                    clampLegendIntoView();
                    m_legend_user_moved = true;
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonRelease:
            {
                if (m_dragging_legend)
                {
                    m_dragging_legend = false;
                    m_legend_overlay->setCursor(Qt::OpenHandCursor);
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
