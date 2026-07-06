#include "plotcustomizationdialog.h"
#include "plotviewmodel.h"
#include "constants.h"

#include <algorithm>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QComboBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QTreeWidget>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMap>

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
    , m_viewModel(viewModel)
{
    setupUi();
    populateData();
}

void PlotCustomizationDialog::setupUi()
{
    setWindowTitle(tr("Customize Plot Series"));
    setMinimumSize(500, 400);

    auto* mainLayout = new QVBoxLayout(this);

    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    // --- Frame Sync Lock Tab ---
    m_lockTab = new QWidget();
    auto* lockTabLayout = new QVBoxLayout(m_lockTab);

    auto* lockDesc = new QLabel(tr("Select the streams to display Frame Sync Lock data for.\n"
                                   "(This toggles both Lock % and Missed Frames plots)"), m_lockTab);
    lockTabLayout->addWidget(lockDesc);

    auto* lockBtnLayout = new QHBoxLayout();
    auto* lockSelectAllBtn = new QPushButton(tr("Select All"), m_lockTab);
    auto* lockSelectNoneBtn = new QPushButton(tr("Select None"), m_lockTab);
    lockBtnLayout->addWidget(lockSelectAllBtn);
    lockBtnLayout->addWidget(lockSelectNoneBtn);
    lockBtnLayout->addStretch();
    lockTabLayout->addLayout(lockBtnLayout);

    auto* lockScrollArea = new QScrollArea(m_lockTab);
    lockScrollArea->setWidgetResizable(true);
    // Budget for 8 visible rows (approx 200px tall depending on style)
    lockScrollArea->setMinimumHeight(200);

    auto* lockScrollWidget = new QWidget();
    m_lockListLayout = new QVBoxLayout(lockScrollWidget);
    m_lockListLayout->setAlignment(Qt::AlignTop);
    lockScrollArea->setWidget(lockScrollWidget);
    lockTabLayout->addWidget(lockScrollArea);

    m_tabWidget->addTab(m_lockTab, tr("Frame Sync Lock Streams"));

    // --- Receiver SNR Tab ---
    m_snrTab = new QWidget();
    auto* snrTabLayout = new QVBoxLayout(m_snrTab);

    auto* snrTopLayout = new QHBoxLayout();
    snrTopLayout->addWidget(new QLabel(tr("Telemetry Stream Group:"), m_snrTab));
    m_snrStreamCombo = new QComboBox(m_snrTab);
    snrTopLayout->addWidget(m_snrStreamCombo, 1);
    snrTabLayout->addLayout(snrTopLayout);

    auto* snrBtnLayout = new QHBoxLayout();
    m_snrExpandBtn = new QPushButton(tr("Expand All"), m_snrTab);
    auto* snrSelectAllBtn = new QPushButton(tr("Select All"), m_snrTab);
    auto* snrSelectNoneBtn = new QPushButton(tr("Select None"), m_snrTab);
    snrBtnLayout->addWidget(m_snrExpandBtn);
    snrBtnLayout->addWidget(snrSelectAllBtn);
    snrBtnLayout->addWidget(snrSelectNoneBtn);
    snrBtnLayout->addStretch();
    snrTabLayout->addLayout(snrBtnLayout);

    auto* snrScrollArea = new QScrollArea(m_snrTab);
    snrScrollArea->setWidgetResizable(true);

    auto* snrTreeContainer = new QWidget();
    m_snrTreeLayout = new QVBoxLayout(snrTreeContainer);
    m_snrTreeLayout->setContentsMargins(0, 0, 0, 0);
    m_snrTreeLayout->setAlignment(Qt::AlignTop);
    snrScrollArea->setWidget(snrTreeContainer);
    snrTabLayout->addWidget(snrScrollArea);

    m_tabWidget->addTab(m_snrTab, tr("Receiver SNR Streams"));

    // --- Dialog Buttons ---
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    // --- Connections ---
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PlotCustomizationDialog::applyChanges);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_snrStreamCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlotCustomizationDialog::onSnrStreamSelected);

    connect(lockSelectAllBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectAllLock);
    connect(lockSelectNoneBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectNoneLock);
    connect(m_snrExpandBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::toggleExpandCollapseSnr);
    connect(snrSelectAllBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectAllSnr);
    connect(snrSelectNoneBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectNoneSnr);
}

void PlotCustomizationDialog::populateData()
{
    if (!m_viewModel) return;

    const auto& series = m_viewModel->allSeries();

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
        auto* row = new QWidget(m_lockTab);
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
        m_lockListLayout->addWidget(row);

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
        m_lockRows.append(lockRow);
        // Captured by index (not pointer/reference into m_lockRows) so a later
        // append's reallocation can't leave this lambda holding a dangling row.
        const int rowIndex = m_lockRows.size() - 1;

        connect(swatch, &QPushButton::clicked, this, [this, rowIndex, swatch]() {
            const QColor picked = QColorDialog::getColor(m_lockRows[rowIndex].color, this,
                                                         tr("Choose Series Color"));
            if (!picked.isValid())
                return;
            m_lockRows[rowIndex].color = picked;
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

        auto* container = new QWidget(m_snrTab);
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
        m_snrTreeLayout->addWidget(container);
        m_snrStreamContainers.insert(streamOrder, container);
        m_snrStreamTrees.insert(streamOrder, trees);

        // Adding the first item triggers currentIndexChanged(0), which calls
        // onSnrStreamSelected(0) — by then this stream's container is already registered.
        m_snrStreamCombo->addItem(groupLabel(streamOrder), streamOrder);
    }

    if (m_snrStreamCombo->count() == 0) {
        m_snrStreamCombo->setEnabled(false);
        m_snrExpandBtn->setEnabled(false);
    }
}

QTreeWidget* PlotCustomizationDialog::buildSnrTree(const QVector<int>& receiverIndices,
                                                    const QMap<int, QVector<int>>& receivers,
                                                    const QVector<PlotSeriesData>& series)
{
    auto* tree = new QTreeWidget(m_snrTab);
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
        const int idx = m_viewModel->indexOfSeriesId(seriesId);
        if (idx < 0)
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
                : m_viewModel->seriesAt(idx).name;
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
                : m_viewModel->seriesAt(idx).color;
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
    int index = m_snrStreamCombo->currentIndex();
    if (index < 0) return {};

    int streamOrder = m_snrStreamCombo->itemData(index).toInt();
    return m_snrStreamTrees.value(streamOrder);
}

void PlotCustomizationDialog::onSnrStreamSelected(int index)
{
    if (index < 0) return;

    int streamOrder = m_snrStreamCombo->itemData(index).toInt();
    for (auto it = m_snrStreamContainers.begin(); it != m_snrStreamContainers.end(); ++it) {
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
    m_snrExpandBtn->setText(anyCollapsed ? tr("Expand All") : tr("Collapse All"));
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
    m_snrExpandBtn->setText(anyCollapsed ? tr("Collapse All") : tr("Expand All"));
}

void PlotCustomizationDialog::onSnrTreeItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updatingSnrTree || column != 0) return;

    m_updatingSnrTree = true;
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
    m_updatingSnrTree = false;
}

void PlotCustomizationDialog::applyChanges()
{
    if (!m_viewModel) return;

    // Apply lock checkboxes. Each checkbox controls both the FrameSyncLock and
    // AccumulatedMissedFrames series for a stream, but only the metric matching the
    // active lock axis view should ever be made visible — the other metric's values
    // are on a different scale and would otherwise be drawn against the wrong axis range.
    // Quiet setter: this can touch dozens of series across the two loops below, and
    // commitAppearanceChanges() at the end applies one batched refresh instead of a
    // full legend rebuild + replot per checkbox.
    const bool showMissedFrames = m_viewModel->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames;
    for (const LockRow& row : m_lockRows) {
        bool checked = row.checkbox->isChecked();
        for (int seriesId : row.seriesIds) {
            const int idx = m_viewModel->indexOfSeriesId(seriesId);
            if (idx < 0)
                continue; // series gone (list changed while dialog open)
            const auto& s = m_viewModel->seriesAt(idx);
            bool isActiveMetric = (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
                == showMissedFrames;
            m_viewModel->setSeriesVisibleQuiet(idx, checked && isActiveMetric);
        }
    }

    // Apply SNR channel checkboxes
    for (const auto& trees : m_snrStreamTrees) {
        for (QTreeWidget* tree : trees) {
            for (int r = 0; r < tree->topLevelItemCount(); r++) {
                QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
                for (int c = 0; c < rcvrItem->childCount(); c++) {
                    QTreeWidgetItem* chItem = rcvrItem->child(c);
                    const int idx = m_viewModel->indexOfSeriesId(chItem->data(0, Qt::UserRole).toInt());
                    if (idx < 0)
                        continue;
                    m_viewModel->setSeriesVisibleQuiet(idx, chItem->checkState(0) == Qt::Checked);
                }
            }
        }
    }

    // Apply per-stream color/name edits (Lock tab). renameSeries/recolorSeries are
    // pure setters that propagate to the lock/missed sibling by stream label.
    for (const LockRow& row : m_lockRows) {
        if (row.seriesIds.isEmpty())
            continue;
        const int firstIdx = m_viewModel->indexOfSeriesId(row.seriesIds.first());
        if (firstIdx < 0)
            continue; // series gone (list changed while dialog open)
        m_viewModel->renameSeries(firstIdx, row.nameEdit->text());
        m_viewModel->recolorSeries(firstIdx, row.color);
    }

    // Apply per-channel color/name edits (SNR tab), taken from the pending item roles.
    for (const auto& trees : m_snrStreamTrees) {
        for (QTreeWidget* tree : trees) {
            for (int r = 0; r < tree->topLevelItemCount(); r++) {
                QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
                for (int c = 0; c < rcvrItem->childCount(); c++) {
                    QTreeWidgetItem* chItem = rcvrItem->child(c);
                    const int idx = m_viewModel->indexOfSeriesId(chItem->data(0, Qt::UserRole).toInt());
                    if (idx < 0)
                        continue;
                    if (chItem->data(0, kRolePendingName).isValid())
                        m_viewModel->renameSeries(idx, chItem->data(0, kRolePendingName).toString());
                    if (chItem->data(0, kRolePendingColor).isValid())
                        m_viewModel->recolorSeries(idx, chItem->data(0, kRolePendingColor).value<QColor>());
                }
            }
        }
    }

    // One batched refresh so the plot re-applies pens and rebuilds the legend once.
    m_viewModel->commitAppearanceChanges();

    accept();
}

void PlotCustomizationDialog::selectAllLock()
{
    for (const LockRow& row : m_lockRows) {
        row.checkbox->setChecked(true);
    }
}

void PlotCustomizationDialog::selectNoneLock()
{
    for (const LockRow& row : m_lockRows) {
        row.checkbox->setChecked(false);
    }
}

void PlotCustomizationDialog::selectAllSnr()
{
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    m_updatingSnrTree = true;
    for (QTreeWidget* tree : trees) {
        for (int r = 0; r < tree->topLevelItemCount(); r++) {
            QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
            rcvrItem->setCheckState(0, Qt::Checked);
            for (int c = 0; c < rcvrItem->childCount(); c++) {
                rcvrItem->child(c)->setCheckState(0, Qt::Checked);
            }
        }
    }
    m_updatingSnrTree = false;
}

void PlotCustomizationDialog::selectNoneSnr()
{
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    m_updatingSnrTree = true;
    for (QTreeWidget* tree : trees) {
        for (int r = 0; r < tree->topLevelItemCount(); r++) {
            QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
            rcvrItem->setCheckState(0, Qt::Unchecked);
            for (int c = 0; c < rcvrItem->childCount(); c++) {
                rcvrItem->child(c)->setCheckState(0, Qt::Unchecked);
            }
        }
    }
    m_updatingSnrTree = false;
}
