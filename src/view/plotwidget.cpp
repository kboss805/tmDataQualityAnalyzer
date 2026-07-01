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
#include <QScrollBar>
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
        graph->setVisible(s.visible);
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
    updateAxisViewButton();

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

    m_graphs[index]->setVisible(m_view_model->seriesAt(index).visible);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    rebuildLegend();
}

void PlotWidget::onSeriesAppearanceChanged()
{
    if (m_view_model == nullptr)
    {
        return;
    }
    // Re-apply per-series colors to the graphs (names live only in the legend/VM),
    // then rebuild the legend rows. Axes and data are untouched, so no rebuildChart.
    const QVector<PlotSeriesData>& all_series = m_view_model->allSeries();
    const qsizetype count = qMin(m_graphs.size(), all_series.size());
    for (qsizetype i = 0; i < count; i++)
    {
        m_graphs[i]->setPen(QPen(all_series[i].color, PlotConstants::kGraphPenWidth));
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
    // via axisRangeChanged -> updateAxes().
    const QVector<PlotSeriesData>& all_series = m_view_model->allSeries();
    const qsizetype count = qMin(m_graphs.size(), all_series.size());
    for (qsizetype i = 0; i < count; i++)
    {
        // Re-apply color as well as visibility: a custom recolor propagates to the
        // lock/missed sibling in the ViewModel, and that sibling first becomes
        // visible here, so its pen must be refreshed from the (updated) series color.
        m_graphs[i]->setPen(QPen(all_series[i].color, PlotConstants::kGraphPenWidth));
        m_graphs[i]->setVisible(all_series[i].visible);
    }

    m_plot->yAxis->setLabel(
        m_view_model->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames
            ? PlotConstants::kMissedFramesAxisLabel
            : PlotConstants::kYAxisLabel);

    updateAxisViewButton();
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
            QString filename = dialog.imagePath();
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

    // --- Title row ---
    auto* title_bar = new QHBoxLayout;
    title_bar->addWidget(new QLabel("Title:"));
    m_title_edit = new QLineEdit;
    m_title_edit->setPlaceholderText(PlotConstants::kDefaultPlotTitle);
    m_title_edit->setToolTip("Plot title");
    m_title_edit->setEnabled(false);
    title_bar->addWidget(m_title_edit, 1);

    // Left-axis mode toggle button: switches between the Lock % metric and the
    // accumulated Missed Frames metric (both share the left axis).
    m_axis_view_btn = new QPushButton(QStringLiteral("Frame lock percentage"));
    m_axis_view_btn->setToolTip(QStringLiteral("Toggle left axis between Frame lock percentage "
                                "and Missed frames"));
    m_axis_view_btn->setEnabled(false);
    title_bar->addWidget(m_axis_view_btn);

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
    m_legend_rows->setContentsMargins(0, 0, 0, 0);
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
    connect(m_axis_view_btn, &QPushButton::clicked, this, &PlotWidget::onAxisViewToggleClicked);
    connect(m_plot, &QCustomPlot::mouseMove, this, &PlotWidget::onPlotMouseMove);

    connect(m_plot->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& range) { handlePlotXRangeChanged(range.lower, range.upper); });
}

////////////////////////////////////////////////////////////////////////////////
// Legend overlay (movable box floating over the chart)
////////////////////////////////////////////////////////////////////////////////

void PlotWidget::rebuildLegend()
{
    // Clear existing rows.
    QLayoutItem* item;
    while ((item = m_legend_rows->takeAt(0)) != nullptr)
    {
        if (item->widget() != nullptr)
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        m_legend_overlay->hide();
        return;
    }

    const auto& all_series = m_view_model->allSeries();
    const PlotViewModel::LockAxisView axis_view = m_view_model->lockAxisView();
    int shown = 0;

    for (const PlotSeriesData& s : all_series)
    {
        // Only show visible series; skip whichever lock metric is not active so the
        // legend matches exactly what is drawn.
        if (!s.visible)
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

        // Read-only row: [line swatch][label]. Both are click-through so a press
        // anywhere on the content falls to the viewport and starts a drag; color
        // and name are edited from the Customize Plot Series dialog.
        auto* row = new QWidget;
        row->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(6);

        auto* swatch = new QLabel;
        swatch->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        swatch->setFixedSize(PlotConstants::kLegendSwatchLen, PlotConstants::kLegendSwatchThick);
        swatch->setStyleSheet(QString("background-color: %1; border-radius: 1px;").arg(s.color.name()));

        auto* name = new QLabel(s.name);
        name->setObjectName("legendLabel");
        name->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        row_layout->addWidget(swatch, 0, Qt::AlignVCenter);
        row_layout->addWidget(name, 1, Qt::AlignVCenter);
        m_legend_rows->addWidget(row);
        ++shown;
    }

    if (shown == 0)
    {
        m_legend_overlay->hide();
        return;
    }

    m_legend_widget->adjustSize();
    styleLegendOverlay(m_dark_theme);
    layoutLegendOverlay();
    m_legend_overlay->show();
    m_legend_overlay->raise();
}

void PlotWidget::layoutLegendOverlay()
{
    if (m_legend_overlay == nullptr || m_plot == nullptr)
    {
        return;
    }

    const QSize content = m_legend_widget->sizeHint();
    const int frame = 2 * PlotConstants::kLegendContentMargin;

    // Cap the legend to a fraction of the chart so a dense plot can't let it grow
    // to swallow the data; overflow past the height cap scrolls.
    const int max_w = qMax(80, static_cast<int>(m_plot->width()  * PlotConstants::kLegendMaxWidthFrac));
    const int max_h = qMax(60, static_cast<int>(m_plot->height() * PlotConstants::kLegendMaxHeightFrac));

    int w = qMin(content.width() + frame, max_w);
    int h = qMin(content.height() + frame, max_h);
    // If the content is taller than the cap, reserve room for the vertical
    // scrollbar so labels are not clipped.
    if (content.height() + frame > max_h)
    {
        w = qMin(w + m_legend_scroll->verticalScrollBar()->sizeHint().width(), max_w);
    }
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
    m_legend_overlay->setStyleSheet(QString(
        "QFrame#legendOverlay { background-color: rgba(%1,%2,%3,%4);"
        " border: 1px solid %5; border-radius: %6px; }"
        "QScrollArea#legendScroll { background: transparent; border: none; }"
        "QWidget#legendContent { background: transparent; }"
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
        if (!all_series[i].visible)
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

void PlotWidget::onAxisViewToggleClicked()
{
    if (m_view_model == nullptr)
    {
        return;
    }
    const PlotViewModel::LockAxisView new_view =
        (m_view_model->lockAxisView() == PlotViewModel::LockAxisView::LockPercent)
            ? PlotViewModel::LockAxisView::MissedFrames
            : PlotViewModel::LockAxisView::LockPercent;

    m_view_model->setLockAxisView(new_view);
}

void PlotWidget::updateAxisViewButton()
{
    if (m_axis_view_btn == nullptr || m_view_model == nullptr)
    {
        return;
    }

    // The button is only meaningful when both metrics are available; it shows the
    // view it will switch *to*.
    const bool enabled = m_view_model->hasLockSeries() && m_view_model->hasMissedFramesSeries();
    m_axis_view_btn->setEnabled(enabled);

    if (enabled)
    {
        const bool is_lock_pct = (m_view_model->lockAxisView() == PlotViewModel::LockAxisView::LockPercent);
        m_axis_view_btn->setText(is_lock_pct
            ? QStringLiteral("Frame lock percentage")
            : QStringLiteral("Missed frames"));

        // Adjust left spinbox range and step to match the active axis mode
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
