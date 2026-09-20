#include "plotcustomizationdialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "constants.h"
#include "plotviewmodel.h"

namespace {
    /// Item data roles storing pending per-channel edits on the SNR tree until OK.
    constexpr int kRolePendingName  = Qt::UserRole + 1;
    constexpr int kRolePendingColor = Qt::UserRole + 2;

    /// @return QSS for a color swatch button filled with @p color.
    QString swatchStyle(const QColor& color)
    {
        return QStringLiteral("background-color: %1; border: 1px solid rgba(0,0,0,60);")
            .arg(color.name());
    }
} // namespace

PlotCustomizationDialog::PlotCustomizationDialog(PlotViewModel* viewModel, QWidget* parent)
    : QDialog(parent)
    , m_view_model(viewModel)
{
    setupUi();
    populateData();
}

void PlotCustomizationDialog::setupUi()
{
    setWindowTitle(tr("Customize Plot Series"));
    setMinimumSize(500, 400);

    auto* mainLayout = new QVBoxLayout(this);

    m_tab_widget = new QTabWidget(this);
    mainLayout->addWidget(m_tab_widget);

    // --- Frame Sync Lock Tab ---
    m_lock_tab = new QWidget();
    auto* lockTabLayout = new QVBoxLayout(m_lock_tab);

    auto* lockDesc = new QLabel(tr("Select the streams to display Frame Sync Lock data for.\n"
                                   "(This toggles both Lock % and Missed Frames plots)"), m_lock_tab);
    lockTabLayout->addWidget(lockDesc);

    auto* lockBtnLayout = new QHBoxLayout();
    auto* lockSelectAllBtn = new QPushButton(tr("Select All"), m_lock_tab);
    auto* lockSelectNoneBtn = new QPushButton(tr("Select None"), m_lock_tab);
    lockBtnLayout->addWidget(lockSelectAllBtn);
    lockBtnLayout->addWidget(lockSelectNoneBtn);
    lockBtnLayout->addStretch();
    lockTabLayout->addLayout(lockBtnLayout);

    auto* lockScrollArea = new QScrollArea(m_lock_tab);
    lockScrollArea->setWidgetResizable(true);
    // Budget for 8 visible rows (approx 200px tall depending on style)
    lockScrollArea->setMinimumHeight(200);

    auto* lockScrollWidget = new QWidget();
    m_lock_list_layout = new QVBoxLayout(lockScrollWidget);
    m_lock_list_layout->setAlignment(Qt::AlignTop);
    lockScrollArea->setWidget(lockScrollWidget);
    lockTabLayout->addWidget(lockScrollArea);

    m_tab_widget->addTab(m_lock_tab, tr("Frame Sync Lock Streams"));

    // --- Receiver SNR Tab ---
    m_snr_tab = new QWidget();
    auto* snrTabLayout = new QVBoxLayout(m_snr_tab);

    auto* snrTopLayout = new QHBoxLayout();
    snrTopLayout->addWidget(new QLabel(tr("Telemetry Stream Group:"), m_snr_tab));
    m_snr_stream_combo = new QComboBox(m_snr_tab);
    snrTopLayout->addWidget(m_snr_stream_combo, 1);
    snrTabLayout->addLayout(snrTopLayout);

    auto* snrBtnLayout = new QHBoxLayout();
    m_snr_expand_btn = new QPushButton(tr("Expand All"), m_snr_tab);
    // Its label toggles between "Expand All" and "Collapse All"; a floor wide
    // enough for the longer text keeps the button from resizing as it flips.
    m_snr_expand_btn->setMinimumWidth(UIConstants::kFlatButtonMinWidth);
    auto* snrSelectAllBtn = new QPushButton(tr("Select All"), m_snr_tab);
    auto* snrSelectNoneBtn = new QPushButton(tr("Select None"), m_snr_tab);
    snrBtnLayout->addWidget(m_snr_expand_btn);
    snrBtnLayout->addWidget(snrSelectAllBtn);
    snrBtnLayout->addWidget(snrSelectNoneBtn);
    snrBtnLayout->addStretch();
    snrTabLayout->addLayout(snrBtnLayout);

    auto* snrScrollArea = new QScrollArea(m_snr_tab);
    snrScrollArea->setWidgetResizable(true);

    auto* snrTreeContainer = new QWidget();
    m_snr_tree_layout = new QVBoxLayout(snrTreeContainer);
    m_snr_tree_layout->setContentsMargins(0, 0, 0, 0);
    m_snr_tree_layout->setAlignment(Qt::AlignTop);
    snrScrollArea->setWidget(snrTreeContainer);
    snrTabLayout->addWidget(snrScrollArea);

    m_tab_widget->addTab(m_snr_tab, tr("Receiver SNR Streams"));

    // --- Dialog Buttons ---
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    // --- Connections ---
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PlotCustomizationDialog::applyChanges);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_snr_stream_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlotCustomizationDialog::onSnrStreamSelected);

    connect(lockSelectAllBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectAllLock);
    connect(lockSelectNoneBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectNoneLock);
    connect(m_snr_expand_btn, &QPushButton::clicked, this, &PlotCustomizationDialog::toggleExpandCollapseSnr);
    connect(snrSelectAllBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectAllSnr);
    connect(snrSelectNoneBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectNoneSnr);
}

void PlotCustomizationDialog::populateData()
{
    if (!m_view_model) return;

    const auto& series = m_view_model->allSeries();

    // streamOrder -> list of series indices (Lock tab)
    QMap<int, QVector<int>> lockStreams;
    // streamOrder -> receiverIndex -> list of series indices (SNR tab)
    QMap<int, QMap<int, QVector<int>>> snrStreams;
    // streamOrder -> human-readable stream label for display
    QMap<int, QString> streamLabels;

    for (int i = 0; i < series.size(); ++i) {
        const auto& s = series.at(i);
        if (!streamLabels.contains(s.streamOrder))
            streamLabels.insert(s.streamOrder, s.streamLabel);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock ||
            s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames) {
            lockStreams[s.streamOrder].append(i);
        } else if (s.metricType == PlotSeriesData::MetricType::SNR) {
            snrStreams[s.streamOrder][s.receiverIndex].append(i);
        }
    }

    // Display label for a stream group: "CH <id> — <name>". The channel id is
    // re-added here for the selection UI even though the bare name is used in the
    // plot legend / config dialog.
    auto groupLabel = [&](int streamOrder) {
        return QStringLiteral("CH %1 — %2").arg(streamOrder).arg(streamLabels.value(streamOrder));
    };

    // Populate Lock Tab
    for (auto it = lockStreams.begin(); it != lockStreams.end(); ++it) {
        int streamOrder = it.key();

        // Find if any series in this group is currently visible
        bool anyVisible = false;
        for (int idx : it.value()) {
            if (series.at(idx).visible) {
                anyVisible = true;
                break;
            }
        }

        // Row: [visibility checkbox "CH <id>"][color swatch][editable series name].
        // The swatch recolors and the edit renames every series in the stream (the
        // ViewModel propagates to the lock/missed sibling); applied to the VM on OK.
        auto* row = new QWidget(m_lock_tab);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto* cb = new QCheckBox(QStringLiteral("CH %1").arg(streamOrder), row);
        cb->setChecked(anyVisible);

        const QColor color0 = it.value().isEmpty() ? QColor(Qt::gray)
                                                   : series.at(it.value().first()).color;
        const QString name0 = it.value().isEmpty() ? streamLabels.value(streamOrder)
                                                   : series.at(it.value().first()).name;

        auto* swatch = new QPushButton(row);
        swatch->setFixedSize(20, 14);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setToolTip(tr("Change series color"));
        swatch->setStyleSheet(swatchStyle(color0));

        auto* nameEdit = new QLineEdit(name0, row);
        nameEdit->setToolTip(tr("Rename this stream's series"));

        rowLayout->addWidget(cb);
        rowLayout->addWidget(swatch);
        rowLayout->addWidget(nameEdit, 1);
        m_lock_list_layout->addWidget(row);

        // Capture stable series ids (not the raw construction-time indices) so the
        // edits survive a series-list change while this modal dialog is open.
        QVector<int> ids;
        ids.reserve(it.value().size());
        for (int idx : it.value())
            ids.append(series.at(idx).id);

        LockRow lockRow;
        lockRow.checkbox = cb;
        lockRow.seriesIds = ids;
        lockRow.nameEdit = nameEdit;
        lockRow.swatch = swatch;
        lockRow.color = color0;
        m_lock_rows.append(lockRow);
        // Captured by index (not pointer/reference into m_lock_rows) so a later
        // append's reallocation can't leave this lambda holding a dangling row.
        const int rowIndex = m_lock_rows.size() - 1;

        connect(swatch, &QPushButton::clicked, this, [this, rowIndex, swatch]() {
            const QColor picked = QColorDialog::getColor(m_lock_rows[rowIndex].color, this,
                                                         tr("Choose Series Color"));
            if (!picked.isValid())
                return;
            m_lock_rows[rowIndex].color = picked;
            swatch->setStyleSheet(swatchStyle(picked));
        });
    }

    // Populate SNR Tab: one or two trees per stream (columns), each with RCVR groups
    // containing L/R/C channel checkboxes.
    for (auto streamIt = snrStreams.begin(); streamIt != snrStreams.end(); ++streamIt) {
        int streamOrder = streamIt.key();
        const auto& receivers = streamIt.value();

        // QMap keys are already sorted ascending by receiver index.
        const QVector<int> receiverIndices = receivers.keys();
        const int splitPoint = (receiverIndices.size() + 1) / 2;
        const QVector<int> leftIndices = receiverIndices.mid(0, splitPoint);
        const QVector<int> rightIndices = receiverIndices.mid(splitPoint);

        auto* container = new QWidget(m_snr_tab);
        auto* containerLayout = new QHBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);

        QVector<QTreeWidget*> trees;
        QTreeWidget* leftTree = buildSnrTree(leftIndices, receivers, series);
        containerLayout->addWidget(leftTree, 1);
        trees.append(leftTree);

        if (!rightIndices.isEmpty()) {
            QTreeWidget* rightTree = buildSnrTree(rightIndices, receivers, series);
            containerLayout->addWidget(rightTree, 1);
            trees.append(rightTree);
        }

        container->hide();
        m_snr_tree_layout->addWidget(container);
        m_snr_stream_containers.insert(streamOrder, container);
        m_snr_stream_trees.insert(streamOrder, trees);

        // Adding the first item triggers currentIndexChanged(0), which calls
        // onSnrStreamSelected(0) — by then this stream's container is already registered.
        m_snr_stream_combo->addItem(groupLabel(streamOrder), streamOrder);
    }

    if (m_snr_stream_combo->count() == 0) {
        m_snr_stream_combo->setEnabled(false);
        m_snr_expand_btn->setEnabled(false);
    }
}

QTreeWidget* PlotCustomizationDialog::buildSnrTree(const QVector<int>& receiverIndices,
                                                    const QMap<int, QVector<int>>& receivers,
                                                    const QVector<PlotSeriesData>& series)
{
    auto* tree = new QTreeWidget(m_snr_tab);
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->setRootIsDecorated(true);
    tree->setIndentation(16);
    tree->setFrameShape(QFrame::NoFrame);

    for (int receiverIndex : receiverIndices) {
        QVector<int> indices = receivers.value(receiverIndex);
        std::sort(indices.begin(), indices.end(), [&series](int a, int b) {
            return series[a].channelIndex < series[b].channelIndex;
        });

        auto* rcvrItem = new QTreeWidgetItem(tree);
        rcvrItem->setText(0, QString("RCVR %1").arg(receiverIndex));
        rcvrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        if (!indices.isEmpty()) {
            rcvrItem->setForeground(0, series[indices.first()].color);
        }

        int checkedCount = 0;
        for (int idx : indices) {
            const auto& s = series[idx];
            auto* chItem = new QTreeWidgetItem(rcvrItem);
            const QString label = (s.channelIndex < static_cast<int>(UIConstants::kChannelPrefixes.size()))
                ? QString(UIConstants::kChannelPrefixes[s.channelIndex])
                : QString::number(s.channelIndex + 1);
            chItem->setText(0, label);
            chItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            chItem->setCheckState(0, s.visible ? Qt::Checked : Qt::Unchecked);
            chItem->setForeground(0, s.color);
            // Store the stable series id (resolved back to an index at apply time),
            // not the raw construction-time index which could go stale.
            chItem->setData(0, Qt::UserRole, s.id);
            if (s.visible) checkedCount++;
        }

        const Qt::CheckState rcvrState = checkedCount == 0 ? Qt::Unchecked
            : (checkedCount == indices.size() ? Qt::Checked : Qt::PartiallyChecked);
        rcvrItem->setCheckState(0, rcvrState);
    }

    tree->collapseAll();
    connect(tree, &QTreeWidget::itemChanged, this, &PlotCustomizationDialog::onSnrTreeItemChanged);

    // Right-click a channel leaf to rename or recolor it (applied to the ViewModel
    // on OK). The leaves show only the channel letter, so an inline editor would be
    // ambiguous — a context menu keeps the compact tree intact.
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeWidget::customContextMenuRequested, this,
            [this, tree](const QPoint& pos) {
        QTreeWidgetItem* item = tree->itemAt(pos);
        if (item == nullptr || item->parent() == nullptr)
            return; // channel leaves only, not RCVR groups
        const int seriesId = item->data(0, Qt::UserRole).toInt();
        const PlotSeriesData* s = m_view_model->seriesById(seriesId);
        if (s == nullptr)
            return; // series was removed/replaced since the dialog opened

        QMenu menu;
        QAction* renameAct = menu.addAction(tr("Rename series…"));
        QAction* colorAct  = menu.addAction(tr("Change color…"));
        QAction* chosen = menu.exec(tree->viewport()->mapToGlobal(pos));
        if (chosen == nullptr)
            return;

        if (chosen == renameAct)
        {
            const QString cur = item->data(0, kRolePendingName).isValid()
                ? item->data(0, kRolePendingName).toString()
                : s->name;
            bool ok = false;
            const QString text = QInputDialog::getText(this, tr("Rename Series"),
                tr("Series name:"), QLineEdit::Normal, cur, &ok);
            if (ok)
            {
                item->setData(0, kRolePendingName, text);
                item->setToolTip(0, text);
            }
        }
        else if (chosen == colorAct)
        {
            const QColor cur = item->data(0, kRolePendingColor).isValid()
                ? item->data(0, kRolePendingColor).value<QColor>()
                : s->color;
            const QColor picked = QColorDialog::getColor(cur, this, tr("Choose Series Color"));
            if (picked.isValid())
            {
                item->setData(0, kRolePendingColor, picked);
                item->setForeground(0, picked);
            }
        }
    });
    return tree;
}

QVector<QTreeWidget*> PlotCustomizationDialog::currentSnrTrees() const
{
    int index = m_snr_stream_combo->currentIndex();
    if (index < 0) return {};

    int streamOrder = m_snr_stream_combo->itemData(index).toInt();
    return m_snr_stream_trees.value(streamOrder);
}

void PlotCustomizationDialog::onSnrStreamSelected(int index)
{
    if (index < 0) return;

    int streamOrder = m_snr_stream_combo->itemData(index).toInt();
    for (auto it = m_snr_stream_containers.begin(); it != m_snr_stream_containers.end(); ++it) {
        it.value()->setVisible(it.key() == streamOrder);
    }

    bool anyCollapsed = false;
    for (QTreeWidget* tree : currentSnrTrees()) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            if (!tree->topLevelItem(i)->isExpanded()) {
                anyCollapsed = true;
                break;
            }
        }
        if (anyCollapsed) break;
    }
    m_snr_expand_btn->setText(anyCollapsed ? tr("Expand All") : tr("Collapse All"));
}

void PlotCustomizationDialog::toggleExpandCollapseSnr()
{
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    bool anyCollapsed = false;
    for (QTreeWidget* tree : trees) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            if (!tree->topLevelItem(i)->isExpanded()) {
                anyCollapsed = true;
                break;
            }
        }
        if (anyCollapsed) break;
    }

    for (QTreeWidget* tree : trees) {
        if (anyCollapsed) {
            tree->expandAll();
        } else {
            tree->collapseAll();
        }
    }
    m_snr_expand_btn->setText(anyCollapsed ? tr("Collapse All") : tr("Expand All"));
}

void PlotCustomizationDialog::onSnrTreeItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updating_snr_tree || column != 0) return;

    m_updating_snr_tree = true;
    if (item->parent() == nullptr) {
        // RCVR group checkbox toggled — push the new state down to its L/R/C channels.
        const Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked) {
            for (int c = 0; c < item->childCount(); c++) {
                item->child(c)->setCheckState(0, state);
            }
        }
    } else {
        // Channel checkbox toggled — recompute the parent RCVR group's tristate.
        QTreeWidgetItem* parent = item->parent();
        int checkedCount = 0;
        for (int c = 0; c < parent->childCount(); c++) {
            if (parent->child(c)->checkState(0) == Qt::Checked) checkedCount++;
        }
        const Qt::CheckState parentState = checkedCount == 0 ? Qt::Unchecked
            : (checkedCount == parent->childCount() ? Qt::Checked : Qt::PartiallyChecked);
        parent->setCheckState(0, parentState);
    }
    m_updating_snr_tree = false;
}

void PlotCustomizationDialog::applyChanges()
{
    if (!m_view_model) return;

    // Apply lock checkboxes. Each checkbox controls both the FrameSyncLock and
    // AccumulatedMissedFrames series for a stream, but only the metric matching the
    // active lock axis view should ever be made visible — the other metric's values
    // are on a different scale and would otherwise be drawn against the wrong axis range.
    // Quiet setter: this can touch dozens of series across the two loops below, and
    // commitAppearanceChanges() at the end applies one batched refresh instead of a
    // full legend rebuild + replot per checkbox.
    const bool showMissedFrames = m_view_model->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames;
    for (const LockRow& row : m_lock_rows) {
        bool checked = row.checkbox->isChecked();
        for (int seriesId : row.seriesIds) {
            const PlotSeriesData* s = m_view_model->seriesById(seriesId);
            if (s == nullptr)
                continue; // series gone (list changed while dialog open)
            const bool isActiveMetric = (s->metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
                == showMissedFrames;
            m_view_model->setSeriesVisibleQuietById(seriesId, checked && isActiveMetric);
        }
    }

    // Apply SNR channel checkboxes
    for (const auto& trees : m_snr_stream_trees) {
        for (QTreeWidget* tree : trees) {
            for (int r = 0; r < tree->topLevelItemCount(); r++) {
                QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
                for (int c = 0; c < rcvrItem->childCount(); c++) {
                    QTreeWidgetItem* chItem = rcvrItem->child(c);
                    m_view_model->setSeriesVisibleQuietById(chItem->data(0, Qt::UserRole).toInt(),
                                                           chItem->checkState(0) == Qt::Checked);
                }
            }
        }
    }

    // Apply per-stream color/name edits (Lock tab). renameSeriesById/recolorSeriesById
    // are pure setters that propagate to the lock/missed sibling by stream label, and
    // no-op if the stream is gone (series list changed while the dialog was open).
    for (const LockRow& row : m_lock_rows) {
        if (row.seriesIds.isEmpty())
            continue;
        const int firstId = row.seriesIds.first();
        m_view_model->renameSeriesById(firstId, row.nameEdit->text());
        m_view_model->recolorSeriesById(firstId, row.color);
    }

    // Apply per-channel color/name edits (SNR tab), taken from the pending item roles.
    for (const auto& trees : m_snr_stream_trees) {
        for (QTreeWidget* tree : trees) {
            for (int r = 0; r < tree->topLevelItemCount(); r++) {
                QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
                for (int c = 0; c < rcvrItem->childCount(); c++) {
                    QTreeWidgetItem* chItem = rcvrItem->child(c);
                    const int seriesId = chItem->data(0, Qt::UserRole).toInt();
                    if (chItem->data(0, kRolePendingName).isValid())
                        m_view_model->renameSeriesById(seriesId, chItem->data(0, kRolePendingName).toString());
                    if (chItem->data(0, kRolePendingColor).isValid())
                        m_view_model->recolorSeriesById(seriesId, chItem->data(0, kRolePendingColor).value<QColor>());
                }
            }
        }
    }

    // One batched refresh so the plot re-applies pens and rebuilds the legend once.
    m_view_model->commitAppearanceChanges();

    accept();
}

void PlotCustomizationDialog::selectAllLock()
{
    for (const LockRow& row : m_lock_rows) {
        row.checkbox->setChecked(true);
    }
}

void PlotCustomizationDialog::selectNoneLock()
{
    for (const LockRow& row : m_lock_rows) {
        row.checkbox->setChecked(false);
    }
}

void PlotCustomizationDialog::selectAllSnr()
{
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    m_updating_snr_tree = true;
    for (QTreeWidget* tree : trees) {
        for (int r = 0; r < tree->topLevelItemCount(); r++) {
            QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
            rcvrItem->setCheckState(0, Qt::Checked);
            for (int c = 0; c < rcvrItem->childCount(); c++) {
                rcvrItem->child(c)->setCheckState(0, Qt::Checked);
            }
        }
    }
    m_updating_snr_tree = false;
}

void PlotCustomizationDialog::selectNoneSnr()
{
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    m_updating_snr_tree = true;
    for (QTreeWidget* tree : trees) {
        for (int r = 0; r < tree->topLevelItemCount(); r++) {
            QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
            rcvrItem->setCheckState(0, Qt::Unchecked);
            for (int c = 0; c < rcvrItem->childCount(); c++) {
                rcvrItem->child(c)->setCheckState(0, Qt::Unchecked);
            }
        }
    }
    m_updating_snr_tree = false;
}
