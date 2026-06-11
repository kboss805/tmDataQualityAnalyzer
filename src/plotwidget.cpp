/**
 * @file plotwidget.cpp
 * @brief Implementation of PlotWidget — QCustomPlot chart with controls.
 */

#include "plotwidget.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QToolTip>
#include <QtSvg/QSvgGenerator>
#include <QVBoxLayout>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>

#include "qcustomplot.h"

#include "constants.h"
#include "plotviewmodel.h"
#include "exportdialog.h"

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
    connect(vm, &PlotViewModel::axisRangeChanged, this, &PlotWidget::updateAxes);
    connect(vm, &PlotViewModel::plotTitleChanged, this, &PlotWidget::updateTitle);
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

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::initLegendSkeleton(int lock_count, int receiver_count, int channels_per_receiver)
{
    clearLegendContents();

    // One group per lock stream (collapses to "Lock") plus one per receiver.
    QVector<QPair<QString, int>> groups;
    for (int l = 0; l < lock_count; l++)
    {
        groups.append({"Lock", -1});
    }
    for (int r = 0; r < receiver_count; r++)
    {
        groups.append({"RCVR " + QString::number(r + 1), channels_per_receiver});
    }

    int group_count = static_cast<int>(groups.size());
    if (group_count <= 0)
    {
        return;
    }

    // Lock groups get their own dedicated column; receiver groups are split
    // round-robin across up to two more columns.
    QVector<QPair<QString, int>> lock_groups;
    QVector<QPair<QString, int>> receiver_group_list;
    for (const auto& group : groups)
    {
        if (group.second < 0)
        {
            lock_groups.append(group);
        }
        else
        {
            receiver_group_list.append(group);
        }
    }

    // Always render the full fixed-width grid (1 lock column + N receiver
    // columns) so the panel's width — and the buttons to its right — stay in
    // a constant position regardless of whether this file has lock/receiver
    // data of either kind.
    const int receiver_columns = UIConstants::kLegendGridColumns - 1;
    int receiver_rows = receiver_group_list.isEmpty()
        ? 0 : (receiver_group_list.size() + receiver_columns - 1) / receiver_columns;
    int max_rows = qMax(qMax(static_cast<int>(lock_groups.size()), receiver_rows), 1);
    int visible_rows = qMin(max_rows, UIConstants::kLegendMaxVisibleRows);
    int treeHeight = (visible_rows * UIConstants::kTreeItemHeightFactor) + UIConstants::kTreeHeightBuffer;

    QPushButton* toggle_btn = new QPushButton("Expand All");
    toggle_btn->setFlat(true);
    toggle_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    toggle_btn->setEnabled(false);
    toggle_btn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    toggle_btn->setToolTip("Expand/Collapse All (Ctrl+E)");
    QPushButton* select_all_btn = new QPushButton("Select All");
    select_all_btn->setFlat(true);
    select_all_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    select_all_btn->setEnabled(false);
    QPushButton* select_none_btn = new QPushButton("Select None");
    select_none_btn->setFlat(true);
    select_none_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    select_none_btn->setEnabled(false);

    auto build_tree = [&](const QVector<QPair<QString, int>>& tree_groups) {
        QTreeWidget* tree = new QTreeWidget;
        tree->setHeaderHidden(true);
        tree->setColumnCount(1);
        tree->setRootIsDecorated(true);
        tree->setAnimated(true);
        tree->setIndentation(0);
        tree->setFixedWidth(UIConstants::kTreeFixedWidth);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        tree->setFixedHeight(qMax(treeHeight, 90));
        tree->setFrameShape(QFrame::NoFrame);
        tree->setEnabled(false);

        for (const auto& [group_label, child_count] : tree_groups)
        {
            QTreeWidgetItem* group_item = new QTreeWidgetItem;
            group_item->setText(0, group_label);
            group_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
            group_item->setData(0, Qt::UserRole, -1);

            if (child_count < 0)
            {
                // Lock group: a single placeholder row, no per-channel breakdown.
                QTreeWidgetItem* channel_item = new QTreeWidgetItem;
                channel_item->setText(0, "Lock %");
                channel_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                channel_item->setCheckState(0, Qt::Checked);
                channel_item->setData(0, Qt::UserRole, -1);
                group_item->addChild(channel_item);
            }
            else
            {
                for (int c = 0; c < child_count; c++)
                {
                    QTreeWidgetItem* channel_item = new QTreeWidgetItem;
                    channel_item->setText(0, c < static_cast<int>(UIConstants::kChannelPrefixes.size())
                                              ? QString(UIConstants::kChannelPrefixes[c])
                                              : QString::number(c + 1));
                    channel_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                    channel_item->setCheckState(0, Qt::Checked);
                    channel_item->setData(0, Qt::UserRole, -1);
                    group_item->addChild(channel_item);
                }
            }

            tree->addTopLevelItem(group_item);
        }

        tree->collapseAll();
        return tree;
    };

    QHBoxLayout* columns_layout = new QHBoxLayout;
    columns_layout->setSpacing(2);
    columns_layout->setContentsMargins(0, 0, 0, 0);

    {
        QTreeWidget* tree = build_tree(lock_groups);
        columns_layout->addWidget(tree);
        m_legend_trees.append(tree);
    }

    {
        QVector<QVector<QPair<QString, int>>> column_groups(receiver_columns);
        for (int i = 0; i < receiver_group_list.size(); i++)
        {
            column_groups[i % receiver_columns].append(receiver_group_list[i]);
        }
        for (const auto& tree_groups : column_groups)
        {
            QTreeWidget* tree = build_tree(tree_groups);
            columns_layout->addWidget(tree);
            m_legend_trees.append(tree);
        }
    }

    // Buttons stacked vertically to the right of the tree columns
    auto* btn_col = new QVBoxLayout;
    btn_col->setContentsMargins(0, 0, 0, 0);
    btn_col->setSpacing(4);
    btn_col->addWidget(toggle_btn);
    btn_col->addWidget(select_all_btn);
    btn_col->addWidget(select_none_btn);
    btn_col->addStretch(1);

    auto* content_row = new QHBoxLayout;
    content_row->setContentsMargins(0, 0, 0, 0);
    content_row->setSpacing(0);
    content_row->addLayout(columns_layout);
    content_row->addSpacing(8);
    content_row->addLayout(btn_col);
    m_legend_layout->addLayout(content_row);

    m_legend_panel->setFixedHeight(qMax(treeHeight, 90));
    syncLegendScrollbars();
    connectExpandCollapseToggle(toggle_btn);
}

void PlotWidget::rebuildChart()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;

    m_plot->clearGraphs();
    m_graphs.clear();

    const auto& all_series = m_view_model->allSeries();

    for (qsizetype i = 0; i < all_series.size(); i++)
    {
        const PlotSeriesData& s = all_series[i];
        QCPAxis* value_axis = (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            ? m_plot->yAxis : m_plot->yAxis2;
        QCPGraph* graph = m_plot->addGraph(m_plot->xAxis, value_axis);
        graph->setName(s.name);
        graph->setPen(QPen(s.color, PlotConstants::kGraphPenWidth));
        graph->setData(s.xValues, s.yValues, true);
        graph->setVisible(s.visible);
        m_graphs.append(graph);
    }

    // Set axis labels
    m_plot->xAxis->setLabel(PlotConstants::kXAxisLabel);
    m_plot->yAxis->setLabel(PlotConstants::kYAxisLabel);

    // Update title and axes without triggering extra replots
    updateTitle();
    updateAxes();

    // Enable all controls when data is loaded
    bool has_data = m_view_model->hasData();
    m_title_edit->setEnabled(has_data);
    m_x_start_edit->setEnabled(has_data);
    m_x_stop_edit->setEnabled(has_data);
    m_y_min_spin->setEnabled(has_data);
    m_y_max_spin->setEnabled(has_data);
    m_reset_btn->setEnabled(has_data);
    m_plot->setInteractions(has_data
        ? QCP::iRangeDrag | QCP::iRangeZoom
        : QCP::Interactions());

    if (has_data)
    {
        m_x_start_edit->setText(m_view_model->formatTime(m_view_model->xViewMin()));
        m_x_stop_edit->setText(m_view_model->formatTime(m_view_model->xViewMax()));

        m_y_min_spin->setValue(m_view_model->yMin());
        m_y_max_spin->setValue(m_view_model->yMax());
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
}

void PlotWidget::updateAxes()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;

    m_plot->xAxis->setRange(m_view_model->xViewMin(), m_view_model->xViewMax());
    
    // Left axis (yAxis) is fixed 0-100 for Lock %
    m_plot->yAxis->setRange(0, 100);
    
    // Right axis (yAxis2) auto-scales to SNR data limits, or manual limits
    m_plot->yAxis2->setRange(m_view_model->yMin(), m_view_model->yMax());

    m_x_start_edit->setText(m_view_model->formatTime(m_view_model->xViewMin()));
    m_x_stop_edit->setText(m_view_model->formatTime(m_view_model->xViewMax()));
    m_y_min_spin->setValue(m_view_model->yMin());
    m_y_max_spin->setValue(m_view_model->yMax());

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

void PlotWidget::onLegendCheckboxToggled(int series_index, bool checked)
{
    if (m_updating_from_vm || m_view_model == nullptr)
    {
        return;
    }
    m_view_model->setSeriesVisible(series_index, checked);
}

void PlotWidget::onManualYChanged()
{
    if (m_updating_from_vm || m_view_model == nullptr)
    {
        return;
    }
    m_view_model->setYManualRange(m_y_min_spin->value(), m_y_max_spin->value());
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

    double raw_start = parseTimeToElapsed(entered_start);
    double raw_stop  = parseTimeToElapsed(entered_stop);

    double start = qBound(data_min, raw_start, data_max);
    double stop  = qBound(data_min, raw_stop,  data_max);

    // Warn if either value was clamped to the file bounds
    if (!qFuzzyCompare(start, raw_start))
    {
        emit logMessage(QString("<span style='color:#DAA520;'>X Start \"%1\" is outside the file time range — clamped to file bounds.</span>")
                        .arg(entered_start));
    }
    if (!qFuzzyCompare(stop, raw_stop))
    {
        emit logMessage(QString("<span style='color:#DAA520;'>X Stop \"%1\" is outside the file time range — clamped to file bounds.</span>")
                        .arg(entered_stop));
    }

    // Enforce start <= stop; clamp whichever field was just edited
    if (start > stop)
    {
        if (m_x_start_edit == focusWidget() || m_x_start_edit->hasFocus())
        {
            emit logMessage("<span style='color:#DAA520;'>X Start time is after X Stop — clamped to stop time.</span>");
            start = stop;
        }
        else
        {
            emit logMessage("<span style='color:#DAA520;'>X Stop time is before X Start — clamped to start time.</span>");
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

    ExportDialog dialog(base_path + ".csv", base_path + ".png", this);
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
                success = m_plot->savePng(filename);
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

void PlotWidget::handlePlotYRangeChanged(double lower, double upper)
{
    if (m_updating_from_vm || m_view_model == nullptr)
    {
        return;
    }
    m_view_model->setYManualRange(lower, upper);
}

double PlotWidget::parseTimeToElapsed(const QString& text) const
{
    if (m_view_model == nullptr)
    {
        return 0.0;
    }
    const QStringList parts = text.split(':');
    if (parts.size() != 4)
    {
        return 0.0;
    }
    int day     = parts[0].toInt();
    int hours   = parts[1].toInt();
    int minutes = parts[2].toInt();
    int secs    = parts[3].toInt();
    double total_absolute = day * 86400.0 + hours * 3600.0 + minutes * 60.0 + secs;
    double base_absolute  = m_view_model->baseDay() * 86400.0 + m_view_model->baseTimeOffset();
    return total_absolute - base_absolute;
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
    m_plot->yAxis2->setLabel(PlotConstants::kLockAxisLabel);
    main_layout->addWidget(m_plot, 1);
    main_layout->addSpacing(8);

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

    // Axis controls grid (X/Y spinboxes + Reset), top-aligned in the bar
    auto* axis_grid = new QGridLayout;
    axis_grid->setContentsMargins(0, 0, 0, 0);
    axis_grid->setHorizontalSpacing(4);
    axis_grid->setVerticalSpacing(4);
    // Col 2 is a dedicated 8px spacer between the first control and second label pair
    axis_grid->setColumnMinimumWidth(2, 8);

    axis_grid->addWidget(new QLabel("X Start:"), 0, 0);
    m_x_start_edit = new QLineEdit;
    m_x_start_edit->setPlaceholderText("DDD:HH:MM:SS");
    m_x_start_edit->setToolTip("X axis start time (DDD:HH:MM:SS)");
    m_x_start_edit->setEnabled(false);
    axis_grid->addWidget(m_x_start_edit, 0, 1);

    axis_grid->addWidget(new QLabel("X Stop:"), 0, 3);
    m_x_stop_edit = new QLineEdit;
    m_x_stop_edit->setPlaceholderText("DDD:HH:MM:SS");
    m_x_stop_edit->setToolTip("X axis stop time (DDD:HH:MM:SS)");
    m_x_stop_edit->setEnabled(false);
    axis_grid->addWidget(m_x_stop_edit, 0, 4);

    axis_grid->addWidget(new QLabel("Y Min:"), 1, 0);
    m_y_min_spin = new QDoubleSpinBox;
    m_y_min_spin->setRange(0.0, PlotConstants::kYSpinBoxMax);
    m_y_min_spin->setDecimals(1);
    m_y_min_spin->setToolTip("Y axis minimum (dB)");
    m_y_min_spin->setEnabled(false);
    axis_grid->addWidget(m_y_min_spin, 1, 1);

    axis_grid->addWidget(new QLabel("Y Max:"), 1, 3);
    m_y_max_spin = new QDoubleSpinBox;
    m_y_max_spin->setRange(0.0, PlotConstants::kYSpinBoxMax);
    m_y_max_spin->setDecimals(1);
    m_y_max_spin->setToolTip("Y axis maximum (dB)");
    m_y_max_spin->setEnabled(false);
    axis_grid->addWidget(m_y_max_spin, 1, 4);

    m_reset_btn = new QPushButton("Reset");
    m_reset_btn->setFlat(true);
    m_reset_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    m_reset_btn->setToolTip("Reset axes to auto range");
    m_reset_btn->setEnabled(false);
    axis_grid->addWidget(m_reset_btn, 0, 5);

    QWidget* axis_widget = new QWidget;
    axis_widget->setLayout(axis_grid);
    axis_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    bottom_bar->addWidget(axis_widget, 0, Qt::AlignTop);

    // Equal stretches on both sides center the legend between axis controls and right margin
    bottom_bar->addStretch(1);

    // Legend tree panel — centered in the bottom bar
    m_legend_panel = new QWidget;
    m_legend_layout = new QVBoxLayout(m_legend_panel);
    m_legend_layout->setContentsMargins(0, 0, 0, 0);
    m_legend_layout->setSpacing(2);
    bottom_bar->addWidget(m_legend_panel, 0, Qt::AlignTop);

    bottom_bar->addStretch(1);

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

    connect(m_y_min_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PlotWidget::onManualYChanged);
    connect(m_y_max_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PlotWidget::onManualYChanged);

    connect(m_x_start_edit, &QLineEdit::editingFinished, this, &PlotWidget::onXRangeChanged);
    connect(m_x_stop_edit, &QLineEdit::editingFinished, this, &PlotWidget::onXRangeChanged);

    connect(m_reset_btn, &QPushButton::clicked, this, &PlotWidget::onResetAxes);
    connect(m_plot, &QCustomPlot::mouseMove, this, &PlotWidget::onPlotMouseMove);

    connect(m_plot->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& range) { handlePlotXRangeChanged(range.lower, range.upper); });
    connect(m_plot->yAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& range) { handlePlotYRangeChanged(range.lower, range.upper); });
}

void PlotWidget::rebuildLegend()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    clearLegendContents();

    const auto& all_series = m_view_model->allSeries();
    if (all_series.isEmpty())
    {
        return;
    }

    // Group series by receiver index (sorted)
    QMap<int, QVector<int>> receiver_groups;
    for (qsizetype i = 0; i < all_series.size(); i++)
    {
        receiver_groups[all_series[i].receiverIndex].append(static_cast<int>(i));
    }

    // Within each group, order by source stream and channel rather than the
    // (possibly out-of-order) arrival order of parallel-processed streams.
    for (auto& indices : receiver_groups)
    {
        std::sort(indices.begin(), indices.end(), [&all_series](int a, int b) {
            const PlotSeriesData& sa = all_series[a];
            const PlotSeriesData& sb = all_series[b];
            if (sa.streamOrder != sb.streamOrder)
            {
                return sa.streamOrder < sb.streamOrder;
            }
            return sa.channelIndex < sb.channelIndex;
        });
    }

    QPushButton* toggle_btn = new QPushButton("Expand All");
    toggle_btn->setFlat(true);
    toggle_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    toggle_btn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    toggle_btn->setToolTip("Expand/Collapse All (Ctrl+E)");
    QPushButton* select_all_btn = new QPushButton("Select All");
    select_all_btn->setFlat(true);
    select_all_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    QPushButton* select_none_btn = new QPushButton("Select None");
    select_none_btn->setFlat(true);
    select_none_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);

    // Lock (receiver index 0) gets its own dedicated column; receiver groups
    // are split round-robin across up to two more columns.
    bool has_lock = receiver_groups.contains(0);
    QVector<int> rcvr_keys;
    for (int key : receiver_groups.keys())
    {
        if (key != 0)
        {
            rcvr_keys.append(key);
        }
    }

    // Always render the full fixed-width grid (1 lock column + N receiver
    // columns) so the panel's width — and the buttons to its right — stay in
    // a constant position regardless of whether this file has lock/receiver
    // data of either kind.
    const int receiver_columns = UIConstants::kLegendGridColumns - 1;
    int receiver_rows = rcvr_keys.isEmpty()
        ? 0 : (rcvr_keys.size() + receiver_columns - 1) / receiver_columns;
    int lock_rows = has_lock ? 1 : 0;
    int max_rows = qMax(qMax(lock_rows, receiver_rows), 1);
    int visible_rows = qMin(max_rows, UIConstants::kLegendMaxVisibleRows);
    int treeHeight = (visible_rows * UIConstants::kTreeItemHeightFactor) + UIConstants::kTreeHeightBuffer;

    auto build_tree = [&](const QVector<int>& keys) {
        QTreeWidget* tree = new QTreeWidget;
        tree->setHeaderHidden(true);
        tree->setColumnCount(1);
        tree->setRootIsDecorated(true);
        tree->setAnimated(true);
        tree->setIndentation(0);
        tree->setFixedWidth(UIConstants::kTreeFixedWidth);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        tree->setFixedHeight(qMax(treeHeight, 90));
        tree->setFrameShape(QFrame::NoFrame);

        for (int receiver_num : keys)
        {
            const QVector<int>& indices = receiver_groups[receiver_num];

            QTreeWidgetItem* receiver_item = new QTreeWidgetItem;
            const QString receiver_label = (receiver_num == 0)
                ? QStringLiteral("Lock")
                : "RCVR " + QString::number(receiver_num);
            receiver_item->setText(0, receiver_label);
            receiver_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
            receiver_item->setData(0, Qt::UserRole, -1);

            // Color receiver title to match its first channel's plot color
            if (!indices.isEmpty())
            {
                receiver_item->setForeground(0, all_series[indices.first()].color);
            }

            for (int idx : indices)
            {
                const PlotSeriesData& s = all_series[idx];
                QTreeWidgetItem* channel_item = new QTreeWidgetItem;
                const QString channel_label =
                    (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
                    ? s.name
                    : (s.channelIndex < static_cast<int>(UIConstants::kChannelPrefixes.size()))
                        ? QString(UIConstants::kChannelPrefixes[s.channelIndex])
                        : s.name;
                channel_item->setText(0, channel_label);
                channel_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                channel_item->setCheckState(0, s.visible ? Qt::Checked : Qt::Unchecked);
                channel_item->setForeground(0, s.color);
                channel_item->setData(0, Qt::UserRole, idx);
                receiver_item->addChild(channel_item);
            }

            tree->addTopLevelItem(receiver_item);
        }

        tree->collapseAll();
        connectLegendItemChanged(tree);
        return tree;
    };

    QHBoxLayout* columns_layout = new QHBoxLayout;
    columns_layout->setSpacing(2);
    columns_layout->setContentsMargins(0, 0, 0, 0);

    {
        QTreeWidget* tree = build_tree(has_lock ? QVector<int>{0} : QVector<int>{});
        columns_layout->addWidget(tree);
        m_legend_trees.append(tree);
    }

    {
        QVector<QVector<int>> column_keys(receiver_columns);
        for (int i = 0; i < rcvr_keys.size(); i++)
        {
            column_keys[i % receiver_columns].append(rcvr_keys[i]);
        }
        for (const auto& keys : column_keys)
        {
            QTreeWidget* tree = build_tree(keys);
            columns_layout->addWidget(tree);
            m_legend_trees.append(tree);
        }
    }

    // Buttons stacked vertically to the right of the tree columns
    auto* btn_col = new QVBoxLayout;
    btn_col->setContentsMargins(0, 0, 0, 0);
    btn_col->setSpacing(4);
    btn_col->addWidget(toggle_btn);
    btn_col->addWidget(select_all_btn);
    btn_col->addWidget(select_none_btn);
    btn_col->addStretch(1);

    auto* content_row = new QHBoxLayout;
    content_row->setContentsMargins(0, 0, 0, 0);
    content_row->setSpacing(0);
    content_row->addLayout(columns_layout);
    content_row->addSpacing(8);
    content_row->addLayout(btn_col);
    m_legend_layout->addLayout(content_row);

    m_legend_panel->setFixedHeight(qMax(treeHeight, 90));
    syncLegendScrollbars();
    connectExpandCollapseToggle(toggle_btn);

    // Select All: check all channels
    connect(select_all_btn, &QPushButton::clicked, this, [this]() {
        setAllLegendChecks(true);
    });

    // Select None: uncheck all channels
    connect(select_none_btn, &QPushButton::clicked, this, [this]() {
        setAllLegendChecks(false);
    });
}

////////////////////////////////////////////////////////////////////////////////
// Legend helpers
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// Recursively removes and deletes all items (and nested layouts/widgets) from a layout.
void deleteLayoutContents(QLayout* layout)
{
    QLayoutItem* item = nullptr;
    while ((item = layout->takeAt(0)) != nullptr)
    {
        if (QLayout* child_layout = item->layout())
        {
            // child_layout is owned by `item`; deleting `item` below also
            // deletes child_layout (QBoxLayoutItem takes ownership), so we
            // only need to empty its contents here, not delete it ourselves.
            deleteLayoutContents(child_layout);
        }
        else if (item->widget() != nullptr)
        {
            item->widget()->deleteLater();
        }
        delete item;
    }
}
}

void PlotWidget::clearLegendContents()
{
    m_legend_trees.clear();
    deleteLayoutContents(m_legend_layout);
}

void PlotWidget::syncLegendScrollbars()
{
    if (m_legend_trees.size() > 1)
    {
        QScrollBar* visible_bar = m_legend_trees.last()->verticalScrollBar();
        for (int i = 0; i < m_legend_trees.size() - 1; i++)
        {
            m_legend_trees[i]->verticalScrollBar()->setFixedWidth(0);
            connect(visible_bar, &QScrollBar::valueChanged,
                    m_legend_trees[i]->verticalScrollBar(), &QScrollBar::setValue);
        }
    }
}

void PlotWidget::connectLegendItemChanged(QTreeWidget* tree)
{
    connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* changed_item, int column) {
        if (m_updating_from_vm || m_view_model == nullptr || column != 0)
        {
            return;
        }

        if (changed_item->parent() != nullptr)
        {
            int idx = changed_item->data(0, Qt::UserRole).toInt();
            bool checked = changed_item->checkState(0) == Qt::Checked;
            onLegendCheckboxToggled(idx, checked);
        }
    });
}

void PlotWidget::connectExpandCollapseToggle(QPushButton* toggle_btn)
{
    connect(toggle_btn, &QPushButton::clicked, this, [this, toggle_btn]() {
        bool any_collapsed = false;
        for (QTreeWidget* t : m_legend_trees)
        {
            for (int i = 0; i < t->topLevelItemCount(); i++)
            {
                if (!t->topLevelItem(i)->isExpanded())
                {
                    any_collapsed = true;
                }
            }
        }

        for (QTreeWidget* t : m_legend_trees)
        {
            if (any_collapsed)
            {
                t->expandAll();
            }
            else
            {
                t->collapseAll();
            }
        }
        toggle_btn->setText(any_collapsed ? "Collapse All" : "Expand All");
    });
}

void PlotWidget::setAllLegendChecks(bool checked)
{
    if (m_view_model == nullptr)
    {
        return;
    }
    m_updating_from_vm = true;
    for (QTreeWidget* t : m_legend_trees)
    {
        t->blockSignals(true);
        for (int r = 0; r < t->topLevelItemCount(); r++)
        {
            QTreeWidgetItem* rcvr = t->topLevelItem(r);
            for (int c = 0; c < rcvr->childCount(); c++)
            {
                rcvr->child(c)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
                int idx = rcvr->child(c)->data(0, Qt::UserRole).toInt();
                m_view_model->setSeriesVisible(idx, checked);
            }
        }
        t->blockSignals(false);
    }
    m_updating_from_vm = false;
    rebuildChart();
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
        const bool is_lock = (all_series[best_series_index].metricType == PlotSeriesData::MetricType::FrameSyncLock);
        const QString unit = is_lock ? "%" : " dB";
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
